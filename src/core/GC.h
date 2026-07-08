#ifndef GC_H
#define GC_H

#include <cstddef>
#include <vector>
#include <unordered_map>
#include "Value.h"
#include "Variable.h"
#include "Managed.h"

namespace iris::core {
    struct Managed;
    struct ObjectData;
    struct ArrayData;
    struct StringData;
    struct NativeObject;
    class GC;

    extern thread_local GC* currentGC;
    extern thread_local bool g_inGc;
    extern Managed* gcObjects;
    extern size_t gcAllocated;
    extern size_t gcThreshold;

    struct NurseryHeader {
        size_t size;
    };

    class GC {
    public:
        static constexpr size_t NURSERY_SIZE = 4 * 1024 * 1024;
        static constexpr size_t MATURE_THRESHOLD = 16 * 1024 * 1024;

    private:
        char* nurseryBase;
        char* nurseryCurrent;
        char* nurseryEnd;
        bool nurseryFromOperatorNew = false;

        std::unordered_map<void*, void*> forwarding;
        std::vector<void*> scanQueue;

    public:
        GC();
        ~GC();

        GC(const GC&) = delete;
        GC& operator=(const GC&) = delete;

        static void* nurseryAlloc(size_t size);

        bool isInNursery(void* ptr) const {
            return ptr >= nurseryBase && ptr < nurseryEnd;
        }

        static bool isInNurseryStatic(void* ptr) {
            return currentGC && currentGC->isInNursery(ptr);
        }

        static void registerObject(Managed* obj, size_t allocSize);

        void* evacuate(void* nurseryPtr);
        void evacuateValue(Value* val);

        void minorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools);

        void majorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools);

        static void markValue(Value v);
        void sweepMature();

        static void unmarkAll(Managed* head);
        static Managed*& globalObjects();

        void destroyNurseryObjects();
        void clearMatureMarks();
    };
}

#endif // GC_H
