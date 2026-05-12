#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <cmath>
#include <memory>
#include <variant>
#include <vector>
#include <cstring>

#include "Native.h"

namespace iris::core {
    struct ObjectData;
    struct ArrayData;

    /**
     * @brief Base class for heap-allocated reference-counted data.
     */
    struct Managed {
        uint32_t refCount = 0;
        Managed() = default;
        Managed(const Managed&) : refCount(0) {}
        Managed& operator=(const Managed&) { return *this; }
        virtual ~Managed() = default;
    };

    /**
     * @brief Heap-allocated string.
     */
    struct StringData : Managed {
        std::string str;
        explicit StringData(std::string s) : str(std::move(s)) {}
    };

    /**
     * @brief Main value type for the IRIS language.
     * Uses 16-byte Small String Optimization (SSO) and reference counting.
     */
    struct Value {
        enum Tag : uint8_t {
            TAG_NULL = 0,
            TAG_BOOL,
            TAG_INT,
            TAG_DOUBLE,
            TAG_OBJECT,
            TAG_ARRAY,
            TAG_STRING_SSO,
            TAG_STRING_HEAP,
            TAG_NATIVE_OBJ
        };

        struct SSOString {
            char data[14];
            uint8_t len;
            uint8_t tag;
        };

        union {
            struct {
                union {
                    int asInt;
                    double asDouble;
                    bool asBool;
                    Managed *asPtr;
                };
                uint8_t _padding[7];
                uint8_t tag;
            };
            SSOString sso;
        };

        Value() { tag = TAG_NULL; asDouble = 0; }
        explicit Value(const int v) { tag = TAG_INT; asDouble = 0; asInt = v; }
        explicit Value(const double v) { tag = TAG_DOUBLE; asDouble = v; }
        explicit Value(const bool v) { tag = TAG_BOOL; asDouble = 0; asBool = v; }
        explicit Value(std::monostate) { tag = TAG_NULL; asDouble = 0; }

        explicit Value(const std::string& v) {
            asDouble = 0; // Clear union
            if (v.size() <= 14) {
                tag = TAG_STRING_SSO;
                sso.len = static_cast<uint8_t>(v.size());
                std::memcpy(sso.data, v.data(), v.size());
            } else {
                tag = TAG_STRING_HEAP;
                asPtr = new StringData(v);
                retain();
            }
        }
        
        explicit Value(std::string&& v) {
            asDouble = 0;
            if (v.size() <= 14) {
                tag = TAG_STRING_SSO;
                sso.len = static_cast<uint8_t>(v.size());
                std::memcpy(sso.data, v.data(), v.size());
            } else {
                tag = TAG_STRING_HEAP;
                asPtr = new StringData(std::move(v));
                retain();
            }
        }
        
        explicit Value(const char* v) {
            asDouble = 0;
            size_t len = std::strlen(v);
            if (len <= 14) {
                tag = TAG_STRING_SSO;
                sso.len = static_cast<uint8_t>(len);
                std::memcpy(sso.data, v, len);
            } else {
                tag = TAG_STRING_HEAP;
                asPtr = new StringData(v);
                retain();
            }
        }

        explicit Value(ObjectData* obj) {
            asDouble = 0;
            tag = TAG_OBJECT;
            asPtr = reinterpret_cast<Managed*>(obj);
            retain();
        }
        explicit Value(ArrayData* arr) {
            asDouble = 0;
            tag = TAG_ARRAY;
            asPtr = reinterpret_cast<Managed*>(arr);
            retain();
        }
        explicit Value(NativeObject* obj) {
            asDouble = 0;
            tag = TAG_NATIVE_OBJ;
            asPtr = reinterpret_cast<Managed*>(obj);
            retain();
        }

        Value(const Value& other) {
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble; // Copies tag too!
                tag = other.tag;
                retain();
            }
        }

        Value(Value&& other) noexcept {
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble;
                tag = other.tag;
                asPtr = other.asPtr;
            }
            other.tag = TAG_NULL;
            other.asPtr = nullptr;
        }

        Value& operator=(const Value& other) {
            if (this == &other) return *this;
            release();
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble;
                tag = other.tag;
                retain();
            }
            return *this;
        }

        Value& operator=(Value&& other) noexcept {
            if (this == &other) return *this;
            release();
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble;
                tag = other.tag;
                asPtr = other.asPtr;
            }
            other.tag = TAG_NULL;
            other.asPtr = nullptr;
            return *this;
        }

        ~Value() {
            release();
        }

        bool isInt() const { return tag == TAG_INT; }
        bool isDouble() const { return tag == TAG_DOUBLE; }
        bool isBool() const { return tag == TAG_BOOL; }
        bool isString() const { return tag == TAG_STRING_SSO || tag == TAG_STRING_HEAP; }
        bool isObject() const { return tag == TAG_OBJECT; }
        bool isArray() const { return tag == TAG_ARRAY; }
        bool isNull() const { return tag == TAG_NULL; }
        bool isHeap() const { return tag == TAG_OBJECT || tag == TAG_ARRAY || tag == TAG_STRING_HEAP || tag == TAG_NATIVE_OBJ; }

        /** @brief Returns the string value. Works for both SSO and heap strings. */
        std::string str() const {
            if (tag == TAG_STRING_SSO) {
                return std::string(sso.data, sso.len);
            }
            if (tag == TAG_STRING_HEAP && asPtr) {
                return static_cast<StringData*>(asPtr)->str;
            }
            return "";
        }

        /** @brief Appends another value to this string. Optimized for heap strings with refCount 1. */
        void append(const Value& other);

        bool operator==(const Value& o) const {
            if (tag != o.tag) return false;
            switch (tag) {
                case TAG_NULL: return true;
                case TAG_INT: return asInt == o.asInt;
                case TAG_DOUBLE: return asDouble == o.asDouble;
                case TAG_BOOL: return asBool == o.asBool;
                case TAG_OBJECT:
                case TAG_ARRAY:
                case TAG_NATIVE_OBJ: return asPtr == o.asPtr;
                case TAG_STRING_SSO:
                    if (sso.len != o.sso.len) return false;
                    return std::memcmp(sso.data, o.sso.data, sso.len) == 0;
                case TAG_STRING_HEAP:
                    return static_cast<StringData*>(asPtr)->str == static_cast<StringData*>(o.asPtr)->str;
            }
            return false;
        }
        bool operator!=(const Value& o) const { return !(*this == o); }

        void retain() {
            if (isHeap() && asPtr) asPtr->refCount++;
        }
        void release() {
            if (isHeap() && asPtr) {
                if (--asPtr->refCount == 0) {
                    delete asPtr;
                }
            }
        }
    };

    /** @brief Converts a Value to its string representation. */
    inline std::string toString(const Value& v) {
        switch (v.tag) {
            case Value::TAG_INT: return std::to_string(v.asInt);
            case Value::TAG_DOUBLE: {
                std::string s = std::to_string(v.asDouble);
                s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                if (s.back() == '.') s.pop_back();
                return s;
            }
            case Value::TAG_BOOL: return v.asBool ? "true" : "false";
            case Value::TAG_STRING_SSO:
            case Value::TAG_STRING_HEAP: return v.str();
            case Value::TAG_OBJECT: return "[object]";
            case Value::TAG_ARRAY: return "[array]";
            case Value::TAG_NATIVE_OBJ: {
                if (v.asPtr) return static_cast<NativeObject*>(v.asPtr)->toString();
                return "[native object]";
            }
            default: return "null";
        }
    }

    /** @brief Converts a Value to a double (if numeric). */
    inline double toDouble(const Value& v) {
        if (v.isInt()) return static_cast<double>(v.asInt);
        if (v.isDouble()) return v.asDouble;
        return 0.0;
    }

    inline bool isNumeric(const Value& v) { return v.isInt() || v.isDouble(); }

    /** @brief Performs addition between two numeric values. */
    inline Value numericAdd(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt + b.asInt);
        return Value(toDouble(a) + toDouble(b));
    }

    /** @brief Performs subtraction between two numeric values. */
    inline Value numericSub(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt - b.asInt);
        return Value(toDouble(a) - toDouble(b));
    }

    /** @brief Performs multiplication between two numeric values. */
    inline Value numericMul(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt * b.asInt);
        return Value(toDouble(a) * toDouble(b));
    }

    /** @brief Performs division between two numeric values. */
    inline Value numericDiv(const Value& a, const Value& b) {
        const double db = toDouble(b);
        if (db == 0.0) return {};
        return Value(toDouble(a) / db);
    }

    /** @brief Performs modulo between two numeric values. */
    inline Value numericMod(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) {
            if (b.asInt == 0) return {};
            return Value(a.asInt % b.asInt);
        }
        const double db = toDouble(b);
        if (db == 0.0) return {};
        return Value(std::fmod(toDouble(a), db));
    }

    /** @brief Negates a numeric value. */
    inline Value numericNegate(const Value& a) {
        if (a.isInt()) return Value(-a.asInt);
        if (a.isDouble()) return Value(-a.asDouble);
        return {};
    }

    inline bool numericLT(const Value& a, const Value& b) { return toDouble(a) < toDouble(b); }
    inline bool numericGT(const Value& a, const Value& b) { return toDouble(a) > toDouble(b); }
    inline bool numericLE(const Value& a, const Value& b) { return toDouble(a) <= toDouble(b); }
    inline bool numericGE(const Value& a, const Value& b) { return toDouble(a) >= toDouble(b); }

    inline void Value::append(const Value& other) {
        if (tag == TAG_STRING_HEAP && asPtr && asPtr->refCount == 1) {
            StringData* sd = static_cast<StringData*>(asPtr);
            if (other.tag == TAG_INT) {
                sd->str += std::to_string(other.asInt);
            } else if (other.tag == TAG_STRING_SSO) {
                sd->str.append(other.sso.data, other.sso.len);
            } else if (other.tag == TAG_STRING_HEAP) {
                sd->str += static_cast<StringData*>(other.asPtr)->str;
            } else {
                sd->str += toString(other);
            }
        } else {
            *this = Value(str() + toString(other));
        }
    }

    struct ObjectData : Managed {
        uint16_t classId;
        std::vector<Value> fields;
    };
}

#endif //VALUE_H