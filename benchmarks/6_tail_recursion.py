# Benchmark 6: Tail-recursive sum (no TCO in Python)
# Tests: Function call overhead
# Note: Python doesn't support TCO, so this will hit recursion limit
# Using iterative version for fair comparison
import time

def sum_tail(n, acc):
    # Iterative version since Python has no TCO
    while n > 0:
        acc = acc + n
        n = n - 1
    return acc

def main():
    start = time.perf_counter()
    result = sum_tail(10000000, 0)
    end = time.perf_counter()
    
    print(f"Tail recursion sum: {result}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")
    return result

if __name__ == "__main__":
    main()
