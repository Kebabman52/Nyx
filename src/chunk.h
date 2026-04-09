#ifndef NYX_CHUNK_H
#define NYX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
    // Constants & literals
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Logic
    OP_NOT,

    // Variables
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    // Stack
    OP_POP,

    // Built-ins
    OP_PRINT,

    // Functions
    OP_CALL,
    OP_CLOSURE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,

    // Error handling & modules
    OP_TRY_UNWRAP,
    OP_IMPORT,

    // Coroutines
    OP_COROUTINE,       // wrap closure into a coroutine object
    OP_YIELD,           // suspend coroutine, return value to caller
    OP_RESUME,          // resume a suspended coroutine      // ? operator: unwrap Ok or return Err

    // Collections
    OP_CONTAINS,        // 'in' operator
    OP_BUILD_RANGE,     // build range from two ints on stack
    OP_BUILD_SET,       // operand: item count
    OP_BUILD_LIST,      // operand: item count
    OP_BUILD_MAP,       // operand: entry count
    OP_INDEX_GET,
    OP_INDEX_SET,

    // Immediate opcodes (optimization)
    OP_LOADI,           // load small int (-128..127) without constant table
    OP_ADDI,            // add small int to TOS
    OP_SUBI,            // subtract small int from TOS

    // Classes
    OP_CLASS,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_METHOD,
    OP_INVOKE,
    OP_INHERIT,
    OP_GET_SUPER,
    OP_SUPER_INVOKE,
    OP_INSTANCEOF,
    OP_DUP,             // duplicate top of stack
    OP_JUMP_IF_NIL,     // jump if TOS is nil (pops)
    OP_CONSTANT_LONG,   // 2-byte index for >256 constants
} NyxOpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;             // line number for each byte of code
    NyxValueArray constants;
} NyxChunk;

void nyx_chunk_init(NyxChunk* chunk);
void nyx_chunk_free(NyxChunk* chunk);
void nyx_chunk_write(NyxChunk* chunk, uint8_t byte, int line);
int  nyx_chunk_add_constant(NyxChunk* chunk, NyxValue value);

#endif
