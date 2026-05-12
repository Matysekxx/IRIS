import time

start = time.time()
s = ""
for i in range(100000):
    s = s + str(i)
end = time.time()

print(f"String length: {len(s)}")
print(f"Python time: {(end - start) * 1000:.2f} ms")