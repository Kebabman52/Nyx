#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static NyxObj* allocateObject(size_t size, NyxObjType type) {
    NyxObj* object = (NyxObj*)nyx_realloc(NULL, 0, size);
    object->type = type;
    object->gcflags = NYX_GEN_YOUNG; // fresh meat for the GC
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static NyxObjString* allocateString(char* chars, int length, uint32_t hash) {
    NyxObjString* string = ALLOCATE_OBJ(NyxObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    // GC protection dance: push it so the reaper can't touch it during interning
    nyx_vm_push(OBJ_VAL(string));
    nyx_table_set(&vm.strings, string, NIL_VAL);
    nyx_vm_pop();
    return string;
}

NyxObjInt64* nyx_new_int64(int64_t value) {
    NyxObjInt64* obj = ALLOCATE_OBJ(NyxObjInt64, OBJ_INT64);
    obj->value = value;
    return obj;
}

NyxObjFunction* nyx_new_function(void) {
    NyxObjFunction* function = ALLOCATE_OBJ(NyxObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->isGenerator = false;
    function->isVariadic = false;
    function->name = NULL;
    nyx_chunk_init(&function->chunk);
    return function;
}

NyxObjClosure* nyx_new_closure(NyxObjFunction* function) {
    // allocate upvalues first — if GC fires mid-allocation, function is still
    // reachable from the compiler. we planned this. probably.
    NyxObjUpvalue** upvalues = ALLOCATE(NyxObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    NyxObjClosure* closure = ALLOCATE_OBJ(NyxObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

NyxObjUpvalue* nyx_new_upvalue(NyxValue* slot) {
    NyxObjUpvalue* upvalue = ALLOCATE_OBJ(NyxObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

NyxObjNative* nyx_new_native(NyxNativeFn function, const char* name) {
    NyxObjNative* native = ALLOCATE_OBJ(NyxObjNative, OBJ_NATIVE);
    native->function = function;
    native->name = name;
    return native;
}

NyxObjClass* nyx_new_class(NyxObjString* name) {
    NyxObjClass* klass = ALLOCATE_OBJ(NyxObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = NULL;
    nyx_table_init(&klass->methods);
    return klass;
}

NyxObjInstance* nyx_new_instance(NyxObjClass* klass) {
    NyxObjInstance* instance = ALLOCATE_OBJ(NyxObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    nyx_table_init(&instance->fields);
    return instance;
}

NyxObjBoundMethod* nyx_new_bound_method(NyxValue receiver, NyxObjClosure* method) {
    NyxObjBoundMethod* bound = ALLOCATE_OBJ(NyxObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    bound->nativeFn = NULL;
    return bound;
}

NyxObjBoundMethod* nyx_new_bound_native_method(NyxValue receiver, NyxNativeFn fn) {
    NyxObjBoundMethod* bound = ALLOCATE_OBJ(NyxObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = NULL;
    bound->nativeFn = fn;
    return bound;
}

NyxObjList* nyx_new_list(void) {
    NyxObjList* list = ALLOCATE_OBJ(NyxObjList, OBJ_LIST);
    nyx_value_array_init(&list->items);
    list->maxSize = -1;
    return list;
}

NyxObjMap* nyx_new_map(void) {
    NyxObjMap* map = ALLOCATE_OBJ(NyxObjMap, OBJ_MAP);
    nyx_table_init(&map->table);
    nyx_value_array_init(&map->keys);
    return map;
}

NyxObjResult* nyx_new_result(bool isOk, NyxValue value) {
    NyxObjResult* result = ALLOCATE_OBJ(NyxObjResult, OBJ_RESULT);
    result->isOk = isOk;
    result->value = value;
    return result;
}

NyxObjRange* nyx_new_range(int64_t start, int64_t end) {
    NyxObjRange* range = ALLOCATE_OBJ(NyxObjRange, OBJ_RANGE);
    range->start = start;
    range->end = end;
    return range;
}

NyxObjSet* nyx_new_set(void) {
    NyxObjSet* set = ALLOCATE_OBJ(NyxObjSet, OBJ_SET);
    nyx_value_array_init(&set->items);
    return set;
}

NyxObjCoroutine* nyx_new_coroutine(NyxObjClosure* closure) {
    NyxObjCoroutine* coro = ALLOCATE_OBJ(NyxObjCoroutine, OBJ_COROUTINE);
    coro->closure = closure;
    coro->state = CORO_CREATED;
    coro->stackTop = coro->stack;
    coro->frameCount = 0;
    coro->openUpvalues = NULL;
    return coro;
}

NyxObjString* nyx_take_string(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    NyxObjString* interned = nyx_table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

NyxObjString* nyx_copy_string(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    NyxObjString* interned = nyx_table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

void nyx_print_object(NyxValue value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_INT64:
            printf("%lld", (long long)((NyxObjInt64*)AS_OBJ(value))->value);
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION: {
            NyxObjFunction* fn = AS_FUNCTION(value);
            if (fn->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", fn->name->chars);
            }
            break;
        }
        case OBJ_CLOSURE:
            if (AS_CLOSURE(value)->function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", AS_CLOSURE(value)->function->name->chars);
            }
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            if (AS_BOUND_METHOD(value)->method->function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", AS_BOUND_METHOD(value)->method->function->name->chars);
            }
            break;
        case OBJ_LIST: {
            NyxObjList* list = AS_LIST(value);
            printf("[");
            for (int i = 0; i < list->items.count; i++) {
                if (i > 0) printf(", ");
                if (IS_STRING(list->items.values[i])) {
                    printf("\"");
                    nyx_print_value(list->items.values[i]);
                    printf("\"");
                } else {
                    nyx_print_value(list->items.values[i]);
                }
            }
            printf("]");
            break;
        }
        case OBJ_RANGE: {
            NyxObjRange* r = AS_RANGE(value);
            printf("%lld..%lld", (long long)r->start, (long long)r->end);
            break;
        }
        case OBJ_SET: {
            NyxObjSet* set = AS_SET(value);
            printf("{");
            for (int i = 0; i < set->items.count; i++) {
                if (i > 0) printf(", ");
                nyx_print_value(set->items.values[i]);
            }
            printf("}");
            break;
        }
        case OBJ_COROUTINE: {
            NyxObjCoroutine* coro = AS_COROUTINE(value);
            const char* stateStr = "unknown";
            switch (coro->state) {
                case CORO_CREATED:   stateStr = "created"; break;
                case CORO_RUNNING:   stateStr = "running"; break;
                case CORO_SUSPENDED: stateStr = "suspended"; break;
                case CORO_DEAD:      stateStr = "dead"; break;
            }
            printf("<coroutine %s>", stateStr);
            break;
        }
        case OBJ_RESULT: {
            NyxObjResult* result = AS_RESULT(value);
            printf("%s(", result->isOk ? "Ok" : "Err");
            nyx_print_value(result->value);
            printf(")");
            break;
        }
        case OBJ_MAP: {
            NyxObjMap* map = AS_MAP(value);
            printf("{");
            bool first = true;
            for (int i = 0; i < map->keys.count; i++) {
                NyxValue key = map->keys.values[i];
                NyxValue val;
                if (IS_STRING(key) && nyx_table_get(&map->table, AS_STRING(key), &val)) {
                    if (!first) printf(", ");
                    printf("\"");
                    nyx_print_value(key);
                    printf("\": ");
                    nyx_print_value(val);
                    first = false;
                }
            }
            printf("}");
            break;
        }
    }
}
