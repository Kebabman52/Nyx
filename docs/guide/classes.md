# Classes

## Defining a class

```nyx
class Point {
    init(x: int, y: int) {
        self.x = x;
        self.y = y;
    }

    fn distance() -> float {
        return sqrt(to_float(self.x * self.x + self.y * self.y));
    }
}

var p = Point(3, 4);
print(p.distance()); // 5.0
print(p.x);          // 3
```

- `init` is the constructor. It runs when you call the class like a function.
- `self` is the instance. Always explicit, never implicit.
- Fields are created by assigning to `self.whatever` in `init`. No separate declaration needed.
- Methods are declared with `fn` inside the class body.

## Field declarations

You can declare fields with `let` for documentation purposes. They're not enforced yet (that's the type checker's job), but they make intent clear:

```nyx
class Player {
    let name: string;
    let hp: int;
    let alive: bool;

    init(name: string, hp: int) {
        self.name = name;
        self.hp = hp;
        self.alive = true;
    }
}
```

## Inheritance

Single inheritance with `:`.

```nyx
class Animal {
    init(name: string) {
        self.name = name;
    }

    fn speak() -> string {
        return self.name + " makes a sound";
    }
}

class Dog : Animal {
    init(name: string, breed: string) {
        super.init(name);
        self.breed = breed;
    }

    fn speak() -> string {
        return self.name + " barks!";
    }
}

var dog = Dog("Rex", "Shepherd");
print(dog.speak());  // "Rex barks!"
```

- `super.init(...)` calls the parent constructor
- `super.method(...)` calls any parent method
- Methods are overridden by declaring them with the same name
- Inherited methods work automatically — `Dog` gets everything `Animal` has

## Methods as values

Methods are first-class. You can grab them and call them later. They remember which instance they belong to.

```nyx
var bark = dog.speak;
print(bark()); // "Rex barks!" — still bound to dog
```

## Type checking

`type()` returns the class name for instances:

```nyx
print(type(dog));    // "Dog"
print(type(Animal)); // "class"
```

### instanceof

The `is` operator checks whether an object is an instance of a class — and it walks the inheritance chain.

```nyx
var dog = Dog("Rex", "Shepherd");
print(dog is Dog);     // true
print(dog is Animal);  // true (walks inheritance chain)
print(dog is Point);   // false
```

This isn't string comparison on `type()`. It checks the actual class hierarchy, so a `Dog` is also an `Animal`. Use it for polymorphic dispatch, guards, anything where you need to know what you're dealing with.

## Static methods

Any method defined on a class can be called directly on the class itself — no instance required.

```nyx
class Process {
    init(pid: int, name: string) {
        self.pid = pid;
        self.name = name;
    }

    fn open(name: string) -> Process {
        // Factory method — called on the class, not an instance
        return Process(0, name);
    }

    fn info() -> string {
        if (self == nil) {
            return "Process class";
        }
        return "Process(${self.pid}, ${self.name})";
    }
}

var p = Process.open("init");     // static call — self is nil
print(p.info());                  // instance call — self is the instance
print(Process.info());            // static call — self is nil
```

The rules are simple:

- **Called on the class:** `self` is `nil`. The method runs without an instance.
- **Called on an instance:** `self` is the instance, as usual.

This is useful for factory methods, utility functions, and anything where you want the method logically grouped with the class but don't need instance state. No special `static` keyword — the same method works both ways, and you check `self` if you need to distinguish.
