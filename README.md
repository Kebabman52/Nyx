# Nyx

**Every scripting language annoyed us in a different way. None of them could keep up. So we built Nyx.**

Nyx is an embeddable scripting language built by [Nemesis Security](https://nemesistech.ee). Written in C11, runs on a bytecode VM, drops into host applications with zero friction. Lua's speed, Rust's syntax, none of the baggage.

```nyx
class Enemy : Entity {
    init(name: string, hp: int = 100) {
        super.init(name);
        self.hp = hp;
    }

    fn take_damage(amount: int) -> Result {
        if (amount < 0) return Err("negative damage");
        self.hp -= amount;
        if (self.hp <= 0) return Err("${self.name} is dead");
        return Ok(self.hp);
    }
}

var enemies = [Enemy("Goblin", 30), Enemy("Dragon", 200)];

for i, enemy in enemies {
    match enemy.take_damage(50) {
        Ok(hp) => { print("${enemy.name}: ${hp} HP left"); },
        Err(msg) => { print(msg); },
    }
}
```

## Performance

Benchmarked against Lua 5.5 and LuaJIT 2.1 on real-world workloads:

| Benchmark | Nyx 1.0 | Lua 5.5 | LuaJIT 2.1 |
|-----------|---------|---------|------------|
| Game sim (OOP + loops) | 25ms | 16ms | 3ms |
| Error handling (100K ops) | **15ms** | 103ms | 80ms |
| Coroutines (100K yields) | 5ms | 78ms | 3ms |
| Log processing (strings) | 1ms | 1ms | 0ms |

**5x faster than LuaJIT on error handling.** OOP is 23% faster than Lua 5.5 in synthetic benchmarks. Error handling uses `Result` types instead of exceptions — no stack unwinding, no `pcall` overhead.

## Features

- **Static typing with inference** — `var x = 42;` or `let y: string = "hello";` — full 64-bit integers, hex literals (`0xFF`)
- **Classes with inheritance** — `class Dog : Animal { }`, `self`, `super`, `instanceof`, static methods
- **First-class functions & closures** — pass them around, capture variables
- **Lambda expressions** — `|x, y| => x + y`
- **Pattern matching** — `match` as statement or expression, with guards
- **Collections** — lists (25+ methods), maps (12+ methods), sets, ranges
- **String methods** — `"hello".reverse()`, `.split()`, `.to_upper()`, `.contains()`, etc.
- **Coroutines** — `yield` and `resume` with zero-copy stack swapping
- **Error handling** — `Result` types, `Ok`/`Err`, `?` propagation operator
- **Null-safe chaining** — `obj?.field?.method()`
- **String interpolation** — `"hello ${name}"`, `"sum: ${x + y}"`, `"${name.to_upper()}"`
- **Enums** — `enum Color { RED, GREEN, BLUE }`
- **Default params & varargs** — `fn log(level = "INFO", ...msgs)`
- **Modules** — `import utils;`
- **Bytecode compilation** — `nyx build ./src -o app.nyxc`
- **Package manager** — `nyx install http`, version pinning, registry support
- **Generational GC** — young/old generations, write barriers, low-latency collection

## Quick Start

```bash
git clone https://github.com/nemesis-security/nyx.git
cd nyx && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

```bash
./nyx                        # REPL
./nyx script.nyx             # Run a script
./nyx compile f.nyx -o f.nyxc  # Compile to bytecode
./nyx app.nyxc               # Run compiled bytecode
```

## Embed in Your App

One header. One static library. Zero dependencies.

```c
#include "nyx.h"

int main() {
    nyx_init();
    nyx_do_string("print(\"Hello from Nyx!\");");
    nyx_free();
    return 0;
}
```

```bash
gcc -o myapp myapp.c -Inyx/include -Lnyx/build -lnyx_lib -lm
```

## Documentation

Full docs at [nyx.nemesistech.ee](https://nyx.nemesistech.ee) (or run locally with `cd docs && npm run dev`).

- [Language Guide](docs/guide/introduction.md) — syntax, types, classes, collections, everything
- [Embedding Guide](docs/embedding/getting-started.md) — drop Nyx into your C application
- [Standard Library](docs/reference/stdlib.md) — 42+ built-in functions

## Architecture

```
Source (.nyx) → Lexer → Parser → Compiler → Bytecode → VM
```

Stack-based bytecode VM. NaN-boxed values (8 bytes each, large integers heap-allocated). Generational mark-and-sweep GC with write barriers. Computed goto dispatch. Inline method caching. Constant folding. ~200KB binary, ~270KB runtime footprint.

## Tests

```bash
# Run all tests
for f in tests/scripts/phase*.nyx; do ./build/nyx "$f"; done

# 131 edge case assertions
./build/nyx tests/scripts/edge_cases.nyx

# Stability stress test (50K maps, 100K lists, coroutines, etc.)
./build/nyx tests/scripts/stress_test.nyx
```

## Known Issues & Limitations

Being honest about what's not perfect yet.

**Compiler:**
- Constant pool supports up to 65,535 entries per function chunk via `OP_CONSTANT_LONG`. Identifiers (variable names, property names) are limited to 256 unique names per function scope.
- Anonymous functions (`fn() { }`) can't be used inline as expression arguments — use lambdas or named functions.

**GC:**
- The generational GC's write barrier coverage is thorough but hasn't been fuzz-tested. Exotic old→young reference patterns could theoretically leak.

**Types:**
- Enums are syntactic sugar over maps — no enum methods, no associated values.
- `instanceof` (`is`) checks the exact inheritance chain but doesn't support interfaces/traits.

**Syntax:**
- `match` expressions with guards use `_ if condition =>` syntax (no pattern binding like Rust).
- No operator overloading. No generics. No multiple inheritance. By design.

**Embedding:**
- Single global VM instance. No multi-VM support yet.

**Tooling:**
- No debugger, no LSP.

If any of these block you, [open an issue](https://github.com/nemesis-security/nyx/issues). Or fix it yourself — the entire VM is ~3000 lines of C.

## License

Built by [Nemesis Security](https://nemesistech.ee).
