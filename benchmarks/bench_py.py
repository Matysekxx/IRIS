import time

def bench_all():
    print("=== PYTHON FULL REPORT ===")
    
    # 1. Loop Math (1M)
    s1 = time.time()
    x = 0
    for i in range(1000000): x += 1
    e1 = time.time()
    print(f"1. Loop Math (1M): {(e1-s1)*1000:.2f} ms")

    # 2. Raw Array (1M)
    s2 = time.time()
    arr = [0] * 1000000
    for i in range(1000000): arr[i] = i
    sum_val = 0
    for i in range(1000000): sum_val += arr[i]
    e2 = time.time()
    print(f"2. Raw Array (1M): {(e2-s2)*1000:.2f} ms")

    # 3. Fibonacci(30)
    def fib(n):
        if n < 2: return n
        return fib(n-1) + fib(n-2)
    s3 = time.time()
    fib(30)
    e3 = time.time()
    print(f"3. Fibonacci(30): {(e3-s3)*1000:.2f} ms")

bench_all()
