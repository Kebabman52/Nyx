# String Methods

Strings in Nyx are immutable. All methods return new strings — the original is never modified.

```nyx
var s = "Hello, World!";
print(s.to_upper());   // HELLO, WORLD!
print(s);              // Hello, World! (unchanged)
```

## Query

### `len() -> int`

Number of characters.

```nyx
print("hello".len());  // 5
print("".len());        // 0
```

### `contains(sub) -> bool`

Check if the string contains a substring.

```nyx
print("hello world".contains("world"));  // true
print("hello world".contains("xyz"));    // false
print("hello".contains(""));             // true
```

### `starts_with(prefix) -> bool`

```nyx
print("hello".starts_with("hel"));  // true
print("hello".starts_with("xyz"));  // false
```

### `ends_with(suffix) -> bool`

```nyx
print("hello".ends_with("llo"));  // true
print("hello".ends_with("xyz"));  // false
```

### `index_of(sub) -> int`

Find the position of a substring. Returns `-1` if not found.

```nyx
print("hello".index_of("ll"));   // 2
print("hello".index_of("xyz"));  // -1
```

### `is_empty() -> bool`

```nyx
print("".is_empty());       // true
print("hello".is_empty());  // false
```

## Transform

### `to_upper() -> string`

```nyx
print("hello".to_upper());  // HELLO
```

### `to_lower() -> string`

```nyx
print("HELLO".to_lower());  // hello
```

### `trim() -> string`

Remove whitespace from both ends.

```nyx
print("  hello  ".trim());  // hello
```

### `reverse() -> string`

```nyx
print("abc".reverse());  // cba
print("".reverse());     // (empty)
```

### `repeat(n) -> string`

```nyx
print("ha".repeat(3));  // hahaha
```

## Extract

### `char_at(index) -> string`

Get a single character by index. Also available via `[]` indexing.

```nyx
print("hello"[0]);           // h
print("hello"[-1]);          // o
print("hello".char_at(2));   // l
```

### `substr(start, length?) -> string`

Extract a substring. If `length` is omitted, returns everything from `start` to the end.

```nyx
print("hello".substr(1, 3));  // ell
print("hello".substr(2));     // llo
```

### `split(delimiter) -> list`

Split into a list of strings.

```nyx
var parts = "a,b,c".split(",");
print(parts);  // [a, b, c]
print(parts.len());  // 3
```

## Replace

### `replace(from, to) -> string`

Replace all occurrences.

```nyx
print("hello world".replace("world", "nyx"));  // hello nyx
print("aaa".replace("a", "b"));                // bbb
```

## Indexing

Strings support integer indexing, including negative indices:

```nyx
var s = "hello";
print(s[0]);    // h
print(s[-1]);   // o
print(s[1]);    // e
```

## Interpolation

Embed expressions directly in strings with `${}`:

```nyx
var name = "Nyx";
var x = 10;
print("Hello ${name}!");                   // Hello Nyx!
print("${x} doubled is ${x * 2}");        // 10 doubled is 20
print("upper: ${name.to_upper()}");        // upper: NYX
```

Any expression works inside `${}` — arithmetic, method calls, function calls, ternary, even nested quotes. The result is automatically converted to a string.

```nyx
var m = {"key": "value"};
print("got: ${m["key"]}");             // got: value
print("upper: ${m["key"].to_upper()}"); // upper: VALUE
```

Nested string literals inside `${}` are fully supported. The interpolation parser tracks quote depth, so `"${m["key"]}"` works exactly as you'd expect.
