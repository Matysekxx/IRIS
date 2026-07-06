# IRIS Performance Optimization Roadmap

Target: LuaJIT-class performance (within 1.2-2x of LuaJIT).

## Completed Optimizations

- **Generational GC** (4MB nursery, bump allocation, minor/major collection)
- **Trace JIT** with inline arithmetic (ADD/SUB/MUL, double, comparisons, bitwise)
- **Unboxed integers** throughout JIT (skip NaN-boxing for int paths)
- **Unboxed bools** in trace JIT (store raw 0/1 via storeUnboxed)
- **Inlined K operations** (OP_ADD_K, OP_SUB_K, OP_MUL_K, OP_DIV_K, etc.)
- **Inlined mixed-type ADD/SUB/MUL** (int+double inline, no C++ helper)
- **Inlined OP_NOT, OP_NEG** with type dispatch
- **Clean SSA prepass** (live interval tracking for future allocator)
- **Write barrier** (dirty flag on Managed, GC scans only dirty mature objects)
- **JMPF/JMPT unboxed guard fix** (inverted cmp-to-zero conditions)
- **OP_MOVE unboxed-to-memory tag fix** (tag raw int when storing to memory)
- **Loop unrolling** (factor 2 in trace JIT)
- **collLenHelper inline** (array fast path in trace JIT)
- **Removed dead incFieldHelper/decFieldHelper** (opcodes never emitted by compiler)
- **NaN-tagged Value** (64-bit, compact representation)
- **SSO strings** (up to 6 chars inlined in Value, zero heap alloc)
- **Rope-based string concat** (avoids O(n²) copies)
- **String interning** (O(1) string equality for interned strings)
- **Typed arrays** (INT/DOUBLE/VALUE with memory-efficient storage)
- **SIMD array ops** (AVX2/SSE4.2 vectorized sum, etc.)
- **Computed-goto dispatch** (GCC/Clang) for interpreter
- **Register-based VM** (flat register file, no stack manipulation overhead)

## Current Architecture

```
src/
├── core/       Value, GC, Memory, Native, ArrayData, SIMD
├── vm/         Interpreter (computed-goto), bytecode, frame management
├── jit/        Trace-based JIT via AsmJit (x64 codegen)
├── ir/         Compiler (AST→bytecode), peephole optimizer
├── frontend/   Lexer, parser, AST definitions
├── std/        Minimal native stdlib (only performance-critical bindings)
└── platform/   OS abstractions (Win32/Unix)

iris_std/
├── core/       Object, String, Array, Map, Set (user-extensible via inheritance)
├── collections/ Lists, Maps, Collections algorithms
├── io/         File, Streams, Console
├── net/        Sockets, URL
├── time/       DateTime, Duration
├── text/       Regex, JSON, Base64, Hex, CSV
├── math/       Math constants and wrappers
└── util/       Logger, Random, UUID, Properties, SystemInfo
```

## Priority 1: Expand JIT to 16 Virtual Registers ⬜ TODO

**Problem:** Only 5 virtual registers (R13-R15, RBP, RBX) mapped to physical.
Spills to main register file for any additional live values. Tight loops with
>5 live variables spill to memory constantly.

**Solution:** Map to R8-R15 + RDI, RSI, RBP, RBX, keeping RCX, RDX, RAX for
scratch. Total 14 integer + 6 XMM registers. Reduces spilling by 60%.

**Estimated impact:** 5-15% on register-pressure-heavy code.

## Priority 2: Loop Unrolling Factor 4-8 ⬜ TODO

**Problem:** Current factor 2 unrolling halves trace dispatch overhead but
the remaining overhead is still ~10% for very small loops.

**Solution:** Dynamic unroll factor based on trace body size:
- <= 5 ops → unroll 8x
- <= 10 ops → unroll 4x  
- > 10 ops → unroll 2x

**Estimated impact:** 5-10% on tight numeric loops.

## Priority 3: Eliminate Pointer Mask (shl 16; shr 16) ⬜ TODO

**Problem:** Every heap value access in the JIT emits `shl rax, 16; shr rax, 16`
(2 cycles) to clear the NaN-boxing tag. This affects every field get/set,
method call, array access.

**Solution C — Low-4GB heap:** Use VirtualAlloc with MEM_TOP_DOWN or
MAP_32BIT to ensure all heap objects are in the lower 4GB of address space.
Then bits 32-47 are always zero, so `shl 16; shr 16` can be replaced by
`mov eax, eax` (32-bit move zero-extends, eliminating the mask). This is
free if the OS allocates low addresses.

**Estimated impact:** 3-8% on all heap-heavy workloads.

## Priority 4: Trace JIT Escape Analysis ⬜ TODO

**Problem:** Objects created inside compiled traces (e.g., MapEntry in
hashmap set, closure contexts, boxed values) always allocate on the nursery
heap. Each allocation consumes nursery space and triggers GC pressure.

**Solution:** Escape analysis pass in the trace JIT prepass:
1. Track each allocated object through the trace
2. If the object never escapes (stored to global, returned, etc.), replace
   the allocation with stack allocation or eliminate it entirely
3. Stack-allocated objects can be freed O(1) at trace exit

**Estimated impact:** 5-10% on allocation-heavy workloads (hash maps, string building).

## Priority 5: Inline All Remaining C++ Helpers ✅ DONE

| Helper | Reason for fallback | Status |
|--------|---------------------|--------|
| `eqHelper` | Mixed-type or string equality | ✅ inlined for unboxed ints |
| `ltHelper` | Mixed-type or string comparison | ✅ inlined for unboxed ints |
| `negHelper` | Rare type paths | ✅ inlined with type dispatch |
| `idxGetHelper` | VALUE-type array slow path | ✅ inlined for typed arrays |
| `idxSetHelper` | VALUE-type array slow path | ✅ inlined for typed arrays |
| `collLenHelper` | Collection length | ✅ inlined (array fast path) |
| `divHelper` | Division fallback | ⬜ needs multi-type dispatch inline |

## Priority 6: Card Table Write Barrier ⬜ TODO

**Problem:** Minor collection iterates a linked list of dirty mature objects.
For large heaps, even the dirty list can be large.

**Solution:** Card table: divide mature heap into 512-byte cards. Set a byte
in the card table when a card contains a pointer to nursery. Minor GC only
scans dirty cards instead of dirty objects. Reduces scan overhead by ~10x.

**Estimated impact:** 2-5% on GC-heavy workloads.

## Priority 7: Constant Folding & Propagation ⬜ TODO

**Problem:** `x = 5 + 3` compiles to `mov eax, 5; add eax, 3` instead of
`mov eax, 8`. The trace JIT does not fold constant expressions.

**Solution:** During the SSA prepass, evaluate expressions with constant
operands. Replace the instruction with a LOADK or LOADINT.

## Priority 8: String Builder Intrinsic ⬜ TODO

**Problem:** String concatenation in a loop creates a chain of RopeData
objects. At flatten time (depth >= 64), the entire chain is walked and
copied.

**Solution:**
1. Native StringBuilder backed by a `std::string` buffer
2. Direct `memcpy` into the buffer instead of rope allocation
3. One final allocation for the result string
4. JIT intrinsic for StringBuilder::append

## Priority 9: Parallel GC Marking ⬜ TODO

**Problem:** Major collection mark phase is single-threaded and recursive.
Marking millions of objects takes milliseconds.

**Solution:** Parallel marking with thread pool (4-8 threads scanning mark
queue using work-stealing). Requires atomic mark bits and a concurrent stack.

**Estimated impact:** 3-8% on large-heap workloads.

## Priority 10: Polymorphic Inline Cache (PIC) ⬜ TODO

**Problem:** Method dispatch currently does a hash table lookup on every
invocation. Monomorphic cache (OP_INVOKE_MONO) helps but only caches one
class.

**Solution:** Small polymorphic cache (2-4 class entries) for method dispatch.
When the cache misses, fall back to the global lookup.

**Estimated impact:** 10-20% on OOP-heavy code with dynamic dispatch.

## Performance Target Model

| Component | Current % | Target % | Optimization |
|-----------|-----------|----------|-------------|
| GC (mark + sweep) | ~15% | ~3% | Generational + write barrier + parallel |
| Pointer masking | ~5% | ~1% | Low-4GB heap |
| Trace dispatch | ~10% | ~3% | Loop unrolling 4-8x |
| Non-inlined helpers | ~8% | ~2% | Inline all helpers |
| Object allocation | ~12% | ~4% | Escape analysis |
| Method dispatch | ~10% | ~3% | Polymorphic IC |
| Everything else | ~40% | ~40% | — |
| **Total** | **100%** | **~56%** | **~1.8x faster** |

## Path to LuaJIT Parity

LuaJIT achieves its speed through:
1. **SSA IR** (trace compiler) — IRIS has basic SSA prepass, needs full IR
2. **Extreme specialization** — IRIS specializes per-opcode, LuaJIT per-trace
3. **Aggressive inlining** — IRIS inlines arithmetic, LuaJIT inlines everything
4. **Register allocation** — IRIS uses 5 regs, LuaJIT uses all 14 GPRs
5. **Guard elimination** — IRIS removes redundant guards, LuaJIT does full CSE

**Near-term (3 months):** 1.5-2x current speed (priorities 1-4, 6)
**Medium-term (6 months):** 1.2-1.5x current speed (priorities 5, 7-10)  
**Long-term (12 months):** LuaJIT parity requires full SSA IR rewrite

## Current State
- ✅ Implemented: NaN-tagged Value, Generational GC, Trace JIT, Typed arrays
- 🔄 In progress: JIT register expansion, escape analysis
- ⬜ Planned: Card table, parallel GC, PIC, SSA IR
