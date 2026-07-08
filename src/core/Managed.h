#ifndef MANAGED_H
#define MANAGED_H

#include <cstdint>

namespace iris::core {
    /**
     * @brief Types of managed objects for fast identification.
     */
    enum class ManagedType : uint8_t {
        String,
        Object,
        Array,
        Native,
        Rope
    };

    /**
     * @brief Base class for heap-allocated objects managed by the Garbage Collector.
     */
    struct Managed {
        Managed* next = nullptr; // Global list of all managed objects
        ManagedType type;
        bool marked = false;
        bool dirty = false;     // Write barrier: set when a field is written

        explicit Managed(ManagedType t, size_t allocSize = 32);

        Managed(const Managed &) = delete;
        Managed &operator=(const Managed &) = delete;
        ~Managed() = default;

        size_t getAllocSize() const { return allocSize_; }

    protected:
        size_t allocSize_ = 0;
    };
}

#endif //MANAGED_H
