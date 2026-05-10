#include "ArrayData.h"
#include "Value.h"
#include <new>
#include <stdexcept>
#include <cstring>

namespace iris::core {
    ArrayData::ArrayData(size_t size, ElementType type) 
        : intData(nullptr), length(size), elemType(type) {
        if (type == DOUBLE) {
            dblData = static_cast<double*>(std::calloc(size, sizeof(double)));
            if (!dblData) throw std::runtime_error("Array allocation failed");
        } else if (type == VALUE) {
            valData = static_cast<Value*>(std::malloc(size * sizeof(Value)));
            if (!valData) throw std::runtime_error("Array allocation failed");
            for (size_t i = 0; i < size; ++i) {
                new (&valData[i]) Value();
            }
        } else {
            intData = static_cast<int*>(std::calloc(size, sizeof(int)));
            if (!intData) throw std::runtime_error("Array allocation failed");
            if (type == UNTYPED) elemType = INT;
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

    // OPTIMIZATION: Copy constructor for COW
    ArrayData::ArrayData(const ArrayData& other) 
        : intData(nullptr), length(other.length), elemType(other.elemType) {
        if (other.elemType == DOUBLE) {
            dblData = static_cast<double*>(std::malloc(length * sizeof(double)));
            if (!dblData) throw std::runtime_error("Array copy allocation failed");
            std::memcpy(dblData, other.dblData, length * sizeof(double));
        } else if (other.elemType == VALUE) {
            valData = static_cast<Value*>(std::malloc(length * sizeof(Value)));
            if (!valData) throw std::runtime_error("Array copy allocation failed");
            for (size_t i = 0; i < length; ++i) {
                new (&valData[i]) Value(other.valData[i]);
            }
        } else {
            intData = static_cast<int*>(std::malloc(length * sizeof(int)));
            if (!intData) throw std::runtime_error("Array copy allocation failed");
            std::memcpy(intData, other.intData, length * sizeof(int));
        }
    }

    // OPTIMIZATION: Copy assignment for COW
    ArrayData& ArrayData::operator=(const ArrayData& other) {
        if (this != &other) {
            // Free current data
            this->~ArrayData();
            
            // Copy from other
            length = other.length;
            elemType = other.elemType;

            if (other.elemType == DOUBLE) {
                dblData = static_cast<double*>(std::malloc(length * sizeof(double)));
                if (dblData) std::memcpy(dblData, other.dblData, length * sizeof(double));
            } else if (other.elemType == VALUE) {
                valData = static_cast<Value*>(std::malloc(length * sizeof(Value)));
                if (valData) {
                    for (size_t i = 0; i < length; ++i) {
                        new (&valData[i]) Value(other.valData[i]);
                    }
                }
            } else {
                intData = static_cast<int*>(std::malloc(length * sizeof(int)));
                if (intData) std::memcpy(intData, other.intData, length * sizeof(int));
            }
        }
        return *this;
    }

    // OPTIMIZATION: Move constructor
    ArrayData::ArrayData(ArrayData&& other) noexcept
        : intData(other.intData), length(other.length), elemType(other.elemType) {
        refCount = other.refCount;
        other.intData = nullptr;
        other.length = 0;
        other.elemType = UNTYPED;
        other.refCount = 0;
    }

    ArrayData* ArrayData::cloneIfShared() const {
        if (refCount > 1) {
            return new ArrayData(*this);
        }
        return nullptr;
    }

    void ArrayData::markShared() {
        refCount++;
    }

    ArrayData& ArrayData::operator=(ArrayData&& other) noexcept {
        if (this != &other) {
            this->~ArrayData();
            
            intData = other.intData;
            length = other.length;
            elemType = other.elemType;
            refCount = other.refCount;
            
            other.intData = nullptr;
            other.length = 0;
            other.elemType = UNTYPED;
            other.refCount = 0;
        }
        return *this;
    }
}
