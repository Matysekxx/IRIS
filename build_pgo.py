#!/usr/bin/env python3
"""
IRIS Profile-Guided Optimization (PGO) Build Script
Supports both MSVC and GCC (MinGW) toolchains.
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

# Base paths
ROOT_DIR = Path(__file__).parent.resolve()
BENCHMARK_DIR = ROOT_DIR / "benchmarks"

# Training workloads to run for profile collection
TRAINING_BENCHMARKS = [
    "fib.iris",
    "sieve.iris",
    "mandelbrot.iris",
    "string_concat.iris",
    "array_sum.iris",
    "spectral_norm.iris",
    "bubble_sort.iris",
    "hashmap_ops.iris",
    "math_heavy.iris"
]

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
            print(f"[*] Appended CLion toolchain paths: {', '.join(paths)}")

def run_command(args, cwd=None):
    """Run a subprocess and check for success."""
    print(f"[CMD] {' '.join(args)}")
    try:
        subprocess.run(args, check=True, cwd=cwd)
    except subprocess.CalledProcessError as e:
        print(f"[-] Command failed with exit code {e.returncode}")
        sys.exit(1)

def copy_pgo_runtime(dest_dir):
    """Find pgort140.dll and pgort140ui.dll in MSVC tools and copy them to dest_dir."""
    print("[*] Copying PGO runtime DLLs...")
    vs_dir = Path(r"C:\Program Files\Microsoft Visual Studio")
    if not vs_dir.exists():
        vs_dir = Path(r"C:\Program Files (x86)\Microsoft Visual Studio")
    if not vs_dir.exists():
        print("    [Warning] Visual Studio directory not found. Skipping DLL copy.")
        return
        
    dll_path = None
    for p in vs_dir.glob("**/Hostx64/x64/pgort140.dll"):
        dll_path = p
        break
        
    if dll_path and dll_path.exists():
        import shutil
        shutil.copy(str(dll_path), str(dest_dir))
        print(f"    Copied pgort140.dll from {dll_path} to {dest_dir}")
        
        ui_dll_path = dll_path.parent / "1033" / "pgort140ui.dll"
        if not ui_dll_path.exists():
            for p in dll_path.parent.glob("*/pgort140ui.dll"):
                ui_dll_path = p
                break
        if ui_dll_path.exists():
            dest_ui_dir = dest_dir / ui_dll_path.parent.name
            dest_ui_dir.mkdir(exist_ok=True)
            shutil.copy(str(ui_dll_path), str(dest_ui_dir))
            print(f"    Copied pgort140ui.dll from {ui_dll_path} to {dest_ui_dir}")
    else:
        print("    [Warning] pgort140.dll not found in Visual Studio folder.")

def run_training_workloads(exe_path):
    """Execute training workloads on the instrumented binary to generate profile data."""
    print(f"[*] Running training workloads with: {exe_path}")
    if not exe_path.exists():
        print(f"[-] Instrumented binary not found at: {exe_path}")
        sys.exit(1)
        
    for bench in TRAINING_BENCHMARKS:
        bench_path = BENCHMARK_DIR / bench
        if bench_path.exists():
            print(f"    Running {bench}...")
            try:
                # Run binary to generate profile data
                env = os.environ.copy()
                env["VCPROFILE_PATH"] = str(exe_path.parent)
                subprocess.run([str(exe_path), str(bench_path)], env=env, capture_output=True, timeout=15, cwd=str(exe_path.parent))
            except subprocess.TimeoutExpired:
                print(f"    [Warning] {bench} timed out during training.")
            except Exception as e:
                print(f"    [Warning] Error running {bench}: {e}")
        else:
            print(f"    [Warning] Benchmark {bench} not found in {BENCHMARK_DIR}")

def build_msvc_pgo():
    """Execute PGO build workflow using MSVC compiler and linker."""
    print("\n" + "="*60)
    print("  MSVC Profile-Guided Optimization (PGO) Build")
    print("="*60)
    
    build_dir = ROOT_DIR / "build_msvc_pgo"
    exe_path = build_dir / "Release" / "IRIS.exe"
    
    # 1. Configure and Build Instrumented Binary
    print("\n[Step 1] Configuring and building instrumented binary...")
    run_command([
        "cmake", "-B", str(build_dir), "-S", str(ROOT_DIR),
        "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DIRIS_MSVC_PGO=GEN"
    ])
    run_command(["cmake", "--build", str(build_dir), "--config", "Release", "--clean-first"])
    
    # 2. Run Training workloads
    print("\n[Step 2] Collecting profile information from benchmarks...")
    copy_pgo_runtime(exe_path.parent)
    run_training_workloads(exe_path)
    
    # 3. Rebuild with Optimization using feedback profiles
    print("\n[Step 3] Rebuilding with profile-guided optimization flags...")
    run_command([
        "cmake", "-B", str(build_dir), "-S", str(ROOT_DIR),
        "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DIRIS_MSVC_PGO=USE"
    ])
    run_command(["cmake", "--build", str(build_dir), "--config", "Release"])
    
    print("\n[+] MSVC PGO Build completed successfully!")
    print(f"    Optimized binary available at: {exe_path}")

def build_gcc_pgo():
    """Execute PGO build workflow using GCC (MinGW)."""
    print("\n" + "="*60)
    print("  GCC (MinGW) Profile-Guided Optimization (PGO) Build")
    print("="*60)
    
    build_dir = ROOT_DIR / "build_gcc_pgo"
    exe_path = build_dir / "IRIS.exe"
    
    # 1. Configure and Build Instrumented Binary
    print("\n[Step 1] Configuring and building instrumented binary...")
    run_command([
        "cmake", "-B", str(build_dir), "-S", str(ROOT_DIR),
        "-G", "MinGW Makefiles", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_FLAGS=-fprofile-generate",
        "-DCMAKE_EXE_LINKER_FLAGS=-fprofile-generate",
        "-DCMAKE_SHARED_LINKER_FLAGS=-fprofile-generate"
    ])
    run_command(["cmake", "--build", str(build_dir), "--clean-first", "--parallel"])
    
    # 2. Run Training workloads
    print("\n[Step 2] Collecting profile information from benchmarks...")
    run_training_workloads(exe_path)
    
    # 3. Rebuild with Optimization using feedback profiles
    print("\n[Step 3] Rebuilding with profile-guided optimization flags...")
    run_command([
        "cmake", "-B", str(build_dir), "-S", str(ROOT_DIR),
        "-G", "MinGW Makefiles", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_FLAGS=-fprofile-use -Wno-error=coverage-mismatch",
        "-DCMAKE_EXE_LINKER_FLAGS=-fprofile-use",
        "-DCMAKE_SHARED_LINKER_FLAGS=-fprofile-use"
    ])
    run_command(["cmake", "--build", str(build_dir), "--parallel"])
    
    print("\n[+] GCC PGO Build completed successfully!")
    print(f"    Optimized binary available at: {exe_path}")

def main():
    parser = argparse.ArgumentParser(description="IRIS PGO Build automation.")
    parser.add_argument("--compiler", choices=["msvc", "gcc", "all"], default="all",
                        help="Select compiler toolchain to build (default: all)")
    args = parser.parse_args()
    
    setup_toolchain_paths()
    
    if args.compiler in ["msvc", "all"]:
        try:
            build_msvc_pgo()
        except Exception as e:
            print(f"[-] MSVC PGO build failed: {e}")
            if args.compiler == "msvc":
                sys.exit(1)
                
    if args.compiler in ["gcc", "all"]:
        try:
            build_gcc_pgo()
        except Exception as e:
            print(f"[-] GCC PGO build failed: {e}")
            if args.compiler == "gcc":
                sys.exit(1)

if __name__ == "__main__":
    main()
