#ifndef NYX_COMMON_H
#define NYX_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NYX_VERSION_MAJOR 1
#define NYX_VERSION_MINOR 0
#define NYX_VERSION_PATCH 0

// stringify macro — preprocessor magic for version strings
#define NYX_STR(x) NYX_STR2(x)
#define NYX_STR2(x) #x
#define NYX_VERSION_STRING NYX_STR(NYX_VERSION_MAJOR) "." NYX_STR(NYX_VERSION_MINOR) "." NYX_STR(NYX_VERSION_PATCH)

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
