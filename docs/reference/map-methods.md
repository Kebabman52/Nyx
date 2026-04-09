# Map Methods

All methods are called on a map instance: `myMap.method(args)`.

Maps use string keys and maintain insertion order during iteration.

## Reading and writing

| Method | Returns | Description |
|--------|---------|-------------|
| `get(key)` | `value\|nil` | Get value by key (nil if missing) |
| `set(key, value)` | `nil` | Set or update a key |
| `remove(key)` | `bool` | Delete a key, return whether it existed |

```nyx
var m = {"a": 1, "b": 2};
m.set("c", 3);
print(m.get("c")); // 3
m.remove("a");      // true
print(m.get("a")); // nil
```

You can also use index syntax:

```nyx
m["d"] = 4;
print(m["d"]); // 4
```

## Querying

| Method | Returns | Description |
|--------|---------|-------------|
| `len()` | `int` | Number of entries |
| `contains_key(key)` | `bool` | Check if key exists |
| `contains_value(value)` | `bool` | Check if any key maps to this value |
| `keys()` | `list` | List of all keys (insertion order) |
| `values()` | `list` | List of all values (insertion order) |
| `is_empty()` | `bool` | Check if empty |

## Modifying

| Method | Description |
|--------|-------------|
| `merge(other)` | Copy all entries from another map into this one |
| `clear()` | Remove all entries |

```nyx
var config = {"debug": true};
var defaults = {"debug": false, "verbose": false, "port": 8080};
config.merge(defaults); // config wins for overlapping keys? No — defaults overwrites.
```

::: tip
`merge` overwrites existing keys with the source map's values. If you want the opposite, merge into a copy of defaults instead.
:::
