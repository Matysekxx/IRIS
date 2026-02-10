//
// Created by chalo on 10.02.2026.
//

#ifndef VALUE_H
#define VALUE_H
#include <string>
#include <variant>

using Value = std::variant<std::monostate ,int, std::string, bool>;

#endif //VALUE_H
