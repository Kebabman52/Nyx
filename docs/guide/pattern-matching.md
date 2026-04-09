# Pattern Matching

The `match` statement. The thing Lua refuses to add and `if/else` chains can never replace.

## Basic matching

```nyx
match command {
    "start" => { print("Starting..."); },
    "stop"  => { print("Stopping..."); },
    "quit"  => {
        print("Saving state...");
        print("Goodbye!");
    },
    _ => { print("Unknown: " + command); },
}
```

- `_` is the wildcard — matches anything
- Each arm uses `=>` followed by `{ body }`
- Arms are separated by commas
- No fallthrough. Each arm is independent.

## Matching numbers

```nyx
match error_code {
    200 => { print("OK"); },
    404 => { print("Not Found"); },
    500 => { print("Server Error"); },
    _   => { print("Code: " + str(error_code)); },
}
```

## Guards

Add `if condition` after the pattern for extra filtering:

```nyx
fn classify(n: int) -> string {
    match n {
        0 => { return "zero"; },
        n if n > 0 => { return "positive"; },
        _ => { return "negative"; },
    }
    return "";
}
```

Guards are checked only if the pattern matches first. Think of them as an extra condition on top.

## Match as expression

`match` isn't just a statement — it's an expression. Assign the result directly.

```nyx
var label = match code {
    200 => "OK",
    404 => "Not Found",
    _ => "Unknown",
};
print(label);
```

When used as an expression, each arm returns a value instead of executing a block. The trailing semicolon after the closing brace is required (it's a `var` statement). Every arm must produce a value — the compiler won't let you leave one dangling.

Guards work in expressions too — use `_ if condition` for filtered wildcards:

```nyx
var label = match score {
    100 => "perfect",
    _ if score >= 90 => "excellent",
    _ if score >= 70 => "good",
    _ if score >= 50 => "pass",
    _ => "fail",
};
```

You can also combine a value match with a guard:

```nyx
var msg = match code {
    200 => "OK",
    _ if code >= 400 => "Error",
    _ => "Other",
};
```

## Why not switch?

`match` *is* the switch. It does everything a traditional switch/case does — matching on ints, strings, any value — without fallthrough bugs, without forgetting `break`, and with guards that `switch` can't do.

If you came from Lua: you're welcome.
