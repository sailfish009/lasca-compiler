#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <gc.h>
#include <sys/types.h> /* for pid_t */
#include <sys/stat.h>
#include <sys/wait.h> /* for wait */
#include <utf8proc.h>
#include "lasca.h"
#include <pcre2.h>


int64_t libcErrno() {
    return errno;
}

String* libcError(int64_t error) {
    return makeString(strerror(error));
}

String* libcCurError() {
    return makeString(strerror(errno));
}

/* ================== IO ================== */

/* Bitwise stuff */

int8_t byteAnd(int8_t a, int8_t b) { return a & b; }
int8_t byteOr(int8_t a, int8_t b) { return a | b; }
int8_t byteXor(int8_t a, int8_t b) { return a ^ b; }
int8_t byteNot(int8_t a) { return ~a; }
int8_t byteShiftL(int8_t a, int8_t b) { return (int8_t) (a << b); }
int8_t byteShiftR(int8_t a, int8_t b) { return (int8_t) (a >> b); }

int64_t intAnd(int64_t a, int64_t b) { return a & b; }
int64_t intOr(int64_t a, int64_t b) { return a | b; }
int64_t intXor(int64_t a, int64_t b) { return a ^ b; }
int64_t intNot(int64_t a) { return ~a; }
int64_t intShiftL(int64_t a, int64_t b) { return a << b; }
int64_t intShiftR(int64_t a, int64_t b) { return a >> b; }
int64_t intPopCount(int64_t a) { return __builtin_popcountll(a); }
int64_t intRem(int64_t a, int64_t b) { return a % b; }

/* Strings / Unicode stuff */

int64_t bytesLength(Box* string) {
    String * str = unbox(LASTRING, string);
    return str->length;
}

Box* codePointsIterate(Box* string, Box* f) {
    String * str = unbox(LASTRING, string);
    bool cont = true;
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t offset = 0;
    size_t length = 0;
    Position pos = {0, 0};
    while (cont) {
        offset += utf8proc_iterate((const utf8proc_uint8_t *) str->bytes + offset, -1, &codepoint);
        if (codepoint == -1) {
            printf("Invalid UTF-8 near position %zd\n", offset);
            exit(1);
        } if (codepoint == 0) {
            return &UNIT_SINGLETON;
        } else {
            Box* cp = (Box*) boxInt32(codepoint);
            Box* res = runtimeApply(f, 1, &cp, pos);
            cont = asBool(unbox(LABOOL, res))->num;
        }
    }
    return &UNIT_SINGLETON;
}

Box* graphemesIterate(Box* string, Box* f) {
    String * str = unbox(LASTRING, string);
    bool cont = true;
    utf8proc_int32_t prev = 0x00ad; // soft hyphen (grapheme break always allowed after this)
    utf8proc_int32_t codepoint = -1;
    utf8proc_int32_t state = 0;
    utf8proc_ssize_t offset = 0;
    size_t length = 0;
    utf8proc_int32_t cluster[65];
    Position pos = {0, 0};
    while (cont) {
        // FIXME
        if (length >= 64) {
            printf("Unsupported grapheme cluster length >= 64 codepoints at %zd\n", offset);
            exit(1);
        }
        offset += utf8proc_iterate((const utf8proc_uint8_t *) str->bytes + offset, -1, &codepoint);
        if (codepoint == 0) return &UNIT_SINGLETON;
        else if (codepoint == -1) {
            printf("Invalid UTF-8 near position %zd\n", offset);
            exit(1);
        } else {
            cluster[length] = codepoint;
            bool brk = utf8proc_grapheme_break_stateful(prev, codepoint, &state);
            length += 1;
            prev = codepoint;

            if (brk) {
                cluster[length + 1] = 0;
                Box* s = (Box*) makeString((char *) cluster);
                Box* res = runtimeApply(f, 1, &s, pos);
                cont = asBool(unbox(LABOOL, res))->num;
                length = 0;
            }
        }
    }
    return &UNIT_SINGLETON;
}

String* codePointToString(int32_t codePoint) {
    assert(utf8proc_codepoint_valid(codePoint));
    utf8proc_uint8_t buf[5];
    utf8proc_ssize_t idx = utf8proc_encode_char(codePoint, buf);
    buf[idx] = 0; // \0 termination
    return makeString((char *) buf);
}

Box* codePointsToString(Box* array) {
    Array* arr = unbox(LAARRAY, array);
    utf8proc_ssize_t len = 4 * arr->length; // potentially each codepoint is encoded as 4 bytes utf8.
    String* string = gcMalloc(sizeof(String) + len + 1);
    utf8proc_ssize_t offset = 0;
    for (size_t i = 0; i < arr->length; i++) {
        offset += utf8proc_encode_char(asInt32(arr->data[i])->num, (utf8proc_uint8_t *) &string->bytes[offset]);
    }
    string->type = LASTRING;
    string->bytes[offset] = 0;
    string->length = offset;
    return box(LASTRING, string);
 }

Box* print(const Box* val) {
    String * str = unbox(LASTRING, val);
    printf("%s", str->bytes);
    return &UNIT_SINGLETON;
}

Box* println(const Box* val) {
//    printf("println: %p %p\n", LASTRING, val->type);
    String * str = unbox(LASTRING, val);
    printf("%s\n", str->bytes);
    return &UNIT_SINGLETON;
}

int64_t runtimeCompare(Box* lhs, Box* rhs) {
    if (!eqTypes(lhs->type, rhs->type)) {
        printf("AAAA!!! runtimeCompare: Type mismatch! lhs = %s, rhs = %s\n", typeIdToName(lhs->type), typeIdToName(rhs->type));
        exit(1);
    }
    int64_t result = 0;
    if (eqTypes(lhs->type, LABOOL)) {
        result =
                asBool(lhs)->num < asBool(rhs)->num ? -1 :
                asBool(lhs)->num == asBool(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LABYTE)) {
        result =
                asByte(lhs)->num < asByte(rhs)->num ? -1 :
                asByte(lhs)->num == asByte(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LAINT)) {
        result =
                asInt(lhs)->num < asInt(rhs)->num ? -1 :
                asInt(lhs)->num == asInt(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LAINT16)) {
        result =
                asInt16(lhs)->num < asInt16(rhs)->num ? -1 :
                asInt16(lhs)->num == asInt16(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LAINT32)) {
        result =
                asInt32(lhs)->num < asInt32(rhs)->num ? -1 :
                asInt32(lhs)->num == asInt32(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LAFLOAT64)) {
        result =
                asFloat(lhs)->num < asFloat(rhs)->num ? -1 :
                asFloat(lhs)->num == asFloat(rhs)->num ? 0 : 1;
    } else if (eqTypes(lhs->type, LASTRING)) {
        result = strcmp(asString(lhs)->bytes, asString(rhs)->bytes); // TODO do proper unicode stuff
    } else {
        printf("AAAA!!! runtimeCompare is not defined for type %s\n", typeIdToName(lhs->type));
        exit(1);
    }
    result = result < 0 ? (int64_t) -1 : result == 0 ? 0 : 1;
    return result;
}

Box* arrayAppend(Box* fst, Box* snd) {
    Array* f = unbox(LAARRAY, fst);
    Array* s = unbox(LAARRAY, snd);
    Array* array = createArray(f->length + s->length);
    memcpy(array->data, f->data, f->length * sizeof(void*));
    memcpy(array->data + f->length, s->data, s->length * sizeof(void*));
    return box(LAARRAY, array);
}

Box* makeArray(int64_t size, Box* init) {
    Array* array = createArray(size);
    for (int64_t i = 0; i < size; i++) {
        array->data[i] = init;
    }
    return box(LAARRAY, array);
}

Box* arrayCopy(Box* src, int64_t srcPos, Box* dest, int64_t destPos, int64_t length) {
    Array* s = unbox(LAARRAY, src);
    Array* d = unbox(LAARRAY, dest);
    assert(srcPos >= 0);
    assert(destPos >= 0);
    assert(length >= 0);
    assert(srcPos+length <= s->length);
    assert(destPos+length <= d->length);
    memcpy(&d->data[destPos], &s->data[srcPos], length * sizeof(void*));
    return &UNIT_SINGLETON;
}

Box* arrayGetIndex(Box* arrayValue, int64_t index) {
    Array* array = unbox(LAARRAY, arrayValue);
    assert(array->length > index);
    return array->data[index];
}

Box* arraySetIndex(Box* arrayValue, int64_t index, Box* value) {
    Array* array = unbox(LAARRAY, arrayValue);
    assert(array->length > index);
    array->data[index] = value;
    return &UNIT_SINGLETON;
}

Box* arrayInit(int64_t size, Box* f) {
    Array* array = createArray(size);
    Closure* cl = unbox(LACLOSURE, f);
    Position pos = {0, 0};
    for (int64_t i = 0; i < size; i++) {
        Box* argv[1];
        argv[0] = (Box*) boxInt(i);
        array->data[i] = runtimeApply(f, 1, argv, pos);
    }
    return box(LAARRAY, array);
}

int64_t arrayLength(Box* arrayValue) {
    Array* array = unbox(LAARRAY, arrayValue);
    return array->length;
}

Box* createByteArray(size_t size) {
    String* val = gcMalloc(sizeof(String) + size);
    val->length = size;
    return box(LABYTEARRAY, val);
}

int64_t byteArrayLength(Box* value) {
    String* array = unbox(LABYTEARRAY, value);
    return array->length;
}

int8_t byteArrayGetIndex(Box* arrayValue, int64_t index) {
    String* array = unbox(LABYTEARRAY, arrayValue);
    assert(array->length > index);
    return array->bytes[index];
}

Box* byteArraySetIndex(Box* arrayValue, int64_t index, int8_t value) {
    String* array = unbox(LABYTEARRAY, arrayValue);
    assert(array->length > index);
    array->bytes[index] = value;
    return &UNIT_SINGLETON;
}

Box* byteArrayCopy(Box* src, int64_t srcPos, Box* dest, int64_t destPos, int64_t length) {
    Array* s = unbox(LABYTEARRAY, src);
    Array* d = unbox(LABYTEARRAY, dest);
    assert(srcPos >= 0);
    assert(destPos >= 0);
    assert(length >= 0);
    assert(srcPos+length <= s->length);
    assert(destPos+length <= d->length);
    memcpy(&d->data[destPos], &s->data[srcPos], length);
    return &UNIT_SINGLETON;
}

Box* lascaOpenFile(Box* filename, Box* mode) {
    String *fname = unbox(LASTRING, filename);
    String *fm = unbox(LASTRING, mode);
    FILE *f = fopen(fname->bytes, fm->bytes);
    if (f == NULL) {
        printf("AAAA!!! lascaOpenFile error: %s\n", strerror(errno));
        exit(1);
    }
    return box(LAFILE_HANDLE, f);
}

Box* lascaReadFile(Box* filename) {
    String *fname = unbox(LASTRING, filename);
    FILE *f = fopen(fname->bytes, "r");
    if (f == NULL) {
        printf("AAAA!!! lascaReadFile error: %s\n", strerror(errno));
        exit(1);
    }
    struct stat st;
    fstat(fileno(f), &st);
    size_t size = st.st_size;
    String *s = gcMalloc(sizeof(String) + size + 1);
    s->length = size;
    size_t read = fread(s->bytes, size, 1, f);
    if (read != 1) {
        printf("AAAA!!! lascaReadFile: Expected to read %zu bytes, but read only %zu: %s\n", size, read, strerror(errno));
        exit(1);
    }
    fclose(f);
    return box(LASTRING, s);
}

Box* lascaWriteFile(Box* filename, Box* string) {
    String *fname = unbox(LASTRING, filename);
    String *s = unbox(LASTRING, string);
    FILE *f = fopen(fname->bytes, "w+");
    if (f == NULL) {
        printf("AAAA!!! lascaWriteFile error: %s\n", strerror(errno));
        exit(1);
    }
    size_t read = fwrite(s->bytes, strlen(s->bytes), 1, f);
    if (read != 1) {
        printf("AAAA!!! lascaWriteFile: Couldn't write to file %s: %s\n", fname->bytes, strerror(errno));
        exit(1);
    }
    fclose(f);
    return &UNIT_SINGLETON;
}

void finalizePcre2Code(Pattern* pattern, void* unused) {
//    printf("finalizePcre2Code\n");
    pcre2_code_free(pattern->re);
}

Pattern* lascaCompileRegex(Box* ptrn) {
    String* string = unbox(LASTRING, ptrn);
    PCRE2_SPTR pattern = (PCRE2_SPTR) string->bytes;
    int errornumber = 0;
    PCRE2_SIZE erroroffset = 0;
    int32_t option_bits = 0;

    pcre2_code *re = pcre2_compile(
      pattern,               /* the pattern */
      string->length,
      PCRE2_UTF,                     /* default options */
      &errornumber,          /* for error number */
      &erroroffset,          /* for error offset */
      NULL);


    int32_t isJit = 0, unicode = 0;
    pcre2_config(PCRE2_CONFIG_JIT, &isJit);
    pcre2_config(PCRE2_CONFIG_UNICODE, &unicode);
    (void)pcre2_pattern_info(re, PCRE2_INFO_ALLOPTIONS, &option_bits);
    // printf("lascaCompileRegex: jit is %d, unicode: %d opts: %x, err: %d, off: %zu, pat: %s\n", isJit, unicode, option_bits, errornumber, erroroffset, pattern);


    /* Compilation failed: print the error message and exit. */

    if (re == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
        printf("PCRE2 compilation failed at offset %d: %s\n", (int)erroroffset, buffer);
        exit(1);
    }
    Pattern* boxedRe = gcMalloc(sizeof(Pattern));
    boxedRe->type = LAPATTERN;
    boxedRe->re = re;
    GC_register_finalizer(boxedRe, (GC_finalization_proc)finalizePcre2Code, 0, 0, 0);
    return boxedRe;
}

int8_t lascaMatchRegex(Box* ptrn, Box* string) {
    pcre2_code *re = ((Pattern*) unbox(LAPATTERN, ptrn))->re;
    String* subject = unbox(LASTRING, string);
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);

    int rc = pcre2_match(
      re,                   /* the compiled pattern */
      (PCRE2_SPTR) subject->bytes,              /* the subject string */
      subject->length,       /* the length of the subject */
      0,                    /* start at offset 0 in the subject */
      PCRE2_NO_JIT,
      match_data,           /* block for storing the result */
      NULL);                /* use default match context */

    pcre2_match_data_free(match_data);   /* Release memory used for the match */
    if (rc < 0) {
        switch(rc) {
            case PCRE2_ERROR_NOMATCH: /* printf("No match\n") */; break;
            default: printf("Matching error %d\n", rc); break;
        }
        return false;
    }
    else return true;
}

Box* lascaRegexReplace(Box* ptrn, Box* string, Box* replace) {
    pcre2_code *re = ((Pattern*) unbox(LAPATTERN, ptrn))->re;
    String* subject = unbox(LASTRING, string);
    String* subst = unbox(LASTRING, replace);

    uint32_t options = PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    size_t len = subject->length * 1.2; // approximate
    PCRE2_SIZE outlengthptr = len;
    String* val = gcMalloc(sizeof(String) + len + 1);  // null terminated

    int rc = pcre2_substitute(re, (PCRE2_SPTR) subject->bytes, subject->length, 0, options, 0, 0,
                (PCRE2_SPTR) subst->bytes, subst->length, (PCRE2_UCHAR *) val->bytes, &outlengthptr);

    if (rc == PCRE2_ERROR_NOMEMORY) {
        // printf("lascaRegexReplace::PCRE2_ERROR_NOMEMORY asked %zu but needed %zu for subst: %s\n",
                // len, outlengthptr, subst->bytes);
        // outlengthptr should contain required length in code units, bytes here,
        // including space for trailing zero, see https://www.pcre.org/current/doc/html/pcre2api.html#SEC36
        val = gcMalloc(sizeof(String) + outlengthptr);
        rc = pcre2_substitute(re, (PCRE2_SPTR) subject->bytes, subject->length, 0, options, 0, 0,
                (PCRE2_SPTR) subst->bytes, subst->length, (PCRE2_UCHAR *) val->bytes, &outlengthptr);
    }
    val->type = LASTRING;
    val->length = outlengthptr;

    if (rc < 0) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(rc, buffer, sizeof(buffer));
        printf("PCRE2 substitution error %d: %s\n", rc, buffer);
        exit(1);
    }
    return box(LASTRING, val);
}

/* OS/POSIX functions */

String* lascaGetCwd() {
    char* dir = getcwd(NULL, 0);
    if (dir == NULL) {
        printf("lascaGetCwd error: %s\n", strerror(errno));
        exit(1);
    }
    return makeString(dir);
}

Option* lascaChdir(Box* path) {
    String* dir = unbox(LASTRING, path);
    int res = chdir(dir->bytes);
    if (res == -1) {
        return some((Box*) libcCurError());
    }
    return &NONE;
}

Option* getEnv(Box* name) {
    String* nm = unbox(LASTRING, name);
    char* res = getenv(nm->bytes);
    if (res == NULL) return &NONE;
    return some((Box*) makeString(res));
}

int64_t setEnv(Box* name, Box* value, int8_t replace) {
    String* nm = unbox(LASTRING, name);
    String* v = unbox(LASTRING, value);
    return setenv(nm->bytes, v->bytes, replace);
}

int64_t unsetEnv(Box* name) {
    String* nm = unbox(LASTRING, name);
    return unsetenv(nm->bytes);
}
