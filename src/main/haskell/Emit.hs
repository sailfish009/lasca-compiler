{-# LANGUAGE Strict #-}

module Emit where

import LLVM.Module
import LLVM.Context
import LLVM.Analysis
import LLVM.PassManager

import qualified LLVM.AST as AST
import qualified LLVM.AST.Global
import qualified LLVM.AST.Type as T
import qualified LLVM.AST.Instruction as I
import qualified LLVM.AST.Constant as C
import qualified LLVM.AST.Float as F
import qualified LLVM.AST.IntegerPredicate as IP
import qualified LLVM.AST.FloatingPointPredicate as FP
import qualified LLVM.AST.FunctionAttribute as FA

-- import qualified Data.Text as Text
import qualified Data.ByteString as ByteString
import qualified Data.Text.Encoding as Encoding
import Text.Printf
import qualified Data.ByteString.UTF8 as UTF8
import Data.String
import qualified Data.ByteString.Char8 as Char8
import qualified Data.ByteString as BS
import qualified Data.ByteString.Short as SBS

import LLVM.ExecutionEngine ( withMCJIT, withModuleInEngine, getFunction )

import qualified Data.Text
import qualified Data.ByteString
import qualified Data.Text.Encoding
import Data.Digest.Murmur32
import Data.Maybe
import qualified Data.List as List
import Data.Word
import Data.Int
import Control.Monad.State
import Control.Monad.Except
import Control.Applicative
import qualified Control.Lens as Lens
import Control.Lens.Operators
import qualified Data.Map.Strict as Map
import qualified Data.Set as Set
import qualified Data.Sequence as Seq
import qualified Debug.Trace as Debug

import Codegen
import Type
import qualified Syntax as S
import Syntax (Ctx, createGlobalContext)


uncurryLambda expr = go expr ([], expr) where
    go (S.Lam _ name e) result = let (args, body) = go e result in (name : args, body)
    go e (args, _) = (args, e)

data Trans = Trans {
    _modNames :: Names,
    _locals :: Map.Map String Type.Type,
    _outers :: Map.Map String Type.Type,
    _usedVars :: Set.Set String,
    _syntacticAst :: [S.Expr]
} deriving (Show)

modStateLocals :: Lens.Lens' Trans (Map.Map String Type.Type)
modStateLocals = Lens.lens _locals (\ms l -> ms { _locals = l } )

emptyTrans = Trans {
    _modNames = Map.empty,
    _locals = Map.empty,
    _outers = Map.empty,
    _usedVars = Set.empty,
    _syntacticAst = []
}

--transform :: (S.Expr -> LLVM S.Expr) -> [S.Expr] -> LLVM [S.Expr]
transform transformer exprs = sequence [transformExpr transformer expr | expr <- exprs]

--transformExpr :: (S.Expr -> LLVM S.Expr) -> S.Expr -> LLVM S.Expr
transformExpr transformer expr = case expr of
    (S.If meta cond true false) -> do
        cond' <- go cond
        true' <- go true
        false' <- go false
        transformer (S.If meta cond' true' false')
    (S.Let meta n e body) -> do
        case S.typeOf e of
          TypeFunc a b -> modStateLocals %= Map.insert n a
          _ -> modStateLocals %= Map.insert n typeAny
        e' <- go e
        body' <- go body
        transformer (S.Let meta n e' body')
    l@(S.Lam m a@(S.Arg n t) e) -> do
        modify (\s -> s { _outers = _locals s } )
        let r = case S.typeOf l of
                    TypeFunc a b -> Map.singleton n a
                    _ -> Map.singleton n typeAny
        modStateLocals .= r
        e' <- go e
        transformer (S.Lam m a e')
    (S.Apply meta e args) -> do
        e' <- go e
        args' <- sequence [go arg | arg <- args]
        transformer (S.Apply meta e' args')
    f@(S.Function meta name tpe args e1) -> do
        let funcTypeToLlvm (S.Arg name _) (TypeFunc a b, acc) = (b, (name, a) : acc)
            funcTypeToLlvm arg t = error $ "AAA2" ++ show arg ++ show t
        let funcType = S.typeOf f
        let argsWithTypes = reverse $ snd $ foldr funcTypeToLlvm (funcType, []) (reverse args)
        let argNames = Map.fromList argsWithTypes


        modify (\s -> s { _locals = argNames, _outers = Map.empty, _usedVars = Set.empty } )
        e' <- go e1
        transformer (S.Function meta name tpe args e')
    e -> transformer e -- FIXME add Val/Match support!
  where go e = transformExpr transformer e



defineStringConstants :: S.Expr -> LLVM ()
defineStringConstants expr = case expr of
    S.Literal _ (S.StringLit s) -> defineStringLit s
    S.Literal _ _ -> return ()
    S.Ident{} -> return ()
    S.If _ cond true false -> do
        defineStringConstants cond
        defineStringConstants true
        defineStringConstants false
        return ()
    S.Select _ lhs rhs -> do
        defineStringConstants lhs
        defineStringConstants rhs
        return ()
    S.Array _ exprs -> mapM_ defineStringConstants exprs
    S.Apply meta _ exprs -> mapM_ defineStringConstants exprs
    S.Let _ _ e body -> do
        defineStringConstants e
        defineStringConstants body
        return ()
    S.Match _ e cases -> do
         defineStringConstants e
         mapM_ (\(S.Case p e) -> defineStringConstants e) cases
         return ()
    S.Lam{} -> error $ printf "defineStringConstants should be called after lambda lift! %s" (show expr)
    S.Val meta name expr -> do
        defineStringConstants expr
        return ()
    S.Function meta name retType args expr -> do
        defineStringConstants expr
        return ()
    S.EmptyExpr -> return ()
    S.BoxFunc{} -> return ()
    S.Data{} -> return ()

-------------------------------------------------------------------------------
-- Operations
-------------------------------------------------------------------------------

binops :: Map.Map String Int
binops = Map.fromList [("+", 10), ("-", 11), ("*", 12), ("/", 13),
    ("==", 42), ("!=", 43), ("<", 44), ("<=", 45), (">=", 46), (">", 47)]

-------------------------------------------------------------------------------
-- Compilation
-------------------------------------------------------------------------------
funcType retTy args = T.FunctionType retTy args False

initLascaRuntimeFuncType = funcType T.void [ptrType]
mainFuncType = funcType ptrType []
boxFuncType = funcType ptrType [T.i32]
boxArrayFuncType = T.FunctionType ptrType [T.i32] True
runtimeBinOpFuncType = funcType ptrType [T.i32, ptrType, ptrType]
runtimeApplyFuncType = funcType ptrType [ptrType, T.i32, ptrType, positionStructType]
runtimeSelectFuncType = funcType ptrType [ptrType, ptrType, positionStructType]
unboxFuncType = funcType ptrType [ptrType, T.i32]

sizeOfType tpe = do
    nullptr <- getelementptr (constOp (constNull tpe)) [constIntOp 1]
    sizeof <- ptrtoint nullptr T.i32 -- FIXME change to T.i64?
    return sizeof

gcMalloc size = callFn (funcType ptrType [T.i32]) "gcMalloc" [size]

gcMallocType tpe = do
    size <- sizeOfType tpe
    ptr <- gcMalloc size
    casted <- bitcast ptr (T.ptr tpe)
    return (ptr, casted)


box (S.BoolLit b) meta = callFn boxFuncType "boxBool" [constIntOp (boolToInt b)]
box (S.IntLit  n) meta = callFn boxFuncType "boxInt" [constIntOp n]
box (S.FloatLit  n) meta = callFn boxFuncType "boxFloat64" [constFloatOp n]
box S.UnitLit meta = callFn boxFuncType "box" [constIntOp 0,  constOp constNullPtr]
box (S.StringLit s) meta = do
    let name = getStringLitName s
    let len = ByteString.length . UTF8.fromString $ s
    let ref = global (stringStructType len) name
    ref' <- bitcast ref ptrType
    callFn boxFuncType "box" [constIntOp 4, ref']

createPosition S.NoPosition = createStruct [constInt 0, constInt 0] -- Postion (0, 0) means No Position. Why not.
createPosition S.Position{S.sourceLine, S.sourceColumn} = createStruct [C.Int 32 (toInteger sourceLine), C.Int 32 (toInteger sourceColumn)]

boxArray values = callFn boxFuncType "boxArray" (constIntOp (length values) : values)

boxFunc name mapping = do
    let idx = fromMaybe (error ("No such function " ++ name)) (Map.lookup name mapping)
    callFn (funcType ptrType [T.i32]) "boxFunc" [constIntOp idx]

boxError name = do
    modify (\s -> s { generatedStrings = name : generatedStrings s })
    let strLitName = getStringLitName name
    let len = length name
    let ref = global (stringStructType len) strLitName
    ref <- bitcast ref ptrType
    callFn (funcType ptrType [T.i32]) "boxError" [ref]

boxClosure :: String -> Map.Map String Int -> [S.Arg] -> Codegen AST.Operand
boxClosure name mapping enclosedVars = do
    syms <- gets symtab
    let idx = fromMaybe (error ("Couldn't find " ++ name ++ " in mapping:\n" ++ show mapping)) (Map.lookup name mapping)
    let argc = length enclosedVars
    let findArg n = fromMaybe (error ("Couldn't find " ++ n ++ " variable in symbols " ++ show syms)) (lookup n syms)
    let args = map (\(S.Arg n _) -> findArg n) enclosedVars
    sargsPtr <- gcMalloc (constIntOp $ ptrSize * argc)
    sargsPtr1 <- bitcast sargsPtr (T.ptr ptrType)
    let asdf (idx, arg) = do
            p <- getelementptr sargsPtr1 [idx]
            bc1 <- bitcast p (T.ptr ptrType)
            bc <- load arg
            store bc1 bc

    let sargs = sargsPtr
    sequence_ [asdf (constIntOp i, a) | (i, a) <- zip [0 .. argc] args]
    callFn (funcType ptrType [T.i32, T.i32, ptrType]) "boxClosure" [constIntOp idx, constIntOp argc, sargsPtr]

boolToInt True = 1
boolToInt False = 0

declareStdFuncs = do
    external T.void  "initLascaRuntime" [("runtime", ptrType)] False []
    external ptrType "gcMalloc" [("size", intType)] False []
    external ptrType "box" [("t", intType), ("ptr", ptrType)] False [FA.GroupID 0]
    external ptrType "unbox" [("t", intType), ("ptr", ptrType)] False [FA.GroupID 0]
    external T.i32   "unboxInt" [("ptr", ptrType)] False [FA.GroupID 0]
    external T.double "unboxFloat64" [("ptr", ptrType)] False [FA.GroupID 0]
    external ptrType "boxError" [("n", ptrType)] False [FA.GroupID 0]
    external ptrType "boxInt" [("d", intType)] False [FA.GroupID 0]
    external ptrType "boxBool" [("d", intType)] False [FA.GroupID 0]
    external ptrType "boxFunc" [("id", intType)] False [FA.GroupID 0]
    external ptrType "boxClosure" [("id", intType), ("argc", intType), ("argv", ptrType)] False []
    external ptrType "boxFloat64" [("d", T.double)] False [FA.GroupID 0]
    external ptrType "boxArray" [("size", intType)] True [FA.GroupID 0]
    external ptrType "runtimeBinOp"  [("code",  intType), ("lhs",  ptrType), ("rhs", ptrType)] False [FA.GroupID 0]
    external ptrType "runtimeApply"  [("func", ptrType), ("argc", intType), ("argv", ptrType), ("pos", positionStructType)] False []
    external ptrType "runtimeSelect" [("tree", ptrType), ("expr", ptrType), ("pos", positionStructType)] False [FA.GroupID 0]
    external T.void  "initEnvironment" [("argc", intType), ("argv", ptrType)] False []
  --  external ptrType "runtimeIsConstr" [("value", ptrType), ("name", ptrType)] False [FA.GroupID 0]
    addDefn $ AST.FunctionAttributes (FA.GroupID 0) [FA.ReadOnly]

--extractLambda :: S.Expr -> LLVM S.Expr
extractLambda (S.Lam meta arg expr) = do
    state <- get
    let nms = _modNames state
    let syntactic = _syntacticAst state
    let outerVars = Map.keysSet $ _outers state
    let usedOuterVars = Set.toList (Set.intersection outerVars (_usedVars state))
    let enclosedArgs = map (\n -> (S.Arg n typeAny, _outers state Map.! n)) usedOuterVars
    let (funcName', nms') = uniqueName "lambda" nms
    let funcName = show funcName'
    let asdf t = foldr (\(_, t) resultType -> TypeFunc t resultType) t enclosedArgs
    let meta' = (S.exprType %~ asdf) meta
    let func = S.Function meta' funcName typeAny (map fst enclosedArgs ++ [arg]) expr
    modify (\s -> s { _modNames = nms', _syntacticAst = syntactic ++ [func] })
  --   Debug.traceM ("Generated lambda " ++ show func ++ ", outerVars = " ++ show outerVars ++ ", usedOuterVars" ++ show usedOuterVars)
    return (S.BoxFunc meta' funcName (map fst enclosedArgs))
extractLambda expr@(S.Ident _ n) = do
    modify (\s -> s { _usedVars = Set.insert n (_usedVars s)})
    return expr
extractLambda expr = return expr

genFunctionMap :: [S.Expr] -> LLVM ()
genFunctionMap fns = do
    defineNames
    defineConst "Functions" (functionsStructType len) struct1
  --   Debug.traceM (show mapping)
  --   Debug.traceM (show array)
    modify (\s -> s { functions = mapping })
  where

    defineNames = mapM (\(name, _) -> defineStringLit name) funcsWithArities

    len = fromIntegral (length funcsWithArities)

    array = C.Array functionStructType (fmap snd entries)

    mapping :: Map.Map String Int
    mapping = go 0 Map.empty entries where
        go i m ((name, _) : es) = go (i + 1) (Map.insert name i m) es
        go i m [] = m



    entries = fmap (\(name, arity) -> (name, struct name arity)) funcsWithArities

    struct1 = createStruct [C.Int 32 (toInteger len), array]

    struct name arity = createStruct
                            [globalStringRefAsPtr name, constRef (fromString name), constInt arity]


    funcsWithArities = foldl go [] fns where
        go s (S.Data _ name consts) =
            -- Add data constructors as global functions
            let m = foldl (\acc (S.DataConst n args) -> (n, length args) : acc) [] consts
            in m ++ s
        go s (S.Function _ name tpe args body) = (name, length args) : s
        go s _ = s

genRuntime opts = defineConst "Runtime" runtimeStructType runtime
  where
    runtime = createStruct [constRef "Functions", constRef "Types", constBool $ S.verboseMode opts]


--genMatch :: Ctx -> S.Expr -> S.Expr
genMatch ctx m@(S.Match meta expr []) = error $ "Should be at least on case in match expression: " ++ show m
genMatch ctx (S.Match meta expr cases) = do
    let body = foldr (\(S.Case p e) acc -> genPattern ctx (S.Ident (expr ^. S.metaLens) "$match") p e acc) genFail cases
    return $ S.Let meta "$match" expr body  -- FIXME hack. Gen unique names
genMatch ctx expr = return expr

genFail = S.Apply S.emptyMeta (S.Ident S.emptyMeta "die") [S.Literal S.emptyMeta $ S.StringLit "Match error!"]

genPattern ctx lhs S.WildcardPattern rhs = const rhs
genPattern ctx lhs (S.VarPattern name) rhs = const (S.Let S.emptyMeta name lhs rhs)
genPattern ctx lhs (S.LitPattern literal) rhs = S.If S.emptyMeta (S.Apply S.emptyMeta (S.Ident S.emptyMeta "==") [lhs, S.Literal S.emptyMeta literal]) rhs
genPattern ctx lhs (S.ConstrPattern name args) rhs = cond
  where cond fail = S.If S.emptyMeta constrCheck (checkArgs name fail) fail
        constrCheck = S.Apply S.emptyMeta (S.Ident S.emptyMeta "runtimeIsConstr") [lhs, S.Literal S.emptyMeta $ S.StringLit name]
        constrMap = let cs = foldr (\ (S.DataDef _ _ constrs) acc -> constrs ++ acc) [] (S.dataDefs ctx)
                        tuples = fmap (\c@(S.DataConst n args) -> (n, args)) cs
                    in  Map.fromList tuples
        checkArgs nm fail =  case Map.lookup nm constrMap of
                            Nothing -> fail
                            Just constrArgs | length args == length constrArgs -> do
                                let argParam = zip args constrArgs
                                foldr (\(a, S.Arg n _) acc -> genPattern ctx (S.Select S.emptyMeta lhs (S.Ident S.emptyMeta n)) a acc fail) rhs argParam
                            Just constrArgs -> error (printf "Constructor %s has %d parameters, but %d given" nm (length constrArgs) (length args)) -- TODO box this error

desugarExpr ctx expr = do
    expr' <- extractLambda expr
    expr'' <- genMatch ctx expr'
    return expr''

desugarExprs ctx exprs = let
    (desugared, st) = runState (transform (desugarExpr ctx) exprs) emptyTrans
    syn = _syntacticAst st
  in desugared ++ syn