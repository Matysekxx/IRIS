#include "GC.h"
#include "ArrayData.h"
#include "Native.h"
#include "MemoryPool.h"
#include <cstring>

namespace iris::core {
    static MemoryPool<ObjectData, 4096> objectPool;
    static MemoryPool<StringData, 4096> stringDataPool;

    static Managed* gcObjects = nullptr;
    size_t gcAllocated = 0;
    size_t gcThreshold = 16 * 1024 * 1024;

    thread_local GC* currentGC = nullptr;

    Managed*& GC::globalObjects() { return gcObjects; }

    static void registerWithGC(Managed* obj, size_t allocSize) {
        obj->next = gcObjects;
        gcObjects = obj;
        gcAllocated += allocSize;
    }

    Managed::Managed(ManagedType t, size_t allocSize) : type(t), marked(false) {
        next = gcObjects;
        gcObjects = this;
        gcAllocated += allocSize;
    }

    void* ObjectData::operator new(size_t size) {
        if (size != sizeof(ObjectData)) return ::operator new(size);
        return objectPool.allocate();
    }

    void ObjectData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(ObjectData)) {
            ::operator delete(ptr);
            return;
        }
        objectPool.deallocate(static_cast<ObjectData*>(ptr));
    }

    void* StringData::operator new(size_t size) {
        if (size != sizeof(StringData)) return ::operator new(size);
        return stringDataPool.allocate();
    }

    void StringData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(StringData)) {
            ::operator delete(ptr);
            return;
        }
        stringDataPool.deallocate(static_cast<StringData*>(ptr));
    }

    GC::GC() {
        currentGC = this;
    }

    GC::~GC() {
        if (currentGC == this) currentGC = nullptr;
    }

    void GC::registerObject(Managed* obj, size_t allocSize) {
        registerWithGC(obj, allocSize);
    }

    void GC::markValue(Value v) {
        if (v.isHeap()) {
            Managed* p = v.asPtr();
            if (p && !p->marked) {
                p->marked = true;
                if (p->type == ManagedType::Object) {
                    ObjectData* o = static_cast<ObjectData*>(p);
                    for (int i = 0; i < o->fieldCount; i++) markValue(o->getField(i));
                } else if (p->type == ManagedType::Array) {
                    ArrayData* a = static_cast<ArrayData*>(p);
                    if (a->elemType == ArrayData::VALUE) {
                        for (size_t i = 0; i < a->length; i++) markValue(a->valData[i]);
                    }
                } else if (p->type == ManagedType::Native) {
                    NativeObject* n = static_cast<NativeObject*>(p);
                    n->mark();
                }
            }
        }
    }

    static void markRoots(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                         const std::vector<const std::vector<Value>*>& constantPools) {
        for (size_t i = 0; i < stackSize; i++) GC::markValue(stack[i]);
        for (const auto& g : globals) GC::markValue(g.value);
        for (auto* pool : constantPools) {
            if (pool) {
                for (const auto& val : *pool) GC::markValue(val);
            }
        }
    }

    void GC::sweepMature() {
        Managed** p = &gcObjects;
        size_t freed = 0;
        while (*p) {
            Managed* obj = *p;
            if (!obj->marked) {
                *p = obj->next;
                switch (obj->type) {
                    case ManagedType::String: delete static_cast<StringData*>(obj); break;
                    case ManagedType::Object: delete static_cast<ObjectData*>(obj); break;
                    case ManagedType::Array:  delete static_cast<ArrayData*>(obj); break;
                    case ManagedType::Native: delete static_cast<NativeObject*>(obj); break;
                }
                freed += sizeof(Managed); // rough estimate
            } else {
                obj->marked = false;
                p = &obj->next;
            }
        }
        gcAllocated = (gcAllocated > freed) ? gcAllocated - freed : 0;
    }

    void GC::minorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools) {
        markRoots(stack, stackSize, globals, constantPools);
        sweepMature();
    }

    void GC::majorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools) {
        markRoots(stack, stackSize, globals, constantPools);
        sweepMature();
        gcThreshold = (gcAllocated < MATURE_THRESHOLD) ? MATURE_THRESHOLD : gcAllocated * 2;
    }

    // Legacy compatibility wrappers for VM.cpp and other consumers
    void markValue(Value v) {
        GC::markValue(v);
    }

    void collectGC(Value* stack, size_t stackSize, const std::vector<Variable>& globals) {
        if (currentGC) {
            currentGC->majorCollect(stack, stackSize, globals, activeConstantPools);
        } else {
            for (size_t i = 0; i < stackSize; i++) markValue(stack[i]);
            for (const auto& g : globals) markValue(g.value);
            for (auto* pool : activeConstantPools) {
                if (pool) {
                    for (const auto& val : *pool) markValue(val);
                }
            }
            Managed** p = &gcObjects;
            while (*p) {
                Managed* obj = *p;
                if (!obj->marked) {
                    *p = obj->next;
                    switch (obj->type) {
                        case ManagedType::String: delete static_cast<StringData*>(obj); break;
                        case ManagedType::Object: delete static_cast<ObjectData*>(obj); break;
                        case ManagedType::Array:  delete static_cast<ArrayData*>(obj); break;
                        case ManagedType::Native: delete static_cast<NativeObject*>(obj); break;
                    }
                } else {
                    obj->marked = false;
                    p = &obj->next;
                }
            }
            gcAllocated = 0;
        }
    }
}
