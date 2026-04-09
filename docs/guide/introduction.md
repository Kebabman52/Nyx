# Introduction

<CopyForLLM />

Nyx is an embeddable scripting language built by [Nemesis Security](https://nemesistech.ee). It's written in C11, runs on a bytecode VM, and is designed to be dropped into host applications with zero friction.

We built it because every existing option had something that drove us insane. Lua forgot that switch statements exist. Python forgot what speed is. JavaScript... let's not go there.

Nyx has the syntax you'd expect from a modern language, near-Lua speed (2.7x off Lua 5.5 overall, OOP is 23% *faster* than Lua), and doesn't make you choose between "nice to write" and "nice to embed."

## What you get

- **Static typing with inference** — types when you want them, `var` when you don't
- **Classes with inheritance** — `self`, `super`, `init`, single inheritance
- **First-class functions and closures** — because it's not 1995
- **Pattern matching** — real `match` with guards, not a chain of `if/else`
- **Collections** — lists (25+ methods), maps (12+ methods), sets, ranges
- **String methods** — `.reverse()`, `.split()`, `.to_upper()`, `.contains()`, and more
- **String interpolation** — `"${x + 1}"` with full expression support
- **Coroutines** — `yield` and `resume`, cooperative multitasking
- **Error handling** — `Result` types with `Ok`/`Err` and `?` propagation
- **break / continue** — loop control that works in `while`, `for`, and `loop`
- **Braceless control flow** — single-statement `if`/`while`/`for` without braces
- **Modules** — `import` and you're done
- **Bytecode compilation** — `nyx build` compiles to `.nyxc`, ship without source

## What it looks like

```nyx
class Enemy {
    init(name: string, hp: int) {
        self.name = name;
        self.hp = hp;
    }

    fn take_damage(amount: int) -> bool {
        self.hp = self.hp - amount;
        if (self.hp <= 0) {
            print(self.name + " is dead.");
            return true;
        }
        print(self.name + " takes " + str(amount) + " damage. HP: " + str(self.hp));
        return false;
    }
}

var enemies = [
    Enemy("Goblin", 30),
    Enemy("Dragon", 200),
    Enemy("Skeleton", 15),
];

for enemy in enemies {
    match enemy.name {
        "Dragon" => { enemy.take_damage(999); },
        _ => { enemy.take_damage(20); },
    }
}
```

No ceremony. No boilerplate. Write what you mean.

## Who it's for

**Scripters** — if you're writing game logic, config scripts, automation, or plugins for an app that embeds Nyx, start with the [language guide](/guide/hello-world).

**Developers** — if you're building an app and want to add scripting support, check the [embedding guide](/embedding/getting-started). It's a C header and a static library. That's the entire dependency.
