# Modules

## Importing

```nyx
import utils;
```

This looks for `utils.nyx` in the same directory as the current file, executes it, and makes everything it defines available in your scope.

```nyx
// utils.nyx
fn clamp(val: int, lo: int, hi: int) -> int {
    return max(lo, min(val, hi));
}

const VERSION = "1.0.0";
```

```nyx
// main.nyx
import utils;

print(clamp(150, 0, 100)); // 100
print(VERSION);              // 1.0.0
```

## How resolution works

When you write `import foo`, Nyx looks for `foo.nyx` relative to the file doing the importing. That's it. No package manager, no search paths, no `node_modules` rabbit hole.

## Circular imports

Handled. Each module is loaded once. If `a.nyx` imports `b.nyx` and `b.nyx` imports `a.nyx`, the second import is a no-op.

## String path imports

```nyx
import "lib/helpers";  // loads lib/helpers.nyx
```

## What gets imported

Everything the module defines at the top level — functions, classes, variables, constants. They all land in your global scope.
