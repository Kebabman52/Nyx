#ifndef NYX_DEBUG_H
#define NYX_DEBUG_H

#include "chunk.h"

void nyx_disassemble_chunk(NyxChunk* chunk, const char* name);
int  nyx_disassemble_instruction(NyxChunk* chunk, int offset);

#endif
