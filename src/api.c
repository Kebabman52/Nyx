#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "chunk.h"
#include "collections.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define NYX_MAGIC_0 'N'
#define NYX_MAGIC_1 'Y'
#define NYX_MAGIC_2 'X'
#define NYX_MAGIC_3 '\0'

typedef void (*NyxHostFn)(int argCount);
typedef enum { NYX_VAL_NIL, NYX_VAL_BOOL, NYX_VAL_INT, NYX_VAL_FLOAT, NYX_VAL_STRING, NYX_VAL_OBJECT } NyxValType;

// VM Lifecycle (on/off switch)

void nyx_init(void) {
    nyx_vm_init();
}

void nyx_free(void) {
    nyx_vm_free();
}

NyxResult nyx_do_string(const char* source) {
    return nyx_vm_interpret(source);
}

NyxResult nyx_do_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "nyx: could not open file \"%s\".\n", path);
        return NYX_RUNTIME_ERROR;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* source = (char*)malloc(fileSize + 1);
    if (source == NULL) {
        fclose(file);
        return NYX_RUNTIME_ERROR;
    }

    size_t bytesRead = fread(source, sizeof(char), fileSize, file);
    source[bytesRead] = '\0';
    fclose(file);

    nyx_vm_set_file(path);
    NyxResult result = nyx_vm_interpret(source);
    free(source);
    return result;
}

void nyx_set_file(const char* path) {
    nyx_vm_set_file(path);
}

// Stack Helpers

static NyxValue* stackSlot(int index) {
    if (index >= 0) {
        return &vm.stack[index];
    } else {
        return vm.stackTop + index;
    }
}

// Stack Push (shove values onto the stack from C-land)

void nyx_push_nil(void) {
    nyx_vm_push(NIL_VAL);
}

void nyx_push_bool(bool value) {
    nyx_vm_push(BOOL_VAL(value));
}

void nyx_push_int(int64_t value) {
    nyx_vm_push(INT_VAL(value));
}

void nyx_push_float(double value) {
    nyx_vm_push(FLOAT_VAL(value));
}

void nyx_push_string(const char* str, int length) {
    nyx_vm_push(OBJ_VAL(nyx_copy_string(str, length)));
}

void nyx_push_cstring(const char* str) {
    nyx_push_string(str, (int)strlen(str));
}

// Stack Query (pull values back out)

NyxValType nyx_type_at(int index) {
    NyxValue val = *stackSlot(index);
    if (IS_NIL(val))    return NYX_VAL_NIL;
    if (IS_BOOL(val))   return NYX_VAL_BOOL;
    if (IS_INT(val))    return NYX_VAL_INT;
    if (IS_FLOAT(val))  return NYX_VAL_FLOAT;
    if (IS_STRING(val)) return NYX_VAL_STRING;
    return NYX_VAL_OBJECT;
}

bool nyx_is_nil(int index)    { return IS_NIL(*stackSlot(index)); }
bool nyx_is_bool(int index)   { return IS_BOOL(*stackSlot(index)); }
bool nyx_is_int(int index)    { return IS_INT(*stackSlot(index)); }
bool nyx_is_float(int index)  { return IS_FLOAT(*stackSlot(index)); }
bool nyx_is_string(int index) { return IS_STRING(*stackSlot(index)); }

bool nyx_to_bool(int index)   { return AS_BOOL(*stackSlot(index)); }
int64_t nyx_to_int(int index) { return AS_INT(*stackSlot(index)); }
double nyx_to_float(int index){ return AS_FLOAT(*stackSlot(index)); }

const char* nyx_to_cstring(int index) {
    return AS_CSTRING(*stackSlot(index));
}

int nyx_string_length(int index) {
    return AS_STRING(*stackSlot(index))->length;
}

void nyx_pop(int count) {
    for (int i = 0; i < count; i++) nyx_vm_pop();
}

int nyx_stack_size(void) {
    return (int)(vm.stackTop - vm.stack);
}

// Calling Nyx from C (crossing the bridge)

bool nyx_get_global(const char* name) {
    NyxObjString* nameStr = nyx_copy_string(name, (int)strlen(name));
    NyxValue value;
    if (!nyx_table_get(&vm.globals, nameStr, &value)) {
        return false;
    }
    nyx_vm_push(value);
    return true;
}

NyxResult nyx_call(int argCount) {
    // callee sits below its args on the stack
    NyxValue callee = vm.stackTop[-argCount - 1];

    // closure: set up a frame and run
    if (IS_OBJ(callee) && IS_CLOSURE(callee)) {
        NyxObjClosure* closure = AS_CLOSURE(callee);
        if (!nyx_vm_call(closure, argCount)) {
            return NYX_RUNTIME_ERROR;
        }
        return nyx_vm_run();
    }

    // native: call it directly, no VM frame needed
    if (IS_OBJ(callee) && IS_NATIVE(callee)) {
        NyxNativeFn native = AS_NATIVE(callee);
        NyxValue result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1;
        nyx_vm_push(result);
        return NYX_OK;
    }

    // class: instantiate it, run init() if it exists
    if (IS_OBJ(callee) && IS_CLASS(callee)) {
        NyxObjClass* klass = AS_CLASS(callee);
        vm.stackTop[-argCount - 1] = OBJ_VAL(nyx_new_instance(klass));
        NyxValue initializer;
        if (nyx_table_get(&klass->methods, vm.initString, &initializer)) {
            if (!nyx_vm_call(AS_CLOSURE(initializer), argCount)) {
                return NYX_RUNTIME_ERROR;
            }
            return nyx_vm_run();
        } else if (argCount != 0) {
            return NYX_RUNTIME_ERROR;
        }
        return NYX_OK;
    }

    return NYX_RUNTIME_ERROR;
}

// Host Function Registration

void nyx_register_fn(const char* name, NyxNativeFn fn) {
    // no push/pop here — we might be mid-import and the stack is sacred.
    // GC safety relies on string interning + immediate storage in globals.
    // there's a theoretical window where GC could eat nameStr, but in practice
    // the interned string has references in the constant pool. don't think about it too hard
    NyxObjString* nameStr = nyx_copy_string(name, (int)strlen(name));
    NyxObjNative* native = nyx_new_native(fn, name);
    nyx_table_set(&vm.globals, nameStr, OBJ_VAL(native));
}

void nyx_register_class(const char* name) {
    NyxObjString* nameStr = nyx_copy_string(name, (int)strlen(name));
    NyxObjClass* klass = nyx_new_class(nameStr);
    nyx_table_set(&vm.globals, nameStr, OBJ_VAL(klass));
}

void nyx_register_method(const char* className, const char* methodName,
                          NyxNativeFn fn) {
    NyxObjString* classStr = nyx_copy_string(className, (int)strlen(className));
    NyxValue classVal;
    if (!nyx_table_get(&vm.globals, classStr, &classVal) || !IS_CLASS(classVal)) {
        return;
    }
    NyxObjClass* klass = AS_CLASS(classVal);
    NyxObjString* methStr = nyx_copy_string(methodName, (int)strlen(methodName));
    NyxObjNative* native = nyx_new_native(fn, methodName);
    nyx_table_set(&klass->methods, methStr, OBJ_VAL(native));
}

// Native Module Loading (dlopen/LoadLibrary dark arts)

// loaded libs stay open forever — function pointers need to remain valid
#define MAX_NATIVE_LIBS 64
static void* nativeLibs[MAX_NATIVE_LIBS];
static int nativeLibCount = 0;

// Native API GC Protection
// can't use push/pop during OP_IMPORT — that'd trash the module's stack frame.
// instead we have this scratch array that markRoots knows about. it's ugly but it works

#define API_GC_ROOTS_MAX 16
static NyxValue apiGcRoots[API_GC_ROOTS_MAX];
static int apiGcRootCount = 0;

static void apiGcPin(NyxValue val) {
    if (apiGcRootCount < API_GC_ROOTS_MAX) {
        apiGcRoots[apiGcRootCount++] = val;
    }
}

static void apiGcUnpin(int count) {
    apiGcRootCount -= count;
    if (apiGcRootCount < 0) apiGcRootCount = 0;
}

// called by markRoots — keep the GC's hands off our scratch space
void nyx_mark_api_roots(void) {
    for (int i = 0; i < apiGcRootCount; i++) {
        nyx_mark_value(apiGcRoots[i]);
    }
}

// wipe the scratch space after every native call
void nyx_api_gc_clear(void) {
    apiGcRootCount = 0;
}

static NyxValue api_string_val(const char* chars, int length) {
    return OBJ_VAL(nyx_copy_string(chars, length));
}

static NyxValue api_result_ok(NyxValue value) {
    return OBJ_VAL(nyx_new_result(true, value));
}

static NyxValue api_result_err(NyxValue value) {
    return OBJ_VAL(nyx_new_result(false, value));
}

static NyxValue api_list_new(void) {
    NyxValue list = OBJ_VAL(nyx_new_list());
    apiGcPin(list); // GC protection — don't let it die before the native returns
    return list;
}

static void api_list_push(NyxValue list, NyxValue item) {
    if (IS_LIST(list)) {
        nyx_value_array_write(&AS_LIST(list)->items, item);
        NYX_WRITE_BARRIER_VALUE(&AS_LIST(list)->obj, item);
    }
}

static NyxValue api_map_new(void) {
    NyxValue map = OBJ_VAL(nyx_new_map());
    apiGcPin(map); // same deal — GC protection
    return map;
}

static void api_map_set(NyxValue map, const char* key, NyxValue value) {
    if (IS_MAP(map)) {
        NyxObjString* keyStr = nyx_copy_string(key, (int)strlen(key));
        nyx_map_set(AS_MAP(map), keyStr, value);
    }
}

// API table — the vtable we hand to native modules so they can talk to us
typedef struct {
    void      (*register_fn)(const char* name, NyxNativeFn fn);
    void      (*register_class)(const char* name);
    void      (*register_method)(const char* cls, const char* method, NyxNativeFn fn);
    NyxValue  (*string_val)(const char* chars, int length);
    NyxValue  (*result_ok)(NyxValue value);
    NyxValue  (*result_err)(NyxValue value);
    NyxValue  (*list_new)(void);
    void      (*list_push)(NyxValue list, NyxValue item);
    NyxValue  (*map_new)(void);
    void      (*map_set)(NyxValue map, const char* key, NyxValue value);
} NyxModuleAPI_t;

typedef void (*NyxModuleInitFn)(const NyxModuleAPI_t* api);

// singleton API table — filled once, handed to every native module
static NyxModuleAPI_t moduleAPI = {0};

bool nyx_load_native(const char* path) {
    if (nativeLibCount >= MAX_NATIVE_LIBS) {
        fprintf(stderr, "nyx: too many native libraries loaded (max %d)\n", MAX_NATIVE_LIBS);
        return false;
    }

    // auto-append .dll/.dylib/.so if no extension given
    char fullPath[1024];
    bool hasExt = false;
    const char* p = path;
    const char* lastDot = NULL;
    const char* lastSlash = NULL;
    for (; *p; p++) {
        if (*p == '.') lastDot = p;
        if (*p == '/' || *p == '\\') lastSlash = p;
    }
    hasExt = (lastDot != NULL && (lastSlash == NULL || lastDot > lastSlash));

    if (hasExt) {
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    } else {
#ifdef _WIN32
        snprintf(fullPath, sizeof(fullPath), "%s.dll", path);
#elif defined(__APPLE__)
        snprintf(fullPath, sizeof(fullPath), "%s.dylib", path);
#else
        snprintf(fullPath, sizeof(fullPath), "%s.so", path);
#endif
    }

    // load it
    void* handle;
#ifdef _WIN32
    handle = (void*)LoadLibraryA(fullPath);
    if (!handle) {
        fprintf(stderr, "nyx: cannot load native library '%s' (error %lu)\n",
                fullPath, GetLastError());
        return false;
    }
#else
    handle = dlopen(fullPath, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "nyx: cannot load native library '%s': %s\n",
                fullPath, dlerror());
        return false;
    }
#endif

    // find nyx_module_init — the handshake function
    NyxModuleInitFn initFn;
#ifdef _WIN32
    initFn = (NyxModuleInitFn)GetProcAddress((HMODULE)handle, "nyx_module_init");
#else
    initFn = (NyxModuleInitFn)dlsym(handle, "nyx_module_init");
#endif

    if (!initFn) {
        fprintf(stderr, "nyx: native library '%s' has no nyx_module_init() function\n", fullPath);
#ifdef _WIN32
        FreeLibrary((HMODULE)handle);
#else
        dlclose(handle);
#endif
        return false;
    }

    // keep the handle alive forever — function pointers point into loaded memory
    nativeLibs[nativeLibCount++] = handle;

    // populate the API vtable (lazy init)
    if (moduleAPI.register_fn == NULL) {
        moduleAPI.register_fn = nyx_register_fn;
        moduleAPI.register_class = nyx_register_class;
        moduleAPI.register_method = nyx_register_method;
        moduleAPI.string_val = api_string_val;
        moduleAPI.result_ok = api_result_ok;
        moduleAPI.result_err = api_result_err;
        moduleAPI.list_new = api_list_new;
        moduleAPI.list_push = api_list_push;
        moduleAPI.map_new = api_map_new;
        moduleAPI.map_set = api_map_set;
    }

    // hand over the API table — module registers its functions through this
    initFn(&moduleAPI);

    return true;
}

// cleanup: close all native library handles
void nyx_unload_natives(void) {
    for (int i = 0; i < nativeLibCount; i++) {
#ifdef _WIN32
        FreeLibrary((HMODULE)nativeLibs[i]);
#else
        dlclose(nativeLibs[i]);
#endif
    }
    nativeLibCount = 0;
}

// GC (public API wrapper)

void nyx_gc_collect_api(void) {
    nyx_gc_collect();
}

// Bytecode Serialization (turning functions into bytes and back)

static void writeUint16(FILE* f, uint16_t val) {
    fputc((val >> 8) & 0xff, f);
    fputc(val & 0xff, f);
}

static void writeUint32(FILE* f, uint32_t val) {
    fputc((val >> 24) & 0xff, f);
    fputc((val >> 16) & 0xff, f);
    fputc((val >> 8) & 0xff, f);
    fputc(val & 0xff, f);
}

static uint16_t readUint16(FILE* f) {
    uint16_t val = (uint8_t)fgetc(f) << 8;
    val |= (uint8_t)fgetc(f);
    return val;
}

static uint32_t readUint32(FILE* f) {
    uint32_t val = (uint8_t)fgetc(f) << 24;
    val |= (uint8_t)fgetc(f) << 16;
    val |= (uint8_t)fgetc(f) << 8;
    val |= (uint8_t)fgetc(f);
    return val;
}

// dump a function to disk
static void serializeFunction(FILE* f, NyxObjFunction* func) {
    // Name
    if (func->name != NULL) {
        writeUint16(f, (uint16_t)func->name->length);
        fwrite(func->name->chars, 1, func->name->length, f);
    } else {
        writeUint16(f, 0);
    }

    // Arity and upvalue count
    fputc(func->arity, f);
    fputc(func->upvalueCount, f);
    fputc(func->isGenerator ? 1 : 0, f);

    // Bytecode
    writeUint32(f, (uint32_t)func->chunk.count);
    fwrite(func->chunk.code, 1, func->chunk.count, f);

    // Line info
    fwrite(func->chunk.lines, sizeof(int), func->chunk.count, f);

    // Constants
    writeUint16(f, (uint16_t)func->chunk.constants.count);
    for (int i = 0; i < func->chunk.constants.count; i++) {
        NyxValue val = func->chunk.constants.values[i];

        if (IS_INT(val)) {
            fputc(0x01, f);
            int64_t iv = AS_INT(val);
            fwrite(&iv, sizeof(int64_t), 1, f);
        } else if (IS_FLOAT(val)) {
            fputc(0x02, f);
            double fv = AS_FLOAT(val);
            fwrite(&fv, sizeof(double), 1, f);
        } else if (IS_BOOL(val)) {
            fputc(0x03, f);
            fputc(AS_BOOL(val) ? 1 : 0, f);
        } else if (IS_NIL(val)) {
            fputc(0x04, f);
        } else if (IS_STRING(val)) {
            fputc(0x05, f);
            NyxObjString* s = AS_STRING(val);
            writeUint16(f, (uint16_t)s->length);
            fwrite(s->chars, 1, s->length, f);
        } else if (IS_FUNCTION(val)) {
            fputc(0x06, f);
            serializeFunction(f, AS_FUNCTION(val));
        } else {
            // unknown constant type? just write nil and pray
            fputc(0x04, f);
        }
    }
}

// resurrect a function from disk
NyxObjFunction* nyx_deserialize_function(FILE* f) {
    NyxObjFunction* func = nyx_new_function();
    nyx_vm_push(OBJ_VAL(func)); // GC protection

    // Name
    uint16_t nameLen = readUint16(f);
    if (nameLen > 0) {
        char* nameBuf = malloc(nameLen + 1);
        fread(nameBuf, 1, nameLen, f);
        nameBuf[nameLen] = '\0';
        func->name = nyx_copy_string(nameBuf, nameLen);
        free(nameBuf);
    }

    // Arity and upvalue count
    func->arity = fgetc(f);
    func->upvalueCount = fgetc(f);
    func->isGenerator = fgetc(f) != 0;

    // Bytecode
    uint32_t codeLen = readUint32(f);
    func->chunk.count = (int)codeLen;
    func->chunk.capacity = (int)codeLen;
    func->chunk.code = ALLOCATE(uint8_t, codeLen);
    fread(func->chunk.code, 1, codeLen, f);

    // Line info
    func->chunk.lines = ALLOCATE(int, codeLen);
    fread(func->chunk.lines, sizeof(int), codeLen, f);

    // Constants
    uint16_t constCount = readUint16(f);
    for (int i = 0; i < constCount; i++) {
        int tag = fgetc(f);
        NyxValue val;

        switch (tag) {
            case 0x01: { // int
                int64_t iv;
                fread(&iv, sizeof(int64_t), 1, f);
                val = INT_VAL(iv);
                break;
            }
            case 0x02: { // float
                double fv;
                fread(&fv, sizeof(double), 1, f);
                val = FLOAT_VAL(fv);
                break;
            }
            case 0x03: { // bool
                val = BOOL_VAL(fgetc(f) != 0);
                break;
            }
            case 0x04: { // nil
                val = NIL_VAL;
                break;
            }
            case 0x05: { // string
                uint16_t sLen = readUint16(f);
                char* sBuf = malloc(sLen + 1);
                fread(sBuf, 1, sLen, f);
                sBuf[sLen] = '\0';
                val = OBJ_VAL(nyx_take_string(sBuf, sLen));
                break;
            }
            case 0x06: { // nested function
                NyxObjFunction* nested = nyx_deserialize_function(f);
                val = OBJ_VAL(nested);
                break;
            }
            default:
                val = NIL_VAL;
                break;
        }

        nyx_chunk_add_constant(&func->chunk, val);
    }

    nyx_vm_pop(); // remove GC protection
    return func;
}

int nyx_compile_to_file(const char* sourcePath, const char* outputPath) {
    // Read source
    FILE* srcFile = fopen(sourcePath, "rb");
    if (srcFile == NULL) {
        fprintf(stderr, "nyx: cannot open '%s'\n", sourcePath);
        return 1;
    }

    fseek(srcFile, 0L, SEEK_END);
    size_t fileSize = ftell(srcFile);
    rewind(srcFile);

    char* source = malloc(fileSize + 1);
    size_t bytesRead = fread(source, 1, fileSize, srcFile);
    source[bytesRead] = '\0';
    fclose(srcFile);

    // Compile
    NyxObjFunction* func = nyx_compile(source);
    free(source);
    if (func == NULL) return 1;

    // Write .nyxc
    FILE* out = fopen(outputPath, "wb");
    if (out == NULL) {
        fprintf(stderr, "nyx: cannot create '%s'\n", outputPath);
        return 1;
    }

    // Header
    fputc(NYX_MAGIC_0, out);
    fputc(NYX_MAGIC_1, out);
    fputc(NYX_MAGIC_2, out);
    fputc(NYX_MAGIC_3, out);
    fputc(NYX_VERSION_MAJOR, out);
    fputc(NYX_VERSION_MINOR, out);
    fputc(NYX_VERSION_PATCH, out);

    // No native binaries in single-file compile
    writeUint16(out, 0);

    // Single module
    writeUint16(out, 1);

    // Module name = source file name
    const char* baseName = sourcePath;
    for (const char* p = sourcePath; *p; p++) {
        if (*p == '/' || *p == '\\') baseName = p + 1;
    }
    int nameLen = (int)strlen(baseName);
    writeUint16(out, (uint16_t)nameLen);
    fwrite(baseName, 1, nameLen, out);

    // Serialize the function
    serializeFunction(out, func);

    fclose(out);
    return 0;
}

NyxResult nyx_run_compiled(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "nyx: cannot open '%s'\n", path);
        return NYX_RUNTIME_ERROR;
    }

    // Check magic
    if (fgetc(f) != NYX_MAGIC_0 || fgetc(f) != NYX_MAGIC_1 ||
        fgetc(f) != NYX_MAGIC_2 || fgetc(f) != NYX_MAGIC_3) {
        fprintf(stderr, "nyx: '%s' is not a valid .nyxc file\n", path);
        fclose(f);
        return NYX_RUNTIME_ERROR;
    }

    // Version
    int major = fgetc(f);
    int minor = fgetc(f);
    int patch = fgetc(f);
    (void)major; (void)minor; (void)patch;

    // native binaries come first so they're loaded before module code runs
    uint16_t nativeCount = readUint16(f);

    if (nativeCount > 0) {
        char currentPlatform[64];
#if defined(_WIN32)
    #if defined(__x86_64__) || defined(_M_X64)
        snprintf(currentPlatform, sizeof(currentPlatform), "windows-x64");
    #elif defined(__aarch64__) || defined(_M_ARM64)
        snprintf(currentPlatform, sizeof(currentPlatform), "windows-arm64");
    #else
        snprintf(currentPlatform, sizeof(currentPlatform), "windows-x86");
    #endif
#elif defined(__APPLE__)
    #if defined(__aarch64__)
        snprintf(currentPlatform, sizeof(currentPlatform), "macos-arm64");
    #else
        snprintf(currentPlatform, sizeof(currentPlatform), "macos-x64");
    #endif
#else
    #if defined(__x86_64__)
        snprintf(currentPlatform, sizeof(currentPlatform), "linux-x64");
    #elif defined(__aarch64__)
        snprintf(currentPlatform, sizeof(currentPlatform), "linux-arm64");
    #else
        snprintf(currentPlatform, sizeof(currentPlatform), "linux-x86");
    #endif
#endif

        char tempDir[1024];
#ifdef _WIN32
        const char* tmp = getenv("TEMP");
        if (!tmp) tmp = ".";
        snprintf(tempDir, sizeof(tempDir), "%s\\nyx_native_%lu", tmp, (unsigned long)GetCurrentProcessId());
        CreateDirectoryA(tempDir, NULL);
#else
        snprintf(tempDir, sizeof(tempDir), "/tmp/nyx_native_%d", (int)getpid());
        mkdir(tempDir, 0755);
#endif

        for (int n = 0; n < nativeCount; n++) {
            uint16_t platLen = readUint16(f);
            char platTag[64] = "";
            if (platLen < sizeof(platTag)) {
                fread(platTag, 1, platLen, f);
                platTag[platLen] = '\0';
            } else { fseek(f, platLen, SEEK_CUR); }

            uint16_t fnameLen = readUint16(f);
            char fname[256] = "";
            if (fnameLen < sizeof(fname)) {
                fread(fname, 1, fnameLen, f);
                fname[fnameLen] = '\0';
            } else { fseek(f, fnameLen, SEEK_CUR); }

            uint32_t dataLen = readUint32(f);

            if (strcmp(platTag, currentPlatform) == 0 && dataLen > 0) {
                char extractPath[1024];
                snprintf(extractPath, sizeof(extractPath), "%s/%s", tempDir, fname);

                FILE* extractFile = fopen(extractPath, "wb");
                if (extractFile) {
                    char buf[8192];
                    uint32_t remaining = dataLen;
                    while (remaining > 0) {
                        size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
                        size_t rd = fread(buf, 1, chunk, f);
                        fwrite(buf, 1, rd, extractFile);
                        remaining -= (uint32_t)rd;
                    }
                    fclose(extractFile);
                    nyx_load_native(extractPath);
                } else {
                    fseek(f, dataLen, SEEK_CUR);
                }
            } else {
                fseek(f, dataLen, SEEK_CUR);
            }
        }
    }

    // Module count
    uint16_t moduleCount = readUint16(f);

    NyxResult result = NYX_OK;

    for (int m = 0; m < moduleCount; m++) {
        // Module name
        uint16_t nameLen = readUint16(f);
        char* modName = malloc(nameLen + 1);
        fread(modName, 1, nameLen, f);
        modName[nameLen] = '\0';

        // register in vm.modules to prevent re-import (strip .nyx for matching)
        char cleanName[512];
        snprintf(cleanName, sizeof(cleanName), "%s", modName);
        int cnLen = (int)strlen(cleanName);
        if (cnLen > 4 && strcmp(cleanName + cnLen - 4, ".nyx") == 0) {
            cleanName[cnLen - 4] = '\0';
            cnLen -= 4;
        }
        NyxObjString* modNameStr = nyx_copy_string(cleanName, cnLen);
        nyx_vm_push(OBJ_VAL(modNameStr));
        nyx_table_set(&vm.modules, modNameStr, BOOL_VAL(true));
        nyx_vm_pop();


        free(modName);

        // load it up and run it
        NyxObjFunction* func = nyx_deserialize_function(f);

        nyx_vm_push(OBJ_VAL(func));
        NyxObjClosure* closure = nyx_new_closure(func);
        nyx_vm_pop();
        nyx_vm_push(OBJ_VAL(closure));

        nyx_vm_call(closure, 0);
        result = nyx_vm_run();

        if (result != NYX_OK) break;
    }

    fclose(f);
    return result;
}

// Module Bundling (stuffing everything into one .nyxc)

#define MAX_BUNDLE_MODULES 256

typedef struct {
    char name[256];          // module name (as used in import)
    char path[1024];         // resolved file path
    NyxObjFunction* func;   // compiled function
} BundleModule;

// scan bytecode for OP_IMPORT instructions to find dependencies
static void collectImports(NyxObjFunction* func, char imports[][256],
                           int* importCount, int maxImports) {
    NyxChunk* chunk = &func->chunk;

    for (int i = 0; i < chunk->count; i++) {
        if (chunk->code[i] == OP_IMPORT) {
            // Next byte is the constant index
            if (i + 1 < chunk->count) {
                uint8_t constIdx = chunk->code[i + 1];
                if (constIdx < chunk->constants.count) {
                    NyxValue val = chunk->constants.values[constIdx];
                    if (IS_STRING(val)) {
                        NyxObjString* name = AS_STRING(val);
                        // Dedup
                        bool found = false;
                        for (int j = 0; j < *importCount; j++) {
                            if (strcmp(imports[j], name->chars) == 0) {
                                found = true;
                                break;
                            }
                        }
                        if (!found && *importCount < maxImports) {
                            snprintf(imports[*importCount], 256, "%s", name->chars);
                            (*importCount)++;
                        }
                    }
                }
            }
        }

        // skip multi-byte operands so we don't misparse random data as OP_IMPORT
        switch (chunk->code[i]) {
            // 2-byte instructions (opcode + 1 operand)
            case OP_CONSTANT: case OP_DEFINE_GLOBAL: case OP_GET_GLOBAL:
            case OP_SET_GLOBAL: case OP_GET_LOCAL: case OP_SET_LOCAL:
            case OP_GET_UPVALUE: case OP_SET_UPVALUE:
            case OP_CALL: case OP_CLASS: case OP_GET_PROPERTY:
            case OP_SET_PROPERTY: case OP_METHOD: case OP_GET_SUPER:
            case OP_IMPORT: case OP_BUILD_SET: case OP_BUILD_LIST:
            case OP_BUILD_MAP: case OP_LOADI: case OP_ADDI: case OP_SUBI:
                i++; break;
            // 3-byte instructions (opcode + 2 operands)
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_LOOP:
            case OP_JUMP_IF_NIL: case OP_INVOKE: case OP_SUPER_INVOKE:
            case OP_CONSTANT_LONG:
                i += 2; break;
            // OP_CLOSURE: variable length — the fun one
            case OP_CLOSURE: {
                uint8_t fnIdx = chunk->code[++i];
                if (fnIdx < chunk->constants.count) {
                    NyxValue fnVal = chunk->constants.values[fnIdx];
                    if (IS_FUNCTION(fnVal)) {
                        // Recurse into the nested function
                        collectImports(AS_FUNCTION(fnVal), imports,
                                       importCount, maxImports);
                        // Skip upvalue entries
                        NyxObjFunction* nested = AS_FUNCTION(fnVal);
                        i += nested->upvalueCount * 2;
                    }
                }
                break;
            }
        }
    }

    // also check nested functions in the constant pool
    for (int c = 0; c < chunk->constants.count; c++) {
        if (IS_FUNCTION(chunk->constants.values[c])) {
            collectImports(AS_FUNCTION(chunk->constants.values[c]),
                           imports, importCount, maxImports);
        }
    }
}

// slurp a file into memory
static char* readSourceFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0L, SEEK_END);
    size_t size = ftell(f);
    rewind(f);
    char* buf = (char*)malloc(size + 1);
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

// resolve "module_name" to an actual file path
static bool resolveImportPath(const char* baseDir, const char* name, char* out, int outMax) {
    // Try name.nyx in baseDir
    snprintf(out, outMax, "%s/%s.nyx", baseDir, name);
    FILE* f = fopen(out, "rb");
    if (f) { fclose(f); return true; }

    // Try name/lib.nyx (package-style)
    snprintf(out, outMax, "%s/%s/lib.nyx", baseDir, name);
    f = fopen(out, "rb");
    if (f) { fclose(f); return true; }

    return false;
}

// recursively collect all modules starting from a root file
static bool collectModules(const char* baseDir, const char* name,
                           const char* filePath,
                           BundleModule* modules, int* moduleCount) {
    // Check if already collected
    for (int i = 0; i < *moduleCount; i++) {
        if (strcmp(modules[i].name, name) == 0) return true;
    }

    if (*moduleCount >= MAX_BUNDLE_MODULES) {
        fprintf(stderr, "nyx build: too many modules (max %d)\n", MAX_BUNDLE_MODULES);
        return false;
    }

    // Read and compile
    char* source = readSourceFile(filePath);
    if (!source) {
        fprintf(stderr, "nyx build: cannot read '%s'\n", filePath);
        return false;
    }

    nyx_vm_set_file(filePath);
    NyxObjFunction* func = nyx_compile(source);
    free(source);
    if (!func) {
        fprintf(stderr, "nyx build: compilation error in '%s'\n", filePath);
        return false;
    }

    // Add to modules list
    int idx = (*moduleCount)++;
    snprintf(modules[idx].name, sizeof(modules[idx].name), "%s", name);
    snprintf(modules[idx].path, sizeof(modules[idx].path), "%s", filePath);
    modules[idx].func = func;

    // Protect from GC
    nyx_vm_push(OBJ_VAL(func));

    // Extract imports and recursively collect them
    char imports[MAX_BUNDLE_MODULES][256];
    int importCount = 0;
    collectImports(func, imports, &importCount, MAX_BUNDLE_MODULES);

    for (int i = 0; i < importCount; i++) {
        char resolvedPath[1024];
        if (resolveImportPath(baseDir, imports[i], resolvedPath, sizeof(resolvedPath))) {
            if (!collectModules(baseDir, imports[i], resolvedPath,
                                modules, moduleCount)) {
                nyx_vm_pop();
                return false;
            }
        } else {
            // not found locally — it'll have to resolve at runtime from NYX_HOME
        }
    }

    nyx_vm_pop();
    return true;
}

int nyx_build(const char* inputDir, const char* outputPath) {
    // figure out the entry point (nyx.toml or main.nyx fallback)

    char mainPath[1024];
    char entryName[256] = "main";
    NyxManifest buildManifest;
    nyx_manifest_init(&buildManifest);
    bool hasManifest = false;

    char tomlPath[1024];
    snprintf(tomlPath, sizeof(tomlPath), "%s/nyx.toml", inputDir);
    if (nyx_manifest_parse(tomlPath, &buildManifest)) {
        hasManifest = true;
        if (buildManifest.entry[0] != '\0') {
            // Use entry from manifest
            snprintf(mainPath, sizeof(mainPath), "%s/%s", inputDir, buildManifest.entry);
            // Derive entry name (strip .nyx extension)
            snprintf(entryName, sizeof(entryName), "%s", buildManifest.entry);
            int elen = (int)strlen(entryName);
            if (elen > 4 && strcmp(entryName + elen - 4, ".nyx") == 0) {
                entryName[elen - 4] = '\0';
            }
        } else {
            snprintf(mainPath, sizeof(mainPath), "%s/main.nyx", inputDir);
        }
    } else {
        snprintf(mainPath, sizeof(mainPath), "%s/main.nyx", inputDir);
    }

    FILE* mainFile = fopen(mainPath, "rb");
    if (mainFile == NULL) {
        fprintf(stderr, "nyx build: cannot find entry point '%s'\n", mainPath);
        if (!hasManifest)
            fprintf(stderr, "         Add a nyx.toml with entry = \"your_file.nyx\" or create main.nyx\n");
        return 1;
    }
    fclose(mainFile);

    // Collect modules recursively
    BundleModule modules[MAX_BUNDLE_MODULES];
    int moduleCount = 0;

    // First, collect dependencies from nyx.toml and resolve them
    if (hasManifest && buildManifest.depCount > 0) {
        for (int d = 0; d < buildManifest.depCount; d++) {
            const char* depName = buildManifest.deps[d].name;
            const char* depVer = buildManifest.deps[d].version;

            // Try to find the dependency: local dir, nyx_modules, NYX_HOME
            char depPath[1024];
            bool found = false;

            // Local: <inputDir>/<dep>.nyx
            snprintf(depPath, sizeof(depPath), "%s/%s.nyx", inputDir, depName);
            if (fopen(depPath, "rb")) { found = true; fclose(fopen(depPath, "rb")); }

            // nyx_modules: <inputDir>/nyx_modules/<dep>/lib.nyx
            if (!found) {
                snprintf(depPath, sizeof(depPath), "%s/nyx_modules/%s/lib.nyx", inputDir, depName);
                FILE* f = fopen(depPath, "rb");
                if (f) { found = true; fclose(f); }
            }

            // NYX_HOME/libs/<dep>/<version>/lib.nyx (or entry from its nyx.toml)
            if (!found && vm.nyxHome[0] != '\0') {
                char libToml[1024];
                char libEntry[256] = "lib.nyx";
                if (depVer[0] != '\0') {
                    snprintf(libToml, sizeof(libToml), "%s/libs/%s/%s/nyx.toml",
                             vm.nyxHome, depName, depVer);
                    NyxManifest libM;
                    if (nyx_manifest_parse(libToml, &libM) && libM.entry[0])
                        snprintf(libEntry, sizeof(libEntry), "%s", libM.entry);
                    snprintf(depPath, sizeof(depPath), "%s/libs/%s/%s/%s",
                             vm.nyxHome, depName, depVer, libEntry);
                } else {
                    snprintf(depPath, sizeof(depPath), "%s/libs/%s/lib.nyx",
                             vm.nyxHome, depName);
                }
                FILE* f = fopen(depPath, "rb");
                if (f) { found = true; fclose(f); }
            }

            if (found) {
                collectModules(inputDir, depName, depPath, modules, &moduleCount);
            } else {
                fprintf(stderr, "nyx build: warning: dependency '%s' not found, skipping\n", depName);
            }
        }
    }

    // Then collect the main entry and its imports
    int mainModuleIdx = moduleCount; // main will be at this index (or already collected)
    if (!collectModules(inputDir, entryName, mainPath, modules, &moduleCount)) {
        return 1;
    }
    // If main was already collected as a dep, find its actual index
    for (int i = 0; i < moduleCount; i++) {
        if (strcmp(modules[i].name, entryName) == 0) {
            mainModuleIdx = i;
            break;
        }
    }

    // write .nyxc — deps first, main last. order matters for execution
    FILE* out = fopen(outputPath, "wb");
    if (out == NULL) {
        fprintf(stderr, "nyx: cannot create '%s'\n", outputPath);
        return 1;
    }

    // Header
    fputc(NYX_MAGIC_0, out);
    fputc(NYX_MAGIC_1, out);
    fputc(NYX_MAGIC_2, out);
    fputc(NYX_MAGIC_3, out);
    fputc(NYX_VERSION_MAJOR, out);
    fputc(NYX_VERSION_MINOR, out);
    fputc(NYX_VERSION_PATCH, out);

    //  scan for native binaries to embed
    char binDir[1024];
    snprintf(binDir, sizeof(binDir), "%s/bin", inputDir);

    typedef struct { char tag[64]; char fname[256]; char path[1024]; } NativeBin;
    NativeBin natives[64];
    int nativeCount = 0;

#ifdef _WIN32
    {
        char searchPath[1024];
        snprintf(searchPath, sizeof(searchPath), "%s/*", binDir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const char* fname = fd.cFileName;
                int flen = (int)strlen(fname);
                const char* ext = NULL;
                for (const char* p = fname + flen - 1; p > fname; p--) {
                    if (*p == '.') { ext = p; break; }
                }
                if (!ext) continue;
                const char* tag = NULL;
                if (strcmp(ext, ".dll") == 0) {
                    if (strstr(fname, "windows-x64")) tag = "windows-x64";
                    else if (strstr(fname, "windows-arm64")) tag = "windows-arm64";
                    else tag = "windows-x64";
                } else if (strcmp(ext, ".so") == 0) {
                    if (strstr(fname, "linux-x64")) tag = "linux-x64";
                    else if (strstr(fname, "linux-arm64")) tag = "linux-arm64";
                    else tag = "linux-x64";
                } else if (strcmp(ext, ".dylib") == 0) {
                    if (strstr(fname, "macos-arm64")) tag = "macos-arm64";
                    else if (strstr(fname, "macos-x64")) tag = "macos-x64";
                    else tag = "macos-arm64";
                }
                if (tag && nativeCount < 64) {
                    snprintf(natives[nativeCount].tag, 64, "%s", tag);
                    snprintf(natives[nativeCount].fname, 256, "%s", fname);
                    snprintf(natives[nativeCount].path, 1024, "%s/%s", binDir, fname);
                    nativeCount++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
#else
    {
        DIR* dir = opendir(binDir);
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                const char* fname = entry->d_name;
                int flen = (int)strlen(fname);
                const char* ext = NULL;
                for (const char* p = fname + flen - 1; p > fname; p--) {
                    if (*p == '.') { ext = p; break; }
                }
                if (!ext) continue;
                const char* tag = NULL;
                if (strcmp(ext, ".dll") == 0) {
                    tag = strstr(fname, "windows-arm64") ? "windows-arm64" : "windows-x64";
                } else if (strcmp(ext, ".so") == 0) {
                    tag = strstr(fname, "linux-arm64") ? "linux-arm64" : "linux-x64";
                } else if (strcmp(ext, ".dylib") == 0) {
                    tag = strstr(fname, "macos-x64") ? "macos-x64" : "macos-arm64";
                }
                if (tag && nativeCount < 64) {
                    snprintf(natives[nativeCount].tag, 64, "%s", tag);
                    snprintf(natives[nativeCount].fname, 256, "%s", fname);
                    snprintf(natives[nativeCount].path, 1024, "%s/%s", binDir, fname);
                    nativeCount++;
                }
            }
            closedir(dir);
        }
    }
#endif

    // write embedded native binaries
    writeUint16(out, (uint16_t)nativeCount);
    for (int i = 0; i < nativeCount; i++) {
        int tagLen = (int)strlen(natives[i].tag);
        writeUint16(out, (uint16_t)tagLen);
        fwrite(natives[i].tag, 1, tagLen, out);
        int fnameLen = (int)strlen(natives[i].fname);
        writeUint16(out, (uint16_t)fnameLen);
        fwrite(natives[i].fname, 1, fnameLen, out);
        FILE* binFile = fopen(natives[i].path, "rb");
        if (binFile) {
            fseek(binFile, 0L, SEEK_END);
            uint32_t binSize = (uint32_t)ftell(binFile);
            rewind(binFile);
            writeUint32(out, binSize);
            char buf[8192];
            uint32_t remaining = binSize;
            while (remaining > 0) {
                size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
                size_t rd = fread(buf, 1, chunk, binFile);
                fwrite(buf, 1, rd, out);
                remaining -= (uint32_t)rd;
            }
            fclose(binFile);
        } else {
            writeUint32(out, 0);
        }
    }

    // write module bytecode
    writeUint16(out, (uint16_t)moduleCount);

    // deps first so their globals are defined when main runs
    for (int i = 0; i < moduleCount; i++) {
        if (i == mainModuleIdx) continue; // skip main, write it last
        int nameLen = (int)strlen(modules[i].name);
        writeUint16(out, (uint16_t)nameLen);
        fwrite(modules[i].name, 1, nameLen, out);
        serializeFunction(out, modules[i].func);
    }
    // Write main entry last
    {
        int nameLen = (int)strlen(modules[mainModuleIdx].name);
        writeUint16(out, (uint16_t)nameLen);
        fwrite(modules[mainModuleIdx].name, 1, nameLen, out);
        serializeFunction(out, modules[mainModuleIdx].func);
    }

    fclose(out);
    printf("Bundled %d module%s", moduleCount, moduleCount > 1 ? "s" : "");
    if (nativeCount > 0) printf(", %d native binar%s", nativeCount, nativeCount > 1 ? "ies" : "y");
    printf("\n");
    return 0;
}
