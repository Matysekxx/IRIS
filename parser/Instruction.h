
#ifndef INSTRUCTION_H
#define INSTRUCTION_H
#include <cstdint>
#include <variant>
#include <string>


enum class OpCode : uint8_t {
    MOVE_MOUSE,
    PRESS_KEY,
    CLICK,
    WAIT,
    LOG,
    EXIT
};

struct Instruction {
    OpCode op;
    std::variant<int, double, std::string> arg1;
    std::variant<int, double, std::string> arg2;
};



#endif //INSTRUCTION_H
