/**
 * @file Value.cpp
 * @brief Implementation of NaN-Tagged Value operations and conversions.
 */

#include "Value.h"
#include "Native.h"
#include "ArrayData.h"
#include "MemoryPool.h"
#include "Variable.h"
#include "GC.h"
#include <cmath>
#include <iostream>
#include <string_view>

namespace iris::core {

    thread_local std::vector<const std::vector<Value>*> activeConstantPools;

    bool Value::isString() const { return isSSO() || (isPtr() && asPtr() && asPtr()->type == ManagedType::String); }
    bool Value::isObject() const { return isPtr() && asPtr() && asPtr()->type == ManagedType::Object; }
    bool Value::isArray()  const { return isPtr() && asPtr() && asPtr()->type == ManagedType::Array; }

    std::string Value::str() const {
        if (isSSO()) return asSSO();
        if (isPtr() && asPtr() && asPtr()->type == ManagedType::String) {
            return static_cast<StringData*>(asPtr())->str;
        }
        return "";
    }

    bool Value::operator==(const Value& o) const {
        if (bits == o.bits) return true;
        if (isDouble() && o.isDouble()) return asDouble() == o.asDouble();
        if ((isInt() || isDouble()) && (o.isInt() || o.isDouble())) {
            return toDouble(*this) == toDouble(o);
        }
        if (isString() && o.isString()) {
            if (isSSO() && o.isSSO()) {
                // Both SSO: compare length and payload directly
                if (stringLength() != o.stringLength()) return false;
                uint64_t mask = (1ULL << (stringLength() * 8)) - 1;
                return (bits & mask) == (o.bits & mask);
            }
            char buf1[8] = {0};
            char buf2[8] = {0};
            std::string_view sv1;
            if (isSSO()) {
                int len = (int)((bits >> 48) - 0x7FF0);
                uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
                std::memcpy(buf1, &payload, 6);
                sv1 = std::string_view(buf1, len);
            } else {
                sv1 = std::string_view(static_cast<StringData*>(asPtr())->str);
            }
            std::string_view sv2;
            if (o.isSSO()) {
                int len = (int)((o.bits >> 48) - 0x7FF0);
                uint64_t payload = o.bits & 0x0000FFFFFFFFFFFFULL;
                std::memcpy(buf2, &payload, 6);
                sv2 = std::string_view(buf2, len);
            } else {
                sv2 = std::string_view(static_cast<StringData*>(o.asPtr())->str);
            }
            return sv1 == sv2;
        }

        return false;
    }

    std::string toString(const Value& v) {
        if (v.isDouble()) {
            std::string s = std::to_string(v.asDouble());
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.') s.pop_back();
            return s;
        }
        if (v.isInt()) return std::to_string(v.asInt());
        if (v.isBool()) return v.asBool() ? "true" : "false";
        if (v.isNull()) return "null";
        if (v.isString()) return v.str();
        if (v.isObject()) return "[object]";
        if (v.isArray()) return "[array]";
        if (v.isPtr()) {
            Managed* p = v.asPtr();
            if (p && p->type == ManagedType::Native) return static_cast<NativeObject*>(p)->toString();
            return "[native]";
        }
        return "unknown";
    }

    double toDouble(const Value& v) {
        if (v.isDouble()) return v.asDouble();
        if (v.isInt()) return static_cast<double>(v.asInt());
        return 0.0;
    }

    bool isNumeric(const Value& v) { return v.isInt() || v.isDouble(); }

    Value numericAdd(const Value& a, const Value& b) {
        if (a.isString() || b.isString()) {
            return Value(new StringData(toString(a) + toString(b)));
        }
        if (a.isInt() && b.isInt()) return Value(a.asInt() + b.asInt());
        return Value(toDouble(a) + toDouble(b));
    }

    Value numericSub(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt() - b.asInt());
        return Value(toDouble(a) - toDouble(b));
    }

    Value numericMul(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) return Value(a.asInt() * b.asInt());
        return Value(toDouble(a) * toDouble(b));
    }

    Value numericDiv(const Value& a, const Value& b) {
        const double db = toDouble(b);
        if (db == 0.0) return Value();
        return Value(toDouble(a) / db);
    }

    Value numericMod(const Value& a, const Value& b) {
        if (a.isInt() && b.isInt()) {
            if (b.asInt() == 0) return Value();
            return Value(a.asInt() % b.asInt());
        }
        const double db = toDouble(b);
        if (db == 0.0) return Value();
        return Value(std::fmod(toDouble(a), db));
    }

    Value numericNegate(const Value& a) {
        if (a.isInt()) return Value(-a.asInt());
        if (a.isDouble()) return Value(-a.asDouble());
        return Value();
    }

    bool numericLT(const Value& a, const Value& b) { return toDouble(a) < toDouble(b); }
    bool numericGT(const Value& a, const Value& b) { return toDouble(a) > toDouble(b); }
    bool numericLE(const Value& a, const Value& b) { return toDouble(a) <= toDouble(b); }
    bool numericGE(const Value& a, const Value& b) { return toDouble(a) >= toDouble(b); }

    void Value::append(const Value& other) {
        std::string s = str() + toString(other);
        if (s.length() <= 6) {
            release();
            bits = (0x7FF0ULL << 48) | ((uint64_t)s.length() << 48);
            uint64_t payload = 0;
            std::memcpy(&payload, s.data(), s.length());
            bits |= payload;
        } else {
            *this = Value(new StringData(s));
        }
    }
}
