/**
 * @file Value.cpp
 * @brief Implementation of NaN-Tagged Value operations and conversions.
 */

#include "Value.h"
#include "Native.h"
#include "ArrayData.h"
#include "MemoryPool.h"
#include <cmath>
#include <iostream>

namespace iris::core {

    static MemoryPool<ObjectData, 4096> objectPool;

    void releaseManaged(Managed* p) {
        if (!p) return;
        // std::cout << "Release: " << (int)p->type << " ptr: " << p << std::endl;
        switch (p->type) {
            case ManagedType::String: delete static_cast<StringData*>(p); break;
            case ManagedType::Object: delete static_cast<ObjectData*>(p); break;
            case ManagedType::Array:  delete static_cast<ArrayData*>(p);  break;
            case ManagedType::Native: delete static_cast<NativeObject*>(p); break;
        }
    }

    void* ObjectData::operator new(size_t size) {
        if (size != sizeof(ObjectData)) return ::operator new(size);
        return objectPool.allocate();
    }

    void ObjectData::operator delete(void* ptr, size_t size) {
        if (size != sizeof(ObjectData)) {
            ::operator delete(ptr);
            return;
        }
        objectPool.deallocate(static_cast<ObjectData*>(ptr));
    }

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
        if (isString() && o.isString()) return str() == o.str();
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
            bits = QNAN | TAG_SSO | ((uint64_t)s.length() << 48);
            uint64_t payload = 0;
            std::memcpy(&payload, s.data(), s.length());
            bits |= payload;
        } else {
            *this = Value(new StringData(s));
        }
    }
}
