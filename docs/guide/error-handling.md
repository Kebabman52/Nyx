# Error Handling

No exceptions. No try/catch. Errors are values. You deal with them or you don't — but at least you can see them.

## Result type

Every function that can fail returns a `Result`. A Result is either `Ok(value)` or `Err(value)`.

```nyx
fn divide(a: float, b: float) -> Result {
    if (b == 0.0) {
        return Err("division by zero");
    }
    return Ok(a / b);
}

var result = divide(10.0, 3.0);
print(result);            // Ok(3.33333)
print(result.is_ok());    // true
print(result.unwrap());   // 3.33333
```

## Checking results

```nyx
var r = divide(10.0, 0.0);

r.is_ok();          // false
r.is_err();         // true
r.unwrap();         // PANIC — crashes because it's an Err
r.unwrap_or(0.0);   // 0.0 — safe default
r.unwrap_err();     // "division by zero"
```

## The ? operator

Propagates errors automatically. If the result is `Err`, the current function immediately returns that `Err`. If it's `Ok`, it unwraps the value.

```nyx
fn step1() -> Result {
    return Ok(10);
}

fn step2(x: int) -> Result {
    return Ok(x + 20);
}

fn pipeline() -> Result {
    var a = step1()?;      // unwraps Ok(10) → a = 10
    var b = step2(a)?;     // unwraps Ok(30) → b = 30
    return Ok(b);
}

print(pipeline()); // Ok(30)
```

If any step fails, the error propagates up without touching the rest:

```nyx
fn failing_step() -> Result {
    return Err("something broke");
}

fn pipeline2() -> Result {
    var a = step1()?;           // Ok(10)
    var b = failing_step()?;    // Err → immediately returns Err("something broke")
    return Ok(b);               // never reached
}

print(pipeline2()); // Err(something broke)
```

## panic

For unrecoverable errors. Prints a message and kills the program.

```nyx
panic("this should never happen");
// Output: panic: this should never happen
// Exit code: 1
```

Use it for programmer errors, not for expected failures. Expected failures get `Result`.

## IO functions return Results

File operations use `Result` so you always handle the error case:

```nyx
var content = read_file("config.nyx");
if (content.is_err()) {
    print_err("Failed to read config");
} else {
    print(content.unwrap());
}
```

## parse_int / parse_float

Parsing also returns `Result`:

```nyx
var n = parse_int("42");
print(n.unwrap());        // 42

var bad = parse_int("abc");
print(bad.is_err());      // true
```
