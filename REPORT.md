# IRIS Performance Report - 2026-05-25

## Benchmark Results (IRIS vs Python 3.11)

| Benchmark | IRIS (ms) | Python (ms) | Comparison |
|-----------|-----------|-------------|------------|
| **Fibonacci (30)** | **19** | 143 | **~7.5x faster** |
| **Loop Math (1M)** | **21** | 29 | **~1.4x faster** |
| **Raw Array (1M)** | **17** | 61 | **~3.5x faster** |
| **Matrix Mult (100x100)** | **40** | 59 | **~1.5x faster** |
| **Sieve of Erato. (1M)** | **48** | 75 | **~1.5x faster** |

## JIT Compiler Enhancements
- **Loop Support**: Implemented `OP_LOOP` in JIT, enabling full compilation of iterative algorithms.
- **Field & Array Access**: Optimized `OP_GET_FIELD`, `OP_SET_FIELD`, and typed array access (`int[]`, `double[]`).
- **Memory Safety**: Fixed a critical bug where local `JITCompiler` instances caused memory corruption due to `JitRuntime` scope. Recursive compilation now correctly shares the same runtime.
- **Branch Fixes**: Resolved signedness issues in jump target calculations, preventing infinite loops.

## Technical Improvements
- **Array Performance**: Optimized `OP_NEW_ARRAY` and index access, resulting in a **2.5x speedup** in array-heavy tasks.
- **Sieve Speedup**: JIT compilation of nested loops and `OP_IDX_SET_INT` improved Sieve performance by **~40%**.
