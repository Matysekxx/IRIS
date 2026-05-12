size = 5000
arr = [0] * size

i = 0
while i < size:
    arr[i] = size - i
    i = i + 1

changed = True
while changed:
    changed = False
    j = 0
    while j < size - 1:
        if arr[j] > arr[j + 1]:
            temp = arr[j]
            arr[j] = arr[j + 1]
            arr[j + 1] = temp
            changed = True
        j = j + 1
