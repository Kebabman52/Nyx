#ifndef NYX_MEMORY_H
#define NYX_MEMORY_H

#include "common.h"
#include "value.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)nyx_realloc(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    nyx_realloc(pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE(type, count) \
    (type*)nyx_realloc(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) \
    nyx_realloc(pointer, sizeof(type), 0)

#define GC_HEAP_GROW_FACTOR 2
#define NYX_MINOR_GC_THRESHOLD (256 * 1024)
#define NYX_MINOR_BEFORE_MAJOR 8

void* nyx_realloc(void* pointer, size_t oldSize, size_t newSize);
void nyx_gc_collect(void);
void nyx_gc_minor(void);
void nyx_gc_major(void);
void nyx_gc_set_ready(bool ready);
void nyx_gc_remember(struct NyxObj* obj);
void nyx_mark_object(struct NyxObj* object);
void nyx_mark_value(NyxValue value);
void nyx_free_objects(void);

// write barrier: when old objects point at young ones, the GC needs to know.
// skip this and enjoy your use-after-free
#define NYX_WRITE_BARRIER(container, child) \
    do { \
        if ((child) != NULL \
            && OBJ_IS_OLD((NyxObj*)(container)) \
            && OBJ_IS_YOUNG((NyxObj*)(child)) \
            && !OBJ_IS_REMEMBERED((NyxObj*)(container))) { \
            nyx_gc_remember((NyxObj*)(container)); \
        } \
    } while (0)

#define NYX_WRITE_BARRIER_VALUE(container, val) \
    do { \
        if (IS_OBJ(val)) \
            NYX_WRITE_BARRIER(container, AS_OBJ(val)); \
    } while (0)

#endif
