#include "ArrayData.h"
#include "Value.h"
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
        : Managed(ManagedType::Array, arrayDataAllocSize(size, type)), intData(nullptr), length(size), elemType(type) {
        if (type == DOUBLE) {
            dblData = static_cast<double *>(std::calloc(size, sizeof(double)));
            if (!dblData) throw std::runtime_error("Array allocation failed for size " + std::to_string(size));
        } else if (type == VALUE || type == UNTYPED) {
            elemType = VALUE;
            valData = static_cast<Value *>(std::malloc(size * sizeof(Value)));
            if (!valData) throw std::runtime_error("Array allocation failed for size " + std::to_string(size));
            size_t i = 0;
            __m128i val128 = _mm_set1_epi64x(0x7FFA000000000000ULL);
            for (; i + 1 < size; i += 2) {
                _mm_storeu_si128(reinterpret_cast<__m128i*>(&valData[i]), val128);
            }
            for (; i < size; ++i) {
                new(&valData[i]) Value();
            }
        } else {
            intData = static_cast<int *>(std::calloc(size, sizeof(int)));
            if (!intData) throw std::runtime_error("Array allocation failed for size " + std::to_string(size));
        }
    }

    ArrayData::~ArrayData() {
        if (elemType == DOUBLE) {
            std::free(dblData);
        } else if (elemType == VALUE) {
            for (size_t i = 0; i < length; ++i) {
                valData[i].~Value();
            }
            std::free(valData);
        } else {
            std::free(intData);
        }
    }

    ArrayData::ArrayData(const ArrayData &other)
        : Managed(ManagedType::Array, arrayDataAllocSize(other.length, other.elemType)), intData(nullptr), length(other.length), elemType(other.elemType) {
        if (other.elemType == DOUBLE) {
            dblData = static_cast<double *>(std::malloc(length * sizeof(double)));
            if (!dblData) throw std::runtime_error("Array copy allocation failed");
            std::memcpy(dblData, other.dblData, length * sizeof(double));
        } else if (other.elemType == VALUE) {
            valData = static_cast<Value *>(std::malloc(length * sizeof(Value)));
            if (!valData) throw std::runtime_error("Array copy allocation failed");
            for (size_t i = 0; i < length; ++i) {
                new(&valData[i]) Value(other.valData[i]);
            }
        } else {
            intData = static_cast<int *>(std::malloc(length * sizeof(int)));
            if (!intData) throw std::runtime_error("Array copy allocation failed");
            std::memcpy(intData, other.intData, length * sizeof(int));
        }
    }

    ArrayData::ArrayData(ArrayData &&other) noexcept
        : Managed(ManagedType::Array, 0), intData(other.intData), length(other.length), elemType(other.elemType) {
        other.intData = nullptr;
        other.length = 0;
        other.elemType = UNTYPED;
    }

    ArrayData &ArrayData::operator=(const ArrayData &other) {
        if (this != &other) {
            this->~ArrayData();
            ::new(this) ArrayData(other);
        }
        return *this;
    }

    ArrayData &ArrayData::operator=(ArrayData &&other) noexcept {
        if (this != &other) {
            this->~ArrayData();
            intData = other.intData;
            length = other.length;
            elemType = other.elemType;

            other.intData = nullptr;
            other.length = 0;
            other.elemType = UNTYPED;
        }
        return *this;
    }

    static MemoryPool<ArrayData, 1024> arrayDataPool;

    void* ArrayData::operator new(size_t size) {
        if (size != sizeof(ArrayData)) return ::operator new(size);
        return arrayDataPool.allocate();
    }

    void ArrayData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(ArrayData)) {
            ::operator delete(ptr);
            return;
        }
        arrayDataPool.deallocate(static_cast<ArrayData*>(ptr));
    }
}

