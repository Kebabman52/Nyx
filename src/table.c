#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"

#define TABLE_MAX_LOAD 0.75

void nyx_table_init(NyxTable* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void nyx_table_free(NyxTable* table) {
    FREE_ARRAY(NyxEntry, table->entries, table->capacity);
    nyx_table_init(table);
}

static NyxEntry* findEntry(NyxEntry* entries, int capacity, NyxObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    NyxEntry* tombstone = NULL;

    for (;;) {
        NyxEntry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                // tombstone — someone used to live here
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // pointer comparison works because strings are interned. love it
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

static void adjustCapacity(NyxTable* table, int capacity) {
    NyxEntry* entries = ALLOCATE(NyxEntry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        NyxEntry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        NyxEntry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(NyxEntry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool nyx_table_get(NyxTable* table, NyxObjString* key, NyxValue* value) {
    if (table->count == 0) return false;

    NyxEntry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

bool nyx_table_set(NyxTable* table, NyxObjString* key, NyxValue value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    NyxEntry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool nyx_table_delete(NyxTable* table, NyxObjString* key) {
    if (table->count == 0) return false;

    NyxEntry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // leave a tombstone — can't just null it or we break probe chains
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void nyx_table_add_all(NyxTable* from, NyxTable* to) {
    for (int i = 0; i < from->capacity; i++) {
        NyxEntry* entry = &from->entries[i];
        if (entry->key != NULL) {
            nyx_table_set(to, entry->key, entry->value);
        }
    }
}

NyxObjString* nyx_table_find_string(NyxTable* table, const char* chars,
                                     int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        NyxEntry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // truly empty = not here. tombstones don't count
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }

        index = (index + 1) & (table->capacity - 1);
    }
}

void nyx_mark_table(NyxTable* table) {
    for (int i = 0; i < table->capacity; i++) {
        NyxEntry* entry = &table->entries[i];
        nyx_mark_object((NyxObj*)entry->key);
        nyx_mark_value(entry->value);
    }
}

void nyx_table_remove_white(NyxTable* table, bool minorGC) {
    for (int i = 0; i < table->capacity; i++) {
        NyxEntry* entry = &table->entries[i];
        if (entry->key == NULL) continue;
        if (!OBJ_IS_MARKED(&entry->key->obj)) {
            // minor GC: only kill young strings. old ones get a pass
            if (minorGC && OBJ_IS_OLD(&entry->key->obj)) continue;
            nyx_table_delete(table, entry->key);
        }
    }
}
