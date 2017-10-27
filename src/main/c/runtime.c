#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <gc.h>

// Operators
const int64_t ADD = 10;
const int64_t SUB = 11;                           // x - y
const int64_t MUL = 12;
const int64_t DIV = 13;                           // x / y
const int64_t MOD = 14;                           // x % y

const int64_t EQ = 42;                            // x == y
const int64_t NE = 43;                            // x != y
const int64_t LT = 44;                            // x < y
const int64_t LE = 45;                            // x <= y
const int64_t GE = 46;                            // x >= y
const int64_t GT = 47;                            // x > y
  // Boolean unary operations
const int64_t ZNOT = 50;                          // !x

  // Boolean binary operations
const int64_t ZOR = 60;                           // x || y
const int64_t ZAND = 61;                          // x && y

// Primitive Types
const int64_t UNIT     = 0;
const int64_t BOOL     = 1;
const int64_t INT      = 2;
const int64_t DOUBLE   = 3;
const int64_t STRING   = 4;
const int64_t CLOSURE  = 5;
const int64_t ARRAY    = 6;

typedef struct {
    int64_t type;
    union Value {
        int64_t num;
        double dbl;
        void* ptr;
    } value;
} Box;

typedef struct {
    int64_t length;
    char bytes[];
} String;

typedef struct {
    int64_t funcIdx;
    Box* args;  // boxed Array of boxed enclosed arguments
} Closure;

typedef struct {
    int64_t length;
    Box** data;
} Array;

typedef struct {
    String* name;
    void * funcPtr;
    int64_t arity;
} Function;

typedef struct {
    int64_t size;
    Function functions[];
} Functions;

typedef struct {
    int64_t typeId;
  //  int64_t tag;   // it's not set now. Not sure we need this
    String* name;
    int64_t numFields;
    String* fields[];
} Struct;

typedef struct {
    int64_t typeId;
    String* name;
    int64_t numValues;
    Struct* constructors[];
} Data;

typedef struct {
    int64_t size;
    Data* data[];
} Types;

typedef struct {
    int64_t argc;
    Box* argv;
} Environment;

typedef struct {
    Functions* functions;
    Types* types;
    int64_t verbose;
} Runtime;

typedef struct {
    int64_t tag;
    Box* values[];
} DataValue;

typedef struct {
    int64_t line;
    int64_t column;
} Position;

Box TRUE_SINGLETON = {
    .type = BOOL,
    .value.num = 1
};
Box FALSE_SINGLETON = {
    .type = BOOL,
    .value.num = 0
};
Box UNIT_SINGLETON = {
    .type = UNIT
};
String EMPTY_STRING = {
    .length = 0,
    .bytes = "\00"
};
Box EMPTY_STRING_BOX = {
    .type = STRING,
    .value.ptr = &EMPTY_STRING
};
Box * UNIT_STRING;
Box  INT_ARRAY[100];
Box  DOUBLE_ZERO = {
    .type = DOUBLE,
    .value.dbl = 0.0
};
Environment ENV;
Runtime* RUNTIME;


void *gcMalloc(size_t s) {
    return GC_malloc(s);
}

void *gcMallocAtomic(size_t s) {
    return GC_malloc_atomic(s);
}

void *gcRealloc(void* old, size_t s) {
    return GC_realloc(old, s);
}

Box* toString(Box* value);
Box* println(Box* val);
Box* boxArray(size_t size, ...);

const char * __attribute__ ((const)) typeIdToName(int64_t typeId) {
    switch (typeId) {
        case 0: return "Unit";
        case 1: return "Bool";
        case 2: return "Int";
        case 3: return "Double";
        case 4: return "String";
        case 5: return "<function>";
        case 6: return "Array";
        default: return "Unknown";
    }
}

/* =============== Boxing ================== */

Box *box(int64_t type_id, void *value) {
    Box* ti = gcMalloc(sizeof(Box));
    ti->type = type_id;
    ti->value.ptr = value;
    return ti;
}

Box * __attribute__ ((pure)) boxBool(int64_t i) {
    switch (i) {
        case 0: return &FALSE_SINGLETON; break;
        default: return &TRUE_SINGLETON; break;
    }
}

Box * __attribute__ ((pure)) boxError(String *name) {
    return box(-1, name);
}

Box * __attribute__ ((pure)) boxInt(int64_t i) {
    if (i >= 0 && i < 100) return &INT_ARRAY[i];
    else {
        Box* ti = gcMallocAtomic(sizeof(Box));
        ti->type = INT;
        ti->value.num = i;
        return ti;
    }
}

Box * __attribute__ ((pure)) boxFloat64(double i) {
    if (i == 0.0) return &DOUBLE_ZERO;
    Box* ti = gcMallocAtomic(sizeof(Box));
    ti->type = DOUBLE;
    ti->value.dbl = i;
    return ti;
}

Box * boxClosure(int64_t idx, Box* args) {
    Closure* cl = gcMalloc(sizeof(Closure));
//    printf("boxClosure(%d, %p)\n", idx, args);
//    fflush(stdout);
    cl->funcIdx = idx;
    cl->args = args;
//    printf("Enclose %d, argc = %d, args[0].type = %d, args[1].type = %d\n", idx, argc, args[0]->type, args[1]->type);
//    fflush(stdout);
    return box(CLOSURE, cl);
}

Box * __attribute__ ((pure)) boxFunc(int64_t idx) {
//    printf("boxFunc(%d)\n", idx);
//    fflush(stdout);
    return boxClosure(idx, NULL);
}

void * __attribute__ ((pure)) unbox(int64_t expected, Box* ti) {
  //  printf("unbox(%d, %d) ", ti->type, (int64_t) ti->value);
    if (ti->type == expected) {
        return ti->value.ptr;
    } else if (ti->type == -1) {
        String *name = (String *) ti->value.ptr;
        printf("AAAA!!! Undefined identifier %s\n", name->bytes);
        exit(1);
    } else {
        printf("AAAA!!! Expected %s but got %s\n", typeIdToName(expected), typeIdToName(ti->type));
        exit(1);
    }
}

int64_t __attribute__ ((pure)) unboxInt(Box* ti) {
  //  printf("unbox(%d, %d) ", ti->type, (int64_t) ti->value);
    if (ti->type == INT) {
        return ti->value.num;
    } else {
        printf("AAAA!!! Expected %s but got %s\n", typeIdToName(INT), typeIdToName(ti->type));
        exit(1);
    }
}

double __attribute__ ((pure)) unboxFloat64(Box* ti) {
  //  printf("unbox(%d, %d) ", ti->type, (int64_t) ti->value);
    if (ti->type == DOUBLE) {
        return ti->value.dbl;
    } else {
        printf("AAAA!!! Expected %s but got %s\n", typeIdToName(DOUBLE), typeIdToName(ti->type));
        exit(1);
    }
}

/* ==================== Runtime Ops ============== */

Box* updateRef(Box* ref, Box* value) {
    DataValue* dataValue = unbox(1000, ref);
    Box* oldValue = dataValue->values[0];
    dataValue->values[0] = value;
    return oldValue;
}

void* die(Box* msg) {
    println(msg);
    exit(1);
}

static int64_t isUserType(Box* v) {
    int64_t type = v->type;
    return type >= 1000 && (type < 1000 + RUNTIME->types->size);
}

#define DO_OP(op) if (lhs->type == INT) { result = boxInt(lhs->value.num op rhs->value.num); } else if (lhs->type == DOUBLE) { result = boxFloat64(lhs->value.dbl op rhs->value.dbl); } else { \
                        printf("AAAA!!! Type mismatch! Expected Int or Double for op but got %s\n", typeIdToName(lhs->type)); exit(1); }

#define DO_CMP(op) switch (lhs->type){ \
                   case BOOL:    { result = boxBool (lhs->value.num op rhs->value.num); break; } \
                   case INT:     { result = boxBool (lhs->value.num op rhs->value.num); break; } \
                   case DOUBLE:  { result = boxBool (lhs->value.dbl op rhs->value.dbl); break; } \
                   default: {printf("AAAA!!! Type mismatch! Expected Bool, Int or Double but got %s\n", typeIdToName(lhs->type)); exit(1); }\
                   }
Box* __attribute__ ((pure)) runtimeBinOp(int64_t code, Box* lhs, Box* rhs) {
    if (lhs->type != rhs->type) {
        printf("AAAA!!! Type mismatch! lhs = %s, rhs = %s\n", typeIdToName(lhs->type), typeIdToName(rhs->type));
        exit(1);
    }

    Box* result = NULL;

    switch (code) {
        case ADD: DO_OP(+); break;
        case SUB: DO_OP(-); break;
        case MUL: DO_OP(*); break;
        case DIV: DO_OP(/); break;
        case EQ:
            DO_CMP(==);
            break;
        case NE:
            DO_CMP(!=);
            break;
        case LT:
            DO_CMP(<);
            break;
        case LE:
            DO_CMP(<=);
            break;
        case GE:
            DO_CMP(>=);
            break;
        case GT:
            DO_CMP(>);
            break;
        default:
            printf("AAAA!!! Unsupported binary operation %lli", code);
            exit(1);
    }
    return result;
}

Box* __attribute__ ((pure)) runtimeUnaryOp(int64_t code, Box* expr) {
    Box* result = NULL;
    switch (code) {
        case 1:
            if (expr->type == INT) {
                result = boxInt(-expr->value.num);
            } else if (expr->type == DOUBLE) {
                result = boxFloat64(-expr->value.dbl);
            } else {
                printf("AAAA!!! Type mismatch! Expected Int or Double for op but got %s\n", typeIdToName(expr->type));
                exit(1);
            }
            break;
        default:
            printf("AAAA!!! Unsupported unary operation %lli", code);
            exit(1);
    }
    return result;
}

Box* arrayApply(Box* arrayValue, int64_t index) {
    Array* array = unbox(ARRAY, arrayValue);
    assert(array->length > index);
    return array->data[index];
}

int64_t arrayLength(Box* arrayValue) {
    Array* array = unbox(ARRAY, arrayValue);
    return array->length;
}

String UNIMPLEMENTED_SELECT = {
    .length = 20,
    .bytes = "Unimplemented select"
};

Box* runtimeApply(Box* val, int64_t argc, Box* argv[], Position pos) {
    Functions* fs = RUNTIME->functions;
    Closure *closure = unbox(CLOSURE, val);
    if (closure->funcIdx >= fs->size) {
        printf("AAAA!!! No such function with id %lld, max id is %lld at line: %lld\n", (int64_t) closure->funcIdx, fs->size, pos.line);
        exit(1);
    }
    Function f = fs->functions[closure->funcIdx];
    if (f.arity != argc + (closure->args == NULL ? 0 : 1)) {
        printf("AAAA!!! Function %s takes %lld params, but passed %d enclosed params and %lld params instead at line: %lld\n",
            f.name->bytes, f.arity, (closure->args == NULL ? 0 : 1), argc, pos.line);
        exit(1);
    }
    // TODO: use platform ABI, like it should be.
    // Currently, it's hardcoded for applying a function with up to 6 arguments in dynamic mode
    switch (f.arity) {
        case 0: {
            void* (*funcptr)() = f.funcPtr;
            return funcptr();
        }
        case 1: {
            void* (*funcptr)(Box*) = f.funcPtr;
            return (argc == 1) ? funcptr(argv[0]) : funcptr(closure->args);
        }
        case 2: {
            void* (*funcptr)(Box*, Box*) = f.funcPtr;
            switch (argc) {
                case 1: return funcptr(closure->args, argv[0]);
                case 2: return funcptr(argv[0], argv[1]);
            }
        }
        case 3: {
            void* (*funcptr)(Box*, Box*, Box*) = f.funcPtr;
                switch (argc) {
                    case 2: return funcptr(closure->args, argv[0], argv[1]);
                    case 3: return funcptr(argv[0], argv[1], argv[2]);
                }
        }
        case 4: {
            void* (*funcptr)(Box*, Box*, Box*, Box*) = f.funcPtr;
            switch (argc) {
                case 3: return funcptr(closure->args, argv[0], argv[1], argv[2]);
                case 4: return funcptr(argv[0], argv[1], argv[2], argv[3]);
            }
        }
        case 5: {
            void* (*funcptr)(Box*, Box*, Box*, Box*, Box*) = f.funcPtr;
            switch (argc) {
                case 4: return funcptr(closure->args, argv[0], argv[1], argv[2], argv[3]);
                case 5: return funcptr(argv[0], argv[1], argv[2], argv[3], argv[4]);
            }
        }
        case 6: {
            void* (*funcptr)(Box*, Box*, Box*, Box*, Box*, Box*) = f.funcPtr;
            switch (argc) {
                case 5: return funcptr(closure->args, argv[0], argv[1], argv[2], argv[3], argv[4]);
                case 6: return funcptr(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
            }
        }
        case 7: {
            void* (*funcptr)(Box*, Box*, Box*, Box*, Box*, Box*, Box*) = f.funcPtr;
            switch (argc) {
                case 6: return funcptr(closure->args, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
                case 7: return funcptr(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
            }
        }
        default:
            printf("AAAA! Unsupported arity %lld at line: %lld\n", f.arity, pos.line);
            exit(1);
            break;
    };
    //  return NULL;

}

Box* __attribute__ ((pure)) runtimeSelect(Box* tree, Box* ident, Position pos) {
    Functions* fs = RUNTIME->functions;
    Types* types = RUNTIME->types;
    if (isUserType(tree)) {
    //    printf("Found structs %d\n", structs->size);

        DataValue* dataValue = tree->value.ptr;
        // if rhs is not a local ident, nor a function, try to find this field in lhs data structure
        if (ident->type == -1) {
            String* name = ident->value.ptr; // should be identifier name
      //      printf("Ident name %s\n", name->bytes);
            Data* data = types->data[tree->type - 1000]; // find struct in global array of structs
      //      printf("Found data type %s %d, tag %d\n", data->name->bytes, tree->type, dataValue->tag);
            Struct* constr = data->constructors[dataValue->tag];
            int64_t numFields = constr->numFields;
            for (int64_t i = 0; i < numFields; i++) {
                String* field = constr->fields[i];
        //        printf("Check field %d %s\n", field->length, field->bytes);
                if (field->length == name->length && strncmp(field->bytes, name->bytes, name->length) == 0) {
                    Box* value = dataValue->values[i];
          //          printf("Found value %d at index %d\n", value->type, i);
          //          println(toString(value));
                    return value;
                }
            }
            printf("Couldn't find field %s at line: %lld\n", name->bytes, pos.line);
        } else if (ident->type == CLOSURE) {
              // FIXME fix for closure?  check arity?
              Closure* f = unbox(CLOSURE, ident);
              assert(fs->functions[f->funcIdx].arity == 1);
              return runtimeApply(ident, 1, &tree, pos);
        }
    } else if (ident->type == CLOSURE) {
        // FIXME fix for closure?  check arity?
        Closure* f = unbox(CLOSURE, ident);
        assert(fs->functions[f->funcIdx].arity == 1);
        return runtimeApply(ident, 1, &tree, pos);
    }
    return boxError(&UNIMPLEMENTED_SELECT);
}

Box* runtimeIsConstr(Box* value, Box* constrName) {
    if (isUserType(value)) {
        String* name = unbox(STRING, constrName);
        Data* data = RUNTIME->types->data[value->type - 1000];
        DataValue* dv = value->value.ptr;
        String* realConstrName = data->constructors[dv->tag]->name;
        if (strncmp(realConstrName->bytes, name->bytes, fmin(realConstrName->length, name->length)) == 0)
            return &TRUE_SINGLETON;
    }
    return &FALSE_SINGLETON;
}
/* ================== IO ================== */

void* putInt(int64_t c) {
    printf("%lld\n", c);
    fflush(stdout);
    return 0;
}

void* putchard(double X) {
    printf("%12.9lf\n", X);
    fflush(stdout);
    return 0;
}

void * runtimePutchar(Box* ch) {
    char c = (char) unboxInt(ch);
    putchar(c);
    fflush(stdout);
    return 0;
}

Box* println(Box* val) {
    String * str = unbox(STRING, val);
    printf("%s\n", str->bytes);
    return &UNIT_SINGLETON;
}


/* =================== Arrays ================= */


Array* createArray(size_t size) {
    Array * array = gcMalloc(sizeof(Array));
    array->length = size;
    array->data = (size > 0 ) ? gcMalloc(sizeof(Box*) * size) : NULL;
    return array;
}

Box* boxArray(size_t size, ...) {
    va_list argp;
    Array * array = createArray(size);
    va_start (argp, size);
    for (int64_t i = 0; i < size; i++) {
        Box* arg = va_arg(argp, Box*);
        array->data[i] = arg;
    }
    va_end (argp);                  /* Clean up. */
    return box(ARRAY, array);
}

Box* append(Box* arrayValue, Box* value) {
    Array* array = unbox(ARRAY, arrayValue);
    Array * newArray = createArray(array->length + 1);
    memcpy(newArray->data, array->data, sizeof(Box*) * array->length);
    newArray->data[array->length] = value;
    return box(ARRAY, newArray);
}

Box* prepend(Box* arrayValue, Box* value) {
    Array* array = unbox(ARRAY, arrayValue);
    Array * newArray = createArray(array->length + 1);
    memcpy(newArray->data + 1, array->data, sizeof(Box*) * array->length);
    newArray->data[0] = value;
    return box(ARRAY, newArray);
}

Box* __attribute__ ((pure)) makeString(char * str) {
    size_t len = strlen(str);
    String* val = gcMalloc(sizeof(String) + len + 1);  // null terminated
    val->length = len;
    strncpy(val->bytes, str, len);
    return box(STRING, val);
}

Box* joinValues(int size, Box* values[], char* start, char* end) {
    int seps = size * 2;
    size_t resultSize = size * 32; // assume 32 bytes per array element. No reason, just guess
    char * result = gcMalloc(resultSize); // zero-initialized, safe to use
    strcat(result, start);
    size_t curSize = strlen(start) + strlen(end);
    for (int i = 0; i < size; i++) {
        Box* elem = values[i];
        String* value = unbox(STRING, toString(elem));
        // realloc if we don't fit it the result buffer
        if (curSize + value->length >= resultSize + 10) { // reserve 10 bytes for trailing ']' etc
            size_t newSize = resultSize * 1.5;
            result = gcRealloc(result, newSize);
            resultSize = newSize;
        }
        strncat(result, value->bytes, value->length);
        curSize += value->length;
        if (i + 1 < size) {
            strcat(result, ", ");
            curSize += 2;
        }
    }
    strcat(result, end);
    return makeString(result);
}

Box* __attribute__ ((pure)) arrayToString(Box* arrayValue)  {
    Array* array = unbox(ARRAY, arrayValue);
    if (array->length == 0) {
        return makeString("[]");
    } else {
        return joinValues(array->length, array->data, "[", "]");
    }
}

/* =============== Strings ============= */

Box* __attribute__ ((pure)) toString(Box* value) {
    char buf[100]; // 100 chars is enough for all (c)

    int64_t type = value->type;
    switch (type) {
        case UNIT: return UNIT_STRING;
        case BOOL: return makeString(value->value.num == 0 ? "false" : "true");
        case INT:
            snprintf(buf, 100, "%lld", value->value.num);
            return makeString(buf);
        case DOUBLE:
            snprintf(buf, 100, "%12.9lf", value->value.dbl);
            return makeString(buf);
        case STRING:  return value;
        case CLOSURE: return makeString("<func>");
        case ARRAY:   return arrayToString(value);
        case -1: {
            String *name = (String *) value->value.ptr;
            printf("AAAA!!! Undefined identifier %s\n", name->bytes);
            exit(1);
        }
        case 1000: {
            DataValue* dataValue = value->value.ptr;
            return toString(dataValue->values[0]);
        }
        default:
            if (isUserType(value)) {
                DataValue* dataValue = value->value.ptr;
                Data* metaData = RUNTIME->types->data[type - 1000];
                Struct* constr = metaData->constructors[dataValue->tag];
                int64_t startlen = constr->name->length + 2; // ending 0 and possibly "(" if constructor has parameters
                char start[startlen];
                snprintf(start, startlen, "%s", constr->name->bytes);
                if (constr->numFields > 0) {
                    strcat(start, "(");
                    return joinValues(constr->numFields, dataValue->values, start, ")");
                } else return makeString(start);
            } else {
                printf("Unsupported type %lld", value->type);
                exit(1);
            }
    }
}

Box* concat(Box* arrayString) {
    Array* array = unbox(ARRAY, arrayString);
    Box* result = &EMPTY_STRING_BOX;
    if (array->length > 0) {
        int64_t len = 0;
        for (int64_t i = 0; i < array->length; i++) {
            String* s = unbox(STRING, array->data[i]);
            len += s->length;
        }
        String* val = gcMalloc(sizeof(String) + len + 1); // +1 for null-termination
        // val->length is 0, because gcMalloc allocates zero-initialized memory
        // it's also zero terminated, because gcMalloc allocates zero-initialized memory
        for (int64_t i = 0; i < array->length; i++) {
            String* s = unbox(STRING, array->data[i]);
            memcpy(&val->bytes[val->length], s->bytes, s->length);
            val->length += s->length;
        }
        result = box(STRING, val);
    }
    return result;
}

/* ============ System ================ */

void initEnvironment(int64_t argc, char* argv[]) {
  //  int64_t len = 0;
  //  for (int64_t i = 0; i< argc; i++) len += strlen(argv[i]);
  //  char buf[len + argc*2 + 10];
  //  for (int64_t i = 0; i < argc; i++) {
  //    strcat(buf, argv[i]);
  //    strcat(buf, " ");
  //  }
  //  printf("Called with %d \n", argc);
    ENV.argc = argc;
    Array* array = createArray(argc);
    for (int64_t i = 0; i < argc; i++) {
        Box* s = makeString(argv[i]);
        array->data[i] = s;
    }
    ENV.argv = box(ARRAY, array);
}

Box* getArgs() {
    return ENV.argv;
}

int64_t toInt(Box* s) {
    String* str = unbox(STRING, s);
  //  println(s);
    char cstr[str->length + 1];
    memcpy(cstr, str->bytes, str->length); // TODO use VLA?
    cstr[str->length] = 0;
  //  printf("cstr = %s\n", cstr);
    char *ep;
    long i = strtol(cstr, &ep, 10);
    if (cstr == ep) {
        printf("Couldn't convert %s to int64_t", cstr);
        exit( EXIT_FAILURE );
    }
    return (int64_t) i;
}

void initLascaRuntime(Runtime* runtime) {
    GC_init();
    GC_expand_hp(4*1024*1024);
    RUNTIME = runtime;
    UNIT_STRING = makeString("()");
    for (int i = 0; i < 100; i++) {
        INT_ARRAY[i].type = INT;
        INT_ARRAY[i].value.num = i;
    }
    if (runtime->verbose)
        printf("Init Lasca 0.0.0.1 runtime. Enjoy :)\n# funcs = %lld, # structs = %lld\n",
          RUNTIME->functions->size, RUNTIME->types->size);
}

/*
Box* lascaOpenFile(Box* filename, Box* mode) {
    String* str = unbox(STRING, filename);
    String* md = unbox(STRING, mode);
    FILE* f = fopen(str->bytes, md->bytes);
    return box
}*/
