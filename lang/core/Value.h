#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <cmath>
#include <memory>
#include <variant>
#include <vector>
#include <cstring>

#include "Managed.h"

namespace iris::core {
    struct ObjectData;
    struct ArrayData;
    struct NativeObject;

    /**
     * @brief Heap-allocated string.
     */
    struct StringData : Managed {
        std::string str;
        explicit StringData(std::string s) : Managed(ManagedType::String), str(std::move(s)) {}
    };

    /**
     * @brief 8-byte NaN-Tagged Value.
     */
    struct Value {
        uint64_t bits;

        static constexpr uint64_t QNAN    = 0x7FFC000000000000ULL;
        static constexpr uint64_t SIGN    = 0x8000000000000000ULL;
        
        static constexpr uint64_t TAG_INT  = 0x0001000000000000ULL;
        static constexpr uint64_t TAG_BOOL = 0x0002000000000000ULL;
        static constexpr uint64_t TAG_NULL = 0x0003000000000000ULL;
        static constexpr uint64_t TAG_STR  = 0x0004000000000000ULL; // SSO String
        static constexpr uint64_t TAG_PTR  = 0x8000000000000000ULL;

        Value() : bits(QNAN | TAG_NULL) {}
        
        explicit Value(int i) : bits(QNAN | TAG_INT | (uint32_t)i) {}
        
        explicit Value(double d) {
            std::memcpy(&bits, &d, 8);
        }
        
        explicit Value(bool b) : bits(QNAN | TAG_BOOL | (b ? 1 : 0)) {}
        
        explicit Value(const std::string& s) {
            if (s.length() <= 6) {
                bits = QNAN | TAG_STR | ((uint64_t)s.length() << 40);
                for (size_t i = 0; i < s.length(); ++i) {
                    bits |= ((uint64_t)(uint8_t)s[i] << (i * 6)); // Pack 6 bits? No, let's use 8 bits.
                }
                // Wait, 6 chars * 8 bits = 48 bits. Perfect!
                bits = QNAN | TAG_STR | ((uint64_t)s.length() << 48);
                uint64_t payload = 0;
                std::memcpy(&payload, s.data(), s.length());
                bits |= payload;
            } else {
                bits = QNAN | TAG_PTR | (uint64_t)new StringData(s);
                retain();
            }
        }

        explicit Value(StringData* s) : bits(QNAN | TAG_PTR | (uint64_t)s) { retain(); }
        explicit Value(ObjectData* o) : bits(QNAN | TAG_PTR | (uint64_t)o) { retain(); }
        explicit Value(ArrayData* a) : bits(QNAN | TAG_PTR | (uint64_t)a) { retain(); }
        explicit Value(NativeObject* n) : bits(QNAN | TAG_PTR | (uint64_t)n) { retain(); }

        Value(const Value& other) : bits(other.bits) { retain(); }
        Value(Value&& other) noexcept : bits(other.bits) { other.bits = QNAN | TAG_NULL; }

        ~Value() { release(); }

        Value& operator=(const Value& other) {
            if (this != &other) {
                release();
                bits = other.bits;
                retain();
            }
            return *this;
        }

        Value& operator=(Value&& other) noexcept {
            if (this != &other) {
                release();
                bits = other.bits;
                other.bits = QNAN | TAG_NULL;
            }
            return *this;
        }

        // --- Checks ---
        inline bool isDouble() const { return (bits & QNAN) != QNAN; }
        inline bool isInt()    const { return (bits & 0xFFFF000000000000ULL) == (QNAN | TAG_INT); }
        inline bool isBool()   const { return (bits & 0xFFFF000000000000ULL) == (QNAN | TAG_BOOL); }
        inline bool isNull()   const { return bits == (QNAN | TAG_NULL); }
        inline bool isPtr()    const { return (bits & (QNAN | SIGN)) == (QNAN | TAG_PTR); }
        inline bool isSSO()    const { return (bits & 0xFFFF000000000000ULL) == (QNAN | TAG_STR); }

        // --- Getters ---
        inline int asInt() const { return (int)(bits & 0xFFFFFFFFULL); }
        inline bool asBool() const { return (bits & 1) != 0; }
        inline double asDouble() const { double d; std::memcpy(&d, &bits, 8); return d; }
        inline Managed* asPtr() const { return reinterpret_cast<Managed*>(bits & 0x0000FFFFFFFFFFFFULL); }
        inline std::string asSSO() const {
            int len = (int)((bits >> 48) & 0xF); 
            char buf[8];
            uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
            std::memcpy(buf, &payload, 6);
            return std::string(buf, len);
        }

        std::string str() const;
        void append(const Value& other);

        bool operator==(const Value& o) const;
        bool operator!=(const Value& o) const { return !(*this == o); }

        inline void retain() {
            if (isPtr()) {
                Managed* p = asPtr();
                if (p) p->refCount++;
            }
        }

        inline void release() {
            if (isPtr()) {
                Managed* p = asPtr();
                if (p && --p->refCount == 0) delete p;
            }
        }

        bool isString() const;
        bool isObject() const;
        bool isArray() const;
        bool isHeap() const { return isPtr(); }
    };

    std::string toString(const Value& v);
    double toDouble(const Value& v);
    bool isNumeric(const Value& v);

    Value numericAdd(const Value& a, const Value& b);
    Value numericSub(const Value& a, const Value& b);
    Value numericMul(const Value& a, const Value& b);
    Value numericDiv(const Value& a, const Value& b);
    Value numericMod(const Value& a, const Value& b);
    Value numericNegate(const Value& a);

    bool numericLT(const Value& a, const Value& b);
    bool numericGT(const Value& a, const Value& b);
    bool numericLE(const Value& a, const Value& b);
    bool numericGE(const Value& a, const Value& b);

    struct ObjectData : Managed {
        uint16_t classId;
        uint16_t fieldCount;
        Value* fields;
        ObjectData(uint16_t cid, uint16_t count) : Managed(ManagedType::Object), classId(cid), fieldCount(count) {
            printf("[DEBUG] ObjectData constructor cid=%d count=%d\n", cid, count);
            fields = count > 0 ? new Value[count] : nullptr;
        }
        ~ObjectData() override { if (fields) delete[] fields; }

        static void* operator new(size_t size);
        static void operator delete(void* ptr, size_t size);
    };
}

#endif //VALUE_H
