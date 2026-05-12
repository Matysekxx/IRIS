#include "Value.h"
#include "Native.h"
#include <cmath>

namespace iris::core {
    std::string toString(const Value& v) {
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

    double toDouble(const Value& v) {
        if (v.isInt()) return static_cast<double>(v.asInt);
        if (v.isDouble()) return v.asDouble;
        return 0.0;
    }

    bool isNumeric(const Value& v) { return v.isInt() || v.isDouble(); }

    Value numericAdd(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt + b.asInt);
        return Value(toDouble(a) + toDouble(b));
    }

    Value numericSub(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt - b.asInt);
        return Value(toDouble(a) - toDouble(b));
    }

    Value numericMul(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt * b.asInt);
        return Value(toDouble(a) * toDouble(b));
    }

    Value numericDiv(const Value& a, const Value& b) {
        const double db = toDouble(b);
        if (db == 0.0) return {};
        return Value(toDouble(a) / db);
    }

    Value numericMod(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) {
            if (b.asInt == 0) return {};
            return Value(a.asInt % b.asInt);
        }
        const double db = toDouble(b);
        if (db == 0.0) return {};
        return Value(std::fmod(toDouble(a), db));
    }

    Value numericNegate(const Value& a) {
        if (a.isInt()) return Value(-a.asInt);
        if (a.isDouble()) return Value(-a.asDouble);
        return {};
    }

    bool numericLT(const Value& a, const Value& b) { return toDouble(a) < toDouble(b); }
    bool numericGT(const Value& a, const Value& b) { return toDouble(a) > toDouble(b); }
    bool numericLE(const Value& a, const Value& b) { return toDouble(a) <= toDouble(b); }
    bool numericGE(const Value& a, const Value& b) { return toDouble(a) >= toDouble(b); }

    void Value::append(const Value& other) {
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
}
