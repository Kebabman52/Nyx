# Control Flow

## Ternary operator

Inline conditional. One expression, two outcomes.

```nyx
var status = hp > 0 ? "alive" : "dead";
var abs = x >= 0 ? x : -x;
```

`condition ? then : else` — evaluates the condition, returns one side. Use it when an `if/else` would be overkill. Don't nest them unless you hate whoever reads your code next.

## if / else

Parentheses around the condition. Braces around the body — or not. Single statements don't need braces.

```nyx
if (x > 0) {
    print("positive");
} else if (x is 0) {
    print("zero");
} else {
    print("negative");
}
```

Braces are optional for single statements:

```nyx
if (x > 5) print("big");
if (x > 0) print("yes");
else print("no");
```

## while

```nyx
var i = 0;
while (i < 10) {
    print(i);
    i = i + 1;
}
```

Braceless works here too:

```nyx
while (running) tick();
```

## for-in (ranges)

```nyx
for i in 0..10 {
    print(i); // 0 through 9
}
```

Braceless:

```nyx
for i in 0..10 print(i);
```

## for-in (collections)

```nyx
var names = ["Alice", "Bob", "Charlie"];
for name in names {
    print(name);
}
```

Two-variable form for maps:

```nyx
var scores = {"alice": 95, "bob": 87};
for key, value in scores {
    print(key + ": " + str(value));
}
```

### Indexed for-each

Need the index too? Two-variable form works on lists — gives you both the position and the value.

```nyx
for i, item in ["a", "b", "c"] {
    print(str(i) + ": " + item);  // 0: a, 1: b, 2: c
}
```

Index starts at 0. For maps, the two-variable form gives key/value (shown above). For lists, it gives index/value.

## loop

Infinite loop. Use `break` to escape.

```nyx
var n = 0;
loop {
    n = n + 1;
    if (n > 5) { break; }
    print(n);
}
```

## break / continue

`break` exits the loop. `continue` skips to the next iteration. Both work in `while`, `for`, and `loop`.

```nyx
// break exits the loop
for i in 0..100 {
    if (i is 5) break;
}

// continue skips to next iteration
for i in 0..10 {
    if (i % 2 is not 0) continue;
    print(i); // only even numbers
}
```

## match

The switch statement you always wanted. See [Pattern Matching](/guide/pattern-matching) for the full breakdown.

```nyx
match command {
    "start" => { print("Starting..."); },
    "stop"  => { print("Stopping..."); },
    _       => { print("Unknown command"); },
}
```
