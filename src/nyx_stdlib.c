#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#include "nyx_stdlib.h"

// Helpers

static void defineNativeStd(const char* name, NyxNativeFn fn) {
    nyx_vm_push(OBJ_VAL(nyx_copy_string(name, (int)strlen(name))));
    nyx_vm_push(OBJ_VAL(nyx_new_native(fn, name)));
    nyx_table_set(&vm.globals, AS_STRING(vm.stackTop[-2]), vm.stackTop[-1]);
    nyx_vm_pop();
    nyx_vm_pop();
}

static double toNum(NyxValue v) {
    if (IS_INT(v)) return (double)AS_INT(v);
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    return 0.0;
}

// Math (the boring-but-necessary section)

static NyxValue mathAbs(int ac, NyxValue* a) {
    (void)ac;
    if (IS_INT(a[0])) {
        int64_t v = AS_INT(a[0]);
        return INT_VAL(v < 0 ? -v : v);
    }
    return FLOAT_VAL(fabs(toNum(a[0])));
}

static NyxValue mathFloor(int ac, NyxValue* a) {
    (void)ac; return INT_VAL((int64_t)floor(toNum(a[0])));
}

static NyxValue mathCeil(int ac, NyxValue* a) {
    (void)ac; return INT_VAL((int64_t)ceil(toNum(a[0])));
}

static NyxValue mathRound(int ac, NyxValue* a) {
    (void)ac; return INT_VAL((int64_t)round(toNum(a[0])));
}

static NyxValue mathSqrt(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(sqrt(toNum(a[0])));
}

static NyxValue mathSin(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(sin(toNum(a[0])));
}

static NyxValue mathCos(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(cos(toNum(a[0])));
}

static NyxValue mathTan(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(tan(toNum(a[0])));
}

static NyxValue mathPow(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(pow(toNum(a[0]), toNum(a[1])));
}

static NyxValue mathLog(int ac, NyxValue* a) {
    (void)ac; return FLOAT_VAL(log(toNum(a[0])));
}

static NyxValue mathMin(int ac, NyxValue* a) {
    (void)ac;
    if (IS_INT(a[0]) && IS_INT(a[1])) {
        int64_t x = AS_INT(a[0]), y = AS_INT(a[1]);
        return INT_VAL(x < y ? x : y);
    }
    double x = toNum(a[0]), y = toNum(a[1]);
    return FLOAT_VAL(x < y ? x : y);
}

static NyxValue mathMax(int ac, NyxValue* a) {
    (void)ac;
    if (IS_INT(a[0]) && IS_INT(a[1])) {
        int64_t x = AS_INT(a[0]), y = AS_INT(a[1]);
        return INT_VAL(x > y ? x : y);
    }
    double x = toNum(a[0]), y = toNum(a[1]);
    return FLOAT_VAL(x > y ? x : y);
}

static NyxValue mathClamp(int ac, NyxValue* a) {
    (void)ac;
    double val = toNum(a[0]), lo = toNum(a[1]), hi = toNum(a[2]);
    double result = val < lo ? lo : (val > hi ? hi : val);
    if (IS_INT(a[0]) && IS_INT(a[1]) && IS_INT(a[2]))
        return INT_VAL((int64_t)result);
    return FLOAT_VAL(result);
}

static bool seeded = false;

static NyxValue mathRandom(int ac, NyxValue* a) {
    (void)ac; (void)a;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
    return FLOAT_VAL((double)rand() / (double)RAND_MAX);
}

static NyxValue mathRandomInt(int ac, NyxValue* a) {
    (void)ac;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
    int64_t lo = IS_INT(a[0]) ? AS_INT(a[0]) : (int64_t)toNum(a[0]);
    int64_t hi = IS_INT(a[1]) ? AS_INT(a[1]) : (int64_t)toNum(a[1]);
    if (hi <= lo) return INT_VAL(lo);
    return INT_VAL(lo + rand() % (hi - lo));
}

static NyxValue mathPI(int ac, NyxValue* a) {
    (void)ac; (void)a; return FLOAT_VAL(3.14159265358979323846);
}

static NyxValue mathE(int ac, NyxValue* a) {
    (void)ac; (void)a; return FLOAT_VAL(2.71828182845904523536);
}

// String Functions (there are always more string functions)

static NyxValue strSplit(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1])) return OBJ_VAL(nyx_new_list());

    NyxObjString* str = AS_STRING(a[0]);
    NyxObjString* delim = AS_STRING(a[1]);
    NyxObjList* list = nyx_new_list();
    nyx_vm_push(OBJ_VAL(list)); // GC protect

    if (delim->length == 0) {
        // empty delimiter = split into individual chars
        for (int i = 0; i < str->length; i++) {
            nyx_value_array_write(&list->items,
                OBJ_VAL(nyx_copy_string(&str->chars[i], 1)));
        }
    } else {
        const char* s = str->chars;
        const char* end = s + str->length;
        while (s < end) {
            const char* found = strstr(s, delim->chars);
            if (found == NULL) {
                nyx_value_array_write(&list->items,
                    OBJ_VAL(nyx_copy_string(s, (int)(end - s))));
                break;
            }
            nyx_value_array_write(&list->items,
                OBJ_VAL(nyx_copy_string(s, (int)(found - s))));
            s = found + delim->length;
        }
    }

    nyx_vm_pop(); // remove GC protect, list stays via return
    return OBJ_VAL(list);
}

static NyxValue strJoin(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_LIST(a[0]) || !IS_STRING(a[1])) return OBJ_VAL(nyx_copy_string("", 0));

    NyxObjList* list = AS_LIST(a[0]);
    NyxObjString* sep = AS_STRING(a[1]);

    // figure out how big the result needs to be
    int totalLen = 0;
    for (int i = 0; i < list->items.count; i++) {
        if (IS_STRING(list->items.values[i])) {
            totalLen += AS_STRING(list->items.values[i])->length;
        }
        if (i > 0) totalLen += sep->length;
    }

    char* buf = ALLOCATE(char, totalLen + 1);
    int pos = 0;
    for (int i = 0; i < list->items.count; i++) {
        if (i > 0) {
            memcpy(buf + pos, sep->chars, sep->length);
            pos += sep->length;
        }
        if (IS_STRING(list->items.values[i])) {
            NyxObjString* s = AS_STRING(list->items.values[i]);
            memcpy(buf + pos, s->chars, s->length);
            pos += s->length;
        }
    }
    buf[pos] = '\0';
    return OBJ_VAL(nyx_take_string(buf, pos));
}

static NyxValue strTrim(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return a[0];
    NyxObjString* s = AS_STRING(a[0]);
    int start = 0, end = s->length;
    while (start < end && isspace((unsigned char)s->chars[start])) start++;
    while (end > start && isspace((unsigned char)s->chars[end - 1])) end--;
    return OBJ_VAL(nyx_copy_string(s->chars + start, end - start));
}

static NyxValue strContains(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1])) return BOOL_VAL(false);
    return BOOL_VAL(strstr(AS_CSTRING(a[0]), AS_CSTRING(a[1])) != NULL);
}

static NyxValue strReplace(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1]) || !IS_STRING(a[2])) return a[0];

    NyxObjString* src = AS_STRING(a[0]);
    NyxObjString* from = AS_STRING(a[1]);
    NyxObjString* to = AS_STRING(a[2]);

    if (from->length == 0) return a[0];

    // count how many times the needle appears
    int count = 0;
    const char* s = src->chars;
    while ((s = strstr(s, from->chars)) != NULL) { count++; s += from->length; }

    if (count == 0) return a[0];

    int newLen = src->length + count * (to->length - from->length);
    char* buf = ALLOCATE(char, newLen + 1);
    nyx_vm_push(a[0]); // GC protect source

    int pos = 0;
    s = src->chars;
    while (*s) {
        const char* found = strstr(s, from->chars);
        if (found == NULL) {
            int rem = (int)(src->chars + src->length - s);
            memcpy(buf + pos, s, rem);
            pos += rem;
            break;
        }
        int chunk = (int)(found - s);
        memcpy(buf + pos, s, chunk);
        pos += chunk;
        memcpy(buf + pos, to->chars, to->length);
        pos += to->length;
        s = found + from->length;
    }
    buf[pos] = '\0';
    nyx_vm_pop();
    return OBJ_VAL(nyx_take_string(buf, pos));
}

static NyxValue strStartsWith(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1])) return BOOL_VAL(false);
    NyxObjString* s = AS_STRING(a[0]);
    NyxObjString* prefix = AS_STRING(a[1]);
    if (prefix->length > s->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(s->chars, prefix->chars, prefix->length) == 0);
}

static NyxValue strEndsWith(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1])) return BOOL_VAL(false);
    NyxObjString* s = AS_STRING(a[0]);
    NyxObjString* suffix = AS_STRING(a[1]);
    if (suffix->length > s->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(s->chars + s->length - suffix->length,
                           suffix->chars, suffix->length) == 0);
}

static NyxValue strToUpper(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return a[0];
    NyxObjString* s = AS_STRING(a[0]);
    char* buf = ALLOCATE(char, s->length + 1);
    for (int i = 0; i < s->length; i++) buf[i] = toupper((unsigned char)s->chars[i]);
    buf[s->length] = '\0';
    return OBJ_VAL(nyx_take_string(buf, s->length));
}

static NyxValue strToLower(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return a[0];
    NyxObjString* s = AS_STRING(a[0]);
    char* buf = ALLOCATE(char, s->length + 1);
    for (int i = 0; i < s->length; i++) buf[i] = tolower((unsigned char)s->chars[i]);
    buf[s->length] = '\0';
    return OBJ_VAL(nyx_take_string(buf, s->length));
}

static NyxValue strSubstr(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return a[0];
    NyxObjString* s = AS_STRING(a[0]);
    int start = IS_INT(a[1]) ? (int)AS_INT(a[1]) : 0;
    int length = (ac >= 3 && IS_INT(a[2])) ? (int)AS_INT(a[2]) : s->length - start;
    if (start < 0) start = s->length + start;
    if (start < 0) start = 0;
    if (start >= s->length) return OBJ_VAL(nyx_copy_string("", 0));
    if (start + length > s->length) length = s->length - start;
    return OBJ_VAL(nyx_copy_string(s->chars + start, length));
}

static NyxValue strRepeat(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_INT(a[1])) return a[0];
    NyxObjString* s = AS_STRING(a[0]);
    int n = (int)AS_INT(a[1]);
    if (n <= 0) return OBJ_VAL(nyx_copy_string("", 0));
    int totalLen = s->length * n;
    char* buf = ALLOCATE(char, totalLen + 1);
    for (int i = 0; i < n; i++) memcpy(buf + i * s->length, s->chars, s->length);
    buf[totalLen] = '\0';
    return OBJ_VAL(nyx_take_string(buf, totalLen));
}

static NyxValue strCharAt(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_INT(a[1])) return NIL_VAL;
    NyxObjString* s = AS_STRING(a[0]);
    int idx = (int)AS_INT(a[1]);
    if (idx < 0) idx = s->length + idx;
    if (idx < 0 || idx >= s->length) return NIL_VAL;
    return OBJ_VAL(nyx_copy_string(&s->chars[idx], 1));
}

static NyxValue strIndexOf(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1])) return INT_VAL(-1);
    const char* found = strstr(AS_CSTRING(a[0]), AS_CSTRING(a[1]));
    if (found == NULL) return INT_VAL(-1);
    return INT_VAL((int64_t)(found - AS_CSTRING(a[0])));
}

static NyxValue strParseInt(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("not a string", 12))));
    char* end;
    int64_t val = strtoll(AS_CSTRING(a[0]), &end, 10);
    if (end == AS_CSTRING(a[0]) || *end != '\0') {
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("invalid integer", 15))));
    }
    return OBJ_VAL(nyx_new_result(true, INT_VAL(val)));
}

static NyxValue strParseFloat(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("not a string", 12))));
    char* end;
    double val = strtod(AS_CSTRING(a[0]), &end);
    if (end == AS_CSTRING(a[0]) || *end != '\0') {
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("invalid float", 13))));
    }
    return OBJ_VAL(nyx_new_result(true, FLOAT_VAL(val)));
}

// IO Functions (talking to the outside world)

static NyxValue ioReadFile(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("path must be string", 19))));

    FILE* f = fopen(AS_CSTRING(a[0]), "rb");
    if (f == NULL) {
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("cannot open file", 16))));
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    char* buf = ALLOCATE(char, size + 1);
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);

    return OBJ_VAL(nyx_new_result(true, OBJ_VAL(nyx_take_string(buf, (int)read))));
}

static NyxValue ioWriteFile(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0]) || !IS_STRING(a[1]))
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("args must be strings", 20))));

    FILE* f = fopen(AS_CSTRING(a[0]), "wb");
    if (f == NULL) {
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("cannot write file", 17))));
    }

    NyxObjString* content = AS_STRING(a[1]);
    fwrite(content->chars, 1, content->length, f);
    fclose(f);

    return OBJ_VAL(nyx_new_result(true, NIL_VAL));
}

static NyxValue ioFileExists(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return BOOL_VAL(false);
    FILE* f = fopen(AS_CSTRING(a[0]), "r");
    if (f) { fclose(f); return BOOL_VAL(true); }
    return BOOL_VAL(false);
}

static NyxValue ioPrintErr(int ac, NyxValue* a) {
    (void)ac;
    if (IS_STRING(a[0])) {
        fprintf(stderr, "%s\n", AS_CSTRING(a[0]));
    }
    return NIL_VAL;
}

static NyxValue ioInput(int ac, NyxValue* a) {
    (void)ac;
    if (IS_STRING(a[0])) {
        printf("%s", AS_CSTRING(a[0]));
        fflush(stdout);
    }
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin) == NULL) return NIL_VAL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return OBJ_VAL(nyx_copy_string(buf, (int)len));
}

// Collection Functions

static NyxValue colRange(int ac, NyxValue* a) {
    (void)ac;
    int64_t start = IS_INT(a[0]) ? AS_INT(a[0]) : 0;
    int64_t end = IS_INT(a[1]) ? AS_INT(a[1]) : 0;

    NyxObjList* list = nyx_new_list();
    nyx_vm_push(OBJ_VAL(list)); // GC protect
    for (int64_t i = start; i < end; i++) {
        nyx_value_array_write(&list->items, INT_VAL(i));
    }
    nyx_vm_pop();
    return OBJ_VAL(list);
}

static NyxValue colRepeat(int ac, NyxValue* a) {
    (void)ac;
    int n = IS_INT(a[1]) ? (int)AS_INT(a[1]) : 0;
    NyxObjList* list = nyx_new_list();
    nyx_vm_push(OBJ_VAL(list));
    for (int i = 0; i < n; i++) {
        nyx_value_array_write(&list->items, a[0]);
    }
    nyx_vm_pop();
    return OBJ_VAL(list);
}

static NyxValue colFlatten(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_LIST(a[0])) return a[0];
    NyxObjList* src = AS_LIST(a[0]);
    NyxObjList* result = nyx_new_list();
    nyx_vm_push(OBJ_VAL(result));
    for (int i = 0; i < src->items.count; i++) {
        NyxValue item = src->items.values[i];
        if (IS_LIST(item)) {
            NyxObjList* inner = AS_LIST(item);
            for (int j = 0; j < inner->items.count; j++) {
                nyx_value_array_write(&result->items, inner->items.values[j]);
            }
        } else {
            nyx_value_array_write(&result->items, item);
        }
    }
    nyx_vm_pop();
    return OBJ_VAL(result);
}

// Conversion (turning one thing into another)

static NyxValue convToInt(int ac, NyxValue* a) {
    (void)ac;
    if (IS_INT(a[0])) return a[0];
    if (IS_FLOAT(a[0])) return INT_VAL((int64_t)AS_FLOAT(a[0]));
    if (IS_BOOL(a[0])) return INT_VAL(AS_BOOL(a[0]) ? 1 : 0);
    if (IS_STRING(a[0])) {
        char* end;
        int64_t val = strtoll(AS_CSTRING(a[0]), &end, 10);
        if (end != AS_CSTRING(a[0])) return INT_VAL(val);
    }
    return INT_VAL(0);
}

static NyxValue convToFloat(int ac, NyxValue* a) {
    (void)ac;
    if (IS_FLOAT(a[0])) return a[0];
    if (IS_INT(a[0])) return FLOAT_VAL((double)AS_INT(a[0]));
    if (IS_STRING(a[0])) {
        char* end;
        double val = strtod(AS_CSTRING(a[0]), &end);
        if (end != AS_CSTRING(a[0])) return FLOAT_VAL(val);
    }
    return FLOAT_VAL(0.0);
}

// System (running shell commands from Nyx — what could go wrong)
static NyxValue sysExec(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("exec: expected string", 20))));

    const char* cmd = AS_CSTRING(a[0]);

#ifdef _WIN32
    // redirect stdout to a temp file, then slurp it. elegant? no. works? yes
    char tmpPath[512];
    snprintf(tmpPath, sizeof(tmpPath), "%s\\nyx_exec_%d.tmp", getenv("TEMP") ? getenv("TEMP") : ".", (int)clock());
    char fullCmd[2048];
    snprintf(fullCmd, sizeof(fullCmd), "%s > \"%s\" 2>&1", cmd, tmpPath);
    int exitCode = system(fullCmd);
#else
    char tmpPath[512];
    snprintf(tmpPath, sizeof(tmpPath), "/tmp/nyx_exec_%d.tmp", (int)clock());
    char fullCmd[2048];
    snprintf(fullCmd, sizeof(fullCmd), "%s > \"%s\" 2>&1", cmd, tmpPath);
    int exitCode = system(fullCmd);
#endif

    // read back the output
    FILE* f = fopen(tmpPath, "rb");
    if (!f) {
        remove(tmpPath);
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(nyx_copy_string("exec: could not read output", 27))));
    }

    fseek(f, 0L, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    char* buf = (char*)malloc(size + 1);
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    remove(tmpPath);

    // trim trailing newline — nobody wants that
    while (read > 0 && (buf[read-1] == '\n' || buf[read-1] == '\r')) buf[--read] = '\0';

    NyxObjString* result = nyx_copy_string(buf, (int)read);
    free(buf);

    if (exitCode != 0) {
        return OBJ_VAL(nyx_new_result(false, OBJ_VAL(result)));
    }
    return OBJ_VAL(nyx_new_result(true, OBJ_VAL(result)));
}

// Platform (OS detection and friends)

static NyxValue sysPlatform(int ac, NyxValue* a) {
    (void)ac; (void)a;
#if defined(_WIN32)
    return OBJ_VAL(nyx_copy_string("windows", 7));
#elif defined(__APPLE__)
    return OBJ_VAL(nyx_copy_string("macos", 5));
#elif defined(__linux__)
    return OBJ_VAL(nyx_copy_string("linux", 5));
#else
    return OBJ_VAL(nyx_copy_string("unknown", 7));
#endif
}

static NyxValue sysArch(int ac, NyxValue* a) {
    (void)ac; (void)a;
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return OBJ_VAL(nyx_copy_string("x64", 3));
#elif defined(__aarch64__) || defined(_M_ARM64)
    return OBJ_VAL(nyx_copy_string("arm64", 5));
#elif defined(__i386__) || defined(_M_IX86)
    return OBJ_VAL(nyx_copy_string("x86", 3));
#else
    return OBJ_VAL(nyx_copy_string("unknown", 7));
#endif
}

static NyxValue sysNyxHome(int ac, NyxValue* a) {
    (void)ac; (void)a;
    if (vm.nyxHome[0] != '\0') {
        return OBJ_VAL(nyx_copy_string(vm.nyxHome, (int)strlen(vm.nyxHome)));
    }
    return NIL_VAL;
}

// Native Loading (dlopen wrapper for Nyx scripts)
extern bool nyx_load_native(const char* path);

static NyxValue sysLoadNative(int ac, NyxValue* a) {
    (void)ac;
    if (!IS_STRING(a[0])) {
        return OBJ_VAL(nyx_new_result(false,
            OBJ_VAL(nyx_copy_string("load_native: expected string path", 33))));
    }

    // resolve relative to the current script's location
    const char* path = AS_CSTRING(a[0]);
    char resolved[1024];

    bool isAbsolute = false;
#ifdef _WIN32
    isAbsolute = (path[0] != '\0' && path[1] == ':'); // C:\...
#else
    isAbsolute = (path[0] == '/');
#endif

    if (!isAbsolute && vm.currentFile != NULL) {
        // prepend the script's directory
        const char* lastSlash = vm.currentFile;
        for (const char* p = vm.currentFile; *p; p++) {
            if (*p == '/' || *p == '\\') lastSlash = p;
        }
        if (lastSlash != vm.currentFile) {
            int dirLen = (int)(lastSlash - vm.currentFile + 1);
            snprintf(resolved, sizeof(resolved), "%.*s%s", dirLen, vm.currentFile, path);
            path = resolved;
        }
    }

    if (nyx_load_native(path)) {
        return OBJ_VAL(nyx_new_result(true, BOOL_VAL(true)));
    }
    return OBJ_VAL(nyx_new_result(false,
        OBJ_VAL(nyx_copy_string("failed to load native library", 29))));
}

// Sets (array-backed because we like living dangerously)

static NyxValue colSet(int argCount, NyxValue* args) {
    NyxObjSet* set = nyx_new_set();
    nyx_vm_push(OBJ_VAL(set)); // GC protect
    for (int i = 0; i < argCount; i++) {
        // dedup — O(n) but sets are usually small. right? ...right?
        bool found = false;
        for (int j = 0; j < set->items.count; j++) {
            if (nyx_values_equal(set->items.values[j], args[i])) { found = true; break; }
        }
        if (!found) nyx_value_array_write(&set->items, args[i]);
    }
    nyx_vm_pop();
    return OBJ_VAL(set);
}

// Registration (plugging everything into the VM)

void nyx_stdlib_init(void) {
    // Math
    defineNativeStd("abs", mathAbs);
    defineNativeStd("floor", mathFloor);
    defineNativeStd("ceil", mathCeil);
    defineNativeStd("round", mathRound);
    defineNativeStd("sqrt", mathSqrt);
    defineNativeStd("sin", mathSin);
    defineNativeStd("cos", mathCos);
    defineNativeStd("tan", mathTan);
    defineNativeStd("pow", mathPow);
    defineNativeStd("log", mathLog);
    defineNativeStd("min", mathMin);
    defineNativeStd("max", mathMax);
    defineNativeStd("clamp", mathClamp);
    defineNativeStd("random", mathRandom);
    defineNativeStd("random_int", mathRandomInt);
    defineNativeStd("PI", mathPI);
    defineNativeStd("E", mathE);

    // String
    defineNativeStd("split", strSplit);
    defineNativeStd("join", strJoin);
    defineNativeStd("trim", strTrim);
    defineNativeStd("str_contains", strContains);
    defineNativeStd("replace", strReplace);
    defineNativeStd("starts_with", strStartsWith);
    defineNativeStd("ends_with", strEndsWith);
    defineNativeStd("to_upper", strToUpper);
    defineNativeStd("to_lower", strToLower);
    defineNativeStd("substr", strSubstr);
    defineNativeStd("str_repeat", strRepeat);
    defineNativeStd("char_at", strCharAt);
    defineNativeStd("str_index_of", strIndexOf);
    defineNativeStd("parse_int", strParseInt);
    defineNativeStd("parse_float", strParseFloat);

    // IO
    defineNativeStd("read_file", ioReadFile);
    defineNativeStd("write_file", ioWriteFile);
    defineNativeStd("file_exists", ioFileExists);
    defineNativeStd("print_err", ioPrintErr);
    defineNativeStd("input", ioInput);

    // Collections
    defineNativeStd("range", colRange);
    defineNativeStd("list_repeat", colRepeat);
    defineNativeStd("flatten", colFlatten);
    defineNativeStd("set", colSet);

    // Conversion
    defineNativeStd("to_int", convToInt);
    defineNativeStd("to_float", convToFloat);

    // System
    defineNativeStd("exec", sysExec);
    defineNativeStd("load_native", sysLoadNative);
    defineNativeStd("platform", sysPlatform);
    defineNativeStd("arch", sysArch);
    defineNativeStd("nyx_home", sysNyxHome);
}
