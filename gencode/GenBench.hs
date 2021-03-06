module Main where

import System.IO
import System.Exit
import System.Environment
import Control.Monad

genTopLevel idx = "def test" ++ (show idx) ++ "() = let x = 1+1 in if x > 2 then 1 else 2"
genLine idx = "x" ++ show idx ++ " = 1" ++ (if idx > 0 then " + x" ++ show (idx - 1) else "") ++ ";"


genNLines funcId n = do
  putStrLn $ "def test" ++ show funcId ++ "() = {"
  loop "" 0 0
  putStrLn "true"
  putStrLn "}"
    where
      loop code lastPrintedLines idx = do
          let newline = genLine idx
          let updatedCode = code ++ newline
          putStrLn newline
          if (idx) >= n
          then return () -- hPutStrLn stderr ("Generated " ++ (show idx) ++ " lines")
          else if (idx - lastPrintedLines) > 1000
               then do
                  let kloc = idx `div` 1000
--                  hPutStrLn stderr ("Generated " ++ (show kloc) ++ " klocs")
                  loop updatedCode idx (idx + 1)
               else loop updatedCode lastPrintedLines (idx + 1)

main :: IO ()
main = do
    args <- getArgs

    putStrLn "def main() = println(\"Done!\")"

    let (numFuncs, numLines) = case args of
            []      -> (10, 20)
            [numFuncs, numLines] -> (read numFuncs :: Integer, read numLines :: Integer)
    forM_ [1..numFuncs] $ \idx -> genNLines idx numLines
    return ()



