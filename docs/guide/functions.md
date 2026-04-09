# Functions

## Basics

```nyx
fn add(a: int, b: int) -> int {
    return a + b;
}

print(add(3, 4)); // 7
```

- `fn` keyword
- Parameters have types
- `-> type` for the return type (optional — defaults to nil)
- Braces around the body

Functions without a return type return `nil`:

```nyx
fn greet(name: string) {
    print("Hello, " + name + "!");
}
```

## Default parameter values

Parameters can have defaults. If the caller doesn't pass them, the default kicks in.

```nyx
fn greet(name: string = "world") {
    print("Hello, " + name + "!");
}
greet();       // Hello, world!
greet("Nyx");  // Hello, Nyx!
```

Defaults go at the end of the parameter list. You can mix required and default params — required first, defaults after.

## First-class functions

Functions are values. Pass them around like anything else.

```nyx
fn square(x: int) -> int {
    return x * x;
}

fn apply(f: fn, x: int) -> int {
    return f(x);
}

print(apply(square, 5)); // 25
```

## Anonymous functions

Assign a function to a variable. Same syntax, no name.

```nyx
var multiply = fn(a: int, b: int) -> int {
    return a * b;
};

print(multiply(3, 4)); // 12
```

## Lambda expressions

Short-form functions with `|params| => expr`. One expression, no braces, no `return`.

```nyx
var double = |x| => x * 2;
print(double(21)); // 42

var add = |a, b| => a + b;
print(add(3, 4));  // 7
```

Lambdas work great as callbacks:

```nyx
print(apply(|x| => x * x, 5)); // 25
```

They capture variables from the enclosing scope, just like closures:

```nyx
var factor = 3;
var mul = |x| => x * factor;
print(mul(10)); // 30
```

Use lambdas when a full `fn` declaration is more ceremony than the logic deserves.

## Varargs

Collect extra arguments into a list with `...`. The rest parameter must be last.

```nyx
fn sum(...nums) {
    var total = 0;
    for n in nums { total += n; }
    return total;
}
print(sum(1, 2, 3));        // 6
print(sum(10, 20, 30, 40)); // 100
```

Inside the function, `nums` is a regular list. Iterate it, index it, pass it around — whatever you need.

## Closures

Functions capture variables from their enclosing scope. They keep them alive even after the outer function returns.

```nyx
fn make_counter() -> fn {
    var count = 0;
    fn increment() -> int {
        count = count + 1;
        return count;
    }
    return increment;
}

var counter = make_counter();
print(counter()); // 1
print(counter()); // 2
print(counter()); // 3
```

Each call to `make_counter()` creates a new, independent counter. The `count` variable lives on the heap, captured by the closure.

```nyx
var c1 = make_counter();
var c2 = make_counter();
print(c1()); // 1
print(c2()); // 1 — independent
print(c1()); // 2
```

## Recursion

Works like you'd expect.

```nyx
fn factorial(n: int) -> int {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

print(factorial(5)); // 120
```

## Native functions

These are built in and always available:

| Function | What it does |
|----------|-------------|
| `print(value)` | Print to stdout |
| `str(value)` | Convert anything to string |
| `len(value)` | Length of string, list, or map |
| `type(value)` | Type name as string |
| `clock()` | Time in seconds (float) |

See the [standard library reference](/reference/stdlib) for the full list.
