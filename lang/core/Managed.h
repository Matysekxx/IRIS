#ifndef MANAGED_H
#define MANAGED_H

#include <cstdint>

namespace iris::core {
    /**
     * @brief Base class for heap-allocated reference-counted data.
     * 
     * Any object that needs to be managed by the IRIS reference counting system
     * must inherit from this struct. The reference count is manipulated by
     * Value::retain() and Value::release().
     */
    struct Managed {
        /** @brief The number of active references to this object. */
        uint32_t refCount = 0;

        Managed() = default;

        /** @brief Copying a managed object creates a new object with refCount 0. */
        Managed(const Managed &) : refCount(0) {
        }

        /** @brief Assignment does not copy the reference count. */
        Managed &operator=(const Managed &) { return *this; }

        /** @brief Virtual destructor to ensure proper cleanup of derived types. */
        virtual ~Managed() = default;
    };
}

#endif //MANAGED_H
