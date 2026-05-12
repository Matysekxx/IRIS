# Benchmark 11: Mandelbrot Set
# Tests: Heavy floating point math, complex conditions, nested loops

import time

def main():
    width = 300
    height = 300
    max_iter = 100
    
    print("Calculating Mandelbrot set (300x300)...")
    
    count = 0
    start = time.perf_counter()
    for y in range(height):
        for x in range(width):
            c_re = (x - width / 2.0) * 4.0 / width
            c_im = (y - height / 2.0) * 4.0 / height
            x_re = 0.0
            x_im = 0.0
            iter_count = 0
            
            while x_re * x_re + x_im * x_im <= 4.0 and iter_count < max_iter:
                x_new = x_re * x_re - x_im * x_im + c_re
                x_im = 2.0 * x_re * x_im + c_im
                x_re = x_new
                iter_count += 1
                
            if iter_count == max_iter:
                count += 1
    end = time.perf_counter()
    
    print(f"Finished. Mandelbrot points found: {count}")
    print(f"Čistý čas Python: {(end - start) * 1000:.4f} ms")

if __name__ == "__main__":
    main()
