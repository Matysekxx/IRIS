# IRIS Optimization Roadmap

## 1. Complete Whole-Function JIT Compiler

**Current state:** The JIT is partial — it compiles `switch(op)` only for ~50% of opcodes. Functions with unsupported opcodes silently fall back to the interpreter, often producing wrong results because the JIT's OP_RET returns garbage (fixed, but other opcodes may still be wrong).

**What to do:**
- Audit and implement ALL remaining opcodes in `JITCompiler::compile()` (lines 51–200):
  - `OP_GET_FIELD_DBL`, `OP_GET_FIELD_INT` (missing)
  - `OP_IDX_GET/IDX_SET` variants for typed arrays
  - `OP_NEW_ARRAY` with typed elements
  - `OP_CALL` / `OP_CALL_NATIVE` (allows JIT of functions that call other functions)
  - `OP_LOOP` (allows JIT of loops inside JIT-compiled functions)
  - `OP_JLT_INT` / `OP_JGT_INT` / `OP_JNE_INT` (fused compare-and-branch)
  - `OP_LT_K` / `OP_GT_K` (constant-argument comparison)
  - `OP_SHL` / `OP_SHR` / `OP_BIT_XOR` etc.
- Set `callCount >= 1` (compile immediately) once all opcodes are handled.
- **Impact:** ~10–100× speedup on all benchmarks. This is the single highest-impact change.

## 2. Fix & Re-enable Trace JIT with Nested-Loop Support

**Current state:** The trace JIT (compileTrace) handles flat loops well (sieve, fibonacci) but crashes/hangs on nested loops because all `OP_LOOP` instructions jump to a single `loopEntry` label. The `compileTrace` also returns a garbage PC from `emitEpilogue` (fixed, but still unused).

**What to do:**
- Replace the single `loopEntry` label with a per-instruction-PC label map.
- For `OP_LOOP`, compute the actual back-edge target PC and jump to that instruction's label, not always `loopEntry`.
- Fix the side-exit trampoline — the initial type guard returns `trace.startPC` which is the LOOP instruction itself. It should return `trace.startPC + 1` (first instruction after LOOP) to avoid re-entering the trace.
- Enable diagnostics in `sideExitDiagnostic` for debugging.
- **Impact:** ~2–5× on hot loops that don't fit a single basic block (nested loops, loops with calls).

## 3. Optimized Interpreter Loop

**Current state:** The interpreter uses a computed goto dispatch table (`goto* dispatchtable[(int)op]`). This is already fast, but each instruction still does: decode op → `CHECK_GC()` → switch → NEXT().

**What to do:**
- Remove `CHECK_GC()` from every instruction and use a counter-based check (e.g., every 256 instructions).
- Use direct-threading (store addresses of handlers in the bytecode stream) to eliminate the dispatch table lookup.
- Pre-decode opcode operands (A, B, C) and store them in a side array alongside the instruction word.
- **Impact:** ~1.5–2× improvement on tight loops.

## 4. Inline Caching for Method Calls

**Current state:** Method calls go through a runtime lookup: find class → find method → call. This involves string comparisons on every call.

**What to do:**
- Add a polymorphic inline cache (PIC) to `OP_CALL`.
- On first call, record the receiver class and method address.
- On subsequent calls, check the receiver class (via tag bits) and jump directly to the cached method.
- Use a 2–4 entry monomorphic/polymorphic cache before falling back to a full lookup.
- **Impact:** ~5–10× on object-heavy code (hashmap_ops, JSON parsing, collections).

## 5. Type-Specialized Array Operations

**Current state:** All array accesses go through `idxGetHelper`/`idxSetHelper` which check the array's element type and dispatch. This involves a function call per access.

**What to do:**
- Inline the type-dispatch logic directly into the JIT output (or even the interpreter).
- For typed arrays (`int[]`, `double[]`), skip boxing/unboxing overhead by directly reading/writing the raw data.
- In the trace JIT, use the known element type (from the array allocation instruction) to generate type-specialized access code.
- **Impact:** ~3–5× on numeric array-heavy code (matrix_mul, spectral_norm, sieve).

## 6. Generational Garbage Collector

**Current state:** Simple stop-the-world mark-sweep GC. Every GC cycle marks the entire heap, which becomes expensive as the heap grows.

**What to do:**
- Split GC into a young generation (nursery) and an old generation.
- Use a bump-allocator for the nursery and only promote survivors.
- Major GC only runs when the old generation fills up.
- Use tri-color marking for the major GC to minimize pause time.
- **Impact:** ~2–10× for long-running programs with allocation-heavy workloads (stress_objects, JSON parsing).

## 7. String Interning

**Current state:** Every string allocation creates a new `StringData` object. Repeated strings (e.g., field names "name", "age") are duplicated.

**What to do:**
- Add a global string intern table.
- When creating a string, check if an identical string already exists and reuse it.
- Use the SSO (Small String Optimization) path aggressively for strings up to 6 characters (already partially implemented).
- **Impact:** Memory savings of 20–50% for object-heavy code, faster equality checks.

## 8. Peephole Optimizer Improvements

**Current state:** Basic peephole optimization is implemented but only handles obvious patterns.

**What to do:**
- Constant folding: `ADD_INT(R0, 1, 2)` → `LOADINT(R0, 3)` if R1 and R2 are known constants.
- Dead store elimination: remove stores to registers that are never read before being overwritten.
- Strength reduction: replace `MUL_INT(R0, R1, 2)` with `ADD_INT(R0, R1, R1)` or a shift.
- NaN-boxing strength reduction: convert `LOADINT → ADDI → INT tag` to a single fused operation.
- **Impact:** ~5–15% bytecode reduction, with secondary effects on JIT code quality.

## 9. Register Allocation & Frame Size Reduction

**Current state:** All registers are 8 bytes (Value struct). Fixed-size register frame (256 registers). Spills go to the frame directly.

**What to do:**
- Use dynamic register frame sizing — only allocate as many slots as the function actually uses.
- In the JIT, keep frequently-accessed values in x86 registers instead of spilling back to the frame.
- Use the 5 virtual registers (`r13`–`rbx`) more aggressively with a proper register allocator.
- Use live-range analysis to free registers earlier.
- **Impact:** ~10–20% on JIT-compiled code, 5–10% on interpreter code.

## 10. Tail Call Optimization (TCO)

**Current state:** Recursive calls build up a full call stack, limiting recursion depth and wasting memory.

**What to do:**
- Detect tail calls (a `return` whose expression is exactly a function call) in the compiler.
- Instead of `OP_CALL` + `OP_RET`, emit `OP_TAILCALL` which reuses the current stack frame.
- **Impact:** Enables recursion depth > 1000, ~2× on recursive algorithms (fibonacci, factorial, ackermann).

## 11. SIMD Auto-Vectorization

**Current state:** All numeric operations are scalar.

**What to do:**
- Detect tight loops with independent iterations (e.g., array_sum, matrix_mul inner loops).
- In the trace JIT, unroll the loop body and pack multiple iterations into SSE/AVX operations.
- Use x86 `addps`, `mulps`, `cvtsi2ss` etc. for packed operations.
- **Impact:** ~4–8× on numerical loops (matrix_mul, spectral_norm, pi_approx).

## 12. Ahead-of-Time (AOT) Compilation

**Current state:** Every run parses and compiles from scratch.

**What to do:**
- Serialize compiled `Chunk` objects (bytecode + constant table) to a binary cache.
- Store a fingerprint (source hash + std lib version) to detect staleness.
- Load cached bytecode directly without re-parsing or re-compiling.
- **Impact:** ~5–10× faster startup for large projects.

## 13. Concurrency (Lightweight Threads / Fibers)

**Current state:** Single-threaded execution.

**What to do:**
- Add `async` / `await` syntax or green-thread primitives.
- Implement a small M:N scheduler that multiplexes iris threads onto OS thread pools.
- Use the existing `WAIT` instruction as a yield point.
- **Impact:** Unlocks IO-bound and parallel workloads. Could use cooperative multitasking.

## 14. Optional Static Types + Inferred Specialization

**Current state:** Types are optional runtime annotations. The JIT sees `Value` boxes everywhere.

**What to do:**
- When a function has explicit type annotations (`fun add(a: int, b: int): int`), the JIT can assume those types and generate native code without boxing.
- In the interpreter, use type-specialized dispatch tables (fast path when all operands are ints).
- Use runtime type feedback (like the existing trace JIT) to auto-specialize even without type annotations.
- **Impact:** ~2–5× on mixed-type code, enables further JIT optimizations.

## Priority Matrix

| # | Optimization | Impact | Effort | Priority |
|---|-------------|--------|--------|----------|
| 1 | Complete Whole-Function JIT | 10–100× | Medium | P0 |
| 2 | Inline Caching | 5–10× | Low | P0 |
| 3 | Fix Trace JIT (nested loops) | 2–5× | High | P1 |
| 4 | Optimized Interpreter Loop | 1.5–2× | Low | P1 |
| 5 | Type-Specialized Arrays | 3–5× | Medium | P1 |
| 6 | Generational GC | 2–10× | High | P2 |
| 7 | Peephole Optimizer | 5–15% | Low | P2 |
| 8 | String Interning | Memory 20–50% | Low | P2 |
| 9 | Tail Call Optimization | Enables recursion | Low | P2 |
| 10 | SIMD Auto-Vectorization | 4–8× | High | P3 |
| 11 | AOT Compilation | 5–10× startup | Medium | P3 |
| 12 | Concurrency | New capability | High | P3 |

**Immediate next step:** Complete the whole-function JIT compiler. This is the lowest-hanging fruit with the highest payoff. Every missing opcode added to `compile()` unlocks JIT execution for more scripts.
