#ifndef NYX_OBJECT_H
#define NYX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

typedef enum {
    OBJ_STRING,
    OBJ_INT64,      // heap-allocated 64-bit integer (for values >48 bits)
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_LIST,
    OBJ_MAP,
    OBJ_RESULT,
    OBJ_RANGE,
    OBJ_SET,
    OBJ_COROUTINE,
} NyxObjType;

// generational GC ages — objects grow up or die trying
#define NYX_GEN_YOUNG     0  // just allocated
#define NYX_GEN_SURVIVED  1  // survived 1 minor GC
#define NYX_GEN_OLD       2  // promoted — skipped in minor collections

// GC flag bits packed into a single byte
#define NYX_GC_MARK_BIT   0x04  // bit 2: marked during tracing
#define NYX_GC_REMEMBERED 0x08  // bit 3: old obj in remembered set

// gcflags layout: [....RMAA]  R=remembered, M=marked, AA=age (0-2)
#define OBJ_AGE(o)             ((o)->gcflags & 0x03)
#define OBJ_SET_AGE(o, a)     ((o)->gcflags = ((o)->gcflags & ~0x03) | (a))
#define OBJ_IS_MARKED(o)      ((o)->gcflags & NYX_GC_MARK_BIT)
#define OBJ_MARK(o)           ((o)->gcflags |= NYX_GC_MARK_BIT)
#define OBJ_UNMARK(o)         ((o)->gcflags &= ~NYX_GC_MARK_BIT)
#define OBJ_IS_OLD(o)         (OBJ_AGE(o) == NYX_GEN_OLD)
#define OBJ_IS_YOUNG(o)       (OBJ_AGE(o) <= NYX_GEN_SURVIVED)
#define OBJ_IS_REMEMBERED(o)  ((o)->gcflags & NYX_GC_REMEMBERED)
#define OBJ_SET_REMEMBERED(o) ((o)->gcflags |= NYX_GC_REMEMBERED)
#define OBJ_CLR_REMEMBERED(o) ((o)->gcflags &= ~NYX_GC_REMEMBERED)

struct NyxObj {
    NyxObjType type;
    uint8_t gcflags;
    struct NyxObj* next;
};

struct NyxObjString {
    NyxObj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// heap-boxed int64 for numbers too big for 48-bit NaN boxing
typedef struct {
    NyxObj obj;
    int64_t value;
} NyxObjInt64;

typedef struct {
    NyxObj obj;
    int arity;
    int upvalueCount;
    bool isGenerator;       // true if function contains 'yield'
    bool isVariadic;        // true if last param is ...args
    NyxChunk chunk;
    NyxObjString* name;
} NyxObjFunction;

typedef struct NyxObjUpvalue {
    NyxObj obj;
    NyxValue* location;
    NyxValue closed;
    struct NyxObjUpvalue* next;
} NyxObjUpvalue;

typedef struct {
    NyxObj obj;
    NyxObjFunction* function;
    NyxObjUpvalue** upvalues;
    int upvalueCount;
} NyxObjClosure;

typedef NyxValue (*NyxNativeFn)(int argCount, NyxValue* args);

typedef struct {
    NyxObj obj;
    NyxNativeFn function;
    const char* name;
} NyxObjNative;

typedef struct NyxObjClass {
    NyxObj obj;
    NyxObjString* name;
    struct NyxObjClass* superclass; // NULL if no parent
    NyxTable methods;
} NyxObjClass;

typedef struct {
    NyxObj obj;
    NyxObjClass* klass;
    NyxTable fields;
} NyxObjInstance;

typedef struct {
    NyxObj obj;
    NyxValue receiver;      // the instance
    NyxObjClosure* method;  // NULL if native
    NyxNativeFn nativeFn;   // non-NULL if bound native method
} NyxObjBoundMethod;

typedef struct {
    NyxObj obj;
    NyxValueArray items;
    int maxSize;            // -1 = unlimited, >0 = evict oldest on overflow
} NyxObjList;

typedef struct {
    NyxObj obj;
    NyxTable table;
    NyxValueArray keys;
} NyxObjMap;

typedef struct {
    NyxObj obj;
    bool isOk;          // true = Ok(value), false = Err(value)
    NyxValue value;
} NyxObjResult;

typedef struct {
    NyxObj obj;
    int64_t start;
    int64_t end;
} NyxObjRange;

typedef struct {
    NyxObj obj;
    NyxValueArray items;  // all value types supported
} NyxObjSet;

// CallFrame lives here (not vm.h) because coroutines embed an array of them
typedef struct {
    NyxObjClosure* closure;
    uint8_t* ip;
    NyxValue* slots;
    const char* prevFile;    // saved currentFile for OP_IMPORT restoration
} CallFrame;

typedef enum {
    CORO_CREATED,
    CORO_RUNNING,
    CORO_SUSPENDED,
    CORO_DEAD,
} NyxCoroState;

#define CORO_STACK_MAX 256
#define CORO_FRAMES_MAX 16

typedef struct {
    NyxObj obj;
    NyxObjClosure* closure;
    NyxCoroState state;

    // coro's own stack and frames — fully independent execution context
    NyxValue stack[CORO_STACK_MAX];
    NyxValue* stackTop;
    CallFrame frames[CORO_FRAMES_MAX];
    int frameCount;
    NyxObjUpvalue* openUpvalues;

    // caller's state — saved/restored on resume/yield (just pointer swaps)
    NyxValue* callerStackTop;
    int callerFrameCount;
    NyxObjUpvalue* callerUpvalues;
} NyxObjCoroutine;

// type checking macros
#define OBJ_TYPE(value)         (AS_OBJ(value)->type)
#define IS_INT64(value)         nyx_is_obj_type(value, OBJ_INT64)
#define AS_INT64(value)         (((NyxObjInt64*)AS_OBJ(value))->value)
#define IS_STRING(value)        nyx_is_obj_type(value, OBJ_STRING)
#define IS_FUNCTION(value)      nyx_is_obj_type(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)       nyx_is_obj_type(value, OBJ_CLOSURE)
#define IS_NATIVE(value)        nyx_is_obj_type(value, OBJ_NATIVE)
#define IS_CLASS(value)         nyx_is_obj_type(value, OBJ_CLASS)
#define IS_INSTANCE(value)      nyx_is_obj_type(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value)  nyx_is_obj_type(value, OBJ_BOUND_METHOD)
#define IS_LIST(value)          nyx_is_obj_type(value, OBJ_LIST)
#define IS_MAP(value)           nyx_is_obj_type(value, OBJ_MAP)
#define IS_RESULT(value)        nyx_is_obj_type(value, OBJ_RESULT)
#define IS_RANGE(value)         nyx_is_obj_type(value, OBJ_RANGE)
#define IS_SET(value)           nyx_is_obj_type(value, OBJ_SET)
#define IS_COROUTINE(value)     nyx_is_obj_type(value, OBJ_COROUTINE)

// unwrap macros — cast the pointer, hope for the best
#define AS_STRING(value)        ((NyxObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((NyxObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((NyxObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((NyxObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value)        (((NyxObjNative*)AS_OBJ(value))->function)
#define AS_CLASS(value)         ((NyxObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((NyxObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((NyxObjBoundMethod*)AS_OBJ(value))
#define AS_LIST(value)          ((NyxObjList*)AS_OBJ(value))
#define AS_MAP(value)           ((NyxObjMap*)AS_OBJ(value))
#define AS_RESULT(value)        ((NyxObjResult*)AS_OBJ(value))
#define AS_RANGE(value)         ((NyxObjRange*)AS_OBJ(value))
#define AS_SET(value)           ((NyxObjSet*)AS_OBJ(value))
#define AS_COROUTINE(value)     ((NyxObjCoroutine*)AS_OBJ(value))

static inline bool nyx_is_obj_type(NyxValue value, NyxObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// Full 64-bit integer support
// override the value.h macros to handle both NaN-boxed (fast) and heap-boxed (big) ints

#ifdef NYX_NAN_BOXING

#undef IS_INT
#undef AS_INT
#undef INT_VAL

// forward decl needed below
NyxObjInt64* nyx_new_int64(int64_t value);

static inline bool nyx_is_any_int(NyxValue v) {
    if (IS_INT_FAST(v)) return true;
    return IS_INT64(v);
}
#define IS_INT(v) nyx_is_any_int(v)

static inline int64_t nyx_as_any_int(NyxValue v) {
    if (IS_INT_FAST(v)) return AS_INT_FAST(v);
    return ((NyxObjInt64*)AS_OBJ(v))->value;
}
#define AS_INT(v) nyx_as_any_int(v)

static inline NyxValue nyx_make_int_val(int64_t i) {
    if (NYX_FITS_48(i)) return INT_VAL_SMALL(i);
    return OBJ_VAL(nyx_new_int64(i));
}
#define INT_VAL(i) nyx_make_int_val((int64_t)(i))

#endif // NYX_NAN_BOXING

NyxObjFunction*    nyx_new_function(void);
NyxObjClosure*     nyx_new_closure(NyxObjFunction* function);
NyxObjUpvalue*     nyx_new_upvalue(NyxValue* slot);
NyxObjNative*      nyx_new_native(NyxNativeFn function, const char* name);
NyxObjClass*       nyx_new_class(NyxObjString* name);
NyxObjInstance*    nyx_new_instance(NyxObjClass* klass);
NyxObjBoundMethod* nyx_new_bound_method(NyxValue receiver, NyxObjClosure* method);
NyxObjBoundMethod* nyx_new_bound_native_method(NyxValue receiver, NyxNativeFn fn);
NyxObjList*        nyx_new_list(void);
NyxObjMap*         nyx_new_map(void);
NyxObjResult*      nyx_new_result(bool isOk, NyxValue value);
NyxObjRange*       nyx_new_range(int64_t start, int64_t end);
NyxObjSet*         nyx_new_set(void);
NyxObjCoroutine*   nyx_new_coroutine(NyxObjClosure* closure);
NyxObjString*      nyx_copy_string(const char* chars, int length);
NyxObjString*      nyx_take_string(char* chars, int length);
void               nyx_print_object(NyxValue value);

#endif
