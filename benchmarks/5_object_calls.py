# Benchmark 5: Object method calls
# Tests: OOP, method dispatch, object creation
import time

class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def distance(self):
        return self.x * self.x + self.y * self.y

def main():
    start = time.perf_counter()
    points = [None] * 1000000
    i = 0
    
    # Create objects
    while i < 1000000:
        points[i] = Point(i, i + 1)
        i = i + 1
    
    # Call methods
    total = 0
    i = 0
    while i < 1000000:
        total = total + points[i].distance()
        i = i + 1
    end = time.perf_counter()
    
    print(f"Total distance: {total}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")
    return total

if __name__ == "__main__":
    main()
