# Variables & Types

## Declaring variables

Three keywords. Pick one.

```nyx
var x = 42;              // type inferred — it's an int
let y: string = "hello"; // type explicit — must match
const PI = 3.14159;      // constant — can't reassign
```

- `var` — infers the type from whatever you assign. Most common.
- `let` — explicit type annotation. Use when you want to be clear.
- `const` — immutable. Must be initialized. Compiler yells if you try to reassign.

## Types

| Type | Example | Notes |
|------|---------|-------|
| `int` | `42`, `-1`, `0`, `0xFF` | 64-bit signed (small values NaN-boxed, large values heap-allocated). Supports hex literals: `0xFF`, `0xDEADBEEF` |
| `float` | `3.14`, `0.5` | 64-bit IEEE 754 double |
| `bool` | `true`, `false` | |
| `string` | `"hello"` | Immutable, UTF-8 |
| `nil` | `nil` | Absence of value |
| `list` | `[1, 2, 3]` | Dynamic array |
| `map` | `{"a": 1}` | Hash map (string keys) |
| `fn` | `fn(x: int) -> int` | First-class function |
| `Result` | `Ok(42)`, `Err("no")` | For error handling |

**A note on int range:** Nyx integers are full 64-bit signed. Small values that fit in 48 bits are NaN-boxed for performance (no allocation). Values outside that range are automatically heap-allocated — seamless, no special syntax. You get the full ±9.2 quintillion range. Hex literals (`0xFF`, `0xDEADBEEF`) are supported anywhere an integer is expected.

## Type inference

`var` figures out the type from the right side. No magic, no surprises.

```nyx
var age = 25;            // int
var price = 9.99;        // float
var name = "Nyx";        // string
var active = true;       // bool
var items = [1, 2, 3];   // list
```

## Type checking

The compiler catches mismatches at compile time. No waiting until runtime to find out you put a string where an int should be.

```nyx
let x: int = "hello";   // Error: Type mismatch in 'let' declaration.
let y: string = 42;     // Error: Type mismatch in 'let' declaration.

fn add(a: int, b: int) -> int {
    return "oops";       // Error: Return type mismatch.
}
```

## Int-to-float promotion

Ints promote to floats automatically. Nothing else converts implicitly.

```nyx
let f: float = 42;      // fine — int promotes to float
var result = 1 + 2.0;   // result is float (3.0)
```

## Nullable types

Append `?` to make a type nullable. Without it, you can't assign `nil`.

```nyx
var maybe: int? = nil;   // allowed
var nope: int = nil;     // Error
```

## Compound assignment

Shorthand for updating a variable in-place.

```nyx
var score = 0;
score += 10;   // score is now 10
score -= 3;    // 7
score *= 2;    // 14
score /= 7;    // 2
score %= 2;    // 0
```

Works with `+=`, `-=`, `*=`, `/=`, `%=`. Left side must be an existing variable.

## Increment / Decrement

`++` and `--` bump a variable by 1. Statement-only — don't try to use them inside expressions.

```nyx
var i = 0;
i++;   // 1
i++;   // 2
i--;   // 1
```

## String operations

Strings concatenate with `+`. Non-string values auto-convert.

```nyx
var msg = "score: " + 42;        // "score: 42"
var full = "hello" + " " + "nyx"; // "hello nyx"
```

### Multi-line strings

Triple-quote syntax for strings that span multiple lines. No escape gymnastics.

```nyx
var poem = """
Roses are red
Nyx is fast
""";
```

Leading and trailing newlines are included as-is. Use these for templates, SQL, multi-line messages — anything where `\n` makes your eyes bleed.

### String interpolation

Embed expressions directly in strings with `${}`. Cleaner than concatenation.

```nyx
var name = "Nyx";
print("Hello ${name}!");          // Hello Nyx!

var x = 42;
print("Value: ${x}");            // Value: 42
print("Double: ${x * 2}");       // Double: 84
```

Any expression works inside `${}`, including nested quotes like `"${m["key"]}"`. The result is converted to a string automatically.

Escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`, `\0`.

## Null-safe chaining

The `?.` operator lets you access fields or call methods on a value that might be `nil`. If the left side is `nil`, the whole expression short-circuits to `nil` instead of crashing.

```nyx
var obj = nil;
print(obj?.x);        // nil (no crash)
print(obj?.method()); // nil
```

Chain as deep as you want. If any link in the chain is `nil`, you get `nil` back — no runtime error, no drama.

```nyx
var result = user?.profile?.email;  // nil if any part is nil
```

## Enums

Named integer constants grouped under a type. Values start at 0 and increment.

```nyx
enum Direction { UP, DOWN, LEFT, RIGHT }
print(Direction["UP"]);    // 0
print(Direction["RIGHT"]); // 3
```

Access variants by string key. That's it — no methods, no associated values, just clean named constants. Use them for state machines, flags, option sets.

## Keyword operators

You can use words instead of symbols. Both work. Mix them. Nobody cares.

```nyx
// These are identical:
if (x == 40 && y != 35) { ... }
if (x is 40 and y is not 35) { ... }

// Full table:
// and  = &&        or   = ||       not  = !
// is   = ==        is not = !=
// in   (membership)     not in (negated membership)
```
