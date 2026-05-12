# Benchmark 9: Matrix Multiplication (100x100)
# Tests: Array indexing, nested loops, floating point math

import time

def main():
    size = 100
    # Flattened list for fairer comparison with IRIS double[]
    a = [float(i + j) for i in range(size) for j in range(size)]
    b = [float(i - j) for i in range(size) for j in range(size)]
    c = [0.0] * (size * size)
    
    print("Multiplying 100x100 matrices...")
    
    start = time.perf_counter()
    for i in range(size):
        offset_i = i * size
        for j in range(size):
            sum_val = 0.0
            for k in range(size):
                sum_val += a[offset_i + k] * b[k * size + j]
            c[offset_i + j] = sum_val
    end = time.perf_counter()
    
    print("Matrix multiplication finished.")
    print(f"Result at [50, 50]: {c[50 * size + 50]}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")

if __name__ == "__main__":
    main()
