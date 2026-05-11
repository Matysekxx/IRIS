# IRIS Language TODO

## Immediate Tasks
- [ ] **Java-style Constructors**: Use class name for constructors instead of `init`.
- [ ] **Constructor Invocation**: Ensure constructors are automatically called upon object instantiation.
- [ ] **Access Modifiers**: Implement `package-private` (default) access modifier in addition to `public` and `private`.
- [ ] **Enhanced Switch**:
    - [ ] Support `case ... -> ...` (single expression/statement).
    - [ ] Support `case ... -> { ... }` (block).
    - [ ] Support traditional Java-style `switch` with fall-through and `break`.

## Performance & Optimizations
- [ ] **Loop Unrolling**: Implement loop unrolling in the compiler.
- [ ] **More Benchmarks**: Add comprehensive benchmarks for classes, objects, and inheritance.
- [ ] **Tail Call Optimization**: Implement TCO for recursive functions.

## Future Ideas
- [ ] **Interoperability**: Enhance hybrid nature by improving integration between different language features.
- [ ] **Standard Library**: Develop a core library (I/O, File System, Networking).
- [ ] **Generics**: Add support for generic types and methods.
- [ ] **Interfaces/Traits**: Implement interface-based polymorphism.
- [ ] **Garbage Collection**: Move from simple reference counting to a more robust GC if needed.
- [ ] **Native Interop**: Easier way to call C/C++ functions from IRIS.
