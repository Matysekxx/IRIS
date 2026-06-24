import subprocess, sys, time
exe = r'build_msvc\Release\IRIS.exe'
benchmarks = [
    'mandelbrot', 'math_heavy', 'matrix_mul', 'nested_loops',
    'sieve', 'spectral_norm', 'pi_approx', 'loop',
    'bubble_sort', 'hashmap_ops', 'stress_objects', 'string_concat'
]
all_ok = True
for name in benchmarks:
    path = f'benchmarks/{name}.iris'
    try:
        start = time.time()
        r = subprocess.run([exe, path], capture_output=True, text=True, timeout=30)
        elapsed = time.time() - start
        ok = r.returncode == 0
        out = r.stdout.strip()[-200:] if r.stdout else ''
        err = r.stderr.strip()[-200:] if r.stderr else ''
        print(f'{name}: exit={r.returncode} ok={ok} {elapsed:.2f}s')
        if out:
            print(f'  STDOUT: {out}')
        if err:
            print(f'  STDERR: {err}')
        if not ok:
            all_ok = False
    except subprocess.TimeoutExpired:
        print(f'{name}: TIMEOUT (>30s)')
        all_ok = False
print(f'\nAll OK: {all_ok}')
