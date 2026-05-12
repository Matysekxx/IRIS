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
    std::string toString(const Value& v);

    /** @brief Converts a Value to a double (if numeric). */
    double toDouble(const Value& v);

    bool isNumeric(const Value& v);

    /** @brief Performs addition between two numeric values. */
    Value numericAdd(const Value& a, const Value& b);

    /** @brief Performs subtraction between two numeric values. */
    Value numericSub(const Value& a, const Value& b);

    /** @brief Performs multiplication between two numeric values. */
    Value numericMul(const Value& a, const Value& b);

    /** @brief Performs division between two numeric values. */
    Value numericDiv(const Value& a, const Value& b);

    /** @brief Performs modulo between two numeric values. */
    Value numericMod(const Value& a, const Value& b);

    /** @brief Negates a numeric value. */
    Value numericNegate(const Value& a);

    bool numericLT(const Value& a, const Value& b);
    bool numericGT(const Value& a, const Value& b);
    bool numericLE(const Value& a, const Value& b);
    bool numericGE(const Value& a, const Value& b);

    struct ObjectData : Managed {
        uint16_t classId;
        std::vector<Value> fields;
    };
}

#endif //VALUE_H