import subprocess, time
exe = r'build_msvc\Release\IRIS.exe'
benchmarks = {
    'ackermann': r'benchmarks/ackermann.iris',
    'array_sum': r'benchmarks/array_sum.iris',
    'bit_ops': r'benchmarks/bit_ops.iris',
    'bubble_sort': r'benchmarks/bubble_sort.iris',
    'factorial': r'benchmarks/factorial.iris',
    'fib': r'benchmarks/fib.iris',
    'fibo': r'benchmarks/fibo.iris',
    'hashmap_ops': r'benchmarks/hashmap_ops.iris',
    'loop': r'benchmarks/loop.iris',
    'mandelbrot': r'benchmarks/mandelbrot.iris',
    'math_heavy': r'benchmarks/math_heavy.iris',
    'string_concat': r'benchmarks/string_concat.iris',
    'stress_objects': r'benchmarks/stress_objects.iris',
}
all_ok = True
for name, path in benchmarks.items():
    try:
        start = time.time()
        r = subprocess.run([exe, path], capture_output=True, text=True, timeout=30)
        elapsed = time.time() - start
        ok = r.returncode == 0
        out_last = r.stdout.strip()[-80:] if r.stdout.strip() else ''
        print(f'{name}: {"OK" if ok else "FAIL"} ({elapsed:.2f}s) | {out_last}')
        if not ok:
            all_ok = False
    except subprocess.TimeoutExpired:
        print(f'{name}: TIMEOUT')
        all_ok = False
print(f'All OK: {all_ok}')
