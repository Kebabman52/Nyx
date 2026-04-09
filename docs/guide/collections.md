# Collections

## Lists

Dynamic arrays. They grow, they shrink, they do what you tell them.

```nyx
var nums = [1, 2, 3, 4, 5];
print(nums[0]);     // 1
print(nums[-1]);    // 5 (negative index = from end)
nums[2] = 99;       // index assignment
print(len(nums));   // 5
```

### List methods

Every method you wish Lua had.

```nyx
var items = [3, 1, 4, 1, 5];

// Add
items.push(9);                   // append to end
items.insert_at(0, 0);          // insert at index
items.insert_before(4, 3);      // insert 3 before value 4
items.insert_after(4, 6);       // insert 6 after value 4

// Remove
items.pop();                     // remove + return last
items.shift();                   // remove + return first
items.remove_at(2);             // remove by index
items.remove(1);                // remove first match by value
items.remove_all(1);            // remove all matches

// Query
items.contains(5);              // true/false
items.index_of(5);              // index or -1
items.first();                  // first element
items.last();                   // last element
items.get(99);                  // safe get — returns nil if out of bounds
items.is_empty();               // true/false

// Transform
items.sort();                   // sort in place
items.reverse();                // reverse in place
items.slice(1, 3);             // return sub-list [1..3)
items.clear();                  // remove everything
```

### Eviction

Set a max size. When the list overflows, oldest entries get evicted automatically.

```nyx
var log = [1, 2, 3, 4, 5];
log.set_max(3);    // evicts [1, 2] → [3, 4, 5]
log.push(6);       // evicts 3 → [4, 5, 6]
```

Useful for capped logs, ring buffers, LRU-style caches.

## Maps

String-keyed hash maps with insertion-order iteration.

```nyx
var scores = {"alice": 95, "bob": 87, "charlie": 91};
print(scores["alice"]);  // 95
scores["dave"] = 88;     // add entry
```

### Map methods

```nyx
var m = {"a": 1, "b": 2, "c": 3};

m.get("a");              // 1 (returns nil if missing)
m.set("d", 4);           // add/update
m.remove("b");           // delete key
m.contains_key("a");     // true
m.contains_value(3);     // true
m.keys();                // ["a", "c", "d"] (list)
m.values();              // [1, 3, 4] (list)
m.len();                 // 3
m.is_empty();            // false
m.clear();               // nuke everything

// Merge another map in
var extra = {"e": 5, "f": 6};
m.merge(extra);
```

## Sets

Unordered collections of unique values. Any type works — ints, strings, floats, bools, mixed.

```nyx
var colors = set("red", "green", "blue");
var primes = set(2, 3, 5, 7, 11);
var mixed = set(1, "hello", true, 3.14);
```

Duplicates are ignored automatically:

```nyx
var s = set(1, 1, 2, 2, 3);
print(s.len());  // 3
```

### Set methods

```nyx
var s = set(1, 2, 3);

s.add(4);            // add element (no-op if already present)
s.remove(2);         // remove element → true/false
s.contains(3);       // true
s.len();             // 3
s.is_empty();        // false
s.clear();           // remove all elements
```

The `in` operator works on sets:

```nyx
print(3 in set(1, 2, 3));  // true
```

::: tip Why `set()` and not `{}`?
Curly braces are already used for maps. Since `{"a": 1}` is a map, we can't also use `{1, 2, 3}` for sets without ambiguity. The `set()` function keeps things clear.
:::

## Ranges

Ranges are lightweight objects representing a sequence of integers. They don't allocate a list — they're just a start and end.

```nyx
var r = 0..10;          // range from 0 to 9
print(type(r));          // range
print(r[0]);             // 0
print(r[4]);             // 4
print(5 in r);           // true
print(10 in r);          // false (exclusive end)
```

Ranges are most commonly used in `for` loops:

```nyx
for i in 0..5 {
    print(i);   // 0, 1, 2, 3, 4
}
```

You can also convert a range to a list with its `.to_list()` method.

## Membership testing

The `in` and `not in` operators work on lists, maps, strings, ranges, and sets.

```nyx
print(3 in [1, 2, 3]);          // true (list)
print("alice" in scores);        // true (map — checks keys)
print("hello" in "hello world"); // true (substring check)
print(5 in 0..10);              // true (range)
print("red" in colors);          // true (set)
print(5 not in [1, 2, 3]);      // true
```

## Iteration

```nyx
// List
for item in [10, 20, 30] {
    print(item);
}

// List with index
for i, item in [10, 20, 30] {
    print(str(i) + ": " + str(item));  // 0: 10, 1: 20, 2: 30
}

// Map (key + value)
for key, value in {"a": 1, "b": 2} {
    print(key + " = " + str(value));
}

// Range
for i in 0..5 {
    print(i); // 0, 1, 2, 3, 4
}
```
