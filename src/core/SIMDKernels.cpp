#include "SIMDKernels.h"
#include <emmintrin.h>

extern "C" {
    double sum_array_double_simd(const double* arr, size_t len) {
        size_t i = 0;
        __m128d sum_vec = _mm_setzero_pd();
        
        // Unroll loop to process 2 doubles (128-bits) per iteration
        for (; i + 1 < len; i += 2) {
            __m128d val = _mm_loadu_pd(arr + i);
            sum_vec = _mm_add_pd(sum_vec, val);
        }
        
        // Sum the elements of the register
        double temp[2];
        _mm_storeu_pd(temp, sum_vec);
        double sum = temp[0] + temp[1];
        
        // Cleanup remainder elements
        for (; i < len; ++i) {
            sum += arr[i];
        }
        
        return sum;
    }

    int sum_array_int_simd(const int* arr, size_t len) {
        size_t i = 0;
        __m128i sum_vec = _mm_setzero_si128();
        
        // Unroll loop to process 4 integers (128-bits) per iteration
        for (; i + 3 < len; i += 4) {
            __m128i val = _mm_loadu_si128(reinterpret_cast<const __m128i*>(arr + i));
            sum_vec = _mm_add_epi32(sum_vec, val);
        }
        
        // Sum the elements of the register
        int temp[4];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(temp), sum_vec);
        int sum = temp[0] + temp[1] + temp[2] + temp[3];
        
        // Cleanup remainder elements
        for (; i < len; ++i) {
            sum += arr[i];
        }
        
        return sum;
    }
}
