# List Methods

All methods are called on a list instance: `myList.method(args)`.

## Adding elements

| Method | Description |
|--------|-------------|
| `push(item)` | Append to end |
| `insert_at(index, item)` | Insert at specific index |
| `insert_before(target, item)` | Insert before target (by index or matching value) |
| `insert_after(target, item)` | Insert after target (by index or matching value) |

```nyx
var list = [1, 3, 5];
list.push(7);              // [1, 3, 5, 7]
list.insert_at(1, 2);     // [1, 2, 3, 5, 7]
list.insert_before(5, 4); // [1, 2, 3, 4, 5, 7]
list.insert_after(5, 6);  // [1, 2, 3, 4, 5, 6, 7]
```

`insert_before` and `insert_after` accept either an integer index or a value to search for. If you pass an int, it's treated as an index. If you pass anything else, it finds the first matching element.

## Removing elements

| Method | Returns | Description |
|--------|---------|-------------|
| `pop()` | `value` | Remove and return last element |
| `shift()` | `value` | Remove and return first element |
| `remove_at(index)` | `value` | Remove by index, return removed |
| `remove(item)` | `bool` | Remove first match, return success |
| `remove_all(item)` | `int` | Remove all matches, return count |
| `clear()` | `nil` | Remove everything |

## Querying

| Method | Returns | Description |
|--------|---------|-------------|
| `len()` | `int` | Number of elements |
| `get(index)` | `value\|nil` | Safe index access (no crash on out of bounds) |
| `first()` | `value\|nil` | First element |
| `last()` | `value\|nil` | Last element |
| `contains(item)` | `bool` | Check if value exists |
| `index_of(item)` | `int` | Index of first match (-1 if not found) |
| `is_empty()` | `bool` | Check if empty |

## Transforming

| Method | Description |
|--------|-------------|
| `sort()` | Sort in place (numbers and strings) |
| `reverse()` | Reverse in place |
| `slice(start, end)` | Return new sub-list [start, end) |

## Eviction

| Method | Description |
|--------|-------------|
| `set_max(n)` | Set max size. Oldest entries are evicted when the list exceeds this limit. |

```nyx
var log = [];
log.set_max(100); // keep only the last 100 entries
```
