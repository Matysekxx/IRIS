import time
import sys

# Increase recursion depth for deep quicksort
sys.setrecursionlimit(200000)

def partition(arr, low, high):
    pivot = arr[high]
    i = low - 1
    for j in range(low, high):
        if arr[j] <= pivot:
            i += 1
            arr[i], arr[j] = arr[j], arr[i]
    arr[i+1], arr[high] = arr[high], arr[i+1]
    return i + 1

def quicksort(arr, low, high):
    if low < high:
        pi = partition(arr, low, high)
        quicksort(arr, low, pi - 1)
        quicksort(arr, pi + 1, high)

def main():
    size = 100000
    arr = []
    seed = 42
    for i in range(size):
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        arr.append(seed % 100000)
    
    print(f"Sorting {size} elements...")
    start = time.time()
    quicksort(arr, 0, size - 1)
    end = time.time()
    
    # Verify
    sorted_ok = 1
    for i in range(size - 1):
        if arr[i] > arr[i+1]:
            sorted_ok = 0
            break
            
    print(f"Sorted correctly: {sorted_ok}")
    print(f"Python time: {(end - start) * 1000:.2f} ms")

main()