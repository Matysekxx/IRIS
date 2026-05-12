import time

start = time.time()
size = 10000000
arr = [0] * size

for i in range(size):
    arr[i] = i * 2 + 1

sum_val = 0
for i in range(size):
    sum_val += arr[i]

end = time.time()
print(f"Array Sum: {sum_val}")
print(f"Python time: {(end - start) * 1000:.2f} ms")