#ifndef NYX_COMPILER_H
#define NYX_COMPILER_H

#include "object.h"
#include "scanner.h"

NyxObjFunction* nyx_compile(const char* source);
void nyx_mark_compiler_roots(void);

#endif
