# IRIS Performance Optimization Roadmap

Target: LuaJIT-class performance (2-10x faster than current).

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

## Priority 1: Generational GC Write Barrier ✅ DONE

**Problem:** Minor collection scans ALL mature objects (gcObjects linked list)
for nursery pointers. O(mature heap) per minor GC. As the heap grows, minor
GC becomes the bottleneck.

**Solution:** Per-object `dirty` flag in `Managed`. Set to `true` whenever a
field of a heap object is written. `minorCollect()` only scans objects with
`dirty == true`, then clears all dirty flags. Typical dirty set is <5% of
mature objects.

**Files changed:**
- `Managed.h`: Added `bool dirty = false;`
- `VM.cpp`: Set `obj->dirty = true` in SET_FIELD, IDX_SET, etc.
- `JITCompiler.cpp`: Emit `mov byte [obj + dirty_offset], 1` in field/array writes
- `GC.cpp`: `minorCollect()` Phase 3 scans only dirty objects

## Priority 2: Card Table / Parallel Marking

**Problem:** Mark phase in major GC is single-threaded and recursive. Marking
millions of objects takes milliseconds.

**Solution:**
1. Card table for mature space → minor GC only scans dirty cards
2. Parallel marking with thread pool (4-8 threads scanning mark queue)

## Priority 3: Eliminate Pointer Mask (shl 16; shr 16)

**Problem:** Every heap value access in the JIT emits:
```
shl rax, 16
shr rax, 16
```
This clears the NaN-boxing tag from the upper 16 bits to extract the pointer.
It adds 2 cycles to every field get/set, method call, array access, etc.

**Solution A — Pointer compression:** Store all pointers as 32-bit offsets
from a 64-bit base register (rBase). Pointer extraction becomes a single
`movsxd rax, dword [val+4]` or similar. Requires modifying Value
representation.

**Solution B — Separate type tag:** Use a 16-byte Value (8 data + 8 type).
Eliminates NaN boxing entirely. Pointer access is direct. Doubles Value size.

**Solution C — Low-4GB heap:** Use VirtualAlloc with MEM_TOP_DOWN or
MAP_32BIT to ensure all heap objects are in the lower 4GB of address space.
Then bits 32-47 are always zero, so `shl 16; shr 16` can be replaced by
`mov eax, eax` (32-bit move zero-extends, eliminating the mask). This is
free if the OS allocates low addresses.

**Estimated impact:** 3-8% on all heap-heavy workloads.

## Priority 4: Trace JIT Escape Analysis

**Problem:** Objects created inside compiled traces (e.g., MapEntry in
hashmap set, closure contexts, boxed values) always allocate on the nursery
heap. Each allocation consumes nursery space and triggers GC pressure.

**Solution:** Escape analysis pass in the trace JIT prepass:
1. Track each allocated object through the trace
2. If the object never escapes (stored to global, returned, etc.), replace
   the allocation with stack allocation or eliminate it entirely
3. Stack-allocated objects can be freed O(1) at trace exit

**Challenge:** Must handle side exits — if the trace exits, stack-allocated
objects must be promoted to the heap or the exit code must handle them.

## Priority 5: Loop Unrolling in Trace JIT ✅ DONE

**Problem:** Every loop iteration pays the trace mechanism overhead:
- Check entry types (guards)
- Emit guard side-exit code
- Trace dispatch

For very small loops (3-5 bytecodes), this overhead is significant.

**Solution:** Unroll the first N iterations of a loop in the trace JIT.
Eliminate the loop guard for all but the last unrolled iteration.

**Implementation:** Factor 2 unrolling in `compileTrace()`. When the last
entry is OP_LOOP, emit UNROLL_FACTOR-1 copies of the body WITHOUT the loop
back jump, then one final copy WITH the loop back. The result: two iterations
execute per entry/exit cycle, halving trace dispatch overhead.

**Files changed:**
- `JITCompiler.cpp`: Unrolled body emission in `compileTrace()`

**Estimated impact:** 10-15% on tight numeric loops.

## Priority 6: Inline All Remaining C++ Helpers

**Problem:** Several operations still fall back to C++ helper calls even
when types are known:

| Helper | Reason for fallback | Status |
|--------|---------------------|--------|
| `divHelper` | Double division, string concat | ✅ trace JIT inlined for unboxed ints |
| `eqHelper` | Mixed-type or string equality | ✅ trace JIT inlined for unboxed ints |
| `ltHelper` | Mixed-type or string comparison | ✅ trace JIT inlined for unboxed ints |
| `negHelper` | Rare type paths | ✅ trace JIT inlined with type dispatch |
| `idxGetHelper` | VALUE-type array slow path | ✅ trace JIT inlined for typed arrays |
| `idxSetHelper` | VALUE-type array slow path | ✅ trace JIT inlined for typed arrays |
| `collLenHelper` | Collection length | ✅ **trace JIT inlined** (array fast path) |
| `incFieldHelper` / `decFieldHelper` | Field increment/decrement | ✅ **removed** — compiler never emits these opcodes |

**Solution:** Inline each helper's logic directly in the JIT with type
dispatch. For VALUE-type arrays, inline the bounds check + element access.

## Priority 7: Constant Folding & Propagation

**Problem:** The trace JIT does not fold constant expressions. For example,
`x = 5 + 3` compiles to `mov eax, 5; add eax, 3` instead of `mov eax, 8`.

**Solution:** During the SSA prepass, evaluate expressions with constant
operands. Replace the instruction with a LOADK or LOADINT.

**Challenge:** Must handle guards — if an expression depends on a value that
was guarded to have a specific type, it's effectively constant for this
trace.

## Priority 8: Specialized Array Types for VALUE Arrays

**Problem:** ArrayData supports typed elements (INT, DOUBLE) for efficient
storage, but most arrays are created as UNTYPED/VALUE, losing the benefit.

**Solution:** Dynamic type detection: track the actual types stored in a
VALUE array. After N consecutive stores of the same type, upgrade the array
to the specialized format.

## Priority 9: String Builder (Rope Optimization)

**Problem:** String concatenation in a loop creates a chain of RopeData
objects. At flatten time (depth >= 64), the entire chain is walked and
copied. This is O(n) but with a high constant factor.

**Solution:**
1. Native StringBuilder backed by a `std::string` buffer
2. Direct `memcpy` into the buffer instead of rope allocation
3. One final allocation for the result string
4. JIT intrinsic for StringBuilder::append

## Priority 10: Write Barrier Fix for ArrayData

**Problem:** ArrayData::VALUE stores `Value` elements which can contain
pointers. The write barrier must also fire on `IDX_SET` for VALUE arrays.

**Status:** Done — both SET_FIELD and IDX_SET set `obj->dirty = true` in
interpreter and JIT.

## Performance Model

| Component | Current % of time | Target % | Optimization |
|-----------|-------------------|----------|-------------|
| GC (mark + sweep) | ~15% | ~3% | Generational + write barrier + parallel |
| Pointer masking | ~5% | ~1% | Low-4GB heap / compression |
| Trace dispatch | ~10% | ~5% | Loop unrolling (half iterations) |
| Non-inlined helpers | ~8% | ~3% | Inline all helpers (mostly done) |
| Object allocation | ~12% | ~4% | Escape analysis |
| Everything else | ~50% | ~50% | — |
| **Total** | **100%** | **~66%** | **~1.5x faster** |

LuaJIT achieves its speed through a combination of ALL of these
optimizations plus a world-class SSA IR optimizer. IRIS can get within
1.5-2x of LuaJIT by implementing priorities 1-6. Priority 7-10 close
the gap to 1.2-1.5x. Perfect parity would require a full SSA IR
rewrite (similar to LuaJIT's own architecture).
