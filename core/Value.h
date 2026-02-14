
#ifndef VALUE_H
#define VALUE_H
#include <string>
#include <variant>

using Value = std::variant<std::monostate ,int, std::string, bool>;

struct ValueToStringVisitor {
    std::string operator()(std::monostate) const { return "null"; }
    std::string operator()(const int val) const { return std::to_string(val); }
    std::string operator()(const bool val) const { return val ? "true" : "false"; }
    std::string operator()(const std::string& val) const { return val; }
};

inline std::string toString(const Value& value) {
    return std::visit(ValueToStringVisitor{}, value);
}

#endif //VALUE_H
