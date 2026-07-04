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

        size_t length; ///< Number of elements in the array
        ElementType elemType; ///< Current element type

        /** @brief Tracks element type homogeneity for JIT optimization hints. */
        int8_t typeScore;

        static ArrayData* create(size_t size, ElementType type = UNTYPED);

        /** @brief Cleans up allocated memory based on element type. */
        ~ArrayData();

        ArrayData(const ArrayData &other) = delete;
        ArrayData &operator=(const ArrayData &other) = delete;
        ArrayData(ArrayData &&other) noexcept = delete;
        ArrayData &operator=(ArrayData &&other) noexcept = delete;

        static void* operator new(size_t size, size_t extra_size);
        static void operator delete(void* ptr, size_t extra_size);
        static void operator delete(void* ptr);

        FORCE_INLINE int* getIntData() {
            return reinterpret_cast<int*>(reinterpret_cast<char*>(this) + sizeof(ArrayData));
        }
        FORCE_INLINE const int* getIntData() const {
            return reinterpret_cast<const int*>(reinterpret_cast<const char*>(this) + sizeof(ArrayData));
        }
        FORCE_INLINE double* getDblData() {
            return reinterpret_cast<double*>(reinterpret_cast<char*>(this) + sizeof(ArrayData));
        }
        FORCE_INLINE const double* getDblData() const {
            return reinterpret_cast<const double*>(reinterpret_cast<const char*>(this) + sizeof(ArrayData));
        }
        FORCE_INLINE Value* getValData() {
            return reinterpret_cast<Value*>(reinterpret_cast<char*>(this) + sizeof(ArrayData));
        }
        FORCE_INLINE const Value* getValData() const {
            return reinterpret_cast<const Value*>(reinterpret_cast<const char*>(this) + sizeof(ArrayData));
        }

        FORCE_INLINE void recordStore(const iris::core::Value& val) {
            if (elemType == VALUE) {
                if (val.isInt()) { if (typeScore < 127) typeScore++; }
                else if (val.isDouble()) { if (typeScore > -127) typeScore--; }
                else { typeScore = 0; }
            }
        }

    private:
        explicit ArrayData(size_t size, ElementType type = UNTYPED);
    };
}

#endif //COLLECTIONS_H
