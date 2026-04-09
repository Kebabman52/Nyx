# Host Functions

The whole point of embedding: expose your app's functionality to Nyx scripts.

## How it works

Your C application registers functions. Nyx scripts call them like any other function. The bridge is the stack — arguments go in, results come out.

## Registering a class

```c
nyx_register_class("Entity");
```

Now Nyx scripts see `Entity` as a class in their global scope. You can define its behavior by combining this with native functions or letting scripts extend it:

```nyx
// Script can extend your host class
class Boss : Entity {
    init(name: string) {
        self.name = name;
    }
}
```

## Defining behavior with scripts

The simplest approach: register your native functions as globals, and use Nyx scripts to wire them into classes:

```c
// In C: register raw functions
nyx_init();
nyx_do_string(
    "fn _spawn(type, x, y) { /* native impl */ }\n"
    "class Entity {\n"
    "    fn spawn(type: string, x: float, y: float) {\n"
    "        _spawn(type, x, y);\n"
    "    }\n"
    "}\n"
);
```

Then scripts use the clean API:

```nyx
Entity.spawn("zombie", 100.0, 200.0);
```

## What scripters see

From the scripter's perspective, they don't know or care what's implemented in C vs Nyx. They just call functions and use classes. That's the whole idea.

```nyx
// The scripter writes this. They don't know spawn() calls C code.
var enemy = Entity.spawn("dragon", 0.0, 0.0);
Sound.play("roar");
enemy.take_damage(50);
```

## Best practices

1. **Keep the C layer thin** — register low-level primitives, build the nice API in Nyx
2. **Use classes for namespacing** — `Entity.spawn()` not `entity_spawn()`
3. **Return Results for fallible operations** — `Ok(value)` / `Err("message")`
4. **Don't expose raw pointers** — wrap everything in Nyx objects
