# Embedding Nyx

You have an application. You want users to script it. Here's how.

## The setup

Nyx is a C11 static library. One header, one library file, zero dependencies.

```
include/nyx.h       ← the only header you need
build/libnyx_lib.a   ← link against this
```

## Minimal example

```c
#include "nyx.h"

int main() {
    nyx_init();
    nyx_do_string("print(\"Hello from Nyx!\");");
    nyx_free();
    return 0;
}
```

Compile:

```bash
gcc -o myapp myapp.c -Inyx/include -Lnyx/build -lnyx_lib -lm
```

Run:

```bash
$ ./myapp
Hello from Nyx!
```

That's a working embedded scripting engine in 5 lines of C.

## Running scripts from files

```c
nyx_init();
nyx_do_file("scripts/init.nyx");
nyx_do_file("scripts/main.nyx");
nyx_free();
```

## Running compiled bytecode

```c
nyx_init();
// Execute a pre-compiled .nyxc file (no source needed)
nyx_run_compiled("scripts/main.nyxc");
nyx_free();
```

## What happens under the hood

1. `nyx_init()` — creates the VM, registers built-in functions
2. `nyx_do_string()` / `nyx_do_file()` — compiles source to bytecode, executes on the VM
3. Everything shares one global scope — functions defined in one call are available in the next
4. `nyx_free()` — tears down the VM, frees all memory

The VM is a global singleton right now. One VM per process. This is the same model Lua uses and it works fine for most use cases.
