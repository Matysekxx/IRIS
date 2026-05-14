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
     * 
     * Uses a 16-byte union with Small String Optimization (SSO) and reference counting 
     * for heap-allocated objects. The structure is designed to be compact and efficient,
     * fitting into two 64-bit registers.
     * 
     * Memory layout:
     * - First 8 bytes: Data (int, double, bool, or pointer)
     * - Next 7 bytes: Padding (used by SSO)
     * - Last 1 byte: Tag (type identifier)
     */
    struct Value {
        /**
         * @brief Type identifier for the Value.
         */
        enum Tag : uint8_t {
            TAG_NULL = 0,   ///< Null value
            TAG_BOOL,       ///< Boolean value (true/false)
            TAG_INT,        ///< 32-bit integer
            TAG_DOUBLE,     ///< 64-bit floating point
            TAG_STRING_SSO, ///< Small String Optimization (up to 14 chars)
            TAG_OBJECT,     ///< Heap-allocated IRIS object (Heap starts here)
            TAG_ARRAY,      ///< Heap-allocated IRIS array
            TAG_STRING_HEAP, ///< Heap-allocated string
            TAG_NATIVE_OBJ  ///< C++ Interop object
        };

        /** @brief SSO string structure (replaces the main union when tag is TAG_STRING_SSO) */
        struct SSOString {
            char data[14];  ///< String data
            uint8_t len;    ///< String length
            uint8_t tag;    ///< Must be TAG_STRING_SSO
        };

        union {
            struct {
                union {
                    int asInt;          ///< Integer data
                    double asDouble;    ///< Double/Boolean data (union overlapped)
                    bool asBool;        ///< Boolean data
                    Managed *asPtr;     ///< Pointer to heap-allocated object
                };
                uint8_t _padding[7];    ///< Padding to align tag to 16th byte
                uint8_t tag;            ///< Type tag
            };
            SSOString sso;              ///< SSO string representation
        };

        /** @brief Default constructor: initializes to NULL. */
        Value() { tag = TAG_NULL; asDouble = 0; }
        
        /** @brief Constructor for integers. */
        explicit Value(const int v) { tag = TAG_INT; asDouble = 0; asInt = v; }
        
        /** @brief Constructor for doubles. */
        explicit Value(const double v) { tag = TAG_DOUBLE; asDouble = v; }
        
        /** @brief Constructor for booleans. */
        explicit Value(const bool v) { tag = TAG_BOOL; asDouble = 0; asBool = v; }
        
        /** @brief Constructor for null (via monostate). */
        explicit Value(std::monostate) { tag = TAG_NULL; asDouble = 0; }

        /** @brief Constructor for strings. Automatically chooses between SSO and heap. */
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
        
        /** @brief Move constructor for strings. */
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
        
        /** @brief Constructor for C-style strings. */
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

        /** @brief Constructor for IRIS objects. */
        explicit Value(ObjectData* obj) {
            asDouble = 0;
            tag = TAG_OBJECT;
            asPtr = reinterpret_cast<Managed*>(obj);
            retain();
        }

        /** @brief Constructor for IRIS arrays. */
        explicit Value(ArrayData* arr) {
            asDouble = 0;
            tag = TAG_ARRAY;
            asPtr = reinterpret_cast<Managed*>(arr);
            retain();
        }

        /** @brief Constructor for Native C++ objects. */
        explicit Value(NativeObject* obj) {
            asDouble = 0;
            tag = TAG_NATIVE_OBJ;
            asPtr = reinterpret_cast<Managed*>(obj);
            retain();
        }

        /** @brief Copy constructor. Optimized for primitive types. */
        Value(const Value& other) {
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble; 
                tag = other.tag;
                if (tag >= TAG_OBJECT) retain();
            }
        }

        /** @brief Move constructor. Transfers ownership without re-retaining. */
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

        /** @brief Copy assignment operator. Optimized for primitive types. */
        Value& operator=(const Value& other) {
            if (this == &other) return *this;
            release();
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else {
                asDouble = other.asDouble;
                tag = other.tag;
                if (tag >= TAG_OBJECT) retain();
            }
            return *this;
        }

        /** @brief Move assignment operator. */
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

        /** @brief Destructor. Decrements reference count if heap-allocated. */
        ~Value() {
            release();
        }

        // --- Type checks ---
        bool isInt() const { return tag == TAG_INT; }
        bool isDouble() const { return tag == TAG_DOUBLE; }
        bool isBool() const { return tag == TAG_BOOL; }
        bool isString() const { return tag == TAG_STRING_SSO || tag == TAG_STRING_HEAP; }
        bool isObject() const { return tag == TAG_OBJECT; }
        bool isArray() const { return tag == TAG_ARRAY; }
        bool isNull() const { return tag == TAG_NULL; }

        /** @brief Checks if the value is a heap-allocated object. */
        inline bool isHeap() const { return tag >= TAG_OBJECT; }

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

        /** @brief Increment reference count of heap object. */
        inline void retain() {
            if (tag >= TAG_OBJECT && asPtr) asPtr->refCount++;
        }
        
        /** @brief Decrement reference count and delete if zero. */
        inline void release() {
            if (tag >= TAG_OBJECT && asPtr) {
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
