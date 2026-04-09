#ifndef NYX_TYPES_H
#define NYX_TYPES_H

#include "common.h"

// Compile-time type tags for type checking
typedef enum {
    NYX_TYPE_UNKNOWN,    // type not yet determined
    NYX_TYPE_ANY,        // explicitly untyped (disables checking)
    NYX_TYPE_NIL,
    NYX_TYPE_BOOL,
    NYX_TYPE_INT,
    NYX_TYPE_FLOAT,
    NYX_TYPE_STRING,
    NYX_TYPE_FN,
    NYX_TYPE_LIST,
    NYX_TYPE_MAP,
    NYX_TYPE_CLASS,      // a specific class (name stored separately)
    NYX_TYPE_RESULT,
    NYX_TYPE_COROUTINE,
} NyxTypeTag;

typedef struct {
    NyxTypeTag tag;
    bool nullable;       // T? = nullable
} NyxType;

#define NYX_TYPE_MAKE(t)          ((NyxType){(t), false})
#define NYX_TYPE_NULLABLE(t)      ((NyxType){(t), true})

// Parse a type annotation string into a NyxType
NyxType nyx_type_from_name(const char* name, int length);

// Check if two types are compatible (assignable)
bool nyx_type_compatible(NyxType target, NyxType source);

// Get a human-readable type name
const char* nyx_type_name(NyxType type);

#endif
