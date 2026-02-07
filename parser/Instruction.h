//
// Created by chalo on 07.02.2026.
//

#ifndef INSTRUCTION_H
#define INSTRUCTION_H
#include <cstdint>
#include <variant>

enum class OpCode : uint8_t {
    MOVE_MOUSE,
    PRESS_KEY,
    WAIT,
    EXIT
};

struct Instruction {
    OpCode op;
    std::variant<int, double, std::string> arg1;
    std::variant<int, double, std::string> arg2;
};



#endif //INSTRUCTION_H
