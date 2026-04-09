#ifndef NYX_COLLECTIONS_H
#define NYX_COLLECTIONS_H

#include "object.h"
#include "value.h"

// Returns true if the method was handled. Result is pushed onto the stack.
bool nyx_list_invoke(NyxObjList* list, const char* name, int nameLen,
                     int argCount, NyxValue* args);
bool nyx_map_invoke(NyxObjMap* map, const char* name, int nameLen,
                    int argCount, NyxValue* args);

// Helpers for map operations
void nyx_map_set(NyxObjMap* map, NyxObjString* key, NyxValue value);
bool nyx_map_get(NyxObjMap* map, NyxObjString* key, NyxValue* value);
bool nyx_map_delete(NyxObjMap* map, NyxObjString* key);

#endif
