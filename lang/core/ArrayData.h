#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#include "Value.h"

// ============================================================================
// ArrayData — fixed-size, type-specialized heap array
// Uses raw malloc for zero overhead. Type is fixed on first write.
// ============================================================================
struct ArrayData : Managed {
    enum ElementType : uint8_t { UNTYPED, INT, DOUBLE, VALUE };

    union {
        int* intData;
        double* dblData;
        Value* valData;
    };
    size_t length;
    ElementType elemType;

    explicit ArrayData(size_t size, ElementType type = UNTYPED);
    ~ArrayData() override;

    ArrayData(const ArrayData&) = delete;
    ArrayData& operator=(const ArrayData&) = delete;
};

#endif //COLLECTIONS_H
