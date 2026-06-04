# IRIS Syntax Specification

IRIS is a hybrid language designed for extreme performance.

## Variables and Constants
```kotlin
var x = 10;       // Mutable
val y = 20;       // Immutable
var z: int = 30;  // Typed
```

## Operators
- Arithmetic: `+`, `-`, `*`, `/`, `%`, `++`, `--`
- Bitwise: `&`, `|`, `^`, `<<`, `>>`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`

## Control Flow
- `if (cond) { ... } else { ... }`
- `while (cond) { ... }`
- `for (init; cond; incr) { ... }`
- `repeat (count) { ... }`

## Functions
```kotlin
fun name(arg: type): retType {
    return val;
}
```

## Classes
```kotlin
class Name {
    var field = 0;
    fun method() { ... }
}
```

## Native Interop
```kotlin
from "Module" import native "Entity" as alias;
```
