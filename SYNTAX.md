# IRIS Language Syntax & Features

IRIS is a hybrid, high-performance programming language combining the strengths of Java/Kotlin, JavaScript, and Go.

## 1. Variables & Constants
Variables are defined using `var` or `val` (for constants/immutable).
```kotlin
var x = 10          // Mutable integer
val name = "IRIS"   // Immutable string
var y: double = 5.5 // Explicit type
```

## 2. Basic Types
- `int`: 32-bit integer
- `double`: 64-bit float
- `string`: UTF-8 string (with SSO optimization)
- `bool`: true / false
- `any`: Dynamic type

## 3. Control Flow
### If-Else
```kotlin
if (x > 10) {
    print("Greater")
} else {
    print("Smaller or equal")
}
```

### Loops
```kotlin
// Repeat loop
repeat(5) {
    print("Hello")
}

// While loop
while (x > 0) {
    x = x - 1
}

// For loop
for (var i = 0; i < 10; i = i + 1) {
    print(i)
}
```

### Switch (Expression & Statement)
```kotlin
val result = switch(x) {
    1 -> "One"
    2 -> "Two"
    else -> "Other"
}
```

## 4. Functions
Functions support types and return values.
```kotlin
fun add(a: int, b: int): int {
    return a + b
}
```

## 5. Classes & Objects
IRIS uses a class-based system with reference counting.
```kotlin
class Player {
    var health = 100
    val name: string

    fun takeDamage(amount: int) {
        health = health - amount
    }
}

val p = Player()
p.takeDamage(10)
```

## 6. Arrays
Efficiently stored heap arrays.
```kotlin
val arr = int[10]    // Allocation
val arr2 = [1, 2, 3] // Literal
val value = arr2[0]  // Access
```

## 7. Error Handling
```kotlin
try {
    throw "Error!"
} catch (e) {
    print(e)
}
```

## 8. Native Interop (IRIS Bridge)
Importing C++ functions directly.
```kotlin
import native "Math"
val s = Math.sin(1.0)
```

## 9. Special Keywords (Planned/In Progress)
- `static`: For class members and global-like persistence.
- `abstract`: For base classes.
- `enum`: For enumerated types.
