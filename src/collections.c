#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collections.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

//Map Helpers (ordered maps — because someone had to be different)

void nyx_map_set(NyxObjMap* map, NyxObjString* key, NyxValue value) {
    // GC protection dance
    nyx_vm_push(OBJ_VAL(key));
    nyx_vm_push(value);

    NyxValue existing;
    if (!nyx_table_get(&map->table, key, &existing)) {
        nyx_value_array_write(&map->keys, OBJ_VAL(key));
    }
    nyx_table_set(&map->table, key, value);

    nyx_vm_pop();
    nyx_vm_pop();

    NYX_WRITE_BARRIER_VALUE(&map->obj, value);
    NYX_WRITE_BARRIER(&map->obj, &key->obj);
}

bool nyx_map_get(NyxObjMap* map, NyxObjString* key, NyxValue* value) {
    return nyx_table_get(&map->table, key, value);
}

bool nyx_map_delete(NyxObjMap* map, NyxObjString* key) {
    if (!nyx_table_delete(&map->table, key)) return false;

    // also remove from the ordered keys list
    for (int i = 0; i < map->keys.count; i++) {
        if (IS_STRING(map->keys.values[i]) &&
            AS_STRING(map->keys.values[i]) == key) {
            // shift the rest down
            for (int j = i; j < map->keys.count - 1; j++) {
                map->keys.values[j] = map->keys.values[j + 1];
            }
            map->keys.count--;
            break;
        }
    }
    return true;
}

// List Helpers

static int listIndexOf(NyxObjList* list, NyxValue item) {
    for (int i = 0; i < list->items.count; i++) {
        if (nyx_values_equal(list->items.values[i], item)) {
            return i;
        }
    }
    return -1;
}

// insert at index — shift everything right
static void listInsertAt(NyxObjList* list, int index, NyxValue item) {
    // make room
    nyx_value_array_write(&list->items, NIL_VAL); // placeholder to grow
    // shift right
    for (int i = list->items.count - 1; i > index; i--) {
        list->items.values[i] = list->items.values[i - 1];
    }
    list->items.values[index] = item;
    // caller handles the write barrier
}

// yank an item out by index
static NyxValue listRemoveAt(NyxObjList* list, int index) {
    NyxValue removed = list->items.values[index];
    for (int i = index; i < list->items.count - 1; i++) {
        list->items.values[i] = list->items.values[i + 1];
    }
    list->items.count--;
    return removed;
}

// evict oldest entries when the list is over capacity. FIFO style
static void listEvict(NyxObjList* list) {
    while (list->maxSize > 0 && list->items.count > list->maxSize) {
        listRemoveAt(list, 0);
    }
}

// sort comparator — numbers first, then strings, then whatever
static int compareValues(const void* a, const void* b) {
    NyxValue va = *(const NyxValue*)a;
    NyxValue vb = *(const NyxValue*)b;

    if (IS_INT(va) && IS_INT(vb)) {
        int64_t diff = AS_INT(va) - AS_INT(vb);
        return diff < 0 ? -1 : (diff > 0 ? 1 : 0);
    }
    if ((IS_INT(va) || IS_FLOAT(va)) && (IS_INT(vb) || IS_FLOAT(vb))) {
        double da = IS_INT(va) ? (double)AS_INT(va) : AS_FLOAT(va);
        double db = IS_INT(vb) ? (double)AS_INT(vb) : AS_FLOAT(vb);
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    if (IS_STRING(va) && IS_STRING(vb)) {
        return strcmp(AS_CSTRING(va), AS_CSTRING(vb));
    }
    return 0;
}

// resolve target to index: int = use directly, otherwise linear search
static int resolveTarget(NyxObjList* list, NyxValue target) {
    if (IS_INT(target)) {
        int idx = (int)AS_INT(target);
        if (idx < 0) idx = list->items.count + idx;
        return idx;
    }
    return listIndexOf(list, target);
}

// List Method Dispatch (the big switch of doom)

bool nyx_list_invoke(NyxObjList* list, const char* name, int nameLen,
                     int argCount, NyxValue* args) {
    // push(item)
    if (nameLen == 4 && memcmp(name, "push", 4) == 0) {
        if (argCount != 1) { fprintf(stderr, "push() takes 1 argument.\n"); return false; }
        nyx_value_array_write(&list->items, args[0]);
        NYX_WRITE_BARRIER_VALUE(&list->obj, args[0]);
        listEvict(list);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // pop()
    if (nameLen == 3 && memcmp(name, "pop", 3) == 0) {
        if (list->items.count == 0) { nyx_vm_push(NIL_VAL); return true; }
        NyxValue val = list->items.values[--list->items.count];
        nyx_vm_push(val);
        return true;
    }

    // shift()
    if (nameLen == 5 && memcmp(name, "shift", 5) == 0) {
        if (list->items.count == 0) { nyx_vm_push(NIL_VAL); return true; }
        nyx_vm_push(listRemoveAt(list, 0));
        return true;
    }

    // len()
    if (nameLen == 3 && memcmp(name, "len", 3) == 0) {
        nyx_vm_push(INT_VAL(list->items.count));
        return true;
    }

    // keys() — returns [0, 1, 2, ...] so for-each works uniformly with maps
    if (nameLen == 4 && memcmp(name, "keys", 4) == 0) {
        NyxObjList* result = nyx_new_list();
        nyx_vm_push(OBJ_VAL(result));
        for (int i = 0; i < list->items.count; i++) {
            nyx_value_array_write(&result->items, INT_VAL(i));
        }
        return true;
    }

    // clear()
    if (nameLen == 5 && memcmp(name, "clear", 5) == 0) {
        list->items.count = 0;
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // is_empty()
    if (nameLen == 8 && memcmp(name, "is_empty", 8) == 0) {
        nyx_vm_push(BOOL_VAL(list->items.count == 0));
        return true;
    }

    // first()
    if (nameLen == 5 && memcmp(name, "first", 5) == 0) {
        nyx_vm_push(list->items.count > 0 ? list->items.values[0] : NIL_VAL);
        return true;
    }

    // last()
    if (nameLen == 4 && memcmp(name, "last", 4) == 0) {
        nyx_vm_push(list->items.count > 0 ? list->items.values[list->items.count - 1] : NIL_VAL);
        return true;
    }

    // get(index)
    if (nameLen == 3 && memcmp(name, "get", 3) == 0) {
        if (argCount != 1 || !IS_INT(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        int idx = (int)AS_INT(args[0]);
        if (idx < 0) idx = list->items.count + idx;
        if (idx < 0 || idx >= list->items.count) { nyx_vm_push(NIL_VAL); return true; }
        nyx_vm_push(list->items.values[idx]);
        return true;
    }

    // contains(item)
    if (nameLen == 8 && memcmp(name, "contains", 8) == 0) {
        if (argCount != 1) { nyx_vm_push(BOOL_VAL(false)); return true; }
        nyx_vm_push(BOOL_VAL(listIndexOf(list, args[0]) != -1));
        return true;
    }

    // index_of(item)
    if (nameLen == 8 && memcmp(name, "index_of", 8) == 0) {
        if (argCount != 1) { nyx_vm_push(INT_VAL(-1)); return true; }
        nyx_vm_push(INT_VAL(listIndexOf(list, args[0])));
        return true;
    }

    // insert_at(index, item)
    if (nameLen == 9 && memcmp(name, "insert_at", 9) == 0) {
        if (argCount != 2 || !IS_INT(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        int idx = (int)AS_INT(args[0]);
        if (idx < 0) idx = list->items.count + idx;
        if (idx < 0) idx = 0;
        if (idx > list->items.count) idx = list->items.count;
        listInsertAt(list, idx, args[1]);
        NYX_WRITE_BARRIER_VALUE(&list->obj, args[1]);
        listEvict(list);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // insert_before(target, item) — target can be int index or value to find
    if (nameLen == 13 && memcmp(name, "insert_before", 13) == 0) {
        if (argCount != 2) { nyx_vm_push(NIL_VAL); return true; }
        int idx = resolveTarget(list, args[0]);
        if (idx < 0) idx = 0;
        if (idx > list->items.count) idx = list->items.count;
        listInsertAt(list, idx, args[1]);
        NYX_WRITE_BARRIER_VALUE(&list->obj, args[1]);
        listEvict(list);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // insert_after — same deal
    if (nameLen == 12 && memcmp(name, "insert_after", 12) == 0) {
        if (argCount != 2) { nyx_vm_push(NIL_VAL); return true; }
        int idx = resolveTarget(list, args[0]);
        if (idx < 0) idx = 0;
        else idx = idx + 1;
        if (idx > list->items.count) idx = list->items.count;
        listInsertAt(list, idx, args[1]);
        NYX_WRITE_BARRIER_VALUE(&list->obj, args[1]);
        listEvict(list);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // remove_at(index)
    if (nameLen == 9 && memcmp(name, "remove_at", 9) == 0) {
        if (argCount != 1 || !IS_INT(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        int idx = (int)AS_INT(args[0]);
        if (idx < 0) idx = list->items.count + idx;
        if (idx < 0 || idx >= list->items.count) { nyx_vm_push(NIL_VAL); return true; }
        nyx_vm_push(listRemoveAt(list, idx));
        return true;
    }

    // remove(item) — first match only
    if (nameLen == 6 && memcmp(name, "remove", 6) == 0) {
        if (argCount != 1) { nyx_vm_push(BOOL_VAL(false)); return true; }
        int idx = listIndexOf(list, args[0]);
        if (idx == -1) { nyx_vm_push(BOOL_VAL(false)); return true; }
        listRemoveAt(list, idx);
        nyx_vm_push(BOOL_VAL(true));
        return true;
    }

    // remove_all(item)
    if (nameLen == 10 && memcmp(name, "remove_all", 10) == 0) {
        if (argCount != 1) { nyx_vm_push(INT_VAL(0)); return true; }
        int removed = 0;
        for (int i = list->items.count - 1; i >= 0; i--) {
            if (nyx_values_equal(list->items.values[i], args[0])) {
                listRemoveAt(list, i);
                removed++;
            }
        }
        nyx_vm_push(INT_VAL(removed));
        return true;
    }

    // reverse()
    if (nameLen == 7 && memcmp(name, "reverse", 7) == 0) {
        int n = list->items.count;
        for (int i = 0; i < n / 2; i++) {
            NyxValue tmp = list->items.values[i];
            list->items.values[i] = list->items.values[n - 1 - i];
            list->items.values[n - 1 - i] = tmp;
        }
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // sort()
    if (nameLen == 4 && memcmp(name, "sort", 4) == 0) {
        qsort(list->items.values, list->items.count, sizeof(NyxValue), compareValues);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // slice(start, end)
    if (nameLen == 5 && memcmp(name, "slice", 5) == 0) {
        if (argCount != 2) { nyx_vm_push(OBJ_VAL(nyx_new_list())); return true; }
        int start = IS_INT(args[0]) ? (int)AS_INT(args[0]) : 0;
        int end = IS_INT(args[1]) ? (int)AS_INT(args[1]) : list->items.count;
        if (start < 0) start = list->items.count + start;
        if (end < 0) end = list->items.count + end;
        if (start < 0) start = 0;
        if (end > list->items.count) end = list->items.count;

        NyxObjList* result = nyx_new_list();
        nyx_vm_push(OBJ_VAL(result)); // GC protection
        for (int i = start; i < end; i++) {
            nyx_value_array_write(&result->items, list->items.values[i]);
        }
        // result is already on stack
        return true;
    }

    // set_max(n)
    if (nameLen == 7 && memcmp(name, "set_max", 7) == 0) {
        if (argCount != 1 || !IS_INT(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        list->maxSize = (int)AS_INT(args[0]);
        listEvict(list);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // each(fn) — functional style iteration
    if (nameLen == 4 && memcmp(name, "each", 4) == 0) {
        if (argCount != 1) { nyx_vm_push(NIL_VAL); return true; }
        NyxValue callback = args[0];
        for (int i = 0; i < list->items.count; i++) {
            nyx_vm_push(callback);
            nyx_vm_push(list->items.values[i]);
            // can't call from C — need to go through the VM's call machinery
            // Instead, we use a simpler approach: just call through the VM's callValue
        }
        // For now, each/map/filter require VM-level integration.
        // We'll return nil and handle these via native functions instead.
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // map(fn) and filter(fn) also need VM-level call support
    // They'll be implemented as native functions that take a list and fn

    return false; // unknown method
}

// Map Method Dispatch

bool nyx_map_invoke(NyxObjMap* map, const char* name, int nameLen,
                    int argCount, NyxValue* args) {
    // len()
    if (nameLen == 3 && memcmp(name, "len", 3) == 0) {
        // Count actual entries (not tombstones)
        int count = 0;
        for (int i = 0; i < map->keys.count; i++) {
            NyxValue val;
            if (IS_STRING(map->keys.values[i]) &&
                nyx_table_get(&map->table, AS_STRING(map->keys.values[i]), &val)) {
                count++;
            }
        }
        nyx_vm_push(INT_VAL(count));
        return true;
    }

    // is_empty()
    if (nameLen == 8 && memcmp(name, "is_empty", 8) == 0) {
        nyx_vm_push(BOOL_VAL(map->keys.count == 0));
        return true;
    }

    // clear()
    if (nameLen == 5 && memcmp(name, "clear", 5) == 0) {
        nyx_table_free(&map->table);
        nyx_table_init(&map->table);
        map->keys.count = 0;
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // get(key)
    if (nameLen == 3 && memcmp(name, "get", 3) == 0) {
        if (argCount != 1 || !IS_STRING(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        NyxValue val;
        if (nyx_table_get(&map->table, AS_STRING(args[0]), &val)) {
            nyx_vm_push(val);
        } else {
            nyx_vm_push(NIL_VAL);
        }
        return true;
    }

    // set(key, value)
    if (nameLen == 3 && memcmp(name, "set", 3) == 0) {
        if (argCount != 2 || !IS_STRING(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        nyx_map_set(map, AS_STRING(args[0]), args[1]);
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // remove(key)
    if (nameLen == 6 && memcmp(name, "remove", 6) == 0) {
        if (argCount != 1 || !IS_STRING(args[0])) { nyx_vm_push(BOOL_VAL(false)); return true; }
        nyx_vm_push(BOOL_VAL(nyx_map_delete(map, AS_STRING(args[0]))));
        return true;
    }

    // contains_key(key)
    if (nameLen == 12 && memcmp(name, "contains_key", 12) == 0) {
        if (argCount != 1 || !IS_STRING(args[0])) { nyx_vm_push(BOOL_VAL(false)); return true; }
        NyxValue val;
        nyx_vm_push(BOOL_VAL(nyx_table_get(&map->table, AS_STRING(args[0]), &val)));
        return true;
    }

    // contains_value(value)
    if (nameLen == 14 && memcmp(name, "contains_value", 14) == 0) {
        if (argCount != 1) { nyx_vm_push(BOOL_VAL(false)); return true; }
        for (int i = 0; i < map->keys.count; i++) {
            if (!IS_STRING(map->keys.values[i])) continue;
            NyxValue val;
            if (nyx_table_get(&map->table, AS_STRING(map->keys.values[i]), &val)) {
                if (nyx_values_equal(val, args[0])) {
                    nyx_vm_push(BOOL_VAL(true));
                    return true;
                }
            }
        }
        nyx_vm_push(BOOL_VAL(false));
        return true;
    }

    // keys()
    if (nameLen == 4 && memcmp(name, "keys", 4) == 0) {
        NyxObjList* result = nyx_new_list();
        nyx_vm_push(OBJ_VAL(result)); // GC protection
        for (int i = 0; i < map->keys.count; i++) {
            NyxValue val;
            if (IS_STRING(map->keys.values[i]) &&
                nyx_table_get(&map->table, AS_STRING(map->keys.values[i]), &val)) {
                nyx_value_array_write(&result->items, map->keys.values[i]);
            }
        }
        return true;
    }

    // values()
    if (nameLen == 6 && memcmp(name, "values", 6) == 0) {
        NyxObjList* result = nyx_new_list();
        nyx_vm_push(OBJ_VAL(result)); // GC protection
        for (int i = 0; i < map->keys.count; i++) {
            NyxValue val;
            if (IS_STRING(map->keys.values[i]) &&
                nyx_table_get(&map->table, AS_STRING(map->keys.values[i]), &val)) {
                nyx_value_array_write(&result->items, val);
            }
        }
        return true;
    }

    // merge(other_map)
    if (nameLen == 5 && memcmp(name, "merge", 5) == 0) {
        if (argCount != 1 || !IS_MAP(args[0])) { nyx_vm_push(NIL_VAL); return true; }
        NyxObjMap* other = AS_MAP(args[0]);
        for (int i = 0; i < other->keys.count; i++) {
            if (!IS_STRING(other->keys.values[i])) continue;
            NyxObjString* key = AS_STRING(other->keys.values[i]);
            NyxValue val;
            if (nyx_table_get(&other->table, key, &val)) {
                nyx_map_set(map, key, val);
            }
        }
        nyx_vm_push(NIL_VAL);
        return true;
    }

    // reverse() — reverse key iteration order
    if (nameLen == 7 && memcmp(name, "reverse", 7) == 0) {
        int n = map->keys.count;
        for (int i = 0; i < n / 2; i++) {
            NyxValue tmp = map->keys.values[i];
            map->keys.values[i] = map->keys.values[n - 1 - i];
            map->keys.values[n - 1 - i] = tmp;
        }
        nyx_vm_push(NIL_VAL);
        return true;
    }

    return false; // unknown map method
}
