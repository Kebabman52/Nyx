#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void nyx_value_array_init(NyxValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void nyx_value_array_write(NyxValueArray* array, NyxValue value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(NyxValue, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void nyx_value_array_free(NyxValueArray* array) {
    FREE_ARRAY(NyxValue, array->values, array->capacity);
    nyx_value_array_init(array);
}

void nyx_print_value(NyxValue value) {
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_INT(value)) {
        printf("%lld", (long long)AS_INT(value));
    } else if (IS_FLOAT(value)) {
        double f = AS_FLOAT(value);
        if (f == (int64_t)f && f >= -1e15 && f <= 1e15) {
            printf("%.1f", f);
        } else {
            printf("%g", f);
        }
    } else if (IS_OBJ(value)) {
        nyx_print_object(value);
    }
}

bool nyx_values_equal(NyxValue a, NyxValue b) {
#ifdef NYX_NAN_BOXING
    // NaN boxing: cross-type numeric comparison. this is cursed but correct
    if (IS_INT(a) && IS_FLOAT(b)) return (double)AS_INT(a) == AS_FLOAT(b);
    if (IS_FLOAT(a) && IS_INT(b)) return AS_FLOAT(a) == (double)AS_INT(b);

    // both ints — might be NaN-boxed inline or heap-allocated OBJ_INT64
    if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);

    // bitwise equality handles nil, bool, and object pointers. NaN boxing is wild
    if (IS_FLOAT(a) && IS_FLOAT(b)) return AS_FLOAT(a) == AS_FLOAT(b);
    return a == b;
#else
    if (IS_INT(a) && IS_FLOAT(b)) return (double)AS_INT(a) == AS_FLOAT(b);
    if (IS_FLOAT(a) && IS_INT(b)) return AS_FLOAT(a) == (double)AS_INT(b);

    if (a.type != b.type) return false;

    switch (a.type) {
        case VAL_BOOL:  return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:   return true;
        case VAL_INT:   return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT: return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_OBJ:   return AS_OBJ(a) == AS_OBJ(b);
        default:        return false;
    }
#endif
}
