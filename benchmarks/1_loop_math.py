import time

start = time.time()
sum_val = 0
for i in range(100000000):
    sum_val += i % 17
end = time.time()
print(f"Loop Math Result: {sum_val}")
print(f"Python time: {(end - start) * 1000:.2f} ms")