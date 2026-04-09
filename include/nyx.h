#ifndef NYX_H
#define NYX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Result Codes ───────────────────────────────────────────────────────────

#ifndef NYX_VM_H
// only define if vm.h hasn't beaten us to it
typedef enum {
    NYX_OK = 0,
    NYX_COMPILE_ERROR = 1,
    NYX_RUNTIME_ERROR = 2,
} NyxResult;
#endif

// ─── Opaque Types (you don't need to know what's inside) ───────────────────

typedef struct NyxVM NyxVM_t;      // forward decl — users only see a pointer

// ─── Value Types (what's on the stack) ──────────────────────────────────────

typedef enum {
    NYX_VAL_NIL,
    NYX_VAL_BOOL,
    NYX_VAL_INT,
    NYX_VAL_FLOAT,
    NYX_VAL_STRING,
    NYX_VAL_OBJECT,     // list, map, class, instance, closure, etc.
} NyxValType;

// ─── Native Function Signature ──────────────────────────────────────────────
// functions: args[0..n] are the arguments
// methods: args[0] is self, args[1..n] are the rest
// return value replaces everything on the stack
#ifndef NYX_VALUE_H
typedef uint64_t NyxValue;
#endif
typedef NyxValue (*NyxNativeFn)(int argCount, NyxValue* args);

// ─── VM Lifecycle (init, run, destroy) ──────────────────────────────────────

void        nyx_init(void);
void        nyx_free(void);
NyxResult   nyx_do_string(const char* source);
NyxResult   nyx_do_file(const char* path);
void        nyx_set_file(const char* path);

// ─── Stack Manipulation ─────────────────────────────────────────────────────
// positive index = from bottom, negative = from top (-1 = top of stack)

void        nyx_push_nil(void);
void        nyx_push_bool(bool value);
void        nyx_push_int(int64_t value);
void        nyx_push_float(double value);
void        nyx_push_string(const char* str, int length);
void        nyx_push_cstring(const char* str);    // null-terminated convenience

NyxValType  nyx_type_at(int index);
bool        nyx_is_nil(int index);
bool        nyx_is_bool(int index);
bool        nyx_is_int(int index);
bool        nyx_is_float(int index);
bool        nyx_is_string(int index);

bool        nyx_to_bool(int index);
int64_t     nyx_to_int(int index);
double      nyx_to_float(int index);
const char* nyx_to_cstring(int index);
int         nyx_string_length(int index);

void        nyx_pop(int count);
int         nyx_stack_size(void);

// ─── Calling Nyx from C ─────────────────────────────────────────────────────
// push the function, push the args, call. that's it
bool        nyx_get_global(const char* name);      // pushes value onto stack
NyxResult   nyx_call(int argCount);                 // calls function at stack[-argCount-1]

// ─── Registering C Functions into Nyx ───────────────────────────────────────

void        nyx_register_fn(const char* name, NyxNativeFn fn);
void        nyx_register_class(const char* name);
void        nyx_register_method(const char* className, const char* methodName,
                                NyxNativeFn fn);

// ─── Native Module Loading ──────────────────────────────────────────────────
// API vtable passed to native modules via function pointer — no linking needed.
// modules use this to register functions and create values
typedef struct {
    // registration
    void      (*register_fn)(const char* name, NyxNativeFn fn);
    void      (*register_class)(const char* name);
    void      (*register_method)(const char* cls, const char* method, NyxNativeFn fn);
    // value creation
    NyxValue  (*string_val)(const char* chars, int length);
    NyxValue  (*result_ok)(NyxValue value);
    NyxValue  (*result_err)(NyxValue value);
    NyxValue  (*list_new)(void);
    void      (*list_push)(NyxValue list, NyxValue item);
    NyxValue  (*map_new)(void);
    void      (*map_set)(NyxValue map, const char* key, NyxValue value);
} NyxModuleAPI;

// native modules export nyx_module_init(const NyxModuleAPI* api)
// use the api table to register your stuff. that's the whole contract
bool        nyx_load_native(const char* path);

// ─── GC Control (poke the garbage collector) ────────────────────────────────

void        nyx_gc_collect(void);

// ─── Bytecode Compilation ───────────────────────────────────────────────────
int         nyx_compile_to_file(const char* sourcePath, const char* outputPath);

// bundle a whole project into a single .nyxc
int         nyx_build(const char* inputDir, const char* outputPath);

// run a compiled .nyxc file
NyxResult   nyx_run_compiled(const char* path);

// ─── .nyxc Format ───────────────────────────────────────────────────────────
// "NYX\0" magic + version(3B) + module count(2B) + modules
// each module: name_len(2B) + name + bytecode_len(4B) + bytecode

#define NYX_MAGIC_0 'N'
#define NYX_MAGIC_1 'Y'
#define NYX_MAGIC_2 'X'
#define NYX_MAGIC_3 '\0'

#ifdef __cplusplus
}
#endif

#endif
