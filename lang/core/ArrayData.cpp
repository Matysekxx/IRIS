#include "ArrayData.h"
#include "Value.h"
#include <new>
#include <stdexcept>

ArrayData::ArrayData(size_t size, ElementType type) : intData(nullptr), length(size), elemType(type) {
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
