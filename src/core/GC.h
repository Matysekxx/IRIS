#ifndef GC_H
#define GC_H

#include <cstddef>
#include <vector>
#include "Value.h"
#include "Variable.h"
#include "Managed.h"

namespace iris::core {
    struct Managed;
    struct ObjectData;
    struct ArrayData;
    struct StringData;
    struct NativeObject;

    /**
     * @brief Generational Garbage Collector.
     *
     * Uses a simple nursery + mature split. New allocations go to the nursery.
     * Minor collections scan nursery roots. Major collections scan everything.
     * Objects surviving collections are promoted to the mature generation.
     */
    class GC {
    public:
        static constexpr size_t NURSERY_SIZE = 4 * 1024 * 1024;   // 4MB
        static constexpr size_t MATURE_THRESHOLD = 16 * 1024 * 1024; // 16MB
        static constexpr int COLLECT_INTERVAL = 256;

        struct Nursery {
            size_t allocated = 0;
        };

    private:
        size_t matureAllocated = 0;
        size_t matureThreshold = MATURE_THRESHOLD;
        int checkCounter = COLLECT_INTERVAL;

        Managed** rootList = nullptr; // Head of all managed objects (mature)

        // Object pools for fast allocation
        static void* allocateObject(size_t size);
        static void* allocateString(size_t size);
        static void* allocateArray(size_t size);

    public:
        GC();
        ~GC();

        // Disable copy
        GC(const GC&) = delete;
        GC& operator=(const GC&) = delete;

        /** @brief Register a newly allocated managed object. */
        static void registerObject(Managed* obj, size_t allocSize);

        /** @brief Perform a minor collection (nursery only). */
        void minorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools);

        /** @brief Perform a full collection (major). */
        void majorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools);

        /** @brief Fast-path check: should we collect now? */
        inline bool shouldCollect() {
            if (--checkCounter <= 0) {
                checkCounter = COLLECT_INTERVAL;
                return true;
            }
            return false;
        }

        /** @brief Mark a single value recursively. */
        static void markValue(Value v);

        /** @brief Sweep unmarked objects from the mature list. */
        void sweepMature();

        /** @brief Reset all mark bits before collection. */
        static void unmarkAll(Managed* head);

        /** @brief Access the global GC list head (for legacy compat). */
        static Managed*& globalObjects();
    };

    extern thread_local GC* currentGC;
}

#endif // GC_H
