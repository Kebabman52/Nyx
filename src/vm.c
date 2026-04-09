#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include "collections.h"
#include "common.h"
#include "nyx_stdlib.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include <windows.h>

NyxVM vm;
NyxProfiler nyx_profiler;

// Profiler (where your bottlenecks go to get roasted)

static double profilerClock(void) {
    return (double)clock() / CLOCKS_PER_SEC;
}

void nyx_profiler_init(void) {
    memset(&nyx_profiler, 0, sizeof(NyxProfiler));
    nyx_profiler.startTime = profilerClock();
}

static int profilerFindOrCreate(const char* name) {
    for (int i = 0; i < nyx_profiler.count; i++) {
        if (nyx_profiler.entries[i].name == name) return i;
        if (name != NULL && nyx_profiler.entries[i].name != NULL &&
            strcmp(nyx_profiler.entries[i].name, name) == 0) return i;
    }
    if (nyx_profiler.count >= NYX_PROFILE_MAX_FUNCS) return 0; // out of slots, sorry
    int idx = nyx_profiler.count++;
    nyx_profiler.entries[idx].name = name;
    nyx_profiler.entries[idx].callCount = 0;
    nyx_profiler.entries[idx].totalTime = 0;
    nyx_profiler.entries[idx].selfTime = 0;
    return idx;
}

void nyx_profiler_enter(const char* name) {
    if (!vm.profilingEnabled) return;

    int idx = profilerFindOrCreate(name);
    nyx_profiler.entries[idx].callCount++;
    nyx_profiler.totalCalls++;

    // high score for memory usage
    if (vm.bytesAllocated > nyx_profiler.peakMemory) {
        nyx_profiler.peakMemory = vm.bytesAllocated;
    }

    // 256 deep should be enough. if not, you have bigger problems
    if (nyx_profiler.callDepth < 256) {
        nyx_profiler.callStack[nyx_profiler.callDepth] = idx;
        nyx_profiler.callStartTimes[nyx_profiler.callDepth] = profilerClock();
        nyx_profiler.callDepth++;
    }
}

void nyx_profiler_exit(void) {
    if (!vm.profilingEnabled) return;
    if (nyx_profiler.callDepth <= 0) return;

    nyx_profiler.callDepth--;
    int idx = nyx_profiler.callStack[nyx_profiler.callDepth];
    double elapsed = profilerClock() - nyx_profiler.callStartTimes[nyx_profiler.callDepth];

    nyx_profiler.entries[idx].totalTime += elapsed;
    nyx_profiler.entries[idx].selfTime += elapsed;

    // don't blame the parent for time spent in the child
    if (nyx_profiler.callDepth > 0) {
        int parentIdx = nyx_profiler.callStack[nyx_profiler.callDepth - 1];
        nyx_profiler.entries[parentIdx].selfTime -= elapsed;
    }
}

// sort by self-time descending — biggest offenders first
static int profileCompare(const void* a, const void* b) {
    const NyxProfileEntry* ea = (const NyxProfileEntry*)a;
    const NyxProfileEntry* eb = (const NyxProfileEntry*)b;
    if (eb->selfTime > ea->selfTime) return 1;
    if (eb->selfTime < ea->selfTime) return -1;
    return 0;
}

void nyx_profiler_report(void) {
    double totalTime = profilerClock() - nyx_profiler.startTime;

    // wall of shame, sorted
    qsort(nyx_profiler.entries, nyx_profiler.count,
          sizeof(NyxProfileEntry), profileCompare);

    fprintf(stderr, "\n");
    fprintf(stderr, "══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "  Nyx Profiler Report\n");
    fprintf(stderr, "══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "  Total time:     %.3fs\n", totalTime);
    fprintf(stderr, "  Total calls:    %d\n", nyx_profiler.totalCalls);
    fprintf(stderr, "  Peak memory:    %.1f KB\n", nyx_profiler.peakMemory / 1024.0);
    fprintf(stderr, "  Functions:      %d unique\n", nyx_profiler.count);
    fprintf(stderr, "──────────────────────────────────────────────────────────────\n");
    fprintf(stderr, "  %-24s %8s %10s %10s %6s\n",
            "Function", "Calls", "Total(ms)", "Self(ms)", "%");
    fprintf(stderr, "──────────────────────────────────────────────────────────────\n");

    for (int i = 0; i < nyx_profiler.count && i < 30; i++) {
        NyxProfileEntry* e = &nyx_profiler.entries[i];
        if (e->callCount == 0) continue;
        const char* name = e->name ? e->name : "<script>";
        double pct = totalTime > 0 ? (e->selfTime / totalTime) * 100.0 : 0;
        fprintf(stderr, "  %-24s %8d %10.2f %10.2f %5.1f%%\n",
                name, e->callCount,
                e->totalTime * 1000, e->selfTime * 1000, pct);
    }

    fprintf(stderr, "══════════════════════════════════════════════════════════════\n\n");
}

// ─── Native Functions (the C-side cheat codes) ─────────────────────────────

static NyxValue nativeStr(int argCount, NyxValue* args) {
    (void)argCount;
    NyxValue val = args[0];

    if (IS_STRING(val)) return val;

    char buffer[64];
    int len = 0;

    if (IS_INT(val)) {
        len = snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_INT(val));
    } else if (IS_FLOAT(val)) {
        double f = AS_FLOAT(val);
        if (f == (int64_t)f && f >= -1e15 && f <= 1e15) {
            len = snprintf(buffer, sizeof(buffer), "%.1f", f);
        } else {
            len = snprintf(buffer, sizeof(buffer), "%g", f);
        }
    } else if (IS_BOOL(val)) {
        len = snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(val) ? "true" : "false");
    } else if (IS_NIL(val)) {
        len = snprintf(buffer, sizeof(buffer), "nil");
    } else {
        len = snprintf(buffer, sizeof(buffer), "<object>");
    }

    return OBJ_VAL(nyx_copy_string(buffer, len));
}

static NyxValue nativeClock(int argCount, NyxValue* args) {
    (void)argCount; (void)args;
    return FLOAT_VAL((double)clock() / CLOCKS_PER_SEC);
}

static NyxValue nativeLen(int argCount, NyxValue* args) {
    (void)argCount;
    if (IS_STRING(args[0])) return INT_VAL(AS_STRING(args[0])->length);
    if (IS_LIST(args[0]))   return INT_VAL(AS_LIST(args[0])->items.count);
    if (IS_MAP(args[0]))    return INT_VAL(AS_MAP(args[0])->keys.count);
    return INT_VAL(0);
}

static NyxValue nativeType(int argCount, NyxValue* args) {
    (void)argCount;
    NyxValue val = args[0];
    const char* name;

    if (IS_INT(val))        name = "int";
    else if (IS_FLOAT(val)) name = "float";
    else if (IS_BOOL(val))  name = "bool";
    else if (IS_NIL(val))   name = "nil";
    else if (IS_STRING(val)) name = "string";
    else if (IS_CLOSURE(val) || IS_FUNCTION(val)) name = "fn";
    else if (IS_NATIVE(val)) name = "fn";
    else if (IS_CLASS(val))  name = "class";
    else if (IS_INSTANCE(val)) {
        // copy the class name — returning a pointer into a GC'd object is how you get CVEs
        return OBJ_VAL(nyx_copy_string(
            AS_INSTANCE(val)->klass->name->chars,
            AS_INSTANCE(val)->klass->name->length));
    }
    else if (IS_LIST(val))  name = "list";
    else if (IS_MAP(val))   name = "map";
    else if (IS_RESULT(val)) name = AS_RESULT(val)->isOk ? "Ok" : "Err";
    else if (IS_RANGE(val))     name = "range";
    else if (IS_SET(val))       name = "set";
    else if (IS_COROUTINE(val)) name = "coroutine";
    else if (IS_BOUND_METHOD(val)) name = "fn";
    else name = "unknown";

    return OBJ_VAL(nyx_copy_string(name, (int)strlen(name)));
}

static NyxValue nativeOk(int argCount, NyxValue* args) {
    (void)argCount;
    return OBJ_VAL(nyx_new_result(true, args[0]));
}

static NyxValue nativeErr(int argCount, NyxValue* args) {
    (void)argCount;
    return OBJ_VAL(nyx_new_result(false, args[0]));
}

static NyxValue nativePanic(int argCount, NyxValue* args) {
    (void)argCount;
    fprintf(stderr, "panic: ");
    if (IS_STRING(args[0])) {
        fprintf(stderr, "%s", AS_CSTRING(args[0]));
    } else {
        fprintf(stderr, "<value>");
    }
    fprintf(stderr, "\n");
    exit(1);
    return NIL_VAL;
}

static NyxValue nativeIsOk(int argCount, NyxValue* args) {
    (void)argCount;
    if (!IS_RESULT(args[0])) return BOOL_VAL(false);
    return BOOL_VAL(AS_RESULT(args[0])->isOk);
}

static NyxValue nativeIsErr(int argCount, NyxValue* args) {
    (void)argCount;
    if (!IS_RESULT(args[0])) return BOOL_VAL(false);
    return BOOL_VAL(!AS_RESULT(args[0])->isOk);
}

static NyxValue nativeUnwrap(int argCount, NyxValue* args) {
    (void)argCount;
    if (!IS_RESULT(args[0])) {
        fprintf(stderr, "panic: unwrap() called on non-Result value\n");
        exit(1);
    }
    NyxObjResult* result = AS_RESULT(args[0]);
    if (!result->isOk) {
        fprintf(stderr, "panic: unwrap() called on Err(");
        nyx_print_value(result->value);
        fprintf(stderr, ")\n");
        exit(1);
    }
    return result->value;
}

static NyxValue nativeUnwrapOr(int argCount, NyxValue* args) {
    (void)argCount;
    if (!IS_RESULT(args[0])) return args[1];
    NyxObjResult* result = AS_RESULT(args[0]);
    return result->isOk ? result->value : args[1];
}

// ─── VM Core (here be dragons) ──────────────────────────────────────────────

static void resetStack(void) {
    vm.stack = vm.mainStack;
    vm.frames = vm.mainFrames;
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // stack trace: here's how you got here, you poor bastard
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        NyxObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

static void defineNative(const char* name, NyxNativeFn function) {
    NyxObjString* nameStr = nyx_copy_string(name, (int)strlen(name));
    nyx_vm_push(OBJ_VAL(nameStr));
    nyx_vm_push(OBJ_VAL(nyx_new_native(function, name)));
    nyx_table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    nyx_vm_pop();
    nyx_vm_pop();
}

void nyx_vm_init(void) {
    // heap-allocate the stack so we don't blow up the binary with a giant static array
    vm.mainStack = (NyxValue*)malloc(sizeof(NyxValue) * STACK_MAX);
    vm.mainFrames = (CallFrame*)malloc(sizeof(CallFrame) * FRAMES_MAX);
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024; // 1MB before the GC starts caring
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    memset(vm.icache, 0, sizeof(vm.icache));
    vm.rememberedSet = NULL;
    vm.rememberedCount = 0;
    vm.rememberedCapacity = 0;
    vm.minorCollections = 0;
    vm.youngBytesAllocated = 0;
    nyx_table_init(&vm.globals);
    nyx_table_init(&vm.strings);
    nyx_table_init(&vm.modules);
    vm.currentFile = NULL;
    vm.runningCoro = NULL;

    // package system: figure out where the hell our libs live
    vm.nyxHome[0] = '\0';
    const char* nyxHome = getenv("NYX_HOME");
    if (nyxHome != NULL && nyxHome[0] != '\0') {
        snprintf(vm.nyxHome, sizeof(vm.nyxHome), "%s", nyxHome);
    }
    vm.projectManifestLoaded = false;
    nyx_manifest_init(&vm.projectManifest);
    vm.profilingEnabled = false;

    vm.initString = nyx_copy_string("init", 4);

    defineNative("str", nativeStr);
    defineNative("clock", nativeClock);
    defineNative("len", nativeLen);
    defineNative("type", nativeType);
    defineNative("Ok", nativeOk);
    defineNative("Err", nativeErr);
    defineNative("panic", nativePanic);
    defineNative("is_ok", nativeIsOk);
    defineNative("is_err", nativeIsErr);
    defineNative("unwrap", nativeUnwrap);
    defineNative("unwrap_or", nativeUnwrapOr);

    nyx_stdlib_init();

    nyx_gc_set_ready(true);
}

extern void nyx_unload_natives(void); // api.c
extern bool nyx_load_native(const char* path); // api.c

void nyx_vm_free(void) {
    nyx_unload_natives();
    nyx_table_free(&vm.globals);
    nyx_table_free(&vm.strings);
    nyx_table_free(&vm.modules);
    nyx_free_objects();
    free(vm.grayStack);
    vm.grayStack = NULL;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    free(vm.mainStack);
    free(vm.mainFrames);
    vm.mainStack = NULL;
    vm.mainFrames = NULL;
    free(vm.rememberedSet);
    vm.rememberedSet = NULL;
    vm.rememberedCount = 0;
    vm.rememberedCapacity = 0;
}

void nyx_vm_push(NyxValue value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

NyxValue nyx_vm_pop(void) {
    vm.stackTop--;
    return *vm.stackTop;
}

static NyxValue peek(int distance) {
    return vm.stackTop[-1 - distance];
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline bool isFalsey(NyxValue value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
    // GC can fire during ALLOCATE and invalidate our pointers. fun!
    // both strings stay on stack so they survive. re-read after alloc
    int lengthA = AS_STRING(peek(1))->length;
    int lengthB = AS_STRING(peek(0))->length;
    int length = lengthA + lengthB;
    char* chars = ALLOCATE(char, length + 1);
    // re-read. yes, really. GC is a bastard
    NyxObjString* a = AS_STRING(peek(1));
    NyxObjString* b = AS_STRING(peek(0));
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    NyxObjString* result = nyx_take_string(chars, length);
    nyx_vm_pop();
    nyx_vm_pop();
    nyx_vm_push(OBJ_VAL(result));
}

// RIP valueToString — OP_ADD does its own thing now with stack-local buffers

static bool call(NyxObjClosure* closure, int argCount) {
    // profiler wants to know who you are
    if (vm.profilingEnabled) {
        const char* name = closure->function->name
            ? closure->function->name->chars : NULL;
        nyx_profiler_enter(name);
    }

    int arity = closure->function->arity;

    if (closure->function->isVariadic) {
        // variadic: shovel the extra args into a list. the last param eats everything
        int requiredArgs = arity - 1;
        if (argCount < requiredArgs) {
            // missing args? nil it is. you get what you get
            while (argCount < requiredArgs) {
                nyx_vm_push(NIL_VAL);
                argCount++;
            }
        }
        // pack the leftovers into a list
        int extraCount = argCount - requiredArgs;
        NyxObjList* varlist = nyx_new_list();
        nyx_vm_push(OBJ_VAL(varlist)); // GC protection. touch this and watch the world burn
        for (int i = extraCount; i > 0; i--) {
            nyx_value_array_write(&varlist->items, vm.stackTop[-1 - i]);
        }
        // yank extras off the stack, put just the varlist back
        vm.stackTop -= extraCount + 1;
        nyx_vm_push(OBJ_VAL(varlist));
        argCount = arity;
    } else {
        if (argCount > arity) {
            runtimeError("Expected %d arguments but got %d.", arity, argCount);
            return false;
        }
        while (argCount < arity) {
            nyx_vm_push(NIL_VAL);
            argCount++;
        }
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    frame->prevFile = vm.currentFile; // breadcrumb for imports — OP_RETURN restores this
    return true;
}

static bool callValue(NyxValue callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE: {
                NyxObjClosure* closure = AS_CLOSURE(callee);
                if (closure->function->isGenerator) {
                    // generator call: don't run it, just set up a coroutine to be resumed later
                    NyxObjCoroutine* coro = nyx_new_coroutine(closure);

                    // copy closure + args into the coro's own stack
                    NyxValue* base = vm.stackTop - argCount - 1;
                    for (int i = 0; i <= argCount; i++) {
                        coro->stack[i] = base[i];
                    }
                    coro->stackTop = coro->stack + argCount + 1;

                    // set up the initial frame — it'll run when someone resumes it
                    CallFrame* coroFrame = &coro->frames[coro->frameCount++];
                    coroFrame->closure = closure;
                    coroFrame->ip = closure->function->chunk.code;
                    coroFrame->slots = coro->stack;

                    coro->state = CORO_SUSPENDED;

                    // swap out the args, give them the coroutine instead
                    vm.stackTop -= argCount + 1;
                    nyx_vm_push(OBJ_VAL(coro));
                    return true;
                }
                return call(closure, argCount);
            }
            case OBJ_NATIVE: {
                NyxNativeFn native = AS_NATIVE(callee);
                extern void nyx_api_gc_clear(void);
                NyxValue result = native(argCount, vm.stackTop - argCount);
                nyx_api_gc_clear(); // done with the native, release GC scratch space
                vm.stackTop -= argCount + 1;
                nyx_vm_push(result);
                return true;
            }
            case OBJ_CLASS: {
                NyxObjClass* klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(nyx_new_instance(klass));
                // call init() if they bothered to write one
                NyxValue initializer;
                if (nyx_table_get(&klass->methods, vm.initString, &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_BOUND_METHOD: {
                NyxObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver;
                if (bound->nativeFn != NULL) {
                    // native method: self gets shoved in as args[0]
                    extern void nyx_api_gc_clear(void);
                    NyxValue result = bound->nativeFn(argCount + 1, vm.stackTop - argCount - 1);
                    nyx_api_gc_clear();
                    vm.stackTop -= argCount + 1;
                    nyx_vm_push(result);
                    return true;
                }
                return call(bound->method, argCount);
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static NyxObjUpvalue* captureUpvalue(NyxValue* local) {
    NyxObjUpvalue* prevUpvalue = NULL;
    NyxObjUpvalue* upvalue = vm.openUpvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    NyxObjUpvalue* createdUpvalue = nyx_new_upvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static bool invokeFromClass(NyxObjClass* klass, NyxObjString* name, int argCount) {
    NyxValue method;
    if (!nyx_table_get(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    if (IS_CLOSURE(method)) {
        return call(AS_CLOSURE(method), argCount);
    }
    if (IS_NATIVE(method)) {
        extern void nyx_api_gc_clear(void);
        NyxNativeFn native = AS_NATIVE(method);
        NyxValue result = native(argCount + 1, vm.stackTop - argCount - 1);
        nyx_api_gc_clear();
        vm.stackTop -= argCount + 1;
        nyx_vm_push(result);
        return true;
    }
    runtimeError("Method '%s' is not callable.", name->chars);
    return false;
}

static bool invoke(NyxObjString* name, int argCount) {
    NyxValue receiver = peek(argCount);

    // list methods — inline dispatch to avoid the overhead of a real method table
    if (IS_LIST(receiver)) {
        NyxObjList* list = AS_LIST(receiver);
        NyxValue* args = vm.stackTop - argCount;
        bool handled = nyx_list_invoke(list, name->chars, name->length, argCount, args);
        if (handled) {
            NyxValue result = nyx_vm_pop();
            vm.stackTop -= argCount + 1;
            nyx_vm_push(result);
            return true;
        }
        runtimeError("Undefined list method '%s'.", name->chars);
        return false;
    }

    // map methods
    if (IS_MAP(receiver)) {
        NyxObjMap* map = AS_MAP(receiver);
        NyxValue* args = vm.stackTop - argCount;
        bool handled = nyx_map_invoke(map, name->chars, name->length, argCount, args);
        if (handled) {
            NyxValue result = nyx_vm_pop();
            vm.stackTop -= argCount + 1;
            nyx_vm_push(result);
            return true;
        }
        runtimeError("Undefined map method '%s'.", name->chars);
        return false;
    }

    // range methods
    if (IS_RANGE(receiver)) {
        NyxObjRange* r = AS_RANGE(receiver);
        if (name->length == 3 && memcmp(name->chars, "len", 3) == 0) {
            vm.stackTop -= argCount + 1;
            nyx_vm_push(INT_VAL(r->end - r->start));
            return true;
        }
        if (name->length == 4 && memcmp(name->chars, "keys", 4) == 0) {
            vm.stackTop -= argCount + 1;
            NyxObjList* list = nyx_new_list();
            nyx_vm_push(OBJ_VAL(list));
            for (int64_t i = r->start; i < r->end; i++)
                nyx_value_array_write(&list->items, INT_VAL(i));
            return true;
        }
        runtimeError("Undefined range method '%s'.", name->chars);
        return false;
    }

    // set methods
    if (IS_SET(receiver)) {
        NyxObjSet* set = AS_SET(receiver);
        if (name->length == 3 && memcmp(name->chars, "len", 3) == 0) {
            vm.stackTop -= argCount + 1;
            nyx_vm_push(INT_VAL(set->items.count));
            return true;
        }
        if (name->length == 3 && memcmp(name->chars, "add", 3) == 0 && argCount >= 1) {
            NyxValue item = vm.stackTop[-argCount];
            vm.stackTop -= argCount + 1;
            // O(n) dedup. yes it's slow. cope
            bool found = false;
            for (int i = 0; i < set->items.count; i++) {
                if (nyx_values_equal(set->items.values[i], item)) { found = true; break; }
            }
            if (!found) {
                nyx_value_array_write(&set->items, item);
                NYX_WRITE_BARRIER_VALUE(&set->obj, item);
            }
            nyx_vm_push(NIL_VAL);
            return true;
        }
        if (name->length == 6 && memcmp(name->chars, "remove", 6) == 0 && argCount >= 1) {
            NyxValue item = vm.stackTop[-argCount];
            vm.stackTop -= argCount + 1;
            bool removed = false;
            for (int i = 0; i < set->items.count; i++) {
                if (nyx_values_equal(set->items.values[i], item)) {
                    // shift the rest down — O(n) again, living our best life
                    for (int j = i; j < set->items.count - 1; j++)
                        set->items.values[j] = set->items.values[j + 1];
                    set->items.count--;
                    removed = true;
                    break;
                }
            }
            nyx_vm_push(BOOL_VAL(removed));
            return true;
        }
        if (name->length == 8 && memcmp(name->chars, "contains", 8) == 0 && argCount >= 1) {
            NyxValue item = vm.stackTop[-argCount];
            vm.stackTop -= argCount + 1;
            bool found = false;
            for (int i = 0; i < set->items.count; i++) {
                if (nyx_values_equal(set->items.values[i], item)) { found = true; break; }
            }
            nyx_vm_push(BOOL_VAL(found));
            return true;
        }
        if (name->length == 5 && memcmp(name->chars, "clear", 5) == 0) {
            vm.stackTop -= argCount + 1;
            set->items.count = 0;
            nyx_vm_push(NIL_VAL);
            return true;
        }
        if (name->length == 8 && memcmp(name->chars, "is_empty", 8) == 0) {
            vm.stackTop -= argCount + 1;
            nyx_vm_push(BOOL_VAL(set->items.count == 0));
            return true;
        }
        runtimeError("Undefined set method '%s'.", name->chars);
        return false;
    }

    // Result methods — Ok/Err unwrapping and friends
    if (IS_RESULT(receiver)) {
        NyxObjResult* result = AS_RESULT(receiver);
        if (name->length == 5 && memcmp(name->chars, "is_ok", 5) == 0) {
            vm.stackTop -= argCount + 1;
            nyx_vm_push(BOOL_VAL(result->isOk));
            return true;
        }
        if (name->length == 6 && memcmp(name->chars, "is_err", 6) == 0) {
            vm.stackTop -= argCount + 1;
            nyx_vm_push(BOOL_VAL(!result->isOk));
            return true;
        }
        if (name->length == 6 && memcmp(name->chars, "unwrap", 6) == 0) {
            if (!result->isOk) {
                runtimeError("panic: unwrap() called on Err value.");
                return false;
            }
            vm.stackTop -= argCount + 1;
            nyx_vm_push(result->value);
            return true;
        }
        if (name->length == 9 && memcmp(name->chars, "unwrap_or", 9) == 0) {
            if (argCount != 1) {
                runtimeError("unwrap_or() takes 1 argument.");
                return false;
            }
            NyxValue def = vm.stackTop[-1];
            vm.stackTop -= argCount + 1;
            nyx_vm_push(result->isOk ? result->value : def);
            return true;
        }
        if (name->length == 10 && memcmp(name->chars, "unwrap_err", 10) == 0) {
            if (result->isOk) {
                runtimeError("panic: unwrap_err() called on Ok value.");
                return false;
            }
            vm.stackTop -= argCount + 1;
            nyx_vm_push(result->value);
            return true;
        }
        runtimeError("Undefined Result method '%s'.", name->chars);
        return false;
    }

    // string methods — a metric ton of them, all inline because method tables are for quitters
    if (IS_STRING(receiver)) {
        NyxObjString* str = AS_STRING(receiver);
        NyxValue* args = vm.stackTop - argCount;
        const char* n = name->chars;
        int nl = name->length;

        // SM = "string method" matcher. don't overthink it
        #define SM(mname) (nl == (int)sizeof(mname)-1 && memcmp(n, mname, sizeof(mname)-1) == 0)

        if (SM("len")) { vm.stackTop -= argCount + 1; nyx_vm_push(INT_VAL(str->length)); return true; }
        if (SM("is_empty")) { vm.stackTop -= argCount + 1; nyx_vm_push(BOOL_VAL(str->length == 0)); return true; }
        if (SM("to_upper")) {
            vm.stackTop -= argCount + 1;
            char* buf = ALLOCATE(char, str->length + 1);
            for (int i = 0; i < str->length; i++) buf[i] = toupper((unsigned char)str->chars[i]);
            buf[str->length] = '\0';
            nyx_vm_push(OBJ_VAL(nyx_take_string(buf, str->length)));
            return true;
        }
        if (SM("to_lower")) {
            vm.stackTop -= argCount + 1;
            char* buf = ALLOCATE(char, str->length + 1);
            for (int i = 0; i < str->length; i++) buf[i] = tolower((unsigned char)str->chars[i]);
            buf[str->length] = '\0';
            nyx_vm_push(OBJ_VAL(nyx_take_string(buf, str->length)));
            return true;
        }
        if (SM("trim")) {
            vm.stackTop -= argCount + 1;
            int s = 0, e = str->length;
            while (s < e && isspace((unsigned char)str->chars[s])) s++;
            while (e > s && isspace((unsigned char)str->chars[e-1])) e--;
            nyx_vm_push(OBJ_VAL(nyx_copy_string(str->chars + s, e - s)));
            return true;
        }
        if (SM("contains") && argCount >= 1) {
            NyxValue a0 = args[0]; vm.stackTop -= argCount + 1;
            nyx_vm_push(BOOL_VAL(IS_STRING(a0) && strstr(str->chars, AS_CSTRING(a0)) != NULL));
            return true;
        }
        if (SM("starts_with") && argCount >= 1) {
            NyxValue a0 = args[0]; vm.stackTop -= argCount + 1;
            if (IS_STRING(a0)) {
                NyxObjString* pfx = AS_STRING(a0);
                nyx_vm_push(BOOL_VAL(pfx->length <= str->length && memcmp(str->chars, pfx->chars, pfx->length) == 0));
            } else nyx_vm_push(BOOL_VAL(false));
            return true;
        }
        if (SM("ends_with") && argCount >= 1) {
            NyxValue a0 = args[0]; vm.stackTop -= argCount + 1;
            if (IS_STRING(a0)) {
                NyxObjString* sfx = AS_STRING(a0);
                nyx_vm_push(BOOL_VAL(sfx->length <= str->length && memcmp(str->chars + str->length - sfx->length, sfx->chars, sfx->length) == 0));
            } else nyx_vm_push(BOOL_VAL(false));
            return true;
        }
        if (SM("index_of") && argCount >= 1) {
            NyxValue a0 = args[0]; vm.stackTop -= argCount + 1;
            if (IS_STRING(a0)) {
                const char* f = strstr(str->chars, AS_CSTRING(a0));
                nyx_vm_push(INT_VAL(f ? (int64_t)(f - str->chars) : -1));
            } else nyx_vm_push(INT_VAL(-1));
            return true;
        }
        if (SM("split") && argCount >= 1) {
            NyxValue a0 = args[0]; vm.stackTop -= argCount + 1;
            NyxObjList* list = nyx_new_list();
            nyx_vm_push(OBJ_VAL(list));
            if (IS_STRING(a0)) {
                NyxObjString* delim = AS_STRING(a0);
                if (delim->length == 0) {
                    for (int i = 0; i < str->length; i++)
                        nyx_value_array_write(&list->items, OBJ_VAL(nyx_copy_string(&str->chars[i], 1)));
                } else {
                    const char* s = str->chars;
                    const char* end = s + str->length;
                    while (s < end) {
                        const char* f = strstr(s, delim->chars);
                        if (!f) { nyx_value_array_write(&list->items, OBJ_VAL(nyx_copy_string(s, (int)(end-s)))); break; }
                        nyx_value_array_write(&list->items, OBJ_VAL(nyx_copy_string(s, (int)(f-s))));
                        s = f + delim->length;
                    }
                }
            }
            return true;
        }
        if (SM("repeat") && argCount >= 1) {
            int rn = IS_INT(args[0]) ? (int)AS_INT(args[0]) : 0;
            vm.stackTop -= argCount + 1;
            if (rn <= 0) { nyx_vm_push(OBJ_VAL(nyx_copy_string("", 0))); }
            else {
                int tl = str->length * rn;
                char* buf = ALLOCATE(char, tl + 1);
                for (int i = 0; i < rn; i++) memcpy(buf + i * str->length, str->chars, str->length);
                buf[tl] = '\0';
                nyx_vm_push(OBJ_VAL(nyx_take_string(buf, tl)));
            }
            return true;
        }
        if (SM("reverse")) {
            vm.stackTop -= argCount + 1;
            char* buf = ALLOCATE(char, str->length + 1);
            for (int i = 0; i < str->length; i++)
                buf[i] = str->chars[str->length - 1 - i];
            buf[str->length] = '\0';
            nyx_vm_push(OBJ_VAL(nyx_take_string(buf, str->length)));
            return true;
        }
        if (SM("char_at") && argCount >= 1) {
            int idx = IS_INT(args[0]) ? (int)AS_INT(args[0]) : -999;
            vm.stackTop -= argCount + 1;
            if (idx < 0) idx = str->length + idx;
            if (idx >= 0 && idx < str->length) nyx_vm_push(OBJ_VAL(nyx_copy_string(&str->chars[idx], 1)));
            else nyx_vm_push(NIL_VAL);
            return true;
        }
        if (SM("replace") && argCount >= 2) {
            NyxValue a0 = args[0], a1 = args[1]; vm.stackTop -= argCount + 1;
            if (IS_STRING(a0) && IS_STRING(a1)) {
                NyxObjString* from = AS_STRING(a0), *to = AS_STRING(a1);
                if (from->length == 0) { nyx_vm_push(OBJ_VAL(str)); }
                else {
                    int cnt = 0; const char* s = str->chars;
                    while ((s = strstr(s, from->chars))) { cnt++; s += from->length; }
                    if (cnt == 0) { nyx_vm_push(OBJ_VAL(str)); }
                    else {
                        int newLen = str->length + cnt * (to->length - from->length);
                        char* buf = ALLOCATE(char, newLen + 1); int pos = 0; s = str->chars;
                        while (*s) {
                            const char* f = strstr(s, from->chars);
                            if (!f) { int rem = (int)(str->chars + str->length - s); memcpy(buf+pos, s, rem); pos += rem; break; }
                            int chunk = (int)(f-s); memcpy(buf+pos, s, chunk); pos += chunk;
                            memcpy(buf+pos, to->chars, to->length); pos += to->length; s = f + from->length;
                        }
                        buf[pos] = '\0'; nyx_vm_push(OBJ_VAL(nyx_take_string(buf, pos)));
                    }
                }
            } else nyx_vm_push(OBJ_VAL(str));
            return true;
        }
        if (SM("substr") && argCount >= 1) {
            int start = IS_INT(args[0]) ? (int)AS_INT(args[0]) : 0;
            int length = (argCount >= 2 && IS_INT(args[1])) ? (int)AS_INT(args[1]) : str->length - start;
            vm.stackTop -= argCount + 1;
            if (start < 0) start = str->length + start;
            if (start < 0) start = 0;
            if (start >= str->length) nyx_vm_push(OBJ_VAL(nyx_copy_string("", 0)));
            else {
                if (start + length > str->length) length = str->length - start;
                nyx_vm_push(OBJ_VAL(nyx_copy_string(str->chars + start, length)));
            }
            return true;
        }

        #undef SM

        runtimeError("Undefined string method '%s'.", name->chars);
        return false;
    }

    // static method call: Class.method() — no `self` for you
    if (IS_CLASS(receiver)) {
        NyxObjClass* klass = AS_CLASS(receiver);
        NyxValue method;
        if (nyx_table_get(&klass->methods, name, &method)) {
            // nil out the receiver slot — statics don't get a `self`
            vm.stackTop[-argCount - 1] = NIL_VAL;
            if (IS_CLOSURE(method)) return call(AS_CLOSURE(method), argCount);
            if (IS_NATIVE(method)) {
                extern void nyx_api_gc_clear(void);
                NyxNativeFn native = AS_NATIVE(method);
                NyxValue result = native(argCount, vm.stackTop - argCount);
                nyx_api_gc_clear();
                vm.stackTop -= argCount + 1;
                nyx_vm_push(result);
                return true;
            }
        }
        runtimeError("Undefined static method '%s'.", name->chars);
        return false;
    }

    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances and classes have methods.");
        return false;
    }

    NyxObjInstance* instance = AS_INSTANCE(receiver);

    NyxValue value;
    if (nyx_table_get(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(NyxObjClass* klass, NyxObjString* name) {
    NyxValue method;
    if (!nyx_table_get(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    if (IS_NATIVE(method)) {
        NyxObjBoundMethod* bound = nyx_new_bound_native_method(peek(0), AS_NATIVE(method));
        nyx_vm_pop();
        nyx_vm_push(OBJ_VAL(bound));
        return true;
    }

    NyxObjBoundMethod* bound = nyx_new_bound_method(peek(0), AS_CLOSURE(method));
    nyx_vm_pop();
    nyx_vm_push(OBJ_VAL(bound));
    return true;
}

static void closeUpvalues(NyxValue* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        NyxObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        NYX_WRITE_BARRIER_VALUE(&upvalue->obj, upvalue->closed);
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// ─── Arithmetic Helpers (macro crimes) ──────────────────────────────────────

#define BINARY_OP(op)                                                       \
    do {                                                                    \
        NyxValue b = peek(0);                                               \
        NyxValue a = peek(1);                                               \
        if (IS_INT(a) && IS_INT(b)) {                                       \
            int64_t bv = AS_INT(nyx_vm_pop());                              \
            int64_t av = AS_INT(nyx_vm_pop());                              \
            nyx_vm_push(INT_VAL(av op bv));                                 \
        } else if ((IS_INT(a) || IS_FLOAT(a)) &&                            \
                   (IS_INT(b) || IS_FLOAT(b))) {                            \
            double bv = IS_INT(b) ? (double)AS_INT(nyx_vm_pop())            \
                                  : AS_FLOAT(nyx_vm_pop());                 \
            double av = IS_INT(a) ? (double)AS_INT(nyx_vm_pop())            \
                                  : AS_FLOAT(nyx_vm_pop());                 \
            nyx_vm_push(FLOAT_VAL(av op bv));                               \
        } else {                                                            \
            runtimeError("Operands must be numbers.");                      \
            return NYX_RUNTIME_ERROR;                                       \
        }                                                                   \
    } while (false)

#define COMPARE_OP(op)                                                      \
    do {                                                                    \
        NyxValue b = peek(0);                                               \
        NyxValue a = peek(1);                                               \
        if (IS_INT(a) && IS_INT(b)) {                                       \
            int64_t bv = AS_INT(nyx_vm_pop());                              \
            int64_t av = AS_INT(nyx_vm_pop());                              \
            nyx_vm_push(BOOL_VAL(av op bv));                                \
        } else if ((IS_INT(a) || IS_FLOAT(a)) &&                            \
                   (IS_INT(b) || IS_FLOAT(b))) {                            \
            double bv = IS_INT(b) ? (double)AS_INT(nyx_vm_pop())            \
                                  : AS_FLOAT(nyx_vm_pop());                 \
            double av = IS_INT(a) ? (double)AS_INT(nyx_vm_pop())            \
                                  : AS_FLOAT(nyx_vm_pop());                 \
            nyx_vm_push(BOOL_VAL(av op bv));                                \
        } else {                                                            \
            runtimeError("Operands must be numbers.");                      \
            return NYX_RUNTIME_ERROR;                                       \
        }                                                                   \
    } while (false)

// ─── Main Dispatch Loop (the beating heart of this whole operation) ─────────

#if defined(__GNUC__) || defined(__clang__)
__attribute__((hot, optimize("O3")))
#endif
static NyxResult run(void) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

// hot path macros — every nanosecond counts in here
#define READ_BYTE()     (*frame->ip++)
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT()    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define PUSH(v)         (*vm.stackTop++ = (v))
#define POP()           (*(--vm.stackTop))
#define PEEK(d)         (vm.stackTop[-1 - (d)])

// ─── Dispatch: computed goto on GCC/Clang (~25% faster), switch on MSVC ─

#if (defined(__GNUC__) || defined(__clang__)) && !defined(NYX_DEBUG_TRACE)
  #define NYX_USE_COMPUTED_GOTO
#endif

#ifdef NYX_USE_COMPUTED_GOTO
    // the unholy dispatch table. don't touch the order unless you enjoy pain
    #define DT(op) [op] = &&L_##op
    static void* dispatchTable[] = {
        DT(OP_CONSTANT), DT(OP_CONSTANT_LONG), DT(OP_NIL), DT(OP_TRUE), DT(OP_FALSE),
        DT(OP_ADD), DT(OP_SUBTRACT), DT(OP_MULTIPLY), DT(OP_DIVIDE),
        DT(OP_MODULO), DT(OP_NEGATE),
        DT(OP_EQUAL), DT(OP_NOT_EQUAL),
        DT(OP_GREATER), DT(OP_GREATER_EQUAL), DT(OP_LESS), DT(OP_LESS_EQUAL),
        DT(OP_NOT),
        DT(OP_DEFINE_GLOBAL), DT(OP_GET_GLOBAL), DT(OP_SET_GLOBAL),
        DT(OP_GET_LOCAL), DT(OP_SET_LOCAL),
        DT(OP_JUMP), DT(OP_JUMP_IF_FALSE), DT(OP_LOOP),
        DT(OP_POP), DT(OP_PRINT),
        DT(OP_CALL), DT(OP_CLOSURE),
        DT(OP_GET_UPVALUE), DT(OP_SET_UPVALUE), DT(OP_CLOSE_UPVALUE),
        DT(OP_RETURN),
        DT(OP_TRY_UNWRAP), DT(OP_IMPORT),
        DT(OP_COROUTINE), DT(OP_YIELD), DT(OP_RESUME),
        DT(OP_LOADI), DT(OP_ADDI), DT(OP_SUBI),
        DT(OP_CONTAINS), DT(OP_BUILD_RANGE), DT(OP_BUILD_SET),
        DT(OP_BUILD_LIST), DT(OP_BUILD_MAP), DT(OP_INDEX_GET), DT(OP_INDEX_SET),
        DT(OP_CLASS), DT(OP_GET_PROPERTY), DT(OP_SET_PROPERTY),
        DT(OP_METHOD), DT(OP_INVOKE),
        DT(OP_INHERIT), DT(OP_GET_SUPER), DT(OP_SUPER_INVOKE),
        DT(OP_INSTANCEOF), DT(OP_DUP), DT(OP_JUMP_IF_NIL),
    };
    #undef DT
  #define DISPATCH()    goto *dispatchTable[*frame->ip++]
  #define TARGET(op)    L_##op
  DISPATCH(); // kick it off
#else
  #define DISPATCH()    break
  #define TARGET(op)    case op
    for (;;) {
#ifdef NYX_DEBUG_TRACE
        printf("          ");
        for (NyxValue* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            nyx_print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        nyx_disassemble_instruction(&frame->closure->function->chunk,
            (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
#endif

            TARGET(OP_CONSTANT): {
                NyxValue constant = READ_CONSTANT();
                PUSH(constant);
                DISPATCH();
            }
            TARGET(OP_CONSTANT_LONG): {
                uint8_t hi = READ_BYTE();
                uint8_t lo = READ_BYTE();
                uint16_t index = (uint16_t)((hi << 8) | lo);
                PUSH(frame->closure->function->chunk.constants.values[index]);
                DISPATCH();
            }
            TARGET(OP_NIL):   PUSH(NIL_VAL); DISPATCH();
            TARGET(OP_TRUE):  PUSH(BOOL_VAL(true)); DISPATCH();
            TARGET(OP_FALSE): PUSH(BOOL_VAL(false)); DISPATCH();

            TARGET(OP_LOADI): {
                int8_t imm = (int8_t)READ_BYTE();
                PUSH(INT_VAL((int64_t)imm));
                DISPATCH();
            }

            TARGET(OP_ADDI): {
                int8_t imm = (int8_t)READ_BYTE();
                NyxValue a = PEEK(0);
                if (IS_INT(a)) {
                    POP();
                    PUSH(INT_VAL(AS_INT(a) + imm));
                } else if (IS_FLOAT(a)) {
                    POP();
                    PUSH(FLOAT_VAL(AS_FLOAT(a) + imm));
                } else {
                    runtimeError("Operand must be a number.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_SUBI): {
                int8_t imm = (int8_t)READ_BYTE();
                NyxValue a = PEEK(0);
                if (IS_INT(a)) {
                    POP();
                    PUSH(INT_VAL(AS_INT(a) - imm));
                } else if (IS_FLOAT(a)) {
                    POP();
                    PUSH(FLOAT_VAL(AS_FLOAT(a) - imm));
                } else {
                    runtimeError("Operand must be a number.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_ADD): {
                NyxValue b = PEEK(0);
                NyxValue a = PEEK(1);

                if (IS_STRING(a) && IS_STRING(b)) {
                    concatenate();
                } else if (IS_STRING(a) || IS_STRING(b)) {
                    // auto-coerce to string for "foo" + 42 style concat.
                    // uses stack buffer to avoid GC headaches during conversion
                    char buf[64];
                    int bufLen = 0;
                    NyxValue nonStr = IS_STRING(b) ? PEEK(1) : PEEK(0);

                    if (IS_INT(nonStr)) {
                        bufLen = snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(nonStr));
                    } else if (IS_FLOAT(nonStr)) {
                        double f = AS_FLOAT(nonStr);
                        if (f == (int64_t)f && f >= -1e15 && f <= 1e15) {
                            bufLen = snprintf(buf, sizeof(buf), "%.1f", f);
                        } else {
                            bufLen = snprintf(buf, sizeof(buf), "%g", f);
                        }
                    } else if (IS_BOOL(nonStr)) {
                        bufLen = snprintf(buf, sizeof(buf), "%s", AS_BOOL(nonStr) ? "true" : "false");
                    } else if (IS_NIL(nonStr)) {
                        bufLen = snprintf(buf, sizeof(buf), "nil");
                    } else {
                        runtimeError("Cannot convert value to string.");
                        return NYX_RUNTIME_ERROR;
                    }

                    // both operands still on stack — GC can't touch 'em
                    NyxObjString* strSide = IS_STRING(b)
                        ? AS_STRING(PEEK(0))
                        : AS_STRING(PEEK(1));

                    int length;
                    char* chars;
                    if (IS_STRING(a)) {
                        length = strSide->length + bufLen;
                        chars = ALLOCATE(char, length + 1);
                        memcpy(chars, strSide->chars, strSide->length);
                        memcpy(chars + strSide->length, buf, bufLen);
                    } else {
                        length = bufLen + strSide->length;
                        chars = ALLOCATE(char, length + 1);
                        memcpy(chars, buf, bufLen);
                        memcpy(chars + bufLen, strSide->chars, strSide->length);
                    }
                    chars[length] = '\0';

                    NyxObjString* result = nyx_take_string(chars, length);
                    POP();
                    POP();
                    PUSH(OBJ_VAL(result));
                } else if (IS_INT(a) && IS_INT(b)) {
                    int64_t bv = AS_INT(POP());
                    int64_t av = AS_INT(POP());
                    PUSH(INT_VAL(av + bv));
                } else if ((IS_INT(a) || IS_FLOAT(a)) &&
                           (IS_INT(b) || IS_FLOAT(b))) {
                    double bv = IS_INT(b) ? (double)AS_INT(POP())
                                          : AS_FLOAT(POP());
                    double av = IS_INT(a) ? (double)AS_INT(POP())
                                          : AS_FLOAT(POP());
                    PUSH(FLOAT_VAL(av + bv));
                } else {
                    runtimeError("Operands must be two numbers or strings.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_SUBTRACT): BINARY_OP(-); DISPATCH();
            TARGET(OP_MULTIPLY): BINARY_OP(*); DISPATCH();
            TARGET(OP_DIVIDE): {
                NyxValue b = PEEK(0);
                if ((IS_INT(b) && AS_INT(b) == 0) ||
                    (IS_FLOAT(b) && AS_FLOAT(b) == 0.0)) {
                    runtimeError("Division by zero.");
                    return NYX_RUNTIME_ERROR;
                }
                BINARY_OP(/);
                DISPATCH();
            }
            TARGET(OP_MODULO): {
                NyxValue b = PEEK(0);
                NyxValue a = PEEK(1);
                if (IS_INT(a) && IS_INT(b)) {
                    if (AS_INT(b) == 0) {
                        runtimeError("Division by zero.");
                        return NYX_RUNTIME_ERROR;
                    }
                    int64_t bv = AS_INT(POP());
                    int64_t av = AS_INT(POP());
                    PUSH(INT_VAL(av % bv));
                } else if ((IS_INT(a) || IS_FLOAT(a)) &&
                           (IS_INT(b) || IS_FLOAT(b))) {
                    double bv = IS_INT(b) ? (double)AS_INT(POP())
                                          : AS_FLOAT(POP());
                    double av = IS_INT(a) ? (double)AS_INT(POP())
                                          : AS_FLOAT(POP());
                    if (bv == 0.0) {
                        runtimeError("Division by zero.");
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(FLOAT_VAL(fmod(av, bv)));
                } else {
                    runtimeError("Operands must be numbers.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_NEGATE): {
                NyxValue val = PEEK(0);
                if (IS_INT(val)) {
                    POP();
                    PUSH(INT_VAL(-AS_INT(val)));
                } else if (IS_FLOAT(val)) {
                    POP();
                    PUSH(FLOAT_VAL(-AS_FLOAT(val)));
                } else {
                    runtimeError("Operand must be a number.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_EQUAL): {
                NyxValue b = POP();
                NyxValue a = POP();
                // `is` operator: walk the class chain to check inheritance
                if (IS_CLASS(b) && IS_INSTANCE(a)) {
                    NyxObjClass* target = AS_CLASS(b);
                    NyxObjClass* klass = AS_INSTANCE(a)->klass;
                    bool found = false;
                    while (klass != NULL) {
                        if (klass == target) { found = true; break; }
                        klass = klass->superclass;
                    }
                    PUSH(BOOL_VAL(found));
                } else {
                    PUSH(BOOL_VAL(nyx_values_equal(a, b)));
                }
                DISPATCH();
            }
            TARGET(OP_NOT_EQUAL): {
                NyxValue b = POP();
                NyxValue a = POP();
                PUSH(BOOL_VAL(!nyx_values_equal(a, b)));
                DISPATCH();
            }

            TARGET(OP_GREATER):       COMPARE_OP(>); DISPATCH();
            TARGET(OP_GREATER_EQUAL): COMPARE_OP(>=); DISPATCH();
            TARGET(OP_LESS):          COMPARE_OP(<); DISPATCH();
            TARGET(OP_LESS_EQUAL):    COMPARE_OP(<=); DISPATCH();

            TARGET(OP_NOT):
                PUSH(BOOL_VAL(isFalsey(POP())));
                DISPATCH();

            TARGET(OP_POP): POP(); DISPATCH();

            TARGET(OP_DEFINE_GLOBAL): {
                NyxObjString* name = READ_STRING();
                nyx_table_set(&vm.globals, name, PEEK(0));
                POP();
                DISPATCH();
            }
            TARGET(OP_GET_GLOBAL): {
                NyxObjString* name = READ_STRING();
                NyxValue value;
                if (!nyx_table_get(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return NYX_RUNTIME_ERROR;
                }
                PUSH(value);
                DISPATCH();
            }
            TARGET(OP_SET_GLOBAL): {
                NyxObjString* name = READ_STRING();
                if (nyx_table_set(&vm.globals, name, PEEK(0))) {
                    nyx_table_delete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            TARGET(OP_GET_LOCAL): {
                uint8_t slot = READ_BYTE();
                PUSH(frame->slots[slot]);
                DISPATCH();
            }
            TARGET(OP_SET_LOCAL): {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = PEEK(0);
                DISPATCH();
            }

            TARGET(OP_GET_UPVALUE): {
                uint8_t slot = READ_BYTE();
                PUSH(*frame->closure->upvalues[slot]->location);
                DISPATCH();
            }
            TARGET(OP_SET_UPVALUE): {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = PEEK(0);
                NYX_WRITE_BARRIER_VALUE(&frame->closure->upvalues[slot]->obj, PEEK(0));
                DISPATCH();
            }

            TARGET(OP_JUMP): {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                DISPATCH();
            }
            TARGET(OP_JUMP_IF_FALSE): {
                uint16_t offset = READ_SHORT();
                if (isFalsey(PEEK(0))) frame->ip += offset;
                DISPATCH();
            }
            TARGET(OP_LOOP): {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                DISPATCH();
            }

            TARGET(OP_CALL): {
                int argCount = READ_BYTE();
                if (!callValue(PEEK(argCount), argCount)) {
                    return NYX_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                DISPATCH();
            }

            TARGET(OP_CLOSURE): {
                NyxObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                NyxObjClosure* closure = nyx_new_closure(function);
                PUSH(OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                DISPATCH();
            }

            TARGET(OP_COROUTINE): {
                NyxValue val = PEEK(0);
                if (!IS_CLOSURE(val)) {
                    runtimeError("Can only create coroutine from a function.");
                    return NYX_RUNTIME_ERROR;
                }
                NyxObjCoroutine* coro = nyx_new_coroutine(AS_CLOSURE(val));
                POP();
                PUSH(OBJ_VAL(coro));
                DISPATCH();
            }

            TARGET(OP_RESUME): {
                NyxValue val = PEEK(0);
                if (!IS_COROUTINE(val)) {
                    runtimeError("Can only resume a coroutine.");
                    return NYX_RUNTIME_ERROR;
                }
                NyxObjCoroutine* coro = AS_COROUTINE(val);
                POP();

                if (coro->state == CORO_DEAD) {
                    PUSH(NIL_VAL);
                    DISPATCH();
                }

                // ── ZERO-COPY CONTEXT SWAP ──
                // just swap some pointers, no memcpy. this is the good shit
                coro->callerStackTop = vm.stackTop;
                coro->callerFrameCount = vm.frameCount;
                coro->callerUpvalues = vm.openUpvalues;
                NyxValue* savedCallerStack = vm.stack;
                CallFrame* savedCallerFrames = vm.frames;
                NyxObjCoroutine* savedCoro = vm.runningCoro;

                // swap the VM's guts to point at the coroutine's memory
                coro->state = CORO_RUNNING;
                vm.runningCoro = coro;
                vm.stack = coro->stack;
                vm.stackTop = coro->stackTop;
                vm.frames = coro->frames;
                vm.frameCount = coro->frameCount;
                vm.openUpvalues = coro->openUpvalues;

                // push nil so OP_POP after yield has something to eat (not on first resume)
                if (vm.frameCount > 0) {
                    CallFrame* cf = &vm.frames[vm.frameCount - 1];
                    if (cf->ip != cf->closure->function->chunk.code)
                        PUSH(NIL_VAL);
                }

                NyxResult coroResult = run();

                NyxValue yieldedValue = NIL_VAL;
                if (coroResult == NYX_YIELDED) {
                    yieldedValue = POP();
                    // bookmark where the coro left off
                    coro->stackTop = vm.stackTop;
                    coro->frameCount = vm.frameCount;
                    coro->openUpvalues = vm.openUpvalues;
                    coro->state = CORO_SUSPENDED;
                } else if (coroResult == NYX_OK) {
                    yieldedValue = POP();
                    coro->state = CORO_DEAD;
                } else {
                    // shit went sideways — restore caller and propagate
                    vm.stack = savedCallerStack;
                    vm.frames = savedCallerFrames;
                    vm.stackTop = coro->callerStackTop;
                    vm.frameCount = coro->callerFrameCount;
                    vm.openUpvalues = coro->callerUpvalues;
                    vm.runningCoro = savedCoro;
                    return coroResult;
                }

                // swap back to the caller's universe
                vm.stack = savedCallerStack;
                vm.frames = savedCallerFrames;
                vm.stackTop = coro->callerStackTop;
                vm.frameCount = coro->callerFrameCount;
                vm.openUpvalues = coro->callerUpvalues;
                vm.runningCoro = savedCoro;

                PUSH(yieldedValue);
                frame = &vm.frames[vm.frameCount - 1];
                DISPATCH();
            }

            TARGET(OP_YIELD): {
                // bail out of run() — the resume handler grabs the yielded value
                return NYX_YIELDED;
            }

            TARGET(OP_IMPORT): {
                NyxObjString* moduleName = READ_STRING();

                // already loaded? don't be greedy
                NyxValue dummy;
                if (nyx_table_get(&vm.modules, moduleName, &dummy)) {
                    // push nil so OP_POP has something to eat
                    PUSH(NIL_VAL);
                    DISPATCH();
                }

                // import resolution: local file > nyx_modules > NYX_HOME/libs
                // first one wins. don't ask about the edge cases

                char path[1024];
                FILE* file = NULL;

                // figure out what directory we're in
                char dirPrefix[1024] = "";
                int dirLen = 0;
                if (vm.currentFile != NULL) {
                    const char* lastSlash = vm.currentFile;
                    for (const char* p = vm.currentFile; *p; p++) {
                        if (*p == '/' || *p == '\\') lastSlash = p;
                    }
                    if (lastSlash != vm.currentFile) {
                        dirLen = (int)(lastSlash - vm.currentFile + 1);
                        snprintf(dirPrefix, sizeof(dirPrefix), "%.*s", dirLen, vm.currentFile);
                    }
                }

                // 1. relative path — try source first, then bytecode
                snprintf(path, sizeof(path), "%s%s.nyx", dirPrefix, moduleName->chars);
                file = fopen(path, "rb");
                if (file == NULL) {
                    snprintf(path, sizeof(path), "%s%s.nyxc", dirPrefix, moduleName->chars);
                    file = fopen(path, "rb");
                }

                // 2. nyx_modules — local packages
                if (file == NULL) {
                    snprintf(path, sizeof(path), "%snyx_modules/%s/lib.nyx", dirPrefix, moduleName->chars);
                    file = fopen(path, "rb");
                    if (file == NULL) {
                        snprintf(path, sizeof(path), "%snyx_modules/%s/lib.nyxc", dirPrefix, moduleName->chars);
                        file = fopen(path, "rb");
                    }
                }

                // 3. global: NYX_HOME/libs — the system-wide stash
                if (file == NULL && vm.nyxHome[0] != '\0') {
                    // pinned version from nyx.toml?
                    const char* pinned = NULL;
                    if (vm.projectManifestLoaded) {
                        pinned = nyx_manifest_dep_version(&vm.projectManifest, moduleName->chars);
                    }

                    if (pinned != NULL) {
                        // pinned — read the lib's manifest to find its entry point
                        char entry[256] = "lib.nyx";
                        char manifestPath[1024];
                        snprintf(manifestPath, sizeof(manifestPath), "%s/libs/%s/%s/nyx.toml",
                                 vm.nyxHome, moduleName->chars, pinned);
                        NyxManifest libManifest;
                        if (nyx_manifest_parse(manifestPath, &libManifest) && libManifest.entry[0]) {
                            snprintf(entry, sizeof(entry), "%s", libManifest.entry);
                        }
                        snprintf(path, sizeof(path), "%s/libs/%s/%s/%s",
                                 vm.nyxHome, moduleName->chars, pinned, entry);
                        file = fopen(path, "rb");
                        // try bytecode version too
                        if (file == NULL) {
                            // .nyx -> .nyxc
                            size_t elen = strlen(entry);
                            if (elen > 4 && strcmp(entry + elen - 4, ".nyx") == 0) {
                                strcat(entry, "c");
                                snprintf(path, sizeof(path), "%s/libs/%s/%s/%s",
                                         vm.nyxHome, moduleName->chars, pinned, entry);
                                file = fopen(path, "rb");
                            }
                        }
                    } else {
                        // no pinned version — YOLO, try lib.nyx directly
                        snprintf(path, sizeof(path), "%s/libs/%s/lib.nyx",
                                 vm.nyxHome, moduleName->chars);
                        file = fopen(path, "rb");
                        if (file == NULL) {
                            snprintf(path, sizeof(path), "%s/libs/%s/lib.nyxc",
                                     vm.nyxHome, moduleName->chars);
                            file = fopen(path, "rb");
                        }

                        if (file == NULL) {
                            // last resort: scan for versioned directories
                            char libDir[1024];
                            snprintf(libDir, sizeof(libDir), "%s/libs/%s", vm.nyxHome, moduleName->chars);
                            char manifestPath[1024];
                            snprintf(manifestPath, sizeof(manifestPath), "%s/nyx.toml", libDir);
                            NyxManifest libManifest;
                            if (nyx_manifest_parse(manifestPath, &libManifest) && libManifest.version[0]) {
                                snprintf(path, sizeof(path), "%s/%s/%s",
                                         libDir, libManifest.version, libManifest.entry);
                                file = fopen(path, "rb");
                                if (file == NULL) {
                                    size_t elen = strlen(libManifest.entry);
                                    if (elen > 4 && strcmp(libManifest.entry + elen - 4, ".nyx") == 0) {
                                        char nyxcEntry[260];
                                        snprintf(nyxcEntry, sizeof(nyxcEntry), "%sc", libManifest.entry);
                                        snprintf(path, sizeof(path), "%s/%s/%s",
                                                 libDir, libManifest.version, nyxcEntry);
                                        file = fopen(path, "rb");
                                    }
                                }
                            }
                        }
                    }
                }

                if (file == NULL) {
                    runtimeError("Could not import module '%s'.", moduleName->chars);
                    return NYX_RUNTIME_ERROR;
                }

                // mark as imported BEFORE running — circular imports are a war crime
                nyx_table_set(&vm.modules, moduleName, BOOL_VAL(true));

                // save our place
                const char* prevFile = vm.currentFile;
                vm.currentFile = path;

                // sniff the magic bytes to see if it's compiled bytecode
                int b0 = fgetc(file);
                int b1 = fgetc(file);
                int b2 = fgetc(file);
                int b3 = fgetc(file);
                bool isBytecode = (b0 == 'N' && b1 == 'Y' && b2 == 'X' && b3 == '\0');

                if (isBytecode) {
                    fgetc(file); fgetc(file); fgetc(file); // skip version bytes

                    // native binary extraction — embedded .dlls/.sos in the bytecode. wild
                    uint8_t ncHi = fgetc(file);
                    uint8_t ncLo = fgetc(file);
                    uint16_t nativeCount = (uint16_t)((ncHi << 8) | ncLo);

                    if (nativeCount > 0) {
                        // figure out what platform we're running on
                        char curPlat[64];
#if defined(_WIN32)
    #if defined(__x86_64__) || defined(_M_X64)
                        snprintf(curPlat, sizeof(curPlat), "windows-x64");
    #elif defined(__aarch64__) || defined(_M_ARM64)
                        snprintf(curPlat, sizeof(curPlat), "windows-arm64");
    #else
                        snprintf(curPlat, sizeof(curPlat), "windows-x86");
    #endif
#elif defined(__APPLE__)
    #if defined(__aarch64__)
                        snprintf(curPlat, sizeof(curPlat), "macos-arm64");
    #else
                        snprintf(curPlat, sizeof(curPlat), "macos-x64");
    #endif
#else
    #if defined(__x86_64__)
                        snprintf(curPlat, sizeof(curPlat), "linux-x64");
    #elif defined(__aarch64__)
                        snprintf(curPlat, sizeof(curPlat), "linux-arm64");
    #else
                        snprintf(curPlat, sizeof(curPlat), "linux-x86");
    #endif
#endif
                        // temp dir to dump native binaries into
                        char tmpDir[1024];
#ifdef _WIN32
                        const char* tmp = getenv("TEMP");
                        if (!tmp) tmp = ".";
                        snprintf(tmpDir, sizeof(tmpDir), "%s\\nyx_native_%lu",
                                 tmp, (unsigned long)GetCurrentProcessId());
                        CreateDirectoryA(tmpDir, NULL);
#else
                        snprintf(tmpDir, sizeof(tmpDir), "/tmp/nyx_native_%d", (int)getpid());
                        mkdir(tmpDir, 0755);
#endif
                        for (int ni = 0; ni < nativeCount; ni++) {
                            uint16_t pLen = (fgetc(file) << 8) | fgetc(file);
                            char pTag[64] = "";
                            if (pLen < sizeof(pTag)) { fread(pTag, 1, pLen, file); pTag[pLen] = '\0'; }
                            else fseek(file, pLen, SEEK_CUR);

                            uint16_t fLen = (fgetc(file) << 8) | fgetc(file);
                            char fName[256] = "";
                            if (fLen < sizeof(fName)) { fread(fName, 1, fLen, file); fName[fLen] = '\0'; }
                            else fseek(file, fLen, SEEK_CUR);

                            uint32_t dLen = ((uint32_t)fgetc(file) << 24) | ((uint32_t)fgetc(file) << 16) |
                                            ((uint32_t)fgetc(file) << 8) | (uint32_t)fgetc(file);

                            if (strcmp(pTag, curPlat) == 0 && dLen > 0) {
                                char ePath[1024];
                                snprintf(ePath, sizeof(ePath), "%s/%s", tmpDir, fName);
                                FILE* ef = fopen(ePath, "wb");
                                if (ef) {
                                    char buf[8192];
                                    uint32_t rem = dLen;
                                    while (rem > 0) {
                                        size_t chunk = rem > sizeof(buf) ? sizeof(buf) : rem;
                                        size_t rd = fread(buf, 1, chunk, file);
                                        fwrite(buf, 1, rd, ef);
                                        rem -= (uint32_t)rd;
                                    }
                                    fclose(ef);
                                    nyx_load_native(ePath);
                                } else {
                                    fseek(file, dLen, SEEK_CUR);
                                }
                            } else {
                                fseek(file, dLen, SEEK_CUR);
                            }
                        }
                    }

                    // read the actual bytecode module
                    uint8_t mcHi = fgetc(file);
                    uint8_t mcLo = fgetc(file);
                    // uint16_t modCount = (mcHi << 8) | mcLo; // we only load the first. multi-module bundles are a future problem
                    (void)mcHi; (void)mcLo;

                    int nameHi = fgetc(file);
                    int nameLo = fgetc(file);
                    fseek(file, (nameHi << 8) | nameLo, SEEK_CUR);
                    NyxObjFunction* moduleFunc = nyx_deserialize_function(file);
                    fclose(file);
                    if (moduleFunc == NULL) {
                        vm.currentFile = prevFile;
                        runtimeError("Error loading module '%s'.", moduleName->chars);
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(OBJ_VAL(moduleFunc));
                    NyxObjClosure* moduleClosure = nyx_new_closure(moduleFunc);
                    POP();
                    PUSH(OBJ_VAL(moduleClosure));
                    call(moduleClosure, 0);
                    frame = &vm.frames[vm.frameCount - 1];
                } else {
                    rewind(file);
                    fseek(file, 0L, SEEK_END);
                    size_t fileSize = ftell(file);
                    rewind(file);
                    char* source = (char*)malloc(fileSize + 1);
                    size_t bytesRead = fread(source, sizeof(char), fileSize, file);
                    source[bytesRead] = '\0';
                    fclose(file);
                    NyxObjFunction* moduleFunc = nyx_compile(source);
                    free(source);
                    if (moduleFunc == NULL) {
                        vm.currentFile = prevFile;
                        runtimeError("Compilation error in module '%s'.", moduleName->chars);
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(OBJ_VAL(moduleFunc));
                    NyxObjClosure* moduleClosure = nyx_new_closure(moduleFunc);
                    POP();
                    PUSH(OBJ_VAL(moduleClosure));
                    call(moduleClosure, 0);
                    frame = &vm.frames[vm.frameCount - 1];
                }

                // module runs as a function call — OP_RETURN + OP_POP cleans up after
                DISPATCH();
            }

            TARGET(OP_TRY_UNWRAP): {
                NyxValue val = PEEK(0);
                if (!IS_RESULT(val)) {
                    runtimeError("'?' operator requires a Result value.");
                    return NYX_RUNTIME_ERROR;
                }
                NyxObjResult* result = AS_RESULT(val);
                if (result->isOk) {
                    // Ok? unwrap it and keep going
                    POP();
                    PUSH(result->value);
                } else {
                    // Err? eject eject eject — propagate it up
                    NyxValue errVal = OBJ_VAL(nyx_new_result(false, result->value));
                    closeUpvalues(frame->slots);
                    vm.frameCount--;
                    if (vm.frameCount == 0) {
                        POP();
                        PUSH(errVal);
                        return NYX_OK;
                    }
                    vm.stackTop = frame->slots;
                    PUSH(errVal);
                    frame = &vm.frames[vm.frameCount - 1];
                }
                DISPATCH();
            }

            TARGET(OP_CONTAINS): {
                NyxValue container = POP();
                NyxValue item = POP();

                if (IS_LIST(container)) {
                    NyxObjList* list = AS_LIST(container);
                    bool found = false;
                    for (int i = 0; i < list->items.count; i++) {
                        if (nyx_values_equal(list->items.values[i], item)) {
                            found = true;
                            break;
                        }
                    }
                    PUSH(BOOL_VAL(found));
                } else if (IS_MAP(container)) {
                    if (!IS_STRING(item)) {
                        runtimeError("Map 'in' operator requires a string key.");
                        return NYX_RUNTIME_ERROR;
                    }
                    NyxValue val;
                    PUSH(BOOL_VAL(nyx_table_get(&AS_MAP(container)->table,
                                                        AS_STRING(item), &val)));
                } else if (IS_STRING(container)) {
                    if (!IS_STRING(item)) {
                        runtimeError("String 'in' operator requires a string.");
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(BOOL_VAL(
                        strstr(AS_CSTRING(container), AS_CSTRING(item)) != NULL));
                } else if (IS_RANGE(container)) {
                    NyxObjRange* r = AS_RANGE(container);
                    if (IS_INT(item)) {
                        int64_t v = AS_INT(item);
                        PUSH(BOOL_VAL(v >= r->start && v < r->end));
                    } else PUSH(BOOL_VAL(false));
                } else if (IS_SET(container)) {
                    NyxObjSet* set = AS_SET(container);
                    bool found = false;
                    for (int i = 0; i < set->items.count; i++) {
                        if (nyx_values_equal(set->items.values[i], item)) { found = true; break; }
                    }
                    PUSH(BOOL_VAL(found));
                } else {
                    runtimeError("'in' requires a list, map, string, range, or set.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_BUILD_RANGE): {
                NyxValue endVal = POP();
                NyxValue startVal = POP();
                int64_t s = IS_INT(startVal) ? AS_INT(startVal) : 0;
                int64_t e = IS_INT(endVal) ? AS_INT(endVal) : 0;
                PUSH(OBJ_VAL(nyx_new_range(s, e)));
                DISPATCH();
            }

            TARGET(OP_BUILD_SET): {
                int count = READ_BYTE();
                NyxObjSet* set = nyx_new_set();
                PUSH(OBJ_VAL(set)); // GC shield
                for (int i = count; i > 0; i--) {
                    NyxValue item = vm.stackTop[-1 - i];
                    // dedup
                    bool found = false;
                    for (int j = 0; j < set->items.count; j++) {
                        if (nyx_values_equal(set->items.values[j], item)) { found = true; break; }
                    }
                    if (!found) nyx_value_array_write(&set->items, item);
                }
                vm.stackTop[-1 - count] = OBJ_VAL(set);
                vm.stackTop -= count;
                DISPATCH();
            }

            TARGET(OP_BUILD_LIST): {
                int count = READ_BYTE();
                NyxObjList* list = nyx_new_list();
                PUSH(OBJ_VAL(list)); // GC shield
                for (int i = count; i > 0; i--) {
                    nyx_value_array_write(&list->items, vm.stackTop[-1 - i]);
                }
                // collapse the stack: yank items out from under the list
                vm.stackTop[-1 - count] = OBJ_VAL(list);
                vm.stackTop -= count;
                DISPATCH();
            }

            TARGET(OP_BUILD_MAP): {
                int count = READ_BYTE();
                NyxObjMap* map = nyx_new_map();
                PUSH(OBJ_VAL(map)); // GC shield
                for (int i = count; i > 0; i--) {
                    NyxValue val = vm.stackTop[-1 - (2 * i - 1)];
                    NyxValue key = vm.stackTop[-1 - (2 * i)];
                    if (!IS_STRING(key)) {
                        runtimeError("Map keys must be strings.");
                        return NYX_RUNTIME_ERROR;
                    }
                    nyx_map_set(map, AS_STRING(key), val);
                }
                vm.stackTop[-1 - count * 2] = OBJ_VAL(map);
                vm.stackTop -= count * 2;
                DISPATCH();
            }

            TARGET(OP_INDEX_GET): {
                NyxValue index = POP();
                NyxValue target = POP();

                if (IS_LIST(target)) {
                    if (!IS_INT(index)) {
                        runtimeError("List index must be an integer.");
                        return NYX_RUNTIME_ERROR;
                    }
                    NyxObjList* list = AS_LIST(target);
                    int idx = (int)AS_INT(index);
                    if (idx < 0) idx = list->items.count + idx;
                    if (idx < 0 || idx >= list->items.count) {
                        runtimeError("List index out of bounds.");
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(list->items.values[idx]);
                } else if (IS_MAP(target)) {
                    NyxObjMap* map = AS_MAP(target);
                    if (IS_STRING(index)) {
                        NyxValue val;
                        if (nyx_map_get(map, AS_STRING(index), &val)) {
                            PUSH(val);
                        } else {
                            PUSH(NIL_VAL);
                        }
                    } else if (IS_INT(index)) {
                        // integer index on a map — access by insertion order. yes, maps are ordered
                        int idx = (int)AS_INT(index);
                        if (idx >= 0 && idx < map->keys.count && IS_STRING(map->keys.values[idx])) {
                            NyxValue val;
                            if (nyx_table_get(&map->table, AS_STRING(map->keys.values[idx]), &val))
                                PUSH(val);
                            else PUSH(NIL_VAL);
                        } else {
                            PUSH(NIL_VAL);
                        }
                    } else {
                        runtimeError("Map key must be a string or integer.");
                        return NYX_RUNTIME_ERROR;
                    }
                } else if (IS_RANGE(target)) {
                    if (!IS_INT(index)) {
                        runtimeError("Range index must be an integer.");
                        return NYX_RUNTIME_ERROR;
                    }
                    NyxObjRange* r = AS_RANGE(target);
                    int64_t idx = AS_INT(index);
                    int64_t len = r->end - r->start;
                    if (idx < 0) idx = len + idx;
                    if (idx < 0 || idx >= len) {
                        runtimeError("Range index out of bounds.");
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(INT_VAL(r->start + idx));
                } else if (IS_STRING(target)) {
                    if (!IS_INT(index)) {
                        runtimeError("String index must be an integer.");
                        return NYX_RUNTIME_ERROR;
                    }
                    NyxObjString* str = AS_STRING(target);
                    int idx = (int)AS_INT(index);
                    if (idx < 0) idx = str->length + idx;
                    if (idx < 0 || idx >= str->length) {
                        runtimeError("String index out of bounds.");
                        return NYX_RUNTIME_ERROR;
                    }
                    PUSH(OBJ_VAL(nyx_copy_string(&str->chars[idx], 1)));
                } else {
                    runtimeError("Only lists, maps, and strings can be indexed.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_INDEX_SET): {
                NyxValue value = POP();
                NyxValue index = POP();
                NyxValue target = POP();

                if (IS_LIST(target)) {
                    if (!IS_INT(index)) {
                        runtimeError("List index must be an integer.");
                        return NYX_RUNTIME_ERROR;
                    }
                    NyxObjList* list = AS_LIST(target);
                    int idx = (int)AS_INT(index);
                    if (idx < 0) idx = list->items.count + idx;
                    if (idx < 0 || idx >= list->items.count) {
                        runtimeError("List index out of bounds.");
                        return NYX_RUNTIME_ERROR;
                    }
                    list->items.values[idx] = value;
                    NYX_WRITE_BARRIER_VALUE(&list->obj, value);
                    PUSH(value);
                } else if (IS_MAP(target)) {
                    if (!IS_STRING(index)) {
                        runtimeError("Map key must be a string.");
                        return NYX_RUNTIME_ERROR;
                    }
                    nyx_map_set(AS_MAP(target), AS_STRING(index), value);
                    NYX_WRITE_BARRIER_VALUE(&AS_MAP(target)->obj, value);
                    PUSH(value);
                } else {
                    runtimeError("Only lists and maps support index assignment.");
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_CLOSE_UPVALUE):
                closeUpvalues(vm.stackTop - 1);
                POP();
                DISPATCH();

            TARGET(OP_PRINT): {
                nyx_print_value(POP());
                printf("\n");
                DISPATCH();
            }

            TARGET(OP_CLASS):
                PUSH(OBJ_VAL(nyx_new_class(READ_STRING())));
                DISPATCH();

            TARGET(OP_GET_PROPERTY): {
                // class property access — static methods
                if (IS_CLASS(PEEK(0))) {
                    NyxObjClass* klass = AS_CLASS(PEEK(0));
                    NyxObjString* name = READ_STRING();
                    NyxValue method;
                    if (nyx_table_get(&klass->methods, name, &method)) {
                        POP();
                        PUSH(method);
                        DISPATCH();
                    }
                    runtimeError("Undefined static property '%s'.", name->chars);
                    return NYX_RUNTIME_ERROR;
                }

                if (!IS_INSTANCE(PEEK(0))) {
                    runtimeError("Only instances and classes have properties.");
                    return NYX_RUNTIME_ERROR;
                }

                NyxObjInstance* instance = AS_INSTANCE(PEEK(0));
                NyxObjString* name = READ_STRING();

                NyxValue value;
                if (nyx_table_get(&instance->fields, name, &value)) {
                    POP();
                    PUSH(value);
                    DISPATCH();
                }

                if (!bindMethod(instance->klass, name)) {
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_SET_PROPERTY): {
                if (!IS_INSTANCE(PEEK(1))) {
                    runtimeError("Only instances have fields.");
                    return NYX_RUNTIME_ERROR;
                }

                NyxObjInstance* instance = AS_INSTANCE(PEEK(1));
                nyx_table_set(&instance->fields, READ_STRING(), PEEK(0));
                NYX_WRITE_BARRIER_VALUE(&instance->obj, PEEK(0));
                NyxValue value = POP();
                POP();
                PUSH(value);
                DISPATCH();
            }

            TARGET(OP_METHOD):
                nyx_table_set(&AS_CLASS(PEEK(1))->methods, READ_STRING(), PEEK(0));
                NYX_WRITE_BARRIER_VALUE(&AS_CLASS(PEEK(1))->obj, PEEK(0));
                POP();
                DISPATCH();

            TARGET(OP_INVOKE): {
                // save callsite address for the inline cache
                uint8_t* callSite = frame->ip - 1;
                NyxObjString* method = READ_STRING();
                int argCount = READ_BYTE();

                NyxValue receiver = PEEK(argCount);

                // inline cache: if we've seen this class at this callsite before, skip the lookup.
                // GC nukes the cache every collection. it's primitive but it works
                if (IS_INSTANCE(receiver)) {
                    NyxObjInstance* inst = AS_INSTANCE(receiver);
                    int slot = ((uintptr_t)callSite >> 2) & 15;
                    if (vm.icache[slot].ip == callSite &&
                        vm.icache[slot].klass == inst->klass &&
                        vm.icache[slot].method != NULL) {
                        vm.stackTop[-argCount - 1] = receiver;
                        if (!call(vm.icache[slot].method, argCount)) {
                            return NYX_RUNTIME_ERROR;
                        }
                        frame = &vm.frames[vm.frameCount - 1];
                        DISPATCH();
                    }

                    NyxValue methodVal;
                    if (!nyx_table_get(&inst->fields, method, &methodVal) &&
                        nyx_table_get(&inst->klass->methods, method, &methodVal) &&
                        IS_CLOSURE(methodVal)) {
                        vm.icache[slot].ip = callSite;
                        vm.icache[slot].klass = inst->klass;
                        vm.icache[slot].method = AS_CLOSURE(methodVal);
                    }
                }

                if (!invoke(method, argCount)) {
                    return NYX_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                DISPATCH();
            }

            TARGET(OP_INHERIT): {
                NyxValue superclass = PEEK(1);
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return NYX_RUNTIME_ERROR;
                }

                NyxObjClass* subclass = AS_CLASS(PEEK(0));
                subclass->superclass = AS_CLASS(superclass);
                NYX_WRITE_BARRIER(&subclass->obj, &AS_CLASS(superclass)->obj);
                nyx_table_add_all(&AS_CLASS(superclass)->methods, &subclass->methods);
                // write barrier for each copied method — the GC needs to know about these
                for (int i = 0; i < subclass->methods.capacity; i++) {
                    NyxEntry* entry = &subclass->methods.entries[i];
                    if (entry->key != NULL) {
                        NYX_WRITE_BARRIER(&subclass->obj, &entry->key->obj);
                        NYX_WRITE_BARRIER_VALUE(&subclass->obj, entry->value);
                    }
                }
                POP();
                DISPATCH();
            }

            TARGET(OP_GET_SUPER): {
                NyxObjString* name = READ_STRING();
                NyxObjClass* superclass = AS_CLASS(POP());

                if (!bindMethod(superclass, name)) {
                    return NYX_RUNTIME_ERROR;
                }
                DISPATCH();
            }

            TARGET(OP_SUPER_INVOKE): {
                NyxObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                NyxObjClass* superclass = AS_CLASS(POP());
                if (!invokeFromClass(superclass, method, argCount)) {
                    return NYX_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                DISPATCH();
            }

            TARGET(OP_DUP): {
                PUSH(PEEK(0));
                DISPATCH();
            }

            TARGET(OP_JUMP_IF_NIL): {
                uint16_t offset = READ_SHORT();
                if (IS_NIL(PEEK(0))) {
                    frame->ip += offset;
                }
                DISPATCH();
            }

            TARGET(OP_INSTANCEOF): {
                NyxValue klassVal = POP();
                NyxValue instVal = POP();
                if (!IS_CLASS(klassVal)) {
                    PUSH(BOOL_VAL(false));
                    DISPATCH();
                }
                if (!IS_INSTANCE(instVal)) {
                    PUSH(BOOL_VAL(false));
                    DISPATCH();
                }
                NyxObjClass* target = AS_CLASS(klassVal);
                NyxObjClass* klass = AS_INSTANCE(instVal)->klass;
                bool found = false;
                while (klass != NULL) {
                    if (klass == target) { found = true; break; }
                    // TODO: walk the full inheritance chain. right now it's exact match only.
                    // this is technically broken for deep hierarchies. don't tell anyone
                    break;
                }
                PUSH(BOOL_VAL(found));
                DISPATCH();
            }

            TARGET(OP_RETURN): {
                NyxValue result = POP();
                closeUpvalues(frame->slots);
                nyx_profiler_exit();
                // restore currentFile — imports change it, returns undo it
                vm.currentFile = frame->prevFile;
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    POP();
                    PUSH(result);
                    return NYX_OK;
                }

                vm.stackTop = frame->slots;
                PUSH(result);
                frame = &vm.frames[vm.frameCount - 1];
                DISPATCH();
            }
#ifndef NYX_USE_COMPUTED_GOTO
        }
    }
#endif

#undef PUSH
#undef POP
#undef PEEK
#undef DISPATCH
#undef TARGET
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
}

void nyx_vm_set_home(const char* path) {
    if (path != NULL && path[0] != '\0') {
        snprintf(vm.nyxHome, sizeof(vm.nyxHome), "%s", path);
    }
}

void nyx_vm_set_file(const char* path) {
    vm.currentFile = path;

    // sniff for a project nyx.toml next to the script
    if (!vm.projectManifestLoaded && path != NULL) {
        char tomlPath[1024];
        const char* lastSlash = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/' || *p == '\\') lastSlash = p;
        }
        if (lastSlash != path) {
            int dirLen = (int)(lastSlash - path + 1);
            snprintf(tomlPath, sizeof(tomlPath), "%.*snyx.toml", dirLen, path);
        } else {
            snprintf(tomlPath, sizeof(tomlPath), "nyx.toml");
        }
        if (nyx_manifest_parse(tomlPath, &vm.projectManifest)) {
            vm.projectManifestLoaded = true;
        }
    }
}

NyxResult nyx_vm_interpret(const char* source) {
    NyxObjFunction* function = nyx_compile(source);
    if (function == NULL) return NYX_COMPILE_ERROR;

    nyx_vm_push(OBJ_VAL(function));
    NyxObjClosure* closure = nyx_new_closure(function);
    nyx_vm_pop();
    nyx_vm_push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}

bool nyx_vm_call(NyxObjClosure* closure, int argCount) {
    return call(closure, argCount);
}

NyxResult nyx_vm_run(void) {
    return run();
}
