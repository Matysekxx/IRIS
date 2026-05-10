#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#include "Value.h"
#include <cstdint>

namespace iris::core {
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

        ArrayData(const ArrayData& other);
        ArrayData& operator=(const ArrayData& other);

        ArrayData(ArrayData&& other) noexcept;
        ArrayData& operator=(ArrayData&& other) noexcept;
        
        /**
         * @brief Creates a copy of this array for write operations.
         * Only creates a new copy if the array is shared (refCount > 1).
         * @return New ArrayData pointer if shared, nullptr if exclusive.
         */
        ArrayData* cloneIfShared() const;
        
        /**
         * @brief Marks this array as shared (increments refCount).
         * Called when array is assigned to another variable.
         */
        void markShared();
    };
}

#endif //COLLECTIONS_H
