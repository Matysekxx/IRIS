//
// Created by chalo on 07.02.2026.
//

#include "InstructionFactory.h"

#include <variant>
#include <stdexcept>

#include "Instruction.h"

InstructionFactory::InstructionFactory() {
    this->init();
}

void InstructionFactory::init() {
    factory["MOVE"] = [](const std::vector<std::string> &args) {
        if (args.size() < 2) throw std::runtime_error("MOVE requires 2 arguments");
        auto inst = Instruction();
        inst.op = OpCode::MOVE_MOUSE;
        inst.arg1 = std::stoi(args[0]);
        inst.arg2 = std::stoi(args[1]);
        return inst;
    };
}

Instruction InstructionFactory::create(const std::string &command, const std::vector<std::string> &args) {
    if (factory.contains(command)) {
        return factory[command](args);
    }
    throw std::runtime_error("Unknown command: " + command);
}

InstructionFactory::~InstructionFactory() = default;