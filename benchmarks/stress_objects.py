import time

class HeavyObject:
    def __init__(self):
        self.a = 0
        self.b = 0
        self.c = 0
        self.d = 0
    
    def init(self, v):
        self.a = v
        self.b = v + 1
        self.c = v + 2
        self.d = v + 3
    
    def sum(self):
        return self.a + self.b + self.c + self.d

total = 0
start = time.time()
for i in range(1000000):
    obj = HeavyObject()
    obj.init(i)
    total += obj.sum()
end = time.time()
print(f"Total sum: {total}")
print(f"Time: {(end-start)*1000:.2f} ms")
