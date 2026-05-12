import array

size = 5000000
arr = array.array('i', [0] * size)

i = 0
while i < size:
    arr[i] = i * 2
    i = i + 1

sum_val = 0
j = 0
while j < size:
    sum_val = sum_val + arr[j]
    j = j + 1
