#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <string_view>
#include <cmath>
#include <memory>
#include <variant>
#include <vector>
#include <cstring>

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

#include "Managed.h"

namespace iris::core {
    struct Managed;
    struct ObjectData;
    struct ArrayData;
    struct NativeObject;
    struct RopeData;
    struct Value;

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
        explicit Value(RopeData* r) : bits(TAG_PTR | QNAN | (uint64_t)r) {}
        explicit Value(ObjectData* o) : bits(TAG_PTR | QNAN | (uint64_t)o) {}
        explicit Value(ArrayData* a) : bits(TAG_PTR | QNAN | (uint64_t)a) {}
        explicit Value(NativeObject* n) : bits(TAG_PTR | QNAN | (uint64_t)n) {}

        static inline Value fromRawBits(uint64_t b) {
            Value v;
            v.bits = b;
            return v;
        }

        size_t stringLength() const;

        FORCE_INLINE const std::string& asStringRef() const {
            return static_cast<StringData*>(asPtr())->str;
        }

        FORCE_INLINE bool isDouble() const { return ((bits >> 52) & 0x7FF) != 0x7FF; }
        FORCE_INLINE bool isInt()    const { return (bits >> 48) == 0x7FF8; }
        FORCE_INLINE bool isBool()   const { return (bits >> 48) == 0x7FF9; }
        FORCE_INLINE bool isNull()   const { return bits == (QNAN | TAG_NULL); }
        FORCE_INLINE bool isPtr()    const { return (bits >> 48) == 0xFFF8; }
        FORCE_INLINE bool isSSO()    const { uint64_t top = bits >> 48; return top >= 0x7FF0 && top <= 0x7FF6; }

        FORCE_INLINE int asInt() const { return (int)(bits & 0xFFFFFFFFULL); }
        FORCE_INLINE bool asBool() const { return (bits & 1) != 0; }
        FORCE_INLINE double asDouble() const { double d; std::memcpy(&d, &bits, 8); return d; }
        FORCE_INLINE Managed* asPtr() const { return reinterpret_cast<Managed*>((bits << 16) >> 16); }

        FORCE_INLINE std::string asSSO() const {
            int len = (int)((bits >> 48) - 0x7FF0); 
            char buf[8] = {0};
            uint64_t payload = (bits << 16) >> 16;
            std::memcpy(buf, &payload, 6);
            return std::string(buf, len);
        }

        std::string str() const;
        std::string_view view() const;
        void append(const Value& other);

        bool operator==(const Value& o) const;
        bool operator!=(const Value& o) const { return !(*this == o); }

        inline void retain() {
            // Disabled: Now managed by Garbage Collector
        }

        inline void release() {
            // Disabled: Now managed by Garbage Collector
        }

        FORCE_INLINE bool isString() const {
            if (isSSO()) return true;
            if (isPtr()) {
                Managed* p = asPtr();
                return p && (p->type == ManagedType::String || p->type == ManagedType::Rope);
            }
            return false;
        }

        FORCE_INLINE bool isObject() const {
            if (isPtr()) {
                Managed* p = asPtr();
                return p && p->type == ManagedType::Object;
            }
            return false;
        }

        FORCE_INLINE bool isArray() const {
            if (isPtr()) {
                Managed* p = asPtr();
                return p && p->type == ManagedType::Array;
            }
            return false;
        }
        FORCE_INLINE bool isHeap() const { return isPtr(); }
    };

    std::string toString(const Value& v);
    double float16ToDouble(uint16_t bits);
    uint16_t doubleToFloat16(double d);

    FORCE_INLINE double toDouble(const Value& v) {
        if (v.isDouble()) return v.asDouble();
        if (v.isInt()) return static_cast<double>(v.asInt());
        return 0.0;
    }

    FORCE_INLINE bool isNumeric(const Value& v) { return v.isInt() || v.isDouble(); }

    Value numericAddString(const Value& a, const Value& b);

    FORCE_INLINE Value numericAdd(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return Value(a.asInt() + b.asInt());
            if (b.isDouble()) return Value(static_cast<double>(a.asInt()) + b.asDouble());
        } else if (a.isDouble()) {
            if (b.isDouble()) return Value(a.asDouble() + b.asDouble());
            if (b.isInt()) return Value(a.asDouble() + static_cast<double>(b.asInt()));
        } else if (a.isString() || b.isString()) {
            return numericAddString(a, b);
        }
        return Value(toDouble(a) + toDouble(b));
    }

    FORCE_INLINE Value numericSub(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return Value(a.asInt() - b.asInt());
            if (b.isDouble()) return Value(static_cast<double>(a.asInt()) - b.asDouble());
        } else if (a.isDouble()) {
            if (b.isDouble()) return Value(a.asDouble() - b.asDouble());
            if (b.isInt()) return Value(a.asDouble() - static_cast<double>(b.asInt()));
        }
        return Value(toDouble(a) - toDouble(b));
    }

    FORCE_INLINE Value numericMul(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return Value(a.asInt() * b.asInt());
            if (b.isDouble()) return Value(static_cast<double>(a.asInt()) * b.asDouble());
        } else if (a.isDouble()) {
            if (b.isDouble()) return Value(a.asDouble() * b.asDouble());
            if (b.isInt()) return Value(a.asDouble() * static_cast<double>(b.asInt()));
        }
        return Value(toDouble(a) * toDouble(b));
    }

    FORCE_INLINE Value numericDiv(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) {
                int ib = b.asInt();
                if (ib == 0) return Value();
                return Value(static_cast<double>(a.asInt()) / static_cast<double>(ib));
            }
            if (b.isDouble()) {
                double db = b.asDouble();
                if (db == 0.0) return Value();
                return Value(static_cast<double>(a.asInt()) / db);
            }
        } else if (a.isDouble()) {
            if (b.isDouble()) {
                double db = b.asDouble();
                if (db == 0.0) return Value();
                return Value(a.asDouble() / db);
            }
            if (b.isInt()) {
                int ib = b.asInt();
                if (ib == 0) return Value();
                return Value(a.asDouble() / static_cast<double>(ib));
            }
        }
        const double db = toDouble(b);
        if (db == 0.0) return Value();
        return Value(toDouble(a) / db);
    }

    FORCE_INLINE Value numericMod(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) {
                int ib = b.asInt();
                if (ib == 0) return Value();
                return Value(a.asInt() % ib);
            }
            if (b.isDouble()) {
                double db = b.asDouble();
                if (db == 0.0) return Value();
                return Value(std::fmod(static_cast<double>(a.asInt()), db));
            }
        } else if (a.isDouble()) {
            if (b.isDouble()) {
                double db = b.asDouble();
                if (db == 0.0) return Value();
                return Value(std::fmod(a.asDouble(), db));
            }
            if (b.isInt()) {
                int ib = b.asInt();
                if (ib == 0) return Value();
                return Value(std::fmod(a.asDouble(), static_cast<double>(ib)));
            }
        }
        const double db = toDouble(b);
        if (db == 0.0) return Value();
        return Value(std::fmod(toDouble(a), db));
    }

    FORCE_INLINE Value numericNegate(const Value& a) {
        if (a.isInt()) return Value(-a.asInt());
        if (a.isDouble()) return Value(-a.asDouble());
        return Value();
    }

    FORCE_INLINE bool numericLT(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return a.asInt() < b.asInt();
            if (b.isDouble()) return static_cast<double>(a.asInt()) < b.asDouble();
        } else if (a.isDouble()) {
            if (b.isDouble()) return a.asDouble() < b.asDouble();
            if (b.isInt()) return a.asDouble() < static_cast<double>(b.asInt());
        }
        return toDouble(a) < toDouble(b);
    }

    FORCE_INLINE bool numericGT(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return a.asInt() > b.asInt();
            if (b.isDouble()) return static_cast<double>(a.asInt()) > b.asDouble();
        } else if (a.isDouble()) {
            if (b.isDouble()) return a.asDouble() > b.asDouble();
            if (b.isInt()) return a.asDouble() > static_cast<double>(b.asInt());
        }
        return toDouble(a) > toDouble(b);
    }

    FORCE_INLINE bool numericLE(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return a.asInt() <= b.asInt();
            if (b.isDouble()) return static_cast<double>(a.asInt()) <= b.asDouble();
        } else if (a.isDouble()) {
            if (b.isDouble()) return a.asDouble() <= b.asDouble();
            if (b.isInt()) return a.asDouble() <= static_cast<double>(b.asInt());
        }
        return toDouble(a) <= toDouble(b);
    }

    FORCE_INLINE bool numericGE(const Value& a, const Value& b) {
        if (a.isInt()) {
            if (b.isInt()) return a.asInt() >= b.asInt();
            if (b.isDouble()) return static_cast<double>(a.asInt()) >= b.asDouble();
        } else if (a.isDouble()) {
            if (b.isDouble()) return a.asDouble() >= b.asDouble();
            if (b.isInt()) return a.asDouble() >= static_cast<double>(b.asInt());
        }
        return toDouble(a) >= toDouble(b);
    }

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

    /**
     * @brief Rope node for lazy string concatenation.
     * A binary tree where leaves are StringData (or SSO) and internal nodes
     * represent concatenation. Avoids O(n^2) copies in chain concatenation.
     * Auto-flattens when depth exceeds MAX_DEPTH.
     */
    struct RopeData : Managed {
        static constexpr int MAX_DEPTH = 64;
        Value left;
        Value right;
        size_t length;
        int depth;
        mutable std::string cachedFlat;

        RopeData(const Value& l, const Value& r);
        void flattenInto(std::string& out) const;
        std::string flatten() const;
        const std::string& getStringRef() const;
    };
}

#endif //VALUE_H
