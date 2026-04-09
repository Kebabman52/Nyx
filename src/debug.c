#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void nyx_disassemble_chunk(NyxChunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = nyx_disassemble_instruction(chunk, offset);
    }
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int constantInstruction(const char* name, NyxChunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    nyx_print_value(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static int byteInstruction(const char* name, NyxChunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int jumpInstruction(const char* name, int sign, NyxChunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nyx_disassemble_instruction(NyxChunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:       return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG: {
            uint16_t index = (uint16_t)(chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s %4d '", "OP_CONSTANT_LONG", index);
            nyx_print_value(chunk->constants.values[index]);
            printf("'\n");
            return offset + 3;
        }
        case OP_NIL:            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:           return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:          return simpleInstruction("OP_FALSE", offset);
        case OP_ADD:            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:       return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:       return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:         return simpleInstruction("OP_DIVIDE", offset);
        case OP_MODULO:         return simpleInstruction("OP_MODULO", offset);
        case OP_NEGATE:         return simpleInstruction("OP_NEGATE", offset);
        case OP_EQUAL:          return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:      return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER:        return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:  return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS:           return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:     return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_NOT:            return simpleInstruction("OP_NOT", offset);
        case OP_POP:            return simpleInstruction("OP_POP", offset);
        case OP_DEFINE_GLOBAL:  return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:     return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:     return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_LOCAL:      return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:      return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_UPVALUE:    return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:    return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_CLOSE_UPVALUE:  return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_JUMP:           return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:  return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:           return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_PRINT:          return simpleInstruction("OP_PRINT", offset);
        case OP_CALL:           return byteInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            uint8_t constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            nyx_print_value(chunk->constants.values[constant]);
            printf("\n");

            NyxObjFunction* fn = AS_FUNCTION(chunk->constants.values[constant]);
            for (int j = 0; j < fn->upvalueCount; j++) {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n",
                       offset - 2, isLocal ? "local" : "upvalue", index);
            }
            return offset;
        }
        case OP_COROUTINE:     return simpleInstruction("OP_COROUTINE", offset);
        case OP_YIELD:         return simpleInstruction("OP_YIELD", offset);
        case OP_RESUME:        return simpleInstruction("OP_RESUME", offset);
        case OP_IMPORT:        return constantInstruction("OP_IMPORT", chunk, offset);
        case OP_TRY_UNWRAP:    return simpleInstruction("OP_TRY_UNWRAP", offset);
        case OP_LOADI:         return byteInstruction("OP_LOADI", chunk, offset);
        case OP_ADDI:          return byteInstruction("OP_ADDI", chunk, offset);
        case OP_SUBI:          return byteInstruction("OP_SUBI", chunk, offset);
        case OP_CONTAINS:      return simpleInstruction("OP_CONTAINS", offset);
        case OP_BUILD_RANGE:   return simpleInstruction("OP_BUILD_RANGE", offset);
        case OP_BUILD_SET:     return byteInstruction("OP_BUILD_SET", chunk, offset);
        case OP_BUILD_LIST:    return byteInstruction("OP_BUILD_LIST", chunk, offset);
        case OP_BUILD_MAP:     return byteInstruction("OP_BUILD_MAP", chunk, offset);
        case OP_INDEX_GET:     return simpleInstruction("OP_INDEX_GET", offset);
        case OP_INDEX_SET:     return simpleInstruction("OP_INDEX_SET", offset);
        case OP_CLASS:          return constantInstruction("OP_CLASS", chunk, offset);
        case OP_GET_PROPERTY:  return constantInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:  return constantInstruction("OP_SET_PROPERTY", chunk, offset);
        case OP_METHOD:        return constantInstruction("OP_METHOD", chunk, offset);
        case OP_INVOKE: {
            uint8_t constant = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s (%d args) %4d '", "OP_INVOKE", argCount, constant);
            nyx_print_value(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
        }
        case OP_INHERIT:       return simpleInstruction("OP_INHERIT", offset);
        case OP_GET_SUPER:     return constantInstruction("OP_GET_SUPER", chunk, offset);
        case OP_SUPER_INVOKE: {
            uint8_t constant = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s (%d args) %4d '", "OP_SUPER_INVOKE", argCount, constant);
            nyx_print_value(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
        }
        case OP_INSTANCEOF:    return simpleInstruction("OP_INSTANCEOF", offset);
        case OP_DUP:           return simpleInstruction("OP_DUP", offset);
        case OP_JUMP_IF_NIL:   return jumpInstruction("OP_JUMP_IF_NIL", 1, chunk, offset);
        case OP_RETURN:        return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
