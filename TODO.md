# IRIS Language TODO

## Immediate Tasks
- [x] **Java-style Constructors**: Use class name for constructors instead of `init`.
- [x] **Constructor Invocation**: Constructors are automatically called upon object instantiation.
- [x] **Access Modifiers**: Implemented `public`, `private`, and `package-private` (default).
- [x] **Enhanced Switch**:
    - [x] Support `case ... -> ...` (single expression/statement).
    - [x] Support `case ... -> { ... }` (block).
    - [x] Support traditional Java-style `switch` with fall-through and `break`.
    - [x] Support `switch` as an expression (`val x = switch(...) { ... }`).
- [ ] **Abstraction Enforcement**:
    - [ ] Prevent instantiation of `abstract` classes.
    - [ ] Ensure all `abstract` methods are implemented in non-abstract subclasses.

## Planned Features & Ideas
- [ ] **Operator Overloading**: Allow classes to define custom behavior for `+`, `-`, `*`, `/`, etc.
- [ ] **Properties with Getters/Setters**: Kotlin-style properties (`val name: String get() = ...`).
- [ ] **Extension Functions**: Add methods to existing classes without inheritance.
- [ ] **Pattern Matching**: Expand `switch` into full pattern matching (destructuring, type patterns).
- [ ] **Null Safety**: Optional types and null-safe operators (`?.`, `!!`, `?:`).
- [ ] **Coroutines/Async-Await**: Built-in support for asynchronous programming.
- [ ] **Modules & Packages**: Better code organization with `import` and `package` keywords.
- [ ] **Standard Library (StdLib)**:
    - [ ] Collections (List, Map, Set).
    - [ ] I/O (File, Socket, Console).
    - [ ] Json parsing/serialization.
- [ ] **Native Interop (FFI)**: Call C/C++ functions directly with minimal overhead.

## Performance & Optimizations
- [ ] **Loop Unrolling**: Implement loop unrolling in the compiler.
- [ ] **Escape Analysis**: Optimize heap allocations by putting objects on stack when possible.
- [ ] **JIT Compilation**: Transition from bytecode interpreter to a simple JIT compiler (using LLVM or custom).
- [ ] **Tail Call Optimization (TCO)**: Optimize recursive calls.

## Documentation & Tooling
- [ ] **Language Specification**: Write a formal spec for IRIS.
- [ ] **LSP Support**: Language Server Protocol for IDE integration (VS Code, CLion).
- [ ] **Package Manager**: A tool to manage dependencies (like `npm` or `cargo`).
