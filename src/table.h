#ifndef NYX_TABLE_H
#define NYX_TABLE_H

#include "common.h"
#include "value.h"

typedef struct {
    NyxObjString* key;
    NyxValue value;
} NyxEntry;

typedef struct {
    int count;
    int capacity;
    NyxEntry* entries;
} NyxTable;

void nyx_table_init(NyxTable* table);
void nyx_table_free(NyxTable* table);
bool nyx_table_get(NyxTable* table, NyxObjString* key, NyxValue* value);
bool nyx_table_set(NyxTable* table, NyxObjString* key, NyxValue value);
bool nyx_table_delete(NyxTable* table, NyxObjString* key);
void nyx_table_add_all(NyxTable* from, NyxTable* to);
NyxObjString* nyx_table_find_string(NyxTable* table, const char* chars,
                                     int length, uint32_t hash);
void nyx_mark_table(NyxTable* table);
void nyx_table_remove_white(NyxTable* table, bool minorGC);

#endif
