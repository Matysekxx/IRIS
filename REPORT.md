# IRIS Performance Report - 2026-05-25

## Benchmark Results (IRIS vs Python 3.11)

| Benchmark | IRIS (ms) | Python (ms) | Comparison |
|-----------|-----------|-------------|------------|
| **Fibonacci (30)** | **19** | 143 | **~7.5x faster** (JIT) |
| **Loop Math (1M)** | **21** | 29 | **~1.4x faster** |
| **Raw Array (1M)** | **43** | 61 | **~1.4x faster** |
| **Matrix Mult (100x100)** | **46** | 59 | **~1.3x faster** |
| **Sieve of Erato. (1M)** | **74** | 75 | **Equivalent** |

## Improvements & Bug Fixes
- **JIT Reactivation**: Fixed a bug where JIT was accidentally disabled in the VM.
- **Compiler Fixes**:
  - Corrected element type tagging for object arrays (fixed memory corruption).
  - Fixed register allocation for class constructor calls (aligned `this` pointer).
- **Performance**: IRIS consistently outperforms Python 3.11 in core computational tasks.

## Current Limitations
- **HashMap**: Issues with object arrays and reference counting still persist in complex scenarios.
- **JIT Coverage**: Loops (`OP_LOOP`) are still executed via VM interpretation; JIT is primarily effective for recursive calls.
