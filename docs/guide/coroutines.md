# Coroutines

Cooperative multitasking. A function that can pause itself, return a value, and pick up where it left off when you ask it to.

## Basic coroutine

Any function that uses `yield` becomes a generator. Calling it doesn't run it — it creates a coroutine. You run it with `resume`.

```nyx
fn counter() {
    yield 1;
    yield 2;
    yield 3;
}

var co = counter();
print(resume co); // 1
print(resume co); // 2
print(resume co); // 3
```

## Infinite generators

The classic use case — produce values on demand without computing them all upfront.

```nyx
fn fibonacci() {
    var a = 0;
    var b = 1;
    while (true) {
        yield a;
        var temp = a;
        a = b;
        b = temp + b;
    }
}

var fib = fibonacci();
print(resume fib); // 0
print(resume fib); // 1
print(resume fib); // 1
print(resume fib); // 2
print(resume fib); // 3
print(resume fib); // 5
```

The coroutine suspends at each `yield`, preserving all local variables. When you `resume`, it continues right where it stopped.

## Generators with parameters

```nyx
fn range_gen(start: int, end_val: int) {
    var i = start;
    while (i < end_val) {
        yield i;
        i = i + 1;
    }
}

var r = range_gen(10, 15);
print(resume r); // 10
print(resume r); // 11
print(resume r); // 12
```

## Multiple independent coroutines

Each coroutine has its own stack. They don't interfere with each other.

```nyx
var r1 = range_gen(0, 3);
var r2 = range_gen(100, 103);

print(resume r1); // 0
print(resume r2); // 100
print(resume r1); // 1
print(resume r2); // 101
```

## Coroutine lifecycle

A coroutine goes through these states:

1. **Created** → you called the generator function
2. **Running** → `resume` is executing it
3. **Suspended** → it hit a `yield`
4. **Dead** → it returned (function ended)

Resuming a dead coroutine returns `nil`.

## Return values

If a generator function uses `return`, the final `resume` gets that value:

```nyx
fn finite() {
    yield 10;
    yield 20;
    return 30;
}

var f = finite();
print(resume f); // 10
print(resume f); // 20
print(resume f); // 30 (the return value)
print(resume f); // nil (dead)
```
