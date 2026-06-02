#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#include "Value.h"
#include <cstdint>

namespace iris::core {
    /**
     * @brief Heap-allocated array data.
     * 
     * Supports both untyped (Value-based) and typed (primitive) arrays.
     * Typed arrays (Int, Double) are more memory-efficient and allow
     * for faster processing via specialized opcodes.
     */
    struct ArrayData : Managed {
        /** @brief Type of elements stored in the array. */
        enum ElementType : uint8_t {
            UNTYPED, ///< Array of Value (slowest, most flexible)
            INT, ///< Array of 32-bit integers
            DOUBLE, ///< Array of 64-bit doubles
            VALUE ///< Explicitly tagged Value array
        };

        union {
            int *intData; ///< Pointer to integer data
            double *dblData; ///< Pointer to double data
            Value *valData; ///< Pointer to Value data
        };

        size_t length; ///< Number of elements in the array
        ElementType elemType; ///< Current element type

        /** @brief Constructs a new array of the given size and type. */
        explicit ArrayData(size_t size, ElementType type = UNTYPED);

        /** @brief Cleans up allocated memory based on element type. */
        ~ArrayData();

        ArrayData(const ArrayData &other);

        ArrayData &operator=(const ArrayData &other);

        ArrayData(ArrayData &&other) noexcept;

        ArrayData &operator=(ArrayData &&other) noexcept;

        /**
         * @brief Creates a deep copy of this array for write operations.
         *
         * Implements Copy-On-Write (COW) logic. Only creates a new copy
         * if the array is shared (refCount > 1).
         *
         * @return New ArrayData pointer if shared, nullptr if exclusive.
         */
        ArrayData *cloneIfShared() const;

        /**
         * @brief Marks this array as shared (increments refCount).
         * Called when array is assigned to another variable.
         */
        void markShared();
    };
}

#endif //COLLECTIONS_H
