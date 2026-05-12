import subprocess

import time
import re
import os
import shutil

benchmarks = [
    ("1_loop_math", "Loop Math (100M iterations)"),
    ("2_fib_recursive", "Fibonacci(35) Recursive"),
    ("3_array_ops", "Array Operations (10M elems)"),
    ("4_string_concat", "String Concat (100K)"),
    ("5_object_calls", "Object Calls (1M objects)"),
    ("6_tail_recursion", "Tail Recursion (10M calls)"),
    ("7_sorting", "Sorting (100K elems)"),
    ("8_nested_loops", "Nested Loops (10K x 10K)"),
    ("9_matrix_mult", "Matrix Multiplication (100x100)"),
    ("10_prime_sieve", "Sieve of Eratosthenes (1M)"),
    ("11_mandelbrot", "Mandelbrot Set (300x300)"),
]

iris_dir = r"C:\Users\chalo\CLionProjects\IRIS\cmake-build-msvc\Release"
benchmark_dir = r"C:\Users\chalo\CLionProjects\IRIS\benchmarks"

# Check for lua executable (including common installation paths)
lua_exe = shutil.which("lua") or shutil.which("luajit")
# Fallback to common LuaJIT installation paths on Windows
if not lua_exe:
    if os.path.exists(r"C:\lua\luajit.exe"):
        lua_exe = r"C:\lua\luajit.exe"
    elif os.path.exists(r"C:\luajit\luajit.exe"):
        lua_exe = r"C:\luajit\luajit.exe"

results = []

print("=" * 90)
print("IRIS vs Python vs Lua Performance Benchmarks (FAIR - VM ONLY)")
print("=" * 90)
if not lua_exe:
    print("NOTE: Lua/LuaJIT not found in PATH. Lua benchmarks will be skipped.")
print()

for bench_id, bench_name in benchmarks:
    print(f"Running: {bench_name}")
    
    # Run IRIS
    iris_path = os.path.join(iris_dir, "IRIS.exe")
    iris_file = os.path.join(benchmark_dir, f"{bench_id}.iris")
    
    iris_exec_time = None
    if os.path.exists(iris_path):
        try:
            result = subprocess.run([iris_path, iris_file], capture_output=True, text=True, timeout=300)
            iris_output = result.stdout.strip()
            
            # Try to find CLEAN VM time first
            vm_match = re.search(r'VM trval:\s*([\d.]+)\s*ms', iris_output)
            # Try to find OLD total time as fallback
            total_match = re.search(r'as \(v.etn.\s*parsov.n.\):\s*([\d.]+)\s*ms', iris_output)
            
            if vm_match:
                iris_exec_time = float(vm_match.group(1))
            elif total_match:
                iris_exec_time = float(total_match.group(1))
        except Exception as e:
            print(f"  IRIS Error: {e}")
    
    # Run Python
    py_file = os.path.join(benchmark_dir, f"{bench_id}.py")
    py_exec_time = None
    if os.path.exists(py_file):
        try:
            result = subprocess.run(["python", py_file], capture_output=True, text=True, timeout=300)
            py_output = result.stdout.strip()
            # Try to find INTERNAL python time
            internal_match = re.search(r'Čistý čas Python:\s*([\d.]+)\s*ms', py_output)
            if internal_match:
                py_exec_time = float(internal_match.group(1))
        except Exception as e:
            print(f"  Python Error: {e}")
    
    # Run Lua
    lua_file = os.path.join(benchmark_dir, f"{bench_id}.lua")
    lua_exec_time = None
    if lua_exe and os.path.exists(lua_file):
        try:
            result = subprocess.run([lua_exe, lua_file], capture_output=True, text=True, timeout=300)
            lua_output = result.stdout.strip()
            internal_match = re.search(r'Clean Lua time:\s*([\d.]+)\s*ms', lua_output)
            if internal_match:
                lua_exec_time = float(internal_match.group(1))
        except Exception as e:
            print(f"  Lua Error: {e}")

    results.append((bench_name, iris_exec_time, py_exec_time, lua_exec_time))
    
    if iris_exec_time: print(f"  IRIS:   {iris_exec_time:8.2f} ms")
    if py_exec_time:   print(f"  Python: {py_exec_time:8.2f} ms")
    if lua_exec_time: print(f"  Lua:    {lua_exec_time:8.2f} ms")
    print()

print("=" * 90)
print("SUMMARY TABLE (Fairest comparison possible)")
print("=" * 90)
print(f"{'Benchmark':<35} {'IRIS (ms)':<12} {'Python (ms)':<12} {'Lua (ms)':<12}")
print("-" * 90)

for name, iris_t, py_t, lua_t in results:
    iris_str = f"{iris_t:.2f}" if iris_t is not None else "N/A"
    py_str = f"{py_t:.2f}" if py_t is not None else "N/A"
    lua_str = f"{lua_t:.2f}" if lua_t is not None else "N/A"
    print(f"{name:<35} {iris_str:<12} {py_str:<12} {lua_str:<12}")

print("=" * 90)

if any(r[1] and r[2] for r in results):
    py_speedups = [r[2]/r[1] for r in results if r[1] and r[2]]
    avg_py = sum(py_speedups) / len(py_speedups)
    print(f"IRIS is {avg_py:.2f}x faster than Python on average")
