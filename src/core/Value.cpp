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
#include <limits>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace iris::core {

    thread_local std::vector<const std::vector<Value>*> activeConstantPools;

    static std::unordered_map<std::string, StringData*> stringPool;

    StringData* internString(const std::string& s) {
        auto it = stringPool.find(s);
        if (it != stringPool.end()) return it->second;
        auto* data = new StringData(s);
        stringPool[s] = data;
        return data;
    }

    // -- RopeData (lazy string concatenation) --

    static void appendValueToString(std::string& out, const Value& v) {
        if (v.isSSO()) {
            int len = (int)((v.bits >> 48) - 0x7FF0);
            out.append(reinterpret_cast<const char*>(&v.bits), len);
        } else if (v.isPtr() && v.asPtr()->type == ManagedType::String) {
            out.append(static_cast<StringData*>(v.asPtr())->str);
        } else if (v.isPtr() && v.asPtr()->type == ManagedType::Rope) {
            static_cast<RopeData*>(v.asPtr())->flattenInto(out);
        } else {
            out.append(toString(v));
        }
    }

    RopeData::RopeData(const Value& l, const Value& r) : Managed(ManagedType::Rope, sizeof(RopeData)) {
        left = l;
        right = r;
        int ld = (l.isPtr() && l.asPtr()->type == ManagedType::Rope) ? static_cast<RopeData*>(l.asPtr())->depth : 0;
        int rd = (r.isPtr() && r.asPtr()->type == ManagedType::Rope) ? static_cast<RopeData*>(r.asPtr())->depth : 0;
        depth = (ld > rd ? ld : rd) + 1;
        length = l.stringLength() + r.stringLength();
        if (depth >= MAX_DEPTH) {
            // Auto-flatten to prevent stack overflow during later flattening
            std::string flat;
            flat.reserve(length);
            appendValueToString(flat, left);
            appendValueToString(flat, right);
            left = Value(new StringData(std::move(flat)));
            right = Value(std::string(""));
            depth = 0;
        }
    }

    void RopeData::flattenInto(std::string& out) const {
        // Explicit stack to avoid deep recursion
        struct Frame { const RopeData* rope; int state; };
        std::vector<Frame> frames;
        frames.push_back({this, 0});
        while (!frames.empty()) {
            Frame& f = frames.back();
            if (f.state == 0) {
                f.state = 1;
                if (f.rope->left.isPtr() && f.rope->left.asPtr()->type == ManagedType::Rope) {
                    frames.push_back({static_cast<RopeData*>(f.rope->left.asPtr()), 0});
                } else {
                    appendValueToString(out, f.rope->left);
                }
            } else if (f.state == 1) {
                f.state = 2;
                if (f.rope->right.isPtr() && f.rope->right.asPtr()->type == ManagedType::Rope) {
                    frames.push_back({static_cast<RopeData*>(f.rope->right.asPtr()), 0});
                } else {
                    appendValueToString(out, f.rope->right);
                }
            } else {
                frames.pop_back();
            }
        }
    }

    std::string RopeData::flatten() const {
        if (!cachedFlat.empty()) return cachedFlat;
        cachedFlat.reserve(length);
        flattenInto(cachedFlat);
        return cachedFlat;
    }

    const std::string& RopeData::getStringRef() const {
        if (cachedFlat.empty()) {
            cachedFlat.reserve(length);
            flattenInto(cachedFlat);
        }
        return cachedFlat;
    }

    size_t Value::stringLength() const {
        if (isSSO()) return (size_t)((bits >> 48) - 0x7FF0);
        if (isPtr()) {
            if (asPtr()->type == ManagedType::Rope) return static_cast<RopeData*>(asPtr())->length;
            return static_cast<StringData*>(asPtr())->str.length();
        }
        return 0;
    }

    size_t Value::hash() const {
        if (isSSO()) return std::hash<std::string_view>{}(view());
        if (isPtr() && asPtr()) {
            if (asPtr()->type == ManagedType::String)
                return static_cast<StringData*>(asPtr())->getCachedHash();
            if (asPtr()->type == ManagedType::Rope)
                return std::hash<std::string>{}(static_cast<RopeData*>(asPtr())->getStringRef());
        }
        return 0;
    }

std::string Value::str() const {
    if (isSSO()) return asSSO();
    if (isPtr() && asPtr()) {
        if (asPtr()->type == ManagedType::String) {
            return static_cast<StringData*>(asPtr())->str;
        }
        if (asPtr()->type == ManagedType::Rope) {
            return static_cast<RopeData*>(asPtr())->flatten();
        }
    }
    return "";
}

std::string_view Value::view() const {
    if (isSSO()) {
        int len = (int)((bits >> 48) - 0x7FF0);
        return std::string_view(reinterpret_cast<const char*>(&bits), len);
    }
    if (isPtr() && asPtr()) {
        if (asPtr()->type == ManagedType::String)
            return static_cast<StringData*>(asPtr())->str;
        if (asPtr()->type == ManagedType::Rope)
            return static_cast<RopeData*>(asPtr())->getStringRef();
    }
    return "";
}

    static const std::string& getStringRefFromManaged(const Managed* p) {
        if (p->type == ManagedType::String)
            return static_cast<const StringData*>(p)->str;
        return static_cast<const RopeData*>(p)->getStringRef();
    }

    bool Value::operator==(const Value& o) const {
        if (bits == o.bits) return true;
        if (isInt() && o.isInt()) return false;
        if (isDouble() && o.isDouble()) return asDouble() == o.asDouble();
        if ((isInt() || isDouble()) && (o.isInt() || o.isDouble())) {
            return toDouble(*this) == toDouble(o);
        }
        if (isString() && o.isString()) {
            if (!isSSO() && !o.isSSO()) {
                Managed* pa = asPtr();
                Managed* pb = o.asPtr();
                if (pa->type == ManagedType::String && pb->type == ManagedType::String) {
                    if (pa == pb) return true;
                    return static_cast<StringData*>(pa)->str == static_cast<StringData*>(pb)->str;
                }
                // One or both are ropes - use reference to avoid copy
                return getStringRefFromManaged(pa) == getStringRefFromManaged(pb);
            }
            return str() == o.str();
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

    Value numericAddString(const Value& a, const Value& b) {
        std::string tmpA, tmpB;
        size_t lenA = a.isString() ? a.stringLength() : (tmpA = toString(a), tmpA.length());
        size_t lenB = b.isString() ? b.stringLength() : (tmpB = toString(b), tmpB.length());
        size_t totalLen = lenA + lenB;
        if (totalLen <= 6) {
            std::string_view va = a.isString() ? a.view() : tmpA;
            std::string_view vb = b.isString() ? b.view() : tmpB;
            char buf[6] = {0};
            std::memcpy(buf, va.data(), va.length());
            std::memcpy(buf + va.length(), vb.data(), vb.length());
            return Value(std::string(buf, totalLen));
        }
        if (totalLen <= 64) {
            std::string flat;
            flat.reserve(totalLen);
            flat.append(a.isString() ? a.view() : tmpA);
            flat.append(b.isString() ? b.view() : tmpB);
            return Value(new StringData(std::move(flat)));
        }
        Value sa = a.isString() ? a : Value(new StringData(tmpA));
        Value sb = b.isString() ? b : Value(new StringData(tmpB));
        return Value(new RopeData(sa, sb));
    }

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

    double float16ToDouble(uint16_t bits) {
        uint16_t sign = bits >> 15;
        uint16_t exp = (bits >> 10) & 0x1F;
        uint16_t mant = bits & 0x3FF;
        if (exp == 0) {
            if (mant == 0) {
                return (sign == 0) ? 0.0 : -0.0;
            }
            double d = (double)mant / 1024.0 * 0.00006103515625;
            return (sign == 0) ? d : -d;
        }
        if (exp == 31) {
            if (mant == 0) {
                return (sign == 0) ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
            }
            return std::numeric_limits<double>::quiet_NaN();
        }
        double d = (1.0 + (double)mant / 1024.0) * std::ldexp(1.0, (int)exp - 15);
        return (sign == 0) ? d : -d;
    }

    uint16_t doubleToFloat16(double d) {
        if (d == 0.0) {
            return (std::signbit(d) ? 0x8000 : 0x0000);
        }
        if (std::isnan(d)) return 0x7E01;
        if (std::isinf(d)) return (d > 0) ? 0x7C00 : 0xFC00;
        int exp;
        double mant = std::frexp(d, &exp);
        int f16Exp = exp + 14;
        if (f16Exp >= 31) return (d > 0) ? 0x7C00 : 0xFC00;
        if (f16Exp <= 0) {
            uint16_t absVal = (uint16_t)(std::ldexp(d, 25) + 0.5);
            if (absVal == 0) return 0x0000;
            return (d > 0) ? absVal : (absVal | 0x8000);
        }
        mant = std::fabs(mant);
        uint16_t f16Mant = (uint16_t)((mant - 0.5) * 2048.0 + 0.5);
        if (f16Mant >= 1024) { f16Exp++; f16Mant = 0; }
        return (d > 0) ? (uint16_t)((uint16_t)f16Exp << 10) | f16Mant
                       : (uint16_t)(0x8000 | ((uint16_t)f16Exp << 10) | f16Mant);
    }
}
