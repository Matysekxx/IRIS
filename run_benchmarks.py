#!/usr/bin/env python3
"""
IRIS Benchmark Runner
Compares multiple IRIS builds vs LuaJIT vs Python with formatted output.
Usage: python run_benchmarks.py [--runs 5] [--filter name] [--build]
"""

import subprocess
import sys
import time
import argparse
import statistics
import math
import re
import os
from pathlib import Path
from dataclasses import dataclass
from typing import Optional

# Force UTF-8 encoding on stdout/stderr to support box drawing characters on Windows
try:
    if sys.platform == 'win32':
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
except Exception:
    pass

BENCHMARK_DIR = Path(__file__).parent / "benchmarks"
DEFAULT_PYTHON = sys.executable

TIMEOUT_SEC = 15
_USE_COLOR = hasattr(sys.stdout, 'isatty') and sys.stdout.isatty()

# ANSI escape code remover
ANSI_ESCAPE = re.compile(r'\033\[[0-9;]*m')

def strip_ansi(text: str) -> str:
    """Remove ANSI escape codes from a string to measure visible length."""
    return ANSI_ESCAPE.sub('', text)

def pad_visible(text: str, width: int, align: str = 'left') -> str:
    """Pad string according to its visible width on the terminal."""
    vis_len = len(strip_ansi(text))
    pad_len = max(0, width - vis_len)
    if align == 'left':
        return text + " " * pad_len
    elif align == 'right':
        return " " * pad_len + text
    else: # center
        l_pad = pad_len // 2
        r_pad = pad_len - l_pad
        return " " * l_pad + text + " " * r_pad

def _c(code, text):
    if _USE_COLOR:
        return f"\033[{code}m{text}\033[0m"
    return text

def green(text):   return _c("1;32", text)
def red(text):     return _c("1;31", text)
def yellow(text):  return _c("1;33", text)
def cyan(text):    return _c("1;36", text)
def bold(text):    return _c("1", text)
def dim(text):     return _c("2", text)
def white(text):   return _c("1;37", text)

def try_exe(exe_path, test_file):
    try:
        r = subprocess.run([str(exe_path), str(test_file)],
                           capture_output=True, timeout=5)
        out = r.stdout.decode("utf-8", errors="replace") if r.stdout else ""
        return r.returncode == 0 and "VM trval" in out
    except Exception:
        return False

def find_luajit():
    """Try to find LuaJIT in default location or PATH."""
    default_path = Path(r"C:\lua\luajit.exe")
    if default_path.exists():
        return default_path
    try:
        # Try 'where' command to find in PATH
        r = subprocess.run(["where", "luajit"], capture_output=True, text=True)
        if r.returncode == 0:
            lines = r.stdout.strip().splitlines()
            if lines:
                return Path(lines[0])
    except Exception:
        pass
    return None

def setup_toolchain_paths():
    """Detect and append CLion's bundled compiler/make/cmake paths to PATH."""
    programs_dir = Path(r"C:\Users\chalo\AppData\Local\Programs")
    if not programs_dir.exists():
        return
        
    clion_dir = None
    for item in programs_dir.iterdir():
        if item.is_dir() and item.name.lower().startswith("clion"):
            clion_dir = item
            break
            
    if clion_dir:
        clion_mingw = clion_dir / "bin" / "mingw" / "bin"
        clion_cmake = clion_dir / "bin" / "cmake" / "win" / "x64" / "bin"
        
        paths = []
        if clion_mingw.exists():
            paths.append(str(clion_mingw))
        if clion_cmake.exists():
            paths.append(str(clion_cmake))
            
        if paths:
            existing = os.environ.get("PATH", "")
            os.environ["PATH"] = ";".join(paths) + ";" + existing

def detect_iris_builds():
    root = Path(__file__).parent
    test_file = BENCHMARK_DIR / "tiny_test.iris"
    builds = {}

    if not test_file.exists():
        test_file = BENCHMARK_DIR / "fib.iris"
    if not test_file.exists():
        return builds

    candidates = [
        ("MSVC", [
            root / "build_msvc" / "Release" / "IRIS.exe",
            root / "build_msvc" / "RelWithDebInfo" / "IRIS.exe",
            root / "build_msvc" / "Debug" / "IRIS.exe",
        ]),
        ("GCC (G++)", [
            root / "build_gcc" / "IRIS.exe",
            root / "build_gcc" / "Release" / "IRIS.exe",
        ]),
        ("Clang", [
            root / "build_clang" / "Release" / "IRIS.exe",
            root / "build_clang" / "IRIS.exe",
            root / "build_clang_release" / "IRIS.exe",
        ]),
        ("CMake", [
            root / "cmake-build-release" / "IRIS.exe",
            root / "build" / "Release" / "IRIS.exe",
            root / "build" / "IRIS.exe",
        ]),
        ("Zig CC", [
            root / "zig-out" / "bin" / "iris.exe",
            root / "zig-out" / "bin" / "IRIS.exe",
            root / "build_zig" / "IRIS.exe",
        ]),
    ]
    for label, paths in candidates:
        for path in paths:
            if path.exists() and try_exe(path, test_file):
                builds[label] = path.resolve()
                break
    return builds

def build_targets():
    print_section("Building Compiler Targets")
    root = Path(__file__).parent

    # g++ / GCC via CMake + MinGW
    print("  Building GCC (G++)... ", end="", flush=True)
    try:
        subprocess.run(["cmake", "-B", "build_gcc", "-S", ".", "-G", "MinGW Makefiles", "-DCMAKE_BUILD_TYPE=Release"],
                       capture_output=True, check=True, cwd=root)
        subprocess.run(["cmake", "--build", "build_gcc", "--parallel"],
                       capture_output=True, check=True, cwd=root)
        print(green("SUCCESS"))
    except Exception as e:
        print(red(f"FAILED (could not run cmake/g++: {str(e)})"))

    # MSVC via CMake + Visual Studio
    print("  Building MSVC... ", end="", flush=True)
    try:
        subprocess.run(["cmake", "-B", "build_msvc", "-S", ".", "-G", "Visual Studio 17 2022", "-A", "x64"],
                       capture_output=True, check=True, cwd=root)
        subprocess.run(["cmake", "--build", "build_msvc", "--config", "Release"],
                       capture_output=True, check=True, cwd=root)
        print(green("SUCCESS"))
    except Exception as e:
        print(red(f"FAILED (could not run MSVC build: {str(e)})"))

    # Clang
    print("  Building Clang... ", end="", flush=True)
    try:
        configured = False
        try:
            subprocess.run(["cmake", "-B", "build_clang", "-S", ".", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_BUILD_TYPE=Release"],
                           capture_output=True, check=True, cwd=root)
            configured = True
        except Exception:
            try:
                subprocess.run(["cmake", "-B", "build_clang", "-S", ".", "-T", "ClangCL"],
                               capture_output=True, check=True, cwd=root)
                configured = True
            except Exception:
                pass
        
        if configured:
            subprocess.run(["cmake", "--build", "build_clang", "--config", "Release"],
                           capture_output=True, check=True, cwd=root)
            print(green("SUCCESS"))
        else:
            raise RuntimeError("clang++ or ClangCL compiler not detected")
    except Exception as e:
        print(red(f"FAILED (could not run Clang build: {str(e)})"))

    # CMake (Generic CLion / Release)
    print("  Building CMake (Generic)... ", end="", flush=True)
    try:
        subprocess.run(["cmake", "-B", "build", "-S", ".", "-DCMAKE_BUILD_TYPE=Release"],
                       capture_output=True, check=True, cwd=root)
        subprocess.run(["cmake", "--build", "build", "--config", "Release"],
                       capture_output=True, check=True, cwd=root)
        print(green("SUCCESS"))
    except Exception as e:
        print(red(f"FAILED (could not run generic CMake build: {str(e)})"))

    # Zig build / Zig CC
    print("  Building Zig CC... ", end="", flush=True)
    try:
        subprocess.run(["zig", "build", "-Doptimize=ReleaseFast"],
                       capture_output=True, check=True, cwd=root)
        print(green("SUCCESS"))
    except Exception as e:
        print(red(f"FAILED (could not run zig build: {str(e)})"))

@dataclass
class BenchStats:
    min: float
    max: float
    median: float
    mean: float
    stdev: float
    samples: list

def run_once(cmd):
    start = time.perf_counter()
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=TIMEOUT_SEC)
    except subprocess.TimeoutExpired:
        return float('inf')
    except Exception:
        return float('inf')
    out = result.stdout.decode("utf-8", errors="replace") if result.stdout else ""
    if result.returncode != 0 and "VM trval" not in out:
        return float('inf')
    elapsed = (time.perf_counter() - start) * 1000.0
    for line in out.splitlines():
        if "VM trval" in line or "celkov" in line.lower():
            try:
                # support both "Běh VM trval: 12.3 ms" and "VM trval 100 ms"
                parts = line.split(":")
                val_str = parts[-1] if len(parts) > 1 else line.replace("VM trval", "")
                val = val_str.replace("ms", "").strip()
                elapsed = float(val)
            except ValueError:
                pass
    return elapsed

def bench_one(exe, src, runs):
    if not src.exists():
        return None
    times = []
    for _ in range(runs):
        t = run_once([str(exe), str(src)])
        times.append(t)
    valid = [t for t in times if t != float('inf')]
    if not valid:
        return None
    return BenchStats(
        min=min(valid), max=max(valid),
        median=statistics.median(valid),
        mean=statistics.mean(valid),
        stdev=statistics.stdev(valid) if len(valid) > 1 else 0.0,
        samples=valid,
    )

def benchmarkfile(name, exes, luajit_exe, python_exe, runs):
    bench_dir = BENCHMARK_DIR
    iris_file = bench_dir / f"{name}.iris"
    lua_file = bench_dir / f"{name}.lua"
    py_file  = bench_dir / f"{name}.py"

    results = {}
    for label, exe_path in exes.items():
        results[label] = bench_one(exe_path, iris_file, runs)

    if luajit_exe and Path(luajit_exe).exists():
        results["LuaJIT"] = bench_one(luajit_exe, lua_file, runs)
    else:
        results["LuaJIT"] = None

    if python_exe and Path(python_exe).exists():
        results["Python"] = bench_one(python_exe, py_file, runs)
    else:
        results["Python"] = None

    return results

def bar(value, max_val, width=12):
    """Generates a beautiful block bar chart."""
    if max_val <= 0 or math.isinf(value) or math.isnan(value):
        return " " * width
    ratio = value / max_val
    filled = int(ratio * width)
    filled = max(0, min(filled, width))
    return "█" * filled + "░" * (width - filled)

def fmt_time(ms):
    """Format time nicely."""
    if ms == float('inf') or math.isnan(ms):
        return "TIMEOUT"
    if ms < 1.0:
        return f"{ms:.3f}"
    if ms < 10.0:
        return f"{ms:.2f}"
    if ms < 100.0:
        return f"{ms:.1f}"
    return f"{ms:.0f}"

def fmt_ratio(ratio):
    """Format speedup ratio with color."""
    if ratio is None or ratio == float('inf') or math.isnan(ratio):
        return dim("N/A")
    if ratio < 0.5:
        return red(f"{ratio:.2f}×")
    if ratio < 1.0:
        return yellow(f"{ratio:.2f}×")
    if ratio >= 1.5:
        return green(f"{ratio:.2f}×")
    return f"{ratio:.2f}×"

def print_summary_table(all_results, labels):
    """Print a beautifully formatted results table with Unicode borders, block bars, and ratios."""
    name_w = max(16, max(len(n) for n in all_results.keys()) + 1)
    col_w = 15

    # Compute speedup ratios vs the fastest runner per benchmark
    def best_of(res):
        vals = [(l, res[l].median) for l in labels if res.get(l)]
        if not vals:
            return None, None
        return min(vals, key=lambda x: x[1])

    # Construct borders
    # ┌, ┬, ┐, ├, ┼, ┤, └, ┴, ┘, ─, │
    top_border    = "┌" + "─" * (name_w + 2) + "┬" + "┬".join(["─" * (col_w + 2) for _ in labels]) + "┬" + "─" * 12 + "┐"
    header_border = "├" + "─" * (name_w + 2) + "┼" + "┼".join(["─" * (col_w + 2) for _ in labels]) + "┼" + "─" * 12 + "┤"
    row_border    = "├" + "─" * (name_w + 2) + "┼" + "┼".join(["─" * (col_w + 2) for _ in labels]) + "┼" + "─" * 12 + "┤"
    bottom_border = "└" + "─" * (name_w + 2) + "┴" + "┴".join(["─" * (col_w + 2) for _ in labels]) + "┴" + "─" * 12 + "┘"

    # Header row
    print()
    print(top_border)
    header_row = f"│ {bold('Benchmark'):<{name_w}} │"
    for label in labels:
        header_row += f" {pad_visible(cyan(label), col_w, 'center')} │"
    header_row += f" {pad_visible(bold('vs LuaJIT'), 10, 'center')} │"
    print(header_row)
    print(header_border)

    for name, res in sorted(all_results.items()):
        fast_label, fast_val = best_of(res)

        # 1. Median times row
        time_row = f"│ {bold(name):<{name_w}} │"
        for label in labels:
            r = res.get(label)
            if r:
                raw = fmt_time(r.median) + " ms"
                if label == fast_label:
                    raw = green(raw)
                time_row += f" {pad_visible(raw, col_w, 'right')} │"
            else:
                time_row += f" {pad_visible(dim('N/A'), col_w, 'right')} │"

        # Ratio vs LuaJIT
        ref_label = "LuaJIT" if "LuaJIT" in labels else labels[-1]
        ref_r = res.get(ref_label)
        our_r = res.get(labels[0])
        if ref_r and our_r:
            ratio = ref_r.median / our_r.median
            time_row += f" {pad_visible(fmt_ratio(ratio), 10, 'center')} │"
        else:
            time_row += f" {pad_visible(dim('N/A'), 10, 'center')} │"
        print(time_row)

        # 2. Min...Max and stdev row
        stats_row = f"│ {dim(''):<{name_w}} │"
        for label in labels:
            r = res.get(label)
            if r:
                pct = (r.stdev / r.mean * 100.0) if r.mean > 0 else 0.0
                stats_str = f"{fmt_time(r.min)}-{fmt_time(r.max)} (±{pct:.1f}%)"
                stats_row += f" {pad_visible(dim(stats_str), col_w, 'right')} │"
            else:
                stats_row += f" {pad_visible(dim(''), col_w, 'right')} │"
        stats_row += f" {pad_visible(dim(''), 10, 'center')} │"
        print(stats_row)

        # 3. Block bar chart row
        bar_row = f"│ {dim(''):<{name_w}} │"
        row_vals = [res.get(l).median for l in labels if res.get(l)]
        row_max = max(row_vals) if row_vals else 1.0
        for label in labels:
            r = res.get(label)
            if r:
                bw = bar(r.median, row_max * 1.05, width=col_w - 4)
                pad = col_w - len(bw)
                l_pad = pad // 2
                r_pad = pad - l_pad
                bw_str = " " * l_pad + bw + " " * r_pad
                if label == fast_label:
                    bw_str = green(" " * l_pad + bw) + " " * r_pad
                bar_row += f" {bw_str} │"
            else:
                bar_row += f" {pad_visible(dim(''), col_w, 'center')} │"
        bar_row += f" {pad_visible(dim(''), 10, 'center')} │"
        print(bar_row)

        if name != sorted(all_results.keys())[-1]:
            print(row_border)

    print(bottom_border)
    print(f"  {dim('Times in milliseconds (median of N runs, with min-max range and stdev %)')}")
    print(f"  {green('█')} relative scale (shorter is better)")

def print_section(title):
    """Print a section header."""
    w = 80
    print()
    print(f"  {bold('─── ' + title + ' ')}" + "─" * max(2, w - len(title) - 6))
    print()

def main():
    setup_toolchain_paths()

    parser = argparse.ArgumentParser(
        description="IRIS Benchmark Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python run_benchmarks.py
  python run_benchmarks.py --build
  python run_benchmarks.py --runs 10 --filter fib,sieve
        """,
    )
    parser.add_argument("--runs", type=int, default=5,
                        help="Number of runs per benchmark (default: 5)")
    parser.add_argument("--filter", type=str, default="",
                        help="Comma-separated substring filter (e.g. fib,sieve)")
    parser.add_argument("--luajit", type=Path, default=None,
                        help="Path to LuaJIT executable")
    parser.add_argument("--python", type=Path, default=Path(DEFAULT_PYTHON),
                        help="Path to Python executable")
    parser.add_argument("--exe", type=str, action="append",
                        help="Label:path/to/IRIS.exe")
    parser.add_argument("--build", action="store_true",
                        help="Build all available compiler targets before running benchmarks")
    parser.add_argument("--no-luajit", action="store_true",
                        help="Skip LuaJIT benchmarks")
    parser.add_argument("--no-python", action="store_true",
                        help="Skip Python benchmarks")
    args = parser.parse_args()

    bench_dir = BENCHMARK_DIR
    if not bench_dir.exists():
        print(f"  {red('✗')} Benchmark directory not found: {bench_dir}")
        sys.exit(1)

    iris_files = sorted(bench_dir.glob("*.iris"))
    if not iris_files:
        print(f"  {red('✗')} No .iris benchmark files found in {bench_dir}")
        sys.exit(1)

    # -- Option to build targets --------------------------------------------
    if args.build:
        build_targets()

    # -- Detect & validate IRIS executables ---------------------------------
    exes = detect_iris_builds()
    if args.exe:
        for item in args.exe:
            if ":" in item:
                label, path = item.split(":", 1)
                exes[label] = Path(path)
            else:
                exes["Custom"] = Path(item)

    if not exes:
        print(f"  {red('✗')} No working IRIS executables found.")
        print(f"  {dim('Tip:')} Build IRIS first, or run with --build flag.")
        sys.exit(1)

    # -- Setup runners -------------------------------------------------------
    labels = list(exes.keys())
    
    luajit_path = None
    if not args.no_luajit:
        if args.luajit:
            luajit_path = args.luajit
        else:
            luajit_path = find_luajit()

    python_path = args.python if not args.no_python else None

    if luajit_path and Path(luajit_path).exists():
        labels.append("LuaJIT")
    if python_path and Path(python_path).exists():
        labels.append("Python")

    # -- Print configuration -------------------------------------------------
    print_section("Configuration")
    print(f"  ┌{'─' * 76}┐")
    for label, path in exes.items():
        line = f"  {cyan(label):<15} {dim(str(path))}"
        print(f"  │ {pad_visible(line, 74)} │")
    if luajit_path and Path(luajit_path).exists():
        line = f"  {cyan('LuaJIT'):<15} {dim(str(luajit_path))}"
        print(f"  │ {pad_visible(line, 74)} │")
    if python_path and Path(python_path).exists():
        line = f"  {cyan('Python'):<15} {dim(str(python_path))}"
        print(f"  │ {pad_visible(line, 74)} │")
    print(f"  │ {'─' * 74} │")
    print(f"  │ {pad_visible(f'  Runs per benchmark: {args.runs}', 74)} │")
    if args.filter:
        print(f"  │ {pad_visible(f'  Filter: \'{args.filter}\'', 74)} │")
    print(f"  └{'─' * 76}┘")

    # -- Run benchmarks ------------------------------------------------------
    all_results = {}

    bench_names = []
    for f in iris_files:
        name = f.stem
        if name.endswith("_timed") or name == "tiny_test":
            continue
        if args.filter:
            filters = [s.strip() for s in args.filter.split(",")]
            if not any(fs in name for fs in filters):
                continue
        bench_names.append(name)

    total = len(bench_names)
    if total == 0:
        print(f"\n  {yellow('⚠')} No benchmarks matched the filter.")
        sys.exit(0)

    print_section(f"Running Benchmarks ({total} total)")
    for idx, name in enumerate(bench_names, 1):
        pct = idx / total
        filled = int(pct * 20)
        bar_str = "█" * filled + "░" * (20 - filled)
        print(f"  {cyan(bar_str)}  {idx:>3}/{total}  {bold(name):<20} ", end="", flush=True)

        res = benchmarkfile(name, exes, luajit_path, python_path, args.runs)
        all_results[name] = res

        our = res.get(labels[0])
        if our:
            t = fmt_time(our.median) + " ms"
            print(f" {green(f' {t:>8} ')}")
        else:
            print(f" {red(' FAILED ')}")

    # -- Print final table ---------------------------------------------------
    print_section("Benchmark Comparison Results")
    print_summary_table(all_results, labels)

    # -- Geometric mean summary statistics ----------------------------------
    valid_runners = []
    for label in labels:
        times = []
        for res in all_results.values():
            r = res.get(label)
            if r and r.median > 0 and not math.isinf(r.median):
                times.append(r.median)
        if len(times) == len(all_results):
            gmean = math.exp(sum(math.log(t) for t in times) / len(times))
            valid_runners.append((label, gmean))
            
    if valid_runners:
        print_section("Summary Statistics")
        print(f"  ┌{'─' * 76}┐")
        print(f"  │ {bold('Geometric Mean (relative speed across all benchmarks)'):<74} │")
        print(f"  │ {'─' * 76} │")
        
        valid_runners.sort(key=lambda x: x[1])
        fastest_name, fastest_gmean = valid_runners[0]
        
        for idx, (label, gmean) in enumerate(valid_runners):
            ratio_vs_fastest = gmean / fastest_gmean if fastest_gmean > 0 else 1.0
            line_str = f"    {idx+1}. {cyan(label):<15} {fmt_time(gmean) + ' ms':<15}"
            if idx == 0:
                line_str += f" {green('(fastest)'):<25}"
            else:
                line_str += f" {yellow(f'+{((ratio_vs_fastest - 1.0)*100.0):.1f}% slower')}"
            print(f"  │ {pad_visible(line_str, 74)} │")
            
        print(f"  └{'─' * 76}┘")

    print()
    print(f"  {dim('Benchmarking completed successfully.')}")

if __name__ == "__main__":
    main()
