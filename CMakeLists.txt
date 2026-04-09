cmake_minimum_required(VERSION 3.16)
project(nyx C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Release mode: enable optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT MSVC)
        add_compile_options(-O2 -DNDEBUG)
    else()
        add_compile_options(/O2 /DNDEBUG)
    endif()
endif()

# NaN boxing (8-byte values instead of 16-byte)
option(NYX_NAN_BOXING "Use NaN boxing for compact values" ON)
if(NYX_NAN_BOXING)
    add_compile_definitions(NYX_NAN_BOXING)
endif()

# GC stress testing mode
option(NYX_GC_STRESS "Trigger GC on every allocation (debug)" OFF)
if(NYX_GC_STRESS)
    add_compile_definitions(NYX_GC_STRESS)
endif()

# Debug trace mode
option(NYX_DEBUG_TRACE "Print bytecode trace during execution" OFF)
if(NYX_DEBUG_TRACE)
    add_compile_definitions(NYX_DEBUG_TRACE)
endif()

# Library sources
set(NYX_SOURCES
    src/memory.c
    src/value.c
    src/chunk.c
    src/scanner.c
    src/object.c
    src/table.c
    src/collections.c
    src/types.c
    src/compiler.c
    src/debug.c
    src/vm.c
    src/nyx_stdlib.c
    src/manifest.c
    src/repl.c
    src/api.c
)

# Static library
add_library(nyx_lib STATIC ${NYX_SOURCES})
target_include_directories(nyx_lib PUBLIC include src)

# CLI executable
add_executable(nyx tools/nyx.c)
target_link_libraries(nyx nyx_lib)


# Math library
target_link_libraries(nyx m)

# Dynamic loading (dlopen) on Linux/Mac
if(NOT WIN32)
    target_link_libraries(nyx_lib dl)
endif()
