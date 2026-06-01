# Roadmap: Defeating LuaJIT

LuaJIT is arguably the fastest dynamic language implementation in the world. It achieves its speed through a highly tuned written-in-assembly interpreter and a highly advanced Tracing JIT compiler. To close the gap and eventually rival LuaJIT, IRIS needs to implement aggressive optimizations across its compiler, VM, and JIT infrastructure.

Here are 20 advanced optimization strategies to consider:

## Tracing & JIT Advancements
1.  **Tracing JIT Integration**: Transition from a pure Method JIT to a Tracing JIT (or a hybrid). Tracing JITs record hot loops across function boundaries, generating highly optimized linear assembly paths that eliminate dispatch overhead.
2.  **Trace Stitching and Side-Exits**: Optimize side-exits from traces. Instead of falling back to the interpreter immediately on a guard failure, stitch traces together so that common side-paths are also compiled into machine code.
3.  **Register Allocation (Linear Scan / Graph Coloring)**: Instead of a static mapping of the first 5 VM registers to physical x86 registers, implement a dynamic register allocator (like Linear Scan) within the JIT to utilize all 15 available GPRs optimally across the entire trace.
4.  **Loop Unrolling (JIT Level)**: Detect tight loops during tracing and unroll them dynamically in the emitted machine code to reduce branch overhead and pipeline stalls.
5.  **Type Specialization & Guard Elision**: When tracing, the JIT knows the exact types of variables. Emit highly specialized machine code (e.g., pure integer math) and hoist type guards out of loops so they are only checked once per trace iteration.

## Data Structures & Memory
6.  **Polymorphic Inline Caches (PICs)**: Enhance the current inline caching for object field access to support polymorphism (multiple class types at the same call site) without falling back to a slow hash map lookup.
7.  **Hidden Classes (Shapes)**: Implement V8-style Hidden Classes for objects. This allows dynamic objects to share structural blueprints, making field access an O(1) array lookup instead of a hash map lookup.
8.  **Object Allocation Sinking**: Use escape analysis in the JIT to determine if an object never leaves a function/trace. If it doesn't escape, allocate it entirely in CPU registers or on the stack, bypassing the heap and GC entirely.
9.  **NaN-Tagging Optimizations**: We already use NaN-tagging, but we can optimize the bitwise extraction. Ensure that type checking and value extraction are done using minimal x86 instructions (e.g., fast bit shifts and tests).
10. **Pointer Compression**: If operating in a 64-bit environment, compress pointers to 32 bits where possible to increase CPU cache density and reduce memory bandwidth pressure.

## Bytecode & Interpreter Enhancements
11. **Super-instructions (Macro-ops)**: Analyze bytecode patterns and combine frequent instruction pairs (e.g., `OP_LT` followed by `OP_JMPF`) into single "super-instructions" (e.g., `OP_JMPF_LT`) to reduce dispatch overhead.
12. **Direct Threaded Code**: If the interpreter is still used for cold code, ensure it uses Computed Gotos (Direct Threaded Code) rather than a giant `switch` statement for O(1) instruction dispatch. *(Note: IRIS already attempts this, but ensure it's fully utilized).*
13. **Bytecode Verification Elision**: Trust the internal compiler. Skip redundant bounds checks and type checks in the JIT if the bytecode compiler can statically guarantee safety.

## Mathematical & Array Operations
14. **Vectorization (SIMD / AVX2)**: Detect array operations in loops (like in `03_array_ops`) and automatically emit SIMD (AVX2/SSE) instructions in the JIT to process 4-8 elements per CPU cycle.
15. **Bounds Check Elimination (BCE)**: When iterating over arrays (e.g., `for i in 0..arr.len`), hoist the bounds check outside the loop so that the inner loop performs unchecked, raw memory access.
16. **Typed Arrays via Type Inference**: Statically infer when an array contains *only* integers or *only* doubles, and compile them down to raw C-style arrays under the hood, eliminating NaN-tag unpacking on every access.

## Call Overheads
17. **Function Inlining**: The compiler or JIT should automatically inline small, frequently called functions (like `Math.abs` or simple getters) to eliminate the overhead of pushing/popping call frames.
18. **Zero-Cost Exceptions**: Instead of checking for exception state after every call, use a table-driven exception handling mechanism (like C++ `try/catch` implementation via `.pdata`/`.xdata`) so the "happy path" has zero overhead.
19. **Fast Native Calls (FFI)**: Currently, `OP_CALL_NATIVE` has high overhead pushing/popping to the VM stack. Implement a fast-path FFI that allows JITted code to call C++ functions directly using the System V / Windows x64 ABI calling convention.

## Concurrency
20. **Lock-Free Allocator**: Replace the standard `malloc`/`new` with a custom thread-local, lock-free bump allocator for extremely fast, contention-free object creation.
