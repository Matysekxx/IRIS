#!/usr/bin/env python3
"""
IRIS Benchmark Runner
Compares IRIS vs LuaJIT vs Python on matching benchmark files.
Usage: python run_benchmarks.py [--exe path/to/IRIS.exe] [--luajit C:\lua\luajit.exe] [--runs 5]
"""

import subprocess
import sys
import time
import argparse
import statistics
from pathlib import Path

BENCHMARK_DIR = Path(__file__).parent / "benchmarks"
DEFAULT_IRIS_EXE = Path(__file__).parent / "build_gcc" / "IRIS.exe"
DEFAULT_LUAJIT = r"C:\lua\luajit.exe"
DEFAULT_PYTHON = sys.executable


def run_once(cmd, capture_stdout=True):
    """Run a command and return (elapsed_ms, stdout)."""
    start = time.perf_counter()
    try:
        result = subprocess.run(
            cmd,
            capture_output=capture_stdout,
            text=True,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        return float('inf'), "TIMEOUT"
    elapsed = (time.perf_counter() - start) * 1000.0
    
    # Try to extract internal time from IRIS output
    out = result.stdout or ""
    for line in out.splitlines():
        if "Běh VM trval" in line or "celkový čas" in line.lower():
            try:
                # e.g. "[INFO] Běh VM trval: 12.34 ms"
                val = line.split(":")[-1].replace("ms", "").strip()
                elapsed = float(val)
            except ValueError:
                pass
    return elapsed, out


def benchmark_file(name, iris_exe, luajit_exe, python_exe, runs):
    bench_dir = BENCHMARK_DIR
    iris_file = bench_dir / f"{name}.iris"
    lua_file = bench_dir / f"{name}.lua"
    py_file = bench_dir / f"{name}.py"

    results = {}

    if iris_file.exists() and iris_exe.exists():
        times = []
        for _ in range(runs):
            t, _ = run_once([str(iris_exe), str(iris_file)])
            times.append(t)
        results["IRIS"] = {
            "min": min(times),
            "max": max(times),
            "median": statistics.median(times),
            "mean": statistics.mean(times),
        }
    else:
        results["IRIS"] = None

    if lua_file.exists() and Path(luajit_exe).exists():
        times = []
        for _ in range(runs):
            t, _ = run_once([str(luajit_exe), str(lua_file)])
            times.append(t)
        results["LuaJIT"] = {
            "min": min(times),
            "max": max(times),
            "median": statistics.median(times),
            "mean": statistics.mean(times),
        }
    else:
        results["LuaJIT"] = None

    if py_file.exists() and Path(python_exe).exists():
        times = []
        for _ in range(runs):
            t, _ = run_once([str(python_exe), str(py_file)])
            times.append(t)
        results["Python"] = {
            "min": min(times),
            "max": max(times),
            "median": statistics.median(times),
            "mean": statistics.mean(times),
        }
    else:
        results["Python"] = None

    return results


def print_results(all_results):
    print("\n" + "=" * 80)
    print(f"{'Benchmark':<20} {'IRIS (ms)':<15} {'LuaJIT (ms)':<15} {'Python (ms)':<15} {'Winner'}")
    print("=" * 80)

    for name, res in all_results.items():
        iris = res.get("IRIS")
        luajit = res.get("LuaJIT")
        python = res.get("Python")

        iris_str = f"{iris['median']:.2f}" if iris else "N/A"
        luajit_str = f"{luajit['median']:.2f}" if luajit else "N/A"
        python_str = f"{python['median']:.2f}" if python else "N/A"

        # Determine winner by median
        winner = "N/A"
        times = []
        if iris: times.append(("IRIS", iris["median"]))
        if luajit: times.append(("LuaJIT", luajit["median"]))
        if python: times.append(("Python", python["median"]))
        if times:
            winner = min(times, key=lambda x: x[1])[0]

        print(f"{name:<20} {iris_str:<15} {luajit_str:<15} {python_str:<15} {winner}")

    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description="IRIS Benchmark Runner")
    parser.add_argument("--exe", type=Path, default=DEFAULT_IRIS_EXE, help="Path to IRIS.exe")
    parser.add_argument("--luajit", type=Path, default=Path(DEFAULT_LUAJIT), help="Path to LuaJIT executable")
    parser.add_argument("--python", type=Path, default=Path(DEFAULT_PYTHON), help="Path to Python executable")
    parser.add_argument("--runs", type=int, default=5, help="Number of runs per benchmark")
    parser.add_argument("--filter", type=str, default="", help="Only run benchmarks matching this string")
    args = parser.parse_args()

    bench_dir = BENCHMARK_DIR
    if not bench_dir.exists():
        print(f"Benchmark directory not found: {bench_dir}")
        sys.exit(1)

    iris_files = sorted(bench_dir.glob("*.iris"))
    if not iris_files:
        print("No .iris benchmark files found.")
        sys.exit(1)

    all_results = {}
    for iris_file in iris_files:
        name = iris_file.stem
        if args.filter and args.filter not in name:
            continue
        print(f"\nRunning benchmark: {name} ...")
        res = benchmark_file(name, args.exe, args.luajit, args.python, args.runs)
        all_results[name] = res

    print_results(all_results)


if __name__ == "__main__":
    main()
