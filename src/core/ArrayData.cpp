#include "ArrayData.h"
#include "Value.h"
#include "GC.h"
#include "MemoryPool.h"
#include <new>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <emmintrin.h>


namespace iris::core {
    static size_t arrayDataAllocSize(size_t size, ArrayData::ElementType type) {
        size_t elemSize = (type == ArrayData::DOUBLE) ? sizeof(double) : (type == ArrayData::INT ? sizeof(int) : sizeof(Value));
        return sizeof(ArrayData) + size * elemSize;
    }

    ArrayData::ArrayData(size_t size, ElementType type)
        : Managed(ManagedType::Array, arrayDataAllocSize(size, type)), length(size), elemType(type), typeScore(0) {
        char* elementStart = reinterpret_cast<char*>(this) + sizeof(ArrayData);
        if (type == DOUBLE) {
            double* dblData = reinterpret_cast<double*>(elementStart);
            std::memset(dblData, 0, size * sizeof(double));
        } else if (type == VALUE || type == UNTYPED) {
            elemType = VALUE;
            Value* valData = reinterpret_cast<Value*>(elementStart);
            size_t i = 0;
            __m128i val128 = _mm_set1_epi64x(0x7FFA000000000000ULL);
            for (; i + 1 < size; i += 2) {
                _mm_storeu_si128(reinterpret_cast<__m128i*>(&valData[i]), val128);
            }
            for (; i < size; ++i) {
                new(&valData[i]) Value();
            }
        } else {
            int* intData = reinterpret_cast<int*>(elementStart);
            std::memset(intData, 0, size * sizeof(int));
        }
    }

    ArrayData::~ArrayData() {
        if (elemType == VALUE) {
            Value* valData = getValData();
            for (size_t i = 0; i < length; ++i) {
                valData[i].~Value();
            }
        }
    }

    ArrayData* ArrayData::create(size_t size, ElementType type) {
        size_t elemSize = (type == DOUBLE) ? sizeof(double) : (type == INT ? sizeof(int) : sizeof(Value));
        size_t extra = size * elemSize;
        return new (extra) ArrayData(size, type);
    }

    void* ArrayData::operator new(size_t size, size_t extra_size) {
        void* ptr = GC::nurseryAlloc(size + extra_size);
        if (ptr) return ptr;
        ptr = std::malloc(size + extra_size);
        if (!ptr) throw std::bad_alloc();
        return ptr;
    }

    void ArrayData::operator delete(void* ptr, size_t extra_size) {
        if (currentGC && currentGC->isInNursery(ptr)) return;
        std::free(ptr);
    }

    void ArrayData::operator delete(void* ptr) {
        if (currentGC && currentGC->isInNursery(ptr)) return;
        std::free(ptr);
    }
}

