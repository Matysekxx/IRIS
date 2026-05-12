# Benchmark 10: Sieve of Eratosthenes
# Tests: Array access, integer math, large array allocation

import time

def main():
    limit = 1000000
    # Use bytearray for performance (similar to IRIS int[] but more compact)
    primes = bytearray([0]) * (limit + 1)
    
    primes[0] = 1
    primes[1] = 1
    
    print("Finding primes up to 1,000,000...")
    
    start = time.perf_counter()
    p = 2
    while p * p <= limit:
        if primes[p] == 0:
            for i in range(p * p, limit + 1, p):
                primes[i] = 1
        p += 1
    
    count = 0
    for i in range(limit + 1):
        if primes[i] == 0:
            count += 1
    end = time.perf_counter()
    
    print(f"Finished. Found {count} primes.")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")

if __name__ == "__main__":
    main()
