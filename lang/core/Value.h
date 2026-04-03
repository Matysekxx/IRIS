#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <cmath>
#include <memory>
#include <variant>
#include <vector>
#include <cstring>

struct ObjectData;
struct ArrayData;

/**
 * @brief Base class for heap-allocated reference-counted data.
 */
struct Managed {
    uint32_t refCount = 0;
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
 * @brief Represents a dynamically typed value in the IRIS language.
 * 
 * OPTIMIZATION: Small String Optimization (SSO)
 * - Strings <= 15 characters stored inline (no allocation)
 * - Longer strings use heap-allocated StringData
 * 
 * Size is intentionally kept to 16 bytes for extreme cache locality.
 */
struct alignas(8) Value {
    enum Tag : uint8_t {
        TAG_NULL, TAG_INT, TAG_DOUBLE, TAG_BOOL,
        TAG_STRING_SSO, TAG_STRING_HEAP,
        TAG_OBJECT, TAG_ARRAY
    };
    Tag tag;

    struct SSOString {
        char data[14];
        uint8_t len;
    };

    union {
        int asInt;
        double asDouble;
        bool asBool;
        Managed *asPtr;
        SSOString sso;
    };

    Value() : tag(TAG_NULL), asInt(0) {}
    explicit Value(const int v) : tag(TAG_INT), asInt(v) {}
    explicit Value(const double v) : tag(TAG_DOUBLE), asDouble(v) {}
    explicit Value(const bool v) : tag(TAG_BOOL), asBool(v) {}
    explicit Value(std::monostate) : tag(TAG_NULL), asInt(0) {}

    explicit Value(const std::string& v) {
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

    explicit Value(ObjectData* obj) : tag(TAG_OBJECT) {
        asPtr = reinterpret_cast<Managed*>(obj);
        retain();
    }
    explicit Value(ArrayData* arr) : tag(TAG_ARRAY) {
        asPtr = reinterpret_cast<Managed*>(arr);
        retain();
    }

    Value(const Value& other) : tag(other.tag) {
        if (other.tag == TAG_STRING_SSO) {
            sso = other.sso;
        } else if (isHeap()) {
            asPtr = other.asPtr;
            retain();
        } else {
            asDouble = other.asDouble;
        }
    }

    Value(Value&& other) noexcept : tag(other.tag) {
        if (other.tag == TAG_STRING_SSO) {
            sso = other.sso;
            other.tag = TAG_NULL;
        } else if (isHeap()) {
            asPtr = other.asPtr;
            other.asPtr = nullptr;
            other.tag = TAG_NULL;
        } else {
            asDouble = other.asDouble;
        }
    }

    Value& operator=(const Value& other) {
        if (this != &other) {
            release();
            tag = other.tag;
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;
            } else if (isHeap()) {
                asPtr = other.asPtr;
                retain();
            } else {
                asDouble = other.asDouble;
            }
        }
        return *this;
    }

    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            release();
            tag = other.tag;
            if (other.tag == TAG_STRING_SSO) {
                sso = other.sso;  // Copy SSO data
                other.tag = TAG_NULL;
            } else if (isHeap()) {
                asPtr = other.asPtr;
                other.asPtr = nullptr;
                other.tag = TAG_NULL;
            } else {
                asDouble = other.asDouble;
            }
        }
        return *this;
    }

    ~Value() {
        release();
    }

    bool isInt() const { return tag == TAG_INT; }
    bool isDouble() const { return tag == TAG_DOUBLE; }
    bool isBool() const { return tag == TAG_BOOL; }
    bool isString() const { return tag == TAG_STRING_SSO || tag == TAG_STRING_HEAP; }
    bool isNull() const { return tag == TAG_NULL; }
    bool isObject() const { return tag == TAG_OBJECT; }
    bool isArray() const { return tag == TAG_ARRAY; }
    bool isHeap() const { return tag == TAG_STRING_HEAP || tag == TAG_OBJECT || tag == TAG_ARRAY; }
    bool isSSO() const { return tag == TAG_STRING_SSO; }

    /** @brief Returns true if this value is any collection type. */
    bool isCollection() const { return tag == TAG_ARRAY; }

    /** @brief Returns the string value. Works for both SSO and heap strings. */
    std::string str() const {
        if (tag == TAG_STRING_SSO) {
            return std::string(sso.data, sso.len);
        }
        return static_cast<StringData*>(asPtr)->str;
    }

    bool operator==(const Value& o) const {
        if (tag != o.tag) return false;
        switch (tag) {
            case TAG_NULL: return true;
            case TAG_INT: return asInt == o.asInt;
            case TAG_DOUBLE: return asDouble == o.asDouble;
            case TAG_BOOL: return asBool == o.asBool;
            case TAG_STRING_SSO: {
                // Fast SSO comparison
                if (sso.len != o.sso.len) return false;
                return std::memcmp(sso.data, o.sso.data, sso.len) == 0;
            }
            case TAG_STRING_HEAP:
                return static_cast<StringData*>(asPtr)->str == static_cast<StringData*>(o.asPtr)->str;
            case TAG_OBJECT: return asPtr == o.asPtr; // check ref equality for runtime speed
            case TAG_ARRAY: return asPtr == o.asPtr;
        }
        return false;
    }
    bool operator!=(const Value& o) const { return !(*this == o); }

private:
    void retain() {
        if (asPtr) asPtr->refCount++;
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
        case Value::TAG_NULL: return "null";
        case Value::TAG_INT: return std::to_string(v.asInt);
        case Value::TAG_DOUBLE: {
            std::string s = std::to_string(v.asDouble);
            auto pos = s.find_last_not_of('0');
            if (pos != std::string::npos && s[pos] == '.') pos--;
            s.erase(pos + 1);
            return s;
        }
        case Value::TAG_BOOL: return v.asBool ? "true" : "false";
        case Value::TAG_STRING_SSO:
        case Value::TAG_STRING_HEAP: return v.str();
        case Value::TAG_OBJECT: return "<object>";
        case Value::TAG_ARRAY: return "<array>";
    }
    return "null";
}

/** @brief Converts a Value to a double (0.0 if not numeric). */
inline double toDouble(const Value& v) {
    if (v.tag == Value::TAG_INT) return v.asInt;
    if (v.tag == Value::TAG_DOUBLE) return v.asDouble;
    return 0.0;
}

/** @brief Checks if the value is an integer or a double. */
inline bool isNumeric(const Value& v) {
    return v.tag == Value::TAG_INT || v.tag == Value::TAG_DOUBLE;
}

/** @brief Adds two values (int+int or double+double). */
inline Value numericAdd(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) return Value(a.asInt + b.asInt);
    return Value(toDouble(a) + toDouble(b));
}

/** @brief Subtracts two values. */
inline Value numericSub(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) return Value(a.asInt - b.asInt);
    return Value(toDouble(a) - toDouble(b));
}

/** @brief Multiplies two values. */
inline Value numericMul(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) return Value(a.asInt * b.asInt);
    return Value(toDouble(a) * toDouble(b));
}

/** @brief Divides two values (returns null on division by zero). */
inline Value numericDiv(const Value& a, const Value& b) {
    const double db = toDouble(b);
    if (db == 0.0) return {};
    if (a.isInt() && b.isInt()) return Value(a.asInt / b.asInt);
    return Value(toDouble(a) / db);
}

/** @brief Calculates modulo (remainder). */
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

struct ObjectData : Managed {
    uint16_t classId;
    std::vector<Value> fields;
};

#endif //VALUE_H
