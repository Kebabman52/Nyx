#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#ifdef NYX_DEBUG_TRACE
#include "debug.h"
#endif

static void freeObject(NyxObj* object);

// don't let the GC eat things before the table is even set
static bool gcReady = false;

void nyx_gc_set_ready(bool ready) {
    gcReady = ready;
}

static bool minorGCActive = false; // are we in "only murder the young" mode?

void nyx_gc_remember(NyxObj* obj) {
    if (OBJ_IS_REMEMBERED(obj)) return;
    OBJ_SET_REMEMBERED(obj);

    if (vm.rememberedCapacity < vm.rememberedCount + 1) {
        vm.rememberedCapacity = vm.rememberedCapacity < 8 ? 8 : vm.rememberedCapacity * 2;
        vm.rememberedSet = (NyxObj**)realloc(vm.rememberedSet,
            sizeof(NyxObj*) * vm.rememberedCapacity);
    }
    vm.rememberedSet[vm.rememberedCount++] = obj;
}

void* nyx_realloc(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) vm.youngBytesAllocated += newSize - oldSize;

    if (newSize > oldSize && gcReady) {
#ifdef NYX_GC_STRESS
        nyx_gc_collect();
#endif
        if (vm.youngBytesAllocated > NYX_MINOR_GC_THRESHOLD) {
            nyx_gc_collect();
        } else if (vm.bytesAllocated > vm.nextGC) {
            vm.minorCollections = NYX_MINOR_BEFORE_MAJOR; // force major
            nyx_gc_collect();
        }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "nyx: out of memory\n");
        exit(1);
    }
    return result;
}

// Mark Phase (figure out who gets to live)

void nyx_mark_object(NyxObj* object) {
    if (object == NULL) return;
    if (OBJ_IS_MARKED(object)) return;
    // minor GC skips the elders. respect your seniors or whatever
    if (minorGCActive && OBJ_IS_OLD(object) && !OBJ_IS_REMEMBERED(object)) return;

#ifdef NYX_DEBUG_TRACE
    printf("%p mark ", (void*)object);
    nyx_print_value(OBJ_VAL(object));
    printf("\n");
#endif

    OBJ_MARK(object);

    // gray stack = the "maybe alive, haven't checked yet" pile
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // raw realloc because nyx_realloc would trigger GC. yes, GC inside GC. no.
        vm.grayStack = (NyxObj**)realloc(vm.grayStack, sizeof(NyxObj*) * vm.grayCapacity);
        if (vm.grayStack == NULL) {
            fprintf(stderr, "nyx: out of memory (GC gray stack)\n");
            exit(1);
        }
    }

    vm.grayStack[vm.grayCount++] = object;
}

void nyx_mark_value(NyxValue value) {
    if (IS_OBJ(value)) nyx_mark_object(AS_OBJ(value));
}

static void markArray(NyxValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        nyx_mark_value(array->values[i]);
    }
}

static void blackenObject(NyxObj* object) {
#ifdef NYX_DEBUG_TRACE
    printf("%p blacken ", (void*)object);
    nyx_print_value(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_CLOSURE: {
            NyxObjClosure* closure = (NyxObjClosure*)object;
            nyx_mark_object((NyxObj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                nyx_mark_object((NyxObj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            NyxObjFunction* function = (NyxObjFunction*)object;
            nyx_mark_object((NyxObj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_UPVALUE:
            nyx_mark_value(((NyxObjUpvalue*)object)->closed);
            break;
        case OBJ_CLASS: {
            NyxObjClass* klass = (NyxObjClass*)object;
            nyx_mark_object((NyxObj*)klass->name);
            nyx_mark_object((NyxObj*)klass->superclass);
            nyx_mark_table(&klass->methods);
            break;
        }
        case OBJ_INSTANCE: {
            NyxObjInstance* instance = (NyxObjInstance*)object;
            nyx_mark_object((NyxObj*)instance->klass);
            nyx_mark_table(&instance->fields);
            break;
        }
        case OBJ_BOUND_METHOD: {
            NyxObjBoundMethod* bound = (NyxObjBoundMethod*)object;
            nyx_mark_value(bound->receiver);
            nyx_mark_object((NyxObj*)bound->method);
            break;
        }
        case OBJ_RESULT:
            nyx_mark_value(((NyxObjResult*)object)->value);
            break;
        case OBJ_RANGE:
            break; // ranges are loners. no friends to mark
        case OBJ_SET:
            markArray(&((NyxObjSet*)object)->items);
            break;
        case OBJ_COROUTINE: {
            NyxObjCoroutine* coro = (NyxObjCoroutine*)object;
            nyx_mark_object((NyxObj*)coro->closure);
            for (NyxValue* slot = coro->stack; slot < coro->stackTop; slot++) {
                nyx_mark_value(*slot);
            }
            for (int i = 0; i < coro->frameCount; i++) {
                nyx_mark_object((NyxObj*)coro->frames[i].closure);
            }
            for (NyxObjUpvalue* uv = coro->openUpvalues; uv != NULL; uv = uv->next) {
                nyx_mark_object((NyxObj*)uv);
            }
            break;
        }
        case OBJ_LIST: {
            NyxObjList* list = (NyxObjList*)object;
            for (int i = 0; i < list->items.count; i++) {
                nyx_mark_value(list->items.values[i]);
            }
            break;
        }
        case OBJ_MAP: {
            NyxObjMap* map = (NyxObjMap*)object;
            nyx_mark_table(&map->table);
            for (int i = 0; i < map->keys.count; i++) {
                nyx_mark_value(map->keys.values[i]);
            }
            break;
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
        case OBJ_INT64:
            break; // leaf nodes. nothing to see here
    }
}

static void markRoots(void) {
    // mark everything on the active stack — these are definitely alive
    for (NyxValue* slot = vm.stack; slot < vm.stackTop; slot++) {
        nyx_mark_value(*slot);
    }

    // coroutine shenanigans: the main stack is still valid, just sleeping
    if (vm.runningCoro != NULL && vm.stack != vm.mainStack) {
        NyxValue* mainTop = vm.runningCoro->callerStackTop;
        for (NyxValue* slot = vm.mainStack; slot < mainTop; slot++) {
            nyx_mark_value(*slot);
        }
        for (int i = 0; i < vm.runningCoro->callerFrameCount; i++) {
            nyx_mark_object((NyxObj*)vm.mainFrames[i].closure);
        }
    }

    // closures in active call frames — don't kill someone mid-conversation
    for (int i = 0; i < vm.frameCount; i++) {
        nyx_mark_object((NyxObj*)vm.frames[i].closure);
    }

    // open upvalues are sneaky bastards — mark 'em
    for (NyxObjUpvalue* upvalue = vm.openUpvalues;
         upvalue != NULL;
         upvalue = upvalue->next) {
        nyx_mark_object((NyxObj*)upvalue);
    }

    // globals, modules — the untouchables
    nyx_mark_table(&vm.globals);
    nyx_mark_table(&vm.modules);

    // "init" string is cached because we look it up approximately ten billion times
    nyx_mark_object((NyxObj*)vm.initString);

    // compiler might be building a function right now. don't shoot it
    nyx_mark_compiler_roots();

    // native API scratch space — GC protection for the C-side hacks
    extern void nyx_mark_api_roots(void);
    nyx_mark_api_roots();
}

static void traceReferences(void) {
    while (vm.grayCount > 0) {
        NyxObj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

// Sweep Phase (take out the trash)

static void sweep(void) {
    NyxObj* previous = NULL;
    NyxObj* object = vm.objects;

    while (object != NULL) {
        if (OBJ_IS_MARKED(object)) {
            OBJ_UNMARK(object);
            previous = object;
            object = object->next;
        } else {
            NyxObj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

static void freeObject(NyxObj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            NyxObjString* string = (NyxObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(NyxObjString, object);
            break;
        }
        case OBJ_INT64:
            FREE(NyxObjInt64, object);
            break;
        case OBJ_FUNCTION: {
            NyxObjFunction* function = (NyxObjFunction*)object;
            nyx_chunk_free(&function->chunk);
            FREE(NyxObjFunction, object);
            break;
        }
        case OBJ_CLOSURE: {
            NyxObjClosure* closure = (NyxObjClosure*)object;
            FREE_ARRAY(NyxObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(NyxObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(NyxObjUpvalue, object);
            break;
        case OBJ_NATIVE:
            FREE(NyxObjNative, object);
            break;
        case OBJ_CLASS: {
            NyxObjClass* klass = (NyxObjClass*)object;
            nyx_table_free(&klass->methods);
            FREE(NyxObjClass, object);
            break;
        }
        case OBJ_INSTANCE: {
            NyxObjInstance* instance = (NyxObjInstance*)object;
            nyx_table_free(&instance->fields);
            FREE(NyxObjInstance, object);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(NyxObjBoundMethod, object);
            break;
        case OBJ_RESULT:
            FREE(NyxObjResult, object);
            break;
        case OBJ_RANGE:
            FREE(NyxObjRange, object);
            break;
        case OBJ_SET: {
            NyxObjSet* set = (NyxObjSet*)object;
            nyx_value_array_free(&set->items);
            FREE(NyxObjSet, object);
            break;
        }
        case OBJ_COROUTINE:
            // stack & frames are inline — one free to rule them all
            FREE(NyxObjCoroutine, object);
            break;
        case OBJ_LIST: {
            NyxObjList* list = (NyxObjList*)object;
            nyx_value_array_free(&list->items);
            FREE(NyxObjList, object);
            break;
        }
        case OBJ_MAP: {
            NyxObjMap* map = (NyxObjMap*)object;
            nyx_table_free(&map->table);
            nyx_value_array_free(&map->keys);
            FREE(NyxObjMap, object);
            break;
        }
    }
}

// Minor GC (cull the youth)

static void minorSweep(void) {
    NyxObj* previous = NULL;
    NyxObj* object = vm.objects;

    while (object != NULL) {
        if (OBJ_IS_OLD(object) && !OBJ_IS_REMEMBERED(object)) {
            // old and unbothered — skip, they've earned it
            previous = object;
            object = object->next;
        } else if (OBJ_IS_MARKED(object)) {
            // survived the purge. congratulations, you age up
            OBJ_UNMARK(object);
            OBJ_CLR_REMEMBERED(object);
            int age = OBJ_AGE(object);
            if (age == NYX_GEN_YOUNG) {
                OBJ_SET_AGE(object, NYX_GEN_SURVIVED);
            } else if (age == NYX_GEN_SURVIVED || age == NYX_GEN_OLD) {
                OBJ_SET_AGE(object, NYX_GEN_OLD);
            }
            previous = object;
            object = object->next;
        } else if (OBJ_IS_YOUNG(object)) {
            // young and unloved. rest in peace
            NyxObj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        } else {
            // old but was on the suspect list — cleared of all charges
            OBJ_CLR_REMEMBERED(object);
            previous = object;
            object = object->next;
        }
    }
}

void nyx_gc_minor(void) {
#ifdef NYX_DEBUG_TRACE
    printf("-- gc minor begin\n");
    size_t before = vm.bytesAllocated;
#endif

    minorGCActive = true;

    markRoots();

    // remembered set: old objects that got caught fraternizing with the youth
    for (int i = 0; i < vm.rememberedCount; i++) {
        NyxObj* obj = vm.rememberedSet[i];
        if (!OBJ_IS_MARKED(obj)) {
            OBJ_MARK(obj);
            // shove onto gray stack so we trace their kids too
            if (vm.grayCapacity < vm.grayCount + 1) {
                vm.grayCapacity = vm.grayCapacity < 8 ? 8 : vm.grayCapacity * 2;
                vm.grayStack = (NyxObj**)realloc(vm.grayStack,
                    sizeof(NyxObj*) * vm.grayCapacity);
            }
            vm.grayStack[vm.grayCount++] = obj;
        }
    }

    traceReferences();

    nyx_table_remove_white(&vm.strings, true /*minorGC*/);

    minorSweep();

    // wipe the suspect list clean
    vm.rememberedCount = 0;

    minorGCActive = false;
    vm.youngBytesAllocated = 0;
    vm.minorCollections++;

#ifdef NYX_DEBUG_TRACE
    printf("-- gc minor end (collected %zu bytes)\n",
           before - vm.bytesAllocated);
#endif
}

// Major GC (nobody is safe)

void nyx_gc_major(void) {
#ifdef NYX_DEBUG_TRACE
    printf("-- gc major begin\n");
    size_t before = vm.bytesAllocated;
#endif

    markRoots();
    traceReferences();
    nyx_table_remove_white(&vm.strings, false /*majorGC*/);
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    vm.youngBytesAllocated = 0;
    vm.minorCollections = 0;
    vm.rememberedCount = 0;

    // everyone who made it through the apocalypse is an elder now
    NyxObj* obj = vm.objects;
    while (obj != NULL) {
        OBJ_SET_AGE(obj, NYX_GEN_OLD);
        OBJ_CLR_REMEMBERED(obj);
        obj = obj->next;
    }

#ifdef NYX_DEBUG_TRACE
    printf("-- gc major end (collected %zu bytes, next at %zu)\n",
           before - vm.bytesAllocated, vm.nextGC);
#endif
}

// Dispatcher (who dies today?)

void nyx_gc_collect(void) {
#ifdef NYX_GC_STRESS
    // stress mode: skip the foreplay, go straight to full collection
    nyx_gc_major();
#else
    // no frames = we're in init/compile, so go nuclear.
    // also go nuclear if we've been too nice for too long
    if (vm.frameCount == 0 || vm.minorCollections >= NYX_MINOR_BEFORE_MAJOR) {
        nyx_gc_major();
    } else {
        nyx_gc_minor();
    }
#endif

    // nuke the inline cache — GC may have freed the closures we were caching. oops
    memset(vm.icache, 0, sizeof(vm.icache));
}

void nyx_free_objects(void) {
    NyxObj* object = vm.objects;
    while (object != NULL) {
        NyxObj* next = object->next;
        freeObject(object);
        object = next;
    }
}
