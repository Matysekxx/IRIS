import subprocess
import re
import datetime

IRIS_EXE = r".\build_mingw\IRIS.exe"
PYTHON_EXE = "python"
LUAJIT_EXE = r"..\..\..\..\lua\luajit.exe"

benchmarks = [
    "01_loop_math",
    "02_fibonacci",
    "03_array_ops",
    "04_prime_sieve",
    "05_matrix_mult",
    "06_bubble_sort",
    "07_string_bench",
    "08_object_dispatch",
    "09_float_math",
    "10_nested_loops"
]

def run_cmd(cmd):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60, shell=True)
        return result.stdout.strip()
    except Exception as e:
        return f"Error: {str(e)}"

def extract_ms(output):
    # Look for anything ending in "XX ms"
    match = re.findall(r"([\d.]+)\s*ms", output)
    if match:
        return match[0] # Return the first match found
    return "N/A"

results = []
for b in benchmarks:
    print(f"Running {b}...")
    iris_out = run_cmd(f"{IRIS_EXE} benchmarks/{b}.iris")
    py_out = run_cmd(f"{PYTHON_EXE} benchmarks/{b}.py")
    lua_out = run_cmd(f"{LUAJIT_EXE} benchmarks/{b}.lua")
    
    iris_ms = extract_ms(iris_out)
    py_ms = extract_ms(py_out)
    lua_ms = extract_ms(lua_out)
    
    results.append({
        "name": b,
        "iris": iris_ms,
        "python": py_ms,
        "lua": lua_ms
    })

# Generate Markdown Report
report = f"# IRIS Performance Report - {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n"
report += "| Benchmark | IRIS (ms) | Python (ms) | LuaJIT (ms) | Speedup vs Py |\n"
report += "|-----------|-----------|-------------|-------------|----------------|\n"

for r in results:
    try:
        speedup = f"{float(r['python']) / float(r['iris']):.2f}x" if r['python'] != "N/A" and r['iris'] != "N/A" else "N/A"
    except:
        speedup = "N/A"
    report += f"| {r['name']} | {r['iris']} | {r['python']} | {r['lua']} | {speedup} |\n"

with open("BENCHMARKS.md", "w", encoding="utf-8") as f:
    with open("BENCHMARKS.md", "w", encoding="utf-8") as f:
        f.write(report)

print("\nResults written to BENCHMARKS.md")
print(report)
