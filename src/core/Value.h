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
    struct Managed;
    struct ObjectData;
    struct ArrayData;
    struct NativeObject;

    extern size_t gcAllocated;
    extern size_t gcThreshold;

    /**
     * @brief Heap-allocated string.
     */
    struct StringData : Managed {
        std::string str;
        explicit StringData(std::string s) : Managed(ManagedType::String, sizeof(StringData) + s.size()), str(std::move(s)) {}

        static void* operator new(size_t size);
        static void operator delete(void* ptr, size_t size);
    };


    /**
     * @brief 8-byte NaN-Tagged Value.
     * Layout:
     * - Double: Any bit pattern that is not a signaling NaN (standard IEEE 754)
     * - Tagged: 0x7FFC + 16-bit Tag + 32-bit Payload
     * - Pointer: 0xFFFC + 48-bit Address (Sign bit set)
     */
    struct Value {
        static constexpr uint64_t QNAN      = 0x7FF8000000000000ULL;
        static constexpr uint64_t TAG_INT   = 0x0000000000000000ULL;
        static constexpr uint64_t TAG_BOOL  = 0x0001000000000000ULL;
        static constexpr uint64_t TAG_NULL  = 0x0002000000000000ULL;
        static constexpr uint64_t TAG_SSO   = 0x0004000000000000ULL;
        static constexpr uint64_t TAG_PTR   = 0x8000000000000000ULL; // Sign bit for heap objects

        uint64_t bits = QNAN | TAG_NULL;

        Value() = default;
        
        explicit Value(int i) : bits(QNAN | TAG_INT | (uint32_t)i) {}
        
        explicit Value(double d) {
            std::memcpy(&bits, &d, 8);
            if ((bits & 0x7FF8000000000000ULL) == 0x7FF8000000000000ULL) {
                bits = 0x7FF8000000000000ULL;
            }
        }
        
        explicit Value(bool b) : bits(QNAN | TAG_BOOL | (b ? 1 : 0)) {}
        
        explicit Value(const std::string& s) {
            if (s.length() <= 6) {
                bits = (0x7FF0ULL << 48) | ((uint64_t)s.length() << 48);
                uint64_t payload = 0;
                std::memcpy(&payload, s.data(), s.length());
                bits |= payload;
            } else {
                bits = TAG_PTR | QNAN | (uint64_t)new StringData(s);
            }
        }
        
        explicit Value(const char* s) : Value(std::string(s)) {}

        explicit Value(StringData* s) : bits(TAG_PTR | QNAN | (uint64_t)s) {}
        explicit Value(ObjectData* o) : bits(TAG_PTR | QNAN | (uint64_t)o) {}
        explicit Value(ArrayData* a) : bits(TAG_PTR | QNAN | (uint64_t)a) {}
        explicit Value(NativeObject* n) : bits(TAG_PTR | QNAN | (uint64_t)n) {}

        static inline Value fromRawBits(uint64_t b) {
            Value v;
            v.bits = b;
            return v;
        }

        inline size_t stringLength() const {
            if (isSSO()) return (size_t)((bits >> 48) - 0x7FF0);
            return static_cast<StringData*>(asPtr())->str.length();
        }

        inline const std::string& asStringRef() const {
            return static_cast<StringData*>(asPtr())->str;
        }

        inline bool isDouble() const { return (bits & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL; }
        inline bool isInt()    const { return (bits & 0xFFFF000000000000ULL) == (QNAN | TAG_INT); }
        inline bool isBool()   const { return (bits & 0xFFFF000000000000ULL) == (QNAN | TAG_BOOL); }
        inline bool isNull()   const { return bits == (QNAN | TAG_NULL); }
        inline bool isPtr()    const { return (bits & 0xFFFF000000000000ULL) == (TAG_PTR | QNAN); }
        inline bool isSSO()    const { uint64_t top = bits >> 48; return top >= 0x7FF0 && top <= 0x7FF6; }

        inline int asInt() const { return (int)(bits & 0xFFFFFFFFULL); }
        inline bool asBool() const { return (bits & 1) != 0; }
        inline double asDouble() const { double d; std::memcpy(&d, &bits, 8); return d; }
        inline Managed* asPtr() const { return reinterpret_cast<Managed*>(bits & 0x0000FFFFFFFFFFFFULL); }

        inline std::string asSSO() const {
            int len = (int)((bits >> 48) - 0x7FF0); 
            char buf[8] = {0};
            uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
            std::memcpy(buf, &payload, 6);
            return std::string(buf, len);
        }

        std::string str() const;
        void append(const Value& other);

        bool operator==(const Value& o) const;
        bool operator!=(const Value& o) const { return !(*this == o); }

        inline void retain() {
            // Disabled: Now managed by Garbage Collector
        }

        inline void release() {
            // Disabled: Now managed by Garbage Collector
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

    struct Variable;
    void markValue(Value v);
    void collectGC(Value* stack, size_t stackSize, const std::vector<Variable>& globals);

    extern thread_local std::vector<const std::vector<Value>*> activeConstantPools;

    /** @brief Global string interning table. Returns a shared StringData* for identical strings. */
    StringData* internString(const std::string& s);

    struct ObjectData : Managed {
        static constexpr int INLINED_FIELDS = 4;
        uint16_t classId;
        uint16_t fieldCount;
        uint32_t padding; 

        Value* overflowFields; // Offset 16
        Value inlinedFields[INLINED_FIELDS]; // Offset 24

        ObjectData(uint16_t cid, uint16_t count) : Managed(ManagedType::Object, sizeof(ObjectData) + (count > INLINED_FIELDS ? (count - INLINED_FIELDS) * sizeof(Value) : 0)), classId(cid), fieldCount(count), padding(0) {
            if (count > INLINED_FIELDS) {
                overflowFields = new Value[count - INLINED_FIELDS];
            } else {
                overflowFields = nullptr;
            }
        }

        ~ObjectData() {
            if (overflowFields) delete[] overflowFields;
        }

        inline Value& getField(int idx) {
            if (idx < INLINED_FIELDS) return inlinedFields[idx];
            return overflowFields[idx - INLINED_FIELDS];
        }

        static void* operator new(size_t size);
        static void operator delete(void* ptr, size_t size);
    };
}

#endif //VALUE_H
