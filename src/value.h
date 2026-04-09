#ifndef NYX_VALUE_H
#define NYX_VALUE_H

#include "common.h"

typedef struct NyxObj NyxObj;
typedef struct NyxObjString NyxObjString;

// NaN Boxing (the dark arts of value representation)
// shove every Nyx value into 8 bytes by abusing IEEE 754 NaN space.
// halves the stack footprint and makes the CPU cache very happy.
//
//   plain double              = float value (just a normal double)
//   QNAN | SIGN_BIT | 0x01   = nil
//   QNAN | SIGN_BIT | 0x02   = false
//   QNAN | SIGN_BIT | 0x03   = true
//   QNAN | pointer bits       = heap object (48-bit ptr)
//   QNAN | SIGN_BIT | TAG_INT | 48-bit signed int
//
// big ints (>48 bits) get boxed on the heap. most ints fit inline.
// 140 trillion should be enough for anyone -- famous last words

#ifdef NYX_NAN_BOXING

#include <string.h> // for memcpy

typedef uint64_t NyxValue;

// tag constants

#define SIGN_BIT  ((uint64_t)0x8000000000000000)
#define QNAN      ((uint64_t)0x7ffc000000000000)

// special value tags
#define TAG_NIL   ((uint64_t)1)
#define TAG_FALSE ((uint64_t)2)
#define TAG_TRUE  ((uint64_t)3)

// int tag: bit 49 flags it as int, leaves 48 bits for the actual value
#define TAG_INT   ((uint64_t)0x0002000000000000)
#define INT_MASK  ((uint64_t)0x0000FFFFFFFFFFFF) // low 48 bits

#define NIL_VAL         ((NyxValue)(QNAN | SIGN_BIT | TAG_NIL))
#define FALSE_VAL       ((NyxValue)(QNAN | SIGN_BIT | TAG_FALSE))
#define TRUE_VAL        ((NyxValue)(QNAN | SIGN_BIT | TAG_TRUE))

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(obj)    ((NyxValue)(QNAN | (uint64_t)(uintptr_t)(obj)))

// Int: sign-extend a 64-bit int into 48-bit representation.
// Values that fit in 48 bits stay NaN-boxed (fast path, zero allocation).
// Values >48 bits are heap-allocated as OBJ_INT64 (see object.h).
//
// INT_VAL_SMALL: always NaN-boxes (for values known to fit)
// INT_VAL: auto-selects based on range (defined in object.h after OBJ_INT64)
#define INT_VAL_SMALL(i) ((NyxValue)(QNAN | SIGN_BIT | TAG_INT | ((uint64_t)(i) & INT_MASK)))

// Fit check for 48-bit signed range
#define NYX_INT48_MIN  ((int64_t)(-((int64_t)1 << 47)))
#define NYX_INT48_MAX  ((int64_t)(((int64_t)1 << 47) - 1))
#define NYX_FITS_48(i) ((int64_t)(i) >= NYX_INT48_MIN && (int64_t)(i) <= NYX_INT48_MAX)

// IS_INT_FAST: only checks NaN-boxed ints (use in hot paths where OBJ_INT64 is impossible)
#define IS_INT_FAST(v)  (((v) & (QNAN | SIGN_BIT | TAG_INT)) == (QNAN | SIGN_BIT | TAG_INT))

// AS_INT_FAST: extract from NaN-boxed int only (no heap check)
static inline int64_t nyx_nanbox_as_int_fast(NyxValue v) {
    uint64_t raw = v & INT_MASK;
    if (raw & ((uint64_t)1 << 47)) raw |= (uint64_t)0xFFFF000000000000;
    return (int64_t)raw;
}
#define AS_INT_FAST(v)  nyx_nanbox_as_int_fast(v)

// Full IS_INT and AS_INT are defined in object.h (after OBJ_INT64 is available)
// Temporary: define IS_INT as IS_INT_FAST here, redefined in object.h
#define IS_INT(v)       IS_INT_FAST(v)
#define AS_INT(v)       AS_INT_FAST(v)
#define INT_VAL(i)      INT_VAL_SMALL(i)

static inline NyxValue nyx_double_to_value(double d) {
    NyxValue v; memcpy(&v, &d, sizeof(double)); return v;
}
static inline double nyx_value_to_double(NyxValue v) {
    double d; memcpy(&d, &v, sizeof(double)); return d;
}

#define FLOAT_VAL(d)    nyx_double_to_value(d)
#define AS_FLOAT(v)     nyx_value_to_double(v)

#define AS_BOOL(v)      ((v) == TRUE_VAL)
#define AS_OBJ(v)       ((NyxObj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))

#define IS_FLOAT(v)     (((v) & QNAN) != QNAN)
#define IS_NIL(v)       ((v) == NIL_VAL)
#define IS_BOOL(v)      (((v) | 1) == TRUE_VAL)
#define IS_INT(v)       (((v) & (QNAN | SIGN_BIT | TAG_INT)) == (QNAN | SIGN_BIT | TAG_INT))
#define IS_OBJ(v)       (((v) & (QNAN | SIGN_BIT)) == QNAN)

#else
// Tagged Union (fallback)

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ,
} NyxValueType;

typedef struct {
    NyxValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        NyxObj* obj;
    } as;
} NyxValue;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_INT(value)     ((value).type == VAL_INT)
#define IS_FLOAT(value)   ((value).type == VAL_FLOAT)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_INT(value)     ((value).as.integer)
#define AS_FLOAT(value)   ((value).as.floating)
#define AS_OBJ(value)     ((value).as.obj)

#define BOOL_VAL(value)   ((NyxValue){VAL_BOOL,  {.boolean = (value)}})
#define NIL_VAL           ((NyxValue){VAL_NIL,    {.integer = 0}})
#define INT_VAL(value)    ((NyxValue){VAL_INT,    {.integer = (value)}})
#define FLOAT_VAL(value)  ((NyxValue){VAL_FLOAT,  {.floating = (value)}})
#define OBJ_VAL(object)   ((NyxValue){VAL_OBJ,    {.obj = (NyxObj*)(object)}})

#endif // NYX_NAN_BOXING

// Value Array (constant pool)

typedef struct {
    int capacity;
    int count;
    NyxValue* values;
} NyxValueArray;

void nyx_value_array_init(NyxValueArray* array);
void nyx_value_array_write(NyxValueArray* array, NyxValue value);
void nyx_value_array_free(NyxValueArray* array);
void nyx_print_value(NyxValue value);
bool nyx_values_equal(NyxValue a, NyxValue b);

#endif
