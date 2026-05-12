import time

def benchmark_list():
    print("--- Python List Benchmark ---")
    start = time.time()
    l = []
    for i in range(1000000):
        l.append(i)
    
    total = 0
    for i in range(1000000):
        total += l[i]
    
    end = time.time()
    print(f"Time: {(end - start) * 1000:.2f} ms")
    print(f"Total: {total}")

def benchmark_map():
    print("--- Python Dict Benchmark ---")
    start = time.time()
    d = {}
    for i in range(500000):
        d[i] = i * 2
    
    total = 0
    for i in range(500000):
        total += d[i]
    
    end = time.time()
    print(f"Time: {(end - start) * 1000:.2f} ms")
    print(f"Total: {total}")

if __name__ == "__main__":
    benchmark_list()
    benchmark_map()
