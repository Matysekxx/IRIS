#ifndef IRIS_SIMD_KERNELS_H
#define IRIS_SIMD_KERNELS_H

#include <cstddef>

extern "C" {
    /** @brief Vectorized sum of double-precision float array using SSE2. */
    double sum_array_double_simd(const double* arr, size_t len);

    /** @brief Vectorized sum of 32-bit integer array using SSE2. */
    int sum_array_int_simd(const int* arr, size_t len);
}

#endif // IRIS_SIMD_KERNELS_H
