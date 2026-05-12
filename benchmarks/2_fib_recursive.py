# Benchmark 2: Recursive Fibonacci
# Tests: Function call overhead, recursion (no TCO possible)
import time

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

def main():
    start = time.perf_counter()
    result = fib(35)
    end = time.perf_counter()
    print(f"Fibonacci(35): {result}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")
    return result

if __name__ == "__main__":
    main()
