# IRIS Massive Optimization Plan

This document outlines the strategic roadmap for optimizing the IRIS programming language to achieve performance parity with state-of-the-art virtual machines and JIT compilers like LuaJIT, V8, and PyPy.

## Phase 1: Virtual Machine & Bytecode Enhancements
1. **Direct Threaded Code (Computed Gotos)**: Pre-decode bytecodes into arrays of label pointers (jump tables) to eliminate the overhead of `switch` dispatch.
2. **Polymorphic Inline Caches (PICs)**: Introduce inline caching for property access (`OP_GET_FIELD`, `OP_SET_FIELD`) and method calls to bypass hash map lookups on hot paths.
3. **Instruction Fusion (Super-instructions)**: Identify common bytecode sequences (e.g., `LOADINT` followed by `ADD`) and fuse them into single specialized instructions to reduce dispatch overhead.
4. **Register Windowing Optimization**: Refine the register-based architecture to minimize memory traffic during function calls.

## Phase 2: JIT Compilation (via AsmJit)
1. **Method JIT / Tracing JIT**: Identify hot functions or loops via execution counters.
2. **Machine Code Generation**: Compile hot IRIS bytecode directly to x86_64 assembly using AsmJit.
3. **Register Allocation**: Map IRIS virtual registers to physical x86_64 registers (e.g., `RAX`, `XMM0` for doubles) for zero-memory-access computations.
4. **Type Speculation & Guarding**: JIT code will assume variable types based on history. If a type changes (guard fails), it deoptimizes back to the VM.

## Phase 3: Memory Management (Replacing RefCounting)
1. **Tracing Garbage Collector**: Reference counting introduces immense overhead on every assignment and parameter passing. Replace it with a precise, generational Mark-and-Sweep or Copying Garbage Collector.
2. **Object Layout**: Optimize memory layout of `Value` and `ObjectData` to improve CPU cache locality (e.g., struct-of-arrays vs array-of-structs for properties).

## Phase 4: Parser and Compiler (AOT) Optimizations
1. **Arena Allocation for AST**: Use a memory arena for AST nodes to eliminate thousands of small `malloc`/`free` calls during parsing.
2. **Advanced Static Analysis**: Implement SSA (Static Single Assignment) form in the compiler to enable advanced constant folding, dead code elimination, and loop-invariant code motion.

## Execution Strategy
We will utilize OpenCode/Antigravity agents to systematically tackle these phases, starting with Phase 1 (VM enhancements).
