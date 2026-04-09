# Standard Library

Every function listed here is available globally. No imports needed.

## Core

| Function | Signature | Description |
|----------|-----------|-------------|
| `print(value)` | `any -> nil` | Print to stdout with newline |
| `str(value)` | `any -> string` | Convert to string |
| `len(value)` | `string\|list\|map -> int` | Length / count |
| `type(value)` | `any -> string` | Type name |
| `clock()` | `-> float` | Seconds since program start |

## Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs(x)` | `int\|float -> int\|float` | Absolute value |
| `floor(x)` | `float -> int` | Round down |
| `ceil(x)` | `float -> int` | Round up |
| `round(x)` | `float -> int` | Round to nearest |
| `sqrt(x)` | `float -> float` | Square root |
| `pow(x, y)` | `float, float -> float` | x to the power of y |
| `sin(x)` | `float -> float` | Sine (radians) |
| `cos(x)` | `float -> float` | Cosine (radians) |
| `tan(x)` | `float -> float` | Tangent (radians) |
| `log(x)` | `float -> float` | Natural logarithm |
| `min(a, b)` | `int\|float -> int\|float` | Smaller value |
| `max(a, b)` | `int\|float -> int\|float` | Larger value |
| `clamp(val, lo, hi)` | `int, int, int -> int` | Clamp to range |
| `random()` | `-> float` | Random float in [0, 1) |
| `random_int(lo, hi)` | `int, int -> int` | Random int in [lo, hi) |
| `PI()` | `-> float` | 3.14159... |
| `E()` | `-> float` | 2.71828... |

## String

| Function | Signature | Description |
|----------|-----------|-------------|
| `split(str, delim)` | `string, string -> list` | Split into list |
| `join(list, sep)` | `list, string -> string` | Join list into string |
| `trim(str)` | `string -> string` | Strip whitespace |
| `str_contains(str, sub)` | `string, string -> bool` | Substring check |
| `replace(str, from, to)` | `string, string, string -> string` | Replace all occurrences |
| `starts_with(str, prefix)` | `string, string -> bool` | Prefix check |
| `ends_with(str, suffix)` | `string, string -> bool` | Suffix check |
| `to_upper(str)` | `string -> string` | Uppercase |
| `to_lower(str)` | `string -> string` | Lowercase |
| `substr(str, start, len?)` | `string, int, int? -> string` | Substring |
| `str_repeat(str, n)` | `string, int -> string` | Repeat n times |
| `char_at(str, idx)` | `string, int -> string` | Single character |
| `str_index_of(str, sub)` | `string, string -> int` | Find index (-1 if not found) |
| `parse_int(str)` | `string -> Result` | Parse to int |
| `parse_float(str)` | `string -> Result` | Parse to float |

## IO

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file(path)` | `string -> Result` | Read file contents |
| `write_file(path, content)` | `string, string -> Result` | Write string to file |
| `file_exists(path)` | `string -> bool` | Check if file exists |
| `print_err(msg)` | `string -> nil` | Print to stderr |
| `input(prompt)` | `string -> string` | Read line from stdin |

## Collections

| Function | Signature | Description |
|----------|-----------|-------------|
| `range(start, end)` | `int, int -> list` | Generate [start, end) list |
| `list_repeat(value, n)` | `any, int -> list` | Create list of n copies |
| `flatten(list)` | `list -> list` | Flatten one level |
| `set(...)` | `any... -> set` | Create a set from values (deduped) |

## System

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec(cmd)` | `string -> Result` | Run a shell command, return stdout as Ok or error as Err |
| `load_native(path)` | `string -> Result` | Load a native module (.dll/.so/.dylib). Registers its functions/classes |
| `platform()` | `-> string` | Current OS: `"windows"`, `"linux"`, or `"macos"` |
| `arch()` | `-> string` | Current architecture: `"x64"`, `"arm64"`, or `"x86"` |

## Conversions

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_int(value)` | `any -> int` | Convert to int (strings with `0x` prefix are parsed as hex) |
| `to_float(value)` | `any -> float` | Convert to float |

## Error Handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `Ok(value)` | `any -> Result` | Create success result |
| `Err(value)` | `any -> Result` | Create error result |
| `panic(msg)` | `string -> never` | Crash with message |
| `is_ok(result)` | `Result -> bool` | Check if Ok |
| `is_err(result)` | `Result -> bool` | Check if Err |
| `unwrap(result)` | `Result -> any` | Get value or panic |
| `unwrap_or(result, default)` | `Result, any -> any` | Get value or default |
