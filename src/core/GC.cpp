#include "GC.h"
#include "ArrayData.h"
#include "Native.h"
#include "MemoryPool.h"
#include <cstring>
#include <cstdlib>

namespace iris::core {
    MemoryPool<ObjectData, 4096> objectPool;
    MemoryPool<StringData, 4096> stringDataPool;

    Managed* gcObjects = nullptr;
    size_t gcAllocated = 0;
    size_t gcThreshold = 16 * 1024 * 1024;

    thread_local GC* currentGC = nullptr;
    thread_local bool g_inGc = false;

    Managed*& GC::globalObjects() { return gcObjects; }

    // -- Managed constructor --

    Managed::Managed(ManagedType t, size_t allocSize) : type(t), marked(false) {
        if (currentGC && currentGC->isInNursery(this)) {
            // Nursery object: skip gcObjects registration
        } else {
            next = gcObjects;
            gcObjects = this;
            gcAllocated += allocSize;
        }
    }

    // -- ObjectData operator new/delete --

    void* ObjectData::operator new(size_t size) {
        if (size != sizeof(ObjectData)) return ::operator new(size);
        void* ptr = currentGC ? GC::nurseryAlloc(size) : nullptr;
        if (ptr) return ptr;
        return objectPool.allocate();
    }

    void ObjectData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(ObjectData)) {
            ::operator delete(ptr);
            return;
        }
        if (currentGC && currentGC->isInNursery(ptr)) return;
        objectPool.deallocate(static_cast<ObjectData*>(ptr));
    }

    // -- StringData operator new/delete --

    void* StringData::operator new(size_t size) {
        if (size != sizeof(StringData)) return ::operator new(size);
        void* ptr = currentGC ? GC::nurseryAlloc(size) : nullptr;
        if (ptr) return ptr;
        return stringDataPool.allocate();
    }

    void StringData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(StringData)) {
            ::operator delete(ptr);
            return;
        }
        if (currentGC && currentGC->isInNursery(ptr)) return;
        stringDataPool.deallocate(static_cast<StringData*>(ptr));
    }

    // -- RopeData operator new/delete --

    void* RopeData::operator new(size_t size) {
        void* ptr = currentGC ? GC::nurseryAlloc(size) : nullptr;
        if (ptr) return ptr;
        return ::operator new(size);
    }

    void RopeData::operator delete(void* ptr, size_t size) {
        if (currentGC && currentGC->isInNursery(ptr)) return;
        ::operator delete(ptr);
    }

    void RopeData::operator delete(void* ptr) {
        if (currentGC && currentGC->isInNursery(ptr)) return;
        ::operator delete(ptr);
    }

    // -- GC --

    GC::GC() {
        currentGC = this;
    }

    GC::~GC() {
        if (currentGC == this) currentGC = nullptr;
    }

    void* GC::nurseryAlloc(size_t size) {
        if (g_inGc) return nullptr;
        if (!currentGC) return nullptr;
        size_t total = size + sizeof(NurseryHeader);
        if (currentGC->nurseryCurrent + total > currentGC->nurseryEnd) return nullptr;
        auto* hdr = reinterpret_cast<NurseryHeader*>(currentGC->nurseryCurrent);
        hdr->size = total;
        void* ptr = currentGC->nurseryCurrent + sizeof(NurseryHeader);
        currentGC->nurseryCurrent += total;
        return ptr;
    }

    void GC::registerObject(Managed* obj, size_t allocSize) {
        // Handled in Managed constructor now
    }

    // -- Evacuation --

    void* GC::evacuate(void* nurseryPtr) {
        auto it = forwarding.find(nurseryPtr);
        if (it != forwarding.end()) return it->second;

        Managed* obj = static_cast<Managed*>(nurseryPtr);
        void* maturePtr = nullptr;

        switch (obj->type) {
            case ManagedType::Object: {
                ObjectData* o = static_cast<ObjectData*>(obj);
                auto* copy = new ObjectData(o->classId, o->fieldCount);
                for (int i = 0; i < o->fieldCount; i++) {
                    copy->getField(i) = o->getField(i);
                }
                maturePtr = copy;
                break;
            }
            case ManagedType::Array: {
                ArrayData* a = static_cast<ArrayData*>(obj);
                auto* copy = ArrayData::create(a->length, a->elemType);
                size_t elemSize = (a->elemType == ArrayData::DOUBLE) ? sizeof(double) :
                                  (a->elemType == ArrayData::INT) ? sizeof(int) : sizeof(Value);
                memcpy(reinterpret_cast<char*>(copy) + sizeof(ArrayData),
                       reinterpret_cast<char*>(a) + sizeof(ArrayData),
                       a->length * elemSize);
                maturePtr = copy;
                break;
            }
            case ManagedType::String: {
                StringData* s = static_cast<StringData*>(obj);
                auto* copy = new StringData(s->str);
                copy->cachedHash = s->cachedHash;
                maturePtr = copy;
                break;
            }
            case ManagedType::Rope: {
                RopeData* r = static_cast<RopeData*>(obj);
                auto* copy = new RopeData(r->left, r->right);
                copy->cachedFlat = r->cachedFlat;
                maturePtr = copy;
                break;
            }
            case ManagedType::Native: {
                NativeObject* n = static_cast<NativeObject*>(obj);
                void* mem = ::operator new(sizeof(NativeObject));
                memcpy(mem, static_cast<void*>(n), sizeof(NativeObject));
                // Register with gcObjects
                Managed* m = static_cast<Managed*>(mem);
                m->next = gcObjects;
                gcObjects = m;
                gcAllocated += sizeof(NativeObject);
                maturePtr = mem;
                break;
            }
        }

        forwarding[nurseryPtr] = maturePtr;
        scanQueue.push_back(maturePtr);
        return maturePtr;
    }

    void GC::evacuateValue(Value* val) {
        if (!val->isHeap()) return;
        Managed* p = val->asPtr();
        if (!p || !isInNursery(p)) return;
        void* forwarded = evacuate(p);
        val->bits = Value::QNAN | Value::TAG_PTR | (uint64_t)forwarded;
    }

    // -- Scan fields of an evacuated/mature object for remaining nursery pointers --

    static void scanFieldsForNursery(void* objPtr) {
        Managed* obj = static_cast<Managed*>(objPtr);
        if (obj->type == ManagedType::Object) {
            ObjectData* o = static_cast<ObjectData*>(obj);
            for (int i = 0; i < o->fieldCount; i++) {
                GC* gc = currentGC;
                if (gc) gc->evacuateValue(&o->getField(i));
            }
        } else if (obj->type == ManagedType::Array) {
            ArrayData* a = static_cast<ArrayData*>(obj);
            if (a->elemType == ArrayData::VALUE) {
                Value* vals = a->getValData();
                for (size_t i = 0; i < a->length; i++) {
                    GC* gc = currentGC;
                    if (gc) gc->evacuateValue(&vals[i]);
                }
            }
        } else if (obj->type == ManagedType::Rope) {
            RopeData* r = static_cast<RopeData*>(obj);
            GC* gc = currentGC;
            if (gc) {
                gc->evacuateValue(&r->left);
                gc->evacuateValue(&r->right);
            }
        }
        // StringData and NativeObject have no pointer fields to scan
    }

    // -- Marking --

    void GC::markValue(Value v) {
        if (!v.isHeap()) return;
        Managed* p = v.asPtr();
        if (!p || p->marked) return;
        p->marked = true;
        if (p->type == ManagedType::Object) {
            ObjectData* o = static_cast<ObjectData*>(p);
            for (int i = 0; i < o->fieldCount; i++) markValue(o->getField(i));
        } else if (p->type == ManagedType::Array) {
            ArrayData* a = static_cast<ArrayData*>(p);
            if (a->elemType == ArrayData::VALUE) {
                Value* valData = a->getValData();
                for (size_t i = 0; i < a->length; i++) markValue(valData[i]);
            }
        } else if (p->type == ManagedType::Native) {
            NativeObject* n = static_cast<NativeObject*>(p);
            n->mark();
        } else if (p->type == ManagedType::Rope) {
            RopeData* r = static_cast<RopeData*>(p);
            markValue(r->left);
            markValue(r->right);
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

    void GC::unmarkAll(Managed* head) {
        while (head) {
            head->marked = false;
            head = head->next;
        }
    }

    void GC::clearMatureMarks() {
        Managed* head = gcObjects;
        while (head) {
            head->marked = false;
            head = head->next;
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
                    case ManagedType::Rope:   delete static_cast<RopeData*>(obj); break;
                }
                freed += sizeof(Managed);
            } else {
                obj->marked = false;
                p = &obj->next;
            }
        }
        gcAllocated = (gcAllocated > freed) ? gcAllocated - freed : 0;
    }

    void GC::destroyNurseryObjects() {
        char* p = nurseryBase;
        while (p < nurseryCurrent) {
            auto* hdr = reinterpret_cast<NurseryHeader*>(p);
            Managed* obj = reinterpret_cast<Managed*>(p + sizeof(NurseryHeader));
            switch (obj->type) {
                case ManagedType::String:
                    static_cast<StringData*>(obj)->~StringData();
                    break;
                case ManagedType::Rope:
                    static_cast<RopeData*>(obj)->~RopeData();
                    break;
                case ManagedType::Object:
                    static_cast<ObjectData*>(obj)->~ObjectData();
                    break;
                case ManagedType::Array:
                    static_cast<ArrayData*>(obj)->~ArrayData();
                    break;
                case ManagedType::Native:
                    static_cast<NativeObject*>(obj)->~NativeObject();
                    break;
            }
            p += hdr->size;
        }
        nurseryCurrent = nurseryBase;
    }

    // -- Collection --

    void GC::minorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools) {
        if (nurseryCurrent == nurseryBase) return;
        g_inGc = true;

        forwarding.clear();
        scanQueue.clear();

        // Phase 1: Mark mature objects from roots (to know which are live)
        unmarkAll(gcObjects);
        markRoots(stack, stackSize, globals, constantPools);

        // Phase 2: Evacuate nursery objects reachable from roots
        for (size_t i = 0; i < stackSize; i++) evacuateValue(&stack[i]);
        for (auto& g : const_cast<std::vector<Variable>&>(globals)) {
            evacuateValue(&g.value);
        }
        for (auto* pool : constantPools) {
            if (pool) {
                for (auto& val : const_cast<std::vector<Value>&>(*pool)) {
                    evacuateValue(&val);
                }
            }
        }

        // Phase 3: Scan marked AND dirty mature objects for nursery pointers
        Managed* m = gcObjects;
        while (m) {
            if (m->marked && m->dirty) {
                scanFieldsForNursery(m);
                m->dirty = false;
            }
            m = m->next;
        }

        // Phase 4: Process evacuation queue (transitive closure)
        size_t idx = 0;
        while (idx < scanQueue.size()) {
            scanFieldsForNursery(scanQueue[idx]);
            idx++;
        }

        // Phase 5: Destroy and reset nursery
        destroyNurseryObjects();

        // Phase 6: Clear mature marks
        clearMatureMarks();

        forwarding.clear();
        scanQueue.clear();
        g_inGc = false;
    }

    void GC::majorCollect(Value* stack, size_t stackSize, const std::vector<Variable>& globals,
                          const std::vector<const std::vector<Value>*>& constantPools) {
        g_inGc = true;

        forwarding.clear();
        scanQueue.clear();

        // First evacuate nursery survivors to mature
        if (nurseryCurrent > nurseryBase) {
            unmarkAll(gcObjects);
            markRoots(stack, stackSize, globals, constantPools);

            for (size_t i = 0; i < stackSize; i++) evacuateValue(&stack[i]);
            for (auto& g : const_cast<std::vector<Variable>&>(globals)) {
                evacuateValue(&g.value);
            }
            for (auto* pool : constantPools) {
                if (pool) {
                    for (auto& val : const_cast<std::vector<Value>&>(*pool)) {
                        evacuateValue(&val);
                    }
                }
            }

            Managed* m = gcObjects;
            while (m) {
                if (m->marked && m->dirty) {
                    scanFieldsForNursery(m);
                    m->dirty = false;
                }
                m = m->next;
            }

            size_t idx = 0;
            while (idx < scanQueue.size()) {
                scanFieldsForNursery(scanQueue[idx]);
                idx++;
            }

            destroyNurseryObjects();
        }

        // Full mark-sweep of mature space
        unmarkAll(gcObjects);
        markRoots(stack, stackSize, globals, constantPools);
        sweepMature();

        gcThreshold = (gcAllocated < MATURE_THRESHOLD) ? MATURE_THRESHOLD : gcAllocated * 2;

        forwarding.clear();
        scanQueue.clear();
        g_inGc = false;
    }

    // -- Legacy wrappers --

    void markValue(Value v) {
        GC::markValue(v);
    }

    void collectGC(Value* stack, size_t stackSize, const std::vector<Variable>& globals) {
        if (currentGC) {
            currentGC->majorCollect(stack, stackSize, globals, activeConstantPools);
        } else {
            // Legacy fallback (no GC object)
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
                        case ManagedType::Rope:   delete static_cast<RopeData*>(obj); break;
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
