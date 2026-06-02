# IRIS Performance Benchmarks

*Last Updated: 2026-06-02*
*OS: Windows 10 (win32) / x64*

## 🚀 The LuaJIT Milestone
Following the implementation of the **Tracing JIT** with **Unboxed Integer Optimizations**, IRIS has achieved performance parity with LuaJIT in core computational tasks.

### Multi-Engine Comparison: 01_loop_math (10M iterations)
| Engine | Build / Version | Time (ms) | Speedup (vs Python) |
|--------|-----------------|-----------|---------------------|
| **IRIS (Tracing JIT)** | **GCC 13.1 (O3 + Unboxing)** | **10.6** | **~71.9x** |
| **LuaJIT** | **2.1.0-beta3** | **11.2** | **~68.1x** |
| IRIS (Method JIT) | Previous Version | 110.4 | ~6.9x |
| IRIS (Interpreter) | Current Threaded Dispatch | 763.5 | ~1.0x |
| Python | 3.13.5 (Standard) | 762.8 | 1.0x |

---

## 📊 Comprehensive Benchmark Suite
Side-by-side comparison across all major benchmarks.

| ID | Benchmark Name | IRIS (ms) | LuaJIT (ms) | Python (ms) | IRIS vs LuaJIT |
|----|----------------|-----------|-------------|-------------|----------------|
| 01 | Loop Math (10M) | **10.6** | 11.2 | 762.8 | **0.95x (Faster!)** |
| 02 | Fib Recursive (35) | 642.0 | 96.0 | 1174.1 | 6.6x Slower |
| 03 | Array Ops (10M) | 532.2 | 106.0 | 679.6 | 5.0x Slower |
| 08 | Nested Loops (5Kx5K) | 546.5 | 64.0 | 4002.6 | 8.5x Slower |
| 11 | Mandelbrot (300x300) | 18.7 | 4.0 | 144.0 | 4.6x Slower |

*Note: While IRIS matches LuaJIT in simple loops, LuaJIT's advanced trace-stitcher and CSE (Common Subexpression Elimination) still provide an edge in complex nested structures and heavy object allocations. This defines the next development phase for IRIS.*

---

## 🛠️ Compiler & Toolchain Impact
Different build configurations significantly affect the VM's dispatch speed and JIT efficiency.

### Impact of C++ Compilers on IRIS
| Compiler | Build System | Optimization Flags | Loop Math (ms) |
|----------|--------------|--------------------|----------------|
| **GCC 13.1 (MinGW)** | **Ninja / CMake** | `-O3 -march=native -ffast-math` | **10.6** |
| Clang 18.1 (LLVM) | Ninja / CMake | `-O3 -flto -ffast-math` | 12.2 |
| Zig CC (Clang/LLVM) | `zig build` | `-Doptimize=ReleaseFast` | 11.8 |
| MSVC 2022 | MSBuild | `/O2 /fp:fast` | 24.5 |

---

## 💡 Optimization Notes (June 2026)
1. **Unboxed Trace Registers**: Hot integer variables are kept in CPU registers (`r12-r15`), bypassing NaN-tagging.
2. **Side-Exit Guards**: Type-prefix checks at trace entry ensure machine code safety.
3. **Hybrid Trace-Method JIT**: IRIS uses a Method JIT for general function calls and a Tracing JIT for tight loops.
4. **Threaded Dispatch**: Interpreter uses `&&labels` for O(1) instruction dispatch when not in JIT.
