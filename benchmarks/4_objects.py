class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        
    def sum(self):
        return self.x + self.y

i = 0
total = 0
while i < 2000000:
    p = Point(i, i + 1)
    total = total + p.sum()
    i = i + 1
