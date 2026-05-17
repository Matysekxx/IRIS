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
        Native
    };

    /**
     * @brief Base class for heap-allocated reference-counted data.
     */
    struct Managed {
        uint32_t refCount = 0;
        ManagedType type;

        explicit Managed(ManagedType t) : type(t) {}

        Managed(const Managed &) : refCount(0), type(ManagedType::Object) {}
        Managed &operator=(const Managed &) { return *this; }
        virtual ~Managed() = default;
    };
}

#endif //MANAGED_H
