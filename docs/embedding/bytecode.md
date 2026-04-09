# Bytecode Compilation

Ship compiled bytecode instead of source. Your users don't need to see your code, and startup is faster since there's no compilation step.

## Compiling a file

```bash
nyx compile script.nyx -o script.nyxc
```

The `.nyxc` file contains serialized bytecode — opcodes, constants, function definitions. No source code.

## Running compiled bytecode

```bash
nyx run script.nyxc
# or just:
nyx script.nyxc
```

Nyx auto-detects `.nyxc` files and runs them directly.

## Building a project

```bash
nyx build ./src -o app.nyxc
```

This compiles `src/main.nyx` as the entry point into a single `.nyxc` bundle. All imports are recursively resolved and bundled — the `.nyxc` is fully self-contained. Delete the source files, ship one file.

If the project has a `bin/` directory with native libraries (`.dll`/`.so`/`.dylib`), those get embedded too:

```
my_project/
├── main.nyx
├── utils.nyx
└── bin/
    ├── audio-windows-x64.dll
    └── audio-linux-x64.so
```

```bash
nyx build ./my_project -o app.nyxc
# Bundled 2 modules, 2 native binaries
```

At runtime, Nyx extracts the matching native binary for the current platform and loads it automatically.

## From C

```c
// Compile
nyx_compile_to_file("script.nyx", "script.nyxc");

// Run
nyx_init();
nyx_run_compiled("script.nyxc");
nyx_free();
```

## .nyxc format

Binary format, not human-readable. Structure:

```
[4 bytes]  Magic: "NYX\0"
[3 bytes]  Version: major, minor, patch
[2 bytes]  Native binary count (uint16)
For each native binary:
  [2+N]    Platform tag ("windows-x64", "linux-arm64", etc.)
  [2+N]    Filename
  [4+N]    Binary data (raw bytes)
[2 bytes]  Module count (uint16)
For each module:
  [2+N]    Module name
  [serialized function]
    - Name, arity, upvalue count
    - Bytecode array
    - Line number array
    - Constant pool (recursive for nested functions)
```

Native binaries are loaded before any module executes, so native functions and classes are available immediately.

## CLI reference

`nyx --help` gives you the full picture:

```
nyx                    Start the REPL
nyx <script.nyx>       Run a script
nyx <file.nyxc>        Run compiled bytecode (auto-detected)
nyx compile <f> -o <o> Compile to bytecode
nyx build <dir> -o <o> Build project
nyx run <file.nyxc>    Execute bytecode
nyx --help             Show usage
nyx --version          Show version
```

## When to use it

- **Distribution** — ship `.nyxc` instead of `.nyx` source
- **Faster startup** — skip the compile step for large scripts
- **Obfuscation** — bytecode isn't source, though it's not encryption either. Don't rely on it for security.

## Round-trip guarantee

Compiled output is identical to source execution. If `script.nyx` produces output X, then `script.nyxc` produces output X. Always. This is tested in CI.
