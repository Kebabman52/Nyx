# Native Modules

Write performance-critical code in C, load it at runtime. Zero overhead — same function pointer dispatch as built-in functions. No marshaling, no JNI-style translation layer.

## How it works

1. You write a C shared library that exports `nyx_module_init`
2. Nyx loads it with `load_native()` and calls your init function
3. Your init registers functions and classes through the API table
4. Nyx scripts call your native code like any other function

## Writing a native module

```c
// sound.c — compile to sound.dll / sound.so / sound.dylib
#include "nyx.h"

// NyxValue is uint64_t (NaN-boxed). Use the macros from value.h
// or define your own matching macros (see example below).

#define QNAN      ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT  ((uint64_t)0x8000000000000000)
#define TAG_INT   ((uint64_t)0x0002000000000000)
#define INT_MASK  ((uint64_t)0x0000FFFFFFFFFFFF)
#define INT_VAL(i) ((NyxValue)(QNAN | SIGN_BIT | TAG_INT | ((uint64_t)(i) & INT_MASK)))

static NyxValue soundPlay(int argCount, NyxValue* args) {
    // args[0] is self (the Sound instance)
    // Your native audio code here
    return INT_VAL(0); // return value
}

// Entry point — called by Nyx when the library is loaded
#ifdef _WIN32
__declspec(dllexport)
#endif
void nyx_module_init(const NyxModuleAPI* api) {
    api->register_class("Sound");
    api->register_method("Sound", "play", soundPlay);
    api->register_fn("beep", someBeepFunction);
}
```

## Compiling

No linking against Nyx needed — the API table is passed by pointer.

```bash
# Windows
gcc -shared -o sound-windows-x64.dll sound.c -I<nyx>/include -DNYX_NAN_BOXING

# Linux
gcc -shared -fPIC -o sound-linux-x64.so sound.c -I<nyx>/include -DNYX_NAN_BOXING

# macOS
gcc -shared -o sound-macos-arm64.dylib sound.c -I<nyx>/include -DNYX_NAN_BOXING
```

## Loading from Nyx

```nyx
// Load the right binary for this platform
load_native("bin/sound-" + platform() + "-" + arch());

// Now Sound class is available
var s = Sound();
s.play();
```

## The NyxModuleAPI table

Your init function receives a pointer to this struct:

```c
typedef struct {
    void (*register_fn)(const char* name, NyxNativeFn fn);
    void (*register_class)(const char* name);
    void (*register_method)(const char* cls, const char* method, NyxNativeFn fn);
} NyxModuleAPI;
```

Use these to register everything your module exposes. Don't call `nyx_register_fn()` directly — use the table.

## NyxNativeFn signature

```c
typedef NyxValue (*NyxNativeFn)(int argCount, NyxValue* args);
```

For standalone functions, `args[0..argCount-1]` are the arguments. For methods, `args[0]` is `self` (the instance).

## Bundling in .nyxc

If your project has a `bin/` directory, `nyx build` automatically embeds any `.dll`/`.so`/`.dylib` files into the `.nyxc` output:

```
my_project/
├── main.nyx
└── bin/
    ├── sound-windows-x64.dll
    ├── sound-linux-x64.so
    └── sound-macos-arm64.dylib
```

```bash
nyx build ./my_project -o app.nyxc
# "Bundled 1 module, 3 native binaries"
```

At runtime, Nyx extracts the binary matching the current platform to a temp directory and loads it automatically. The `.nyxc` is fully self-contained — ship one file, runs everywhere.

Platform is detected from the filename convention: `name-platform-arch.ext`. Supported tags: `windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

## Performance

Native function calls go through the exact same dispatch path as built-in functions. The only overhead is:
- **One-time:** `dlopen`/`LoadLibrary` when loading the module
- **Per-call:** Zero. Same function pointer, same `NyxNativeFn` signature. No marshaling, no type conversion, no wrapper layers.
