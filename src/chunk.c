#include "chunk.h"
#include "memory.h"

void nyx_chunk_init(NyxChunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    nyx_value_array_init(&chunk->constants);
}

void nyx_chunk_free(NyxChunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    nyx_value_array_free(&chunk->constants);
    nyx_chunk_init(chunk);
}

void nyx_chunk_write(NyxChunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

// strict equality for constant dedup — int 1 and float 1.0 are NOT the same slot
static bool constantsMatch(NyxValue a, NyxValue b) {
#ifdef NYX_NAN_BOXING
    // NaN boxing: bit-exact for non-floats, value compare for floats
    if (IS_FLOAT(a) && IS_FLOAT(b)) return AS_FLOAT(a) == AS_FLOAT(b);
    if (IS_FLOAT(a) != IS_FLOAT(b)) return false;
    // OBJ_INT64: compare by value, not pointer — multiple boxed ints can hold the same number
    if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);
    return a == b;
#else
    if (a.type != b.type) return false;
    return nyx_values_equal(a, b);
#endif
}

int nyx_chunk_add_constant(NyxChunk* chunk, NyxValue value) {
    for (int i = 0; i < chunk->constants.count; i++) {
        if (constantsMatch(chunk->constants.values[i], value)) return i;
    }

    nyx_value_array_write(&chunk->constants, value);
    return chunk->constants.count - 1;
}
