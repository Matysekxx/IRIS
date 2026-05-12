# Benchmark 8: Nested loops
# Tests: Nested loop overhead and basic math
import time

def main():
    start = time.perf_counter()
    count = 0
    i = 0
    while i < 10000:
        j = 0
        while j < 10000:
            count = count + 1
            j = j + 1
        i = i + 1
    end = time.perf_counter()
    
    print(f"Nested loop result: {count}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")
    return count

if __name__ == "__main__":
    main()
