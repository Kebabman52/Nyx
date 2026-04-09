# Set Methods

Sets are unordered collections of unique values. Unlike maps, sets can hold any value type — ints, floats, bools, strings, or a mix.

Create sets with the `set()` function:

```nyx
var s = set(1, 2, 3);
var tags = set("fast", "embeddable", "typed");
var mixed = set(1, "hello", true);
```

## Adding

### `add(value)`

Add an element. No-op if the value already exists.

```nyx
var s = set(1, 2, 3);
s.add(4);    // {1, 2, 3, 4}
s.add(2);    // {1, 2, 3, 4} — no duplicate
```

## Removing

### `remove(value) -> bool`

Remove an element. Returns `true` if it was found and removed, `false` otherwise.

```nyx
var s = set("a", "b", "c");
s.remove("b");    // true  → {a, c}
s.remove("z");    // false → {a, c}
```

### `clear()`

Remove all elements.

```nyx
var s = set(1, 2, 3);
s.clear();
print(s.len());  // 0
```

## Querying

### `contains(value) -> bool`

Check if the set contains a value.

```nyx
var s = set(10, 20, 30);
print(s.contains(20));   // true
print(s.contains(99));   // false
```

You can also use the `in` operator:

```nyx
print(20 in s);    // true
print(99 in s);    // false
```

### `len() -> int`

Number of elements.

```nyx
var s = set("a", "b", "c");
print(s.len());  // 3
```

### `is_empty() -> bool`

Check if the set has no elements.

```nyx
var s = set();
print(s.is_empty());  // true
s.add(1);
print(s.is_empty());  // false
```

## Supported value types

Sets support all Nyx value types. Equality is checked by value — two ints with the same number are the same element.

```nyx
var s = set(1, "hello", true, 3.14);
print(s.contains(1));        // true
print(s.contains("hello"));  // true
print(s.contains(false));    // false
print(s.len());              // 4
```

Duplicates are removed automatically:

```nyx
var s = set(1, 1, 2, 2, 3, 3);
print(s.len());  // 3
print(s);         // {1, 2, 3}
```
