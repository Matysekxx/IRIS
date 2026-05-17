import time

def bench_sso():
    print("=== PYTHON SSO TEST ===")
    start = time.time()
    count = 0
    for i in range(1000000):
        s1 = "id" + str(i)
        if s1 == "id500000":
            count += 1
    end = time.time()
    print(f"Result: {(end - start) * 1000:.2f} ms")

bench_sso()
