#ifndef NYX_VM_H
#define NYX_VM_H

#include "chunk.h"
#include "manifest.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX  (FRAMES_MAX * UINT8_COUNT)

// CallFrame lives in object.h (coroutines need it)

typedef struct {
    // active stack/frames — these get swapped when entering/leaving coroutines
    CallFrame* frames;
    int frameCount;
    NyxValue* stack;
    NyxValue* stackTop;

    // main stack/frames — heap-allocated so we don't bloat the binary
    CallFrame* mainFrames;
    NyxValue* mainStack;

    NyxTable globals;
    NyxTable strings;

    NyxObjUpvalue* openUpvalues;
    NyxObjString* initString;    // cached "init" string
    NyxObj* objects;

    // coroutine support
    NyxObjCoroutine* runningCoro;   // currently executing coroutine (NULL if main)

    // module system
    const char* currentFile;     // path of currently executing file (for relative imports)
    NyxTable modules;            // already-imported module paths -> true (dedup)

    // package system
    char nyxHome[1024];          // NYX_HOME env var (cached on init)
    NyxManifest projectManifest; // project nyx.toml (loaded on first do_file)
    bool projectManifestLoaded;

    // GC state — the reaper's bookkeeping
    size_t bytesAllocated;
    size_t nextGC;
    int grayCount;
    int grayCapacity;
    NyxObj** grayStack;

    // inline cache: (callsite, class) -> closure. GC nukes it every collection
    struct {
        uint8_t* ip;              // instruction pointer of the OP_INVOKE
        NyxObjClass* klass;       // last seen class
        NyxObjClosure* method;    // cached method closure
    } icache[16];                 // 16-entry direct-mapped cache

    // generational GC bookkeeping
    NyxObj** rememberedSet;
    int rememberedCount;
    int rememberedCapacity;
    int minorCollections;
    size_t youngBytesAllocated;
    // profiler flag
    bool profilingEnabled;
} NyxVM;

// Profiler

#define NYX_PROFILE_MAX_FUNCS 512

typedef struct {
    const char* name;     // function name (NULL = script)
    int callCount;
    double totalTime;     // seconds
    double selfTime;      // seconds (excluding callees)
} NyxProfileEntry;

typedef struct {
    NyxProfileEntry entries[NYX_PROFILE_MAX_FUNCS];
    int count;
    double startTime;
    size_t peakMemory;
    int totalCalls;
    // call stack for self-time tracking
    int callStack[256];   // indices into entries
    double callStartTimes[256];
    int callDepth;
} NyxProfiler;

extern NyxProfiler nyx_profiler;

void nyx_profiler_init(void);
void nyx_profiler_enter(const char* name);
void nyx_profiler_exit(void);
void nyx_profiler_report(void);

typedef enum {
    NYX_OK,
    NYX_COMPILE_ERROR,
    NYX_RUNTIME_ERROR,
    NYX_YIELDED,        // coroutine yielded (internal use)
} NyxResult;

extern NyxVM vm;

void nyx_vm_init(void);
void nyx_vm_free(void);
NyxResult nyx_vm_interpret(const char* source);
void nyx_vm_push(NyxValue value);
NyxValue nyx_vm_pop(void);
void nyx_vm_set_file(const char* path);
void nyx_vm_set_home(const char* path);
bool nyx_vm_call(NyxObjClosure* closure, int argCount);
NyxResult nyx_vm_run(void);

// bytecode deserialization — lives in api.c, used by the import handler
NyxObjFunction* nyx_deserialize_function(FILE* f);

#endif
