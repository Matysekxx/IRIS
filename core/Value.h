#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <cmath>
#include <memory>
#include <variant>

/**
 * @brief Represents a dynamically typed value in the IRIS language.
 * Uses a tagged union to store integers, doubles, booleans, or strings.
 */
struct Value {
    enum Tag : uint8_t { TAG_NULL, TAG_INT, TAG_DOUBLE, TAG_BOOL, TAG_STRING };
    Tag tag;

    union {
        int asInt;
        double asDouble;
        bool asBool;
    };
    std::shared_ptr<std::string> sptr;

    Value() : tag(TAG_NULL), asInt(0) {}
    explicit Value(const int v) : tag(TAG_INT), asInt(v) {}
    explicit Value(const double v) : tag(TAG_DOUBLE), asDouble(v) {}
    explicit Value(const bool v) : tag(TAG_BOOL), asBool(v) {}
    explicit Value(const std::string& v) : tag(TAG_STRING), asInt(0), sptr(std::make_shared<std::string>(v)) {}
    explicit Value(std::string&& v) : tag(TAG_STRING), asInt(0), sptr(std::make_shared<std::string>(std::move(v))) {}
    explicit Value(const char* v) : tag(TAG_STRING), asInt(0), sptr(std::make_shared<std::string>(v)) {}
    explicit Value(std::monostate) : tag(TAG_NULL), asInt(0) {}

    bool isInt() const { return tag == TAG_INT; }
    bool isDouble() const { return tag == TAG_DOUBLE; }
    bool isBool() const { return tag == TAG_BOOL; }
    bool isString() const { return tag == TAG_STRING; }
    bool isNull() const { return tag == TAG_NULL; }

    /** @brief Returns the string value (unsafe if not a string). */
    const std::string& str() const { return *sptr; }

    bool operator==(const Value& o) const {
        if (tag != o.tag) return false;
        switch (tag) {
            case TAG_NULL: return true;
            case TAG_INT: return asInt == o.asInt;
            case TAG_DOUBLE: return asDouble == o.asDouble;
            case TAG_BOOL: return asBool == o.asBool;
            case TAG_STRING: return *sptr == *o.sptr;
        }
        return false;
    }
    bool operator!=(const Value& o) const { return !(*this == o); }
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
        case Value::TAG_STRING: return v.str();
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

#endif //VALUE_H
