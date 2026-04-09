# Installation

## Quick install

**Windows** (PowerShell):
```powershell
powershell -ExecutionPolicy Bypass -File install.ps1
```

**Linux / macOS** (Terminal):
```bash
curl -sSf https://nyx.nemesistech.ee/install.sh | sh
```

Both scripts download the `nyx` binary, put it in the right place, and add it to your PATH. Open a new terminal and you're good.

::: tip Where does it go?
- **Windows:** `%LOCALAPPDATA%\Nyx\` (e.g., `C:\Users\You\AppData\Local\Nyx\`)
- **Linux/macOS:** `~/.nyx/`

Packages install to `libs/` inside the same directory. Nyx auto-detects its own location at runtime — no environment variables needed.
:::

## Build from source

If you want to compile Nyx yourself:

```bash
git clone https://github.com/nemesis-security/nyx.git
cd nyx
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Requires a C11 compiler (GCC, Clang, MSVC) and CMake 3.16+. No external dependencies. Produces `nyx` (or `nyx.exe` on Windows) and `libnyx_lib.a`.

## Run something

```bash
nyx                         # start the REPL
nyx script.nyx              # run a script
nyx compile script.nyx -o out.nyxc  # compile to bytecode
nyx script.nyxc             # run compiled bytecode
nyx build ./src -o app.nyxc # bundle a project
nyx --profile script.nyx    # run with profiler
```

## CLI reference

```
nyx                         Start the REPL
nyx <script.nyx>            Run a script
nyx <script.nyxc>           Run compiled bytecode
nyx compile <file> -o <out> Compile a file to bytecode
nyx build <dir> -o <out>    Bundle a project (with native libs)
nyx run <file.nyxc>         Run compiled bytecode

nyx install <pkg>           Install package from registry
nyx install <pkg> <ver>     Install specific version
nyx install <git-url>       Install from git URL
nyx uninstall <pkg>         Remove a package
nyx list                    Show installed packages
nyx registry update         Update package registry

--profile                   Show profiler report after execution
--version / -v              Print version
--help / -h                 Print help
```

## Profiler

Add `--profile` before any script to get a performance report:

```bash
nyx --profile my_script.nyx
```

Shows function call counts, time spent per function, self-time (excluding callees), and peak memory usage. Zero overhead when not enabled.
