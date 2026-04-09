# C API Reference

Everything in `nyx.h`. This is the complete public API.

## Lifecycle

```c
void      nyx_init(void);           // Initialize the VM
void      nyx_free(void);           // Destroy the VM, free all memory
NyxResult nyx_do_string(const char* source);  // Compile and run source
NyxResult nyx_do_file(const char* path);      // Read, compile, and run a file
```

`NyxResult` is one of:
- `NYX_OK` — everything worked
- `NYX_COMPILE_ERROR` — syntax or type error
- `NYX_RUNTIME_ERROR` — runtime error (division by zero, undefined variable, etc.)

## Stack API

Nyx uses a stack to pass values between C and Nyx. Push values before calling, read results after.

### Pushing values

```c
void nyx_push_nil(void);
void nyx_push_bool(bool value);
void nyx_push_int(int64_t value);
void nyx_push_float(double value);
void nyx_push_string(const char* str, int length);
void nyx_push_cstring(const char* str);  // null-terminated
```

### Reading values

Index: `0` = stack bottom, `-1` = top.

```c
NyxValType  nyx_type_at(int index);
bool        nyx_is_nil(int index);
bool        nyx_is_bool(int index);
bool        nyx_is_int(int index);
bool        nyx_is_float(int index);
bool        nyx_is_string(int index);

bool        nyx_to_bool(int index);
int64_t     nyx_to_int(int index);
double      nyx_to_float(int index);
const char* nyx_to_cstring(int index);
int         nyx_string_length(int index);
```

### Stack management

```c
void nyx_pop(int count);
int  nyx_stack_size(void);
```

## Calling Nyx from C

```c
bool      nyx_get_global(const char* name);   // push global onto stack
NyxResult nyx_call(int argCount);             // call function at stack[-argCount-1]
```

Push the function, push the arguments, call it. Result ends up on top of the stack.

```c
nyx_do_string("fn add(a: int, b: int) -> int { return a + b; }");

nyx_get_global("add");     // push the function
nyx_push_int(10);          // push arg 1
nyx_push_int(20);          // push arg 2
nyx_call(2);               // call add(10, 20) — result on stack

int64_t result = nyx_to_int(-1);  // read result from top
nyx_pop(1);                        // clean up
printf("Result: %lld\n", result);  // 30
```

Works with closures, native functions, and class constructors:

```c
// Instantiate a class from C
nyx_do_string("class Vec { init(x: int, y: int) { self.x = x; self.y = y; } }");

nyx_get_global("Vec");
nyx_push_int(3);
nyx_push_int(4);
nyx_call(2);   // creates Vec(3, 4) — instance on stack
```

## Registering host functions

```c
void nyx_register_fn(const char* name, NyxNativeFn fn);
```

Register a C function as a global in Nyx. The function receives arguments directly and returns a `NyxValue`.

```c
static NyxValue myRandom(int argCount, NyxValue* args) {
    (void)argCount; (void)args;
    return FLOAT_VAL((double)rand() / RAND_MAX);
}

nyx_register_fn("my_random", myRandom);
```

Now Nyx scripts can call `my_random()` like any other function.

## Registering host classes and methods

```c
void nyx_register_class(const char* name);
void nyx_register_method(const char* className, const char* methodName, NyxNativeFn fn);
```

Register a class, then add native methods to it. For methods, `args[0]` is `self` (the instance).

```c
static NyxValue entityGetHP(int argCount, NyxValue* args) {
    (void)argCount;
    // args[0] is self — the Entity instance
    // Access fields via the Nyx instance's field table
    return INT_VAL(100); // placeholder
}

nyx_register_class("Entity");
nyx_register_method("Entity", "get_hp", entityGetHP);
```

Nyx scripts see it as a normal class:

```nyx
var e = Entity();
print(e.get_hp());    // 100

// Scripters can even subclass host classes
class Boss : Entity {
    fn rage() { print("Boss is angry!"); }
}
```

## GC

```c
void nyx_gc_collect(void);  // Manually trigger garbage collection
```

## Bytecode

```c
int       nyx_compile_to_file(const char* source, const char* output);
int       nyx_build(const char* inputDir, const char* output);
NyxResult nyx_run_compiled(const char* path);
```
