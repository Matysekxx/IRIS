import time

def matrix_mult(n):
    A = [[float(i + j) for j in range(n)] for i in range(n)]
    B = [[float(i * j) for j in range(n)] for i in range(n)]
    C = [[0.0 for _ in range(n)] for _ in range(n)]
    
    start = time.time()
    for i in range(n):
        for j in range(n):
            s = 0.0
            for k in range(n):
                s += A[i][k] * B[k][j]
            C[i][j] = s
    end = time.time()
    return (end - start) * 1000

def sieve(limit):
    start = time.time()
    primes = [True] * (limit + 1)
    primes[0] = primes[1] = False
    for p in range(2, int(limit**0.5) + 1):
        if primes[p]:
            for i in range(p * p, limit + 1, p):
                primes[i] = False
    count = sum(1 for p in range(limit + 1) if primes[p])
    end = time.time()
    return (end - start) * 1000

print(f"Matrix Mult (100x100): {matrix_mult(100):.2f} ms")
print(f"Sieve of Erato. (1M): {sieve(1000000):.2f} ms")
