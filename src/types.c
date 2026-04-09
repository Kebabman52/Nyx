#include <string.h>

#include "types.h"

NyxType nyx_type_from_name(const char* name, int length) {
    if (length == 3 && memcmp(name, "int", 3) == 0)       return NYX_TYPE_MAKE(NYX_TYPE_INT);
    if (length == 5 && memcmp(name, "float", 5) == 0)     return NYX_TYPE_MAKE(NYX_TYPE_FLOAT);
    if (length == 4 && memcmp(name, "bool", 4) == 0)      return NYX_TYPE_MAKE(NYX_TYPE_BOOL);
    if (length == 6 && memcmp(name, "string", 6) == 0)    return NYX_TYPE_MAKE(NYX_TYPE_STRING);
    if (length == 2 && memcmp(name, "fn", 2) == 0)        return NYX_TYPE_MAKE(NYX_TYPE_FN);
    if (length == 4 && memcmp(name, "list", 4) == 0)      return NYX_TYPE_MAKE(NYX_TYPE_LIST);
    if (length == 3 && memcmp(name, "map", 3) == 0)       return NYX_TYPE_MAKE(NYX_TYPE_MAP);
    if (length == 6 && memcmp(name, "Result", 6) == 0)    return NYX_TYPE_MAKE(NYX_TYPE_RESULT);
    if (length == 9 && memcmp(name, "coroutine", 9) == 0) return NYX_TYPE_MAKE(NYX_TYPE_COROUTINE);
    if (length == 3 && memcmp(name, "nil", 3) == 0)       return NYX_TYPE_MAKE(NYX_TYPE_NIL);
    // Assume it's a class name — treat as CLASS for now
    return NYX_TYPE_MAKE(NYX_TYPE_CLASS);
}

bool nyx_type_compatible(NyxType target, NyxType source) {
    // UNKNOWN or ANY matches everything
    if (target.tag == NYX_TYPE_UNKNOWN || target.tag == NYX_TYPE_ANY) return true;
    if (source.tag == NYX_TYPE_UNKNOWN || source.tag == NYX_TYPE_ANY) return true;

    // nil is assignable to any nullable type
    if (source.tag == NYX_TYPE_NIL && target.nullable) return true;

    // int promotes to float
    if (target.tag == NYX_TYPE_FLOAT && source.tag == NYX_TYPE_INT) return true;

    // Exact match
    if (target.tag == source.tag) return true;

    return false;
}

const char* nyx_type_name(NyxType type) {
    switch (type.tag) {
        case NYX_TYPE_UNKNOWN:   return "unknown";
        case NYX_TYPE_ANY:       return "any";
        case NYX_TYPE_NIL:       return "nil";
        case NYX_TYPE_BOOL:      return "bool";
        case NYX_TYPE_INT:       return "int";
        case NYX_TYPE_FLOAT:     return "float";
        case NYX_TYPE_STRING:    return "string";
        case NYX_TYPE_FN:        return "fn";
        case NYX_TYPE_LIST:      return "list";
        case NYX_TYPE_MAP:       return "map";
        case NYX_TYPE_CLASS:     return "class";
        case NYX_TYPE_RESULT:    return "Result";
        case NYX_TYPE_COROUTINE: return "coroutine";
    }
    return "unknown";
}
