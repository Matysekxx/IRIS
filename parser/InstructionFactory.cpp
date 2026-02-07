//
// Created by chalo on 07.02.2026.
//

#include "InstructionFactory.h"

#include <variant>
#include <stdexcept>

#include "Instruction.h"
#include <windows.h>

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

    factory["WAIT"] = [](const std::vector<std::string> &args) {
        auto inst = Instruction();
        inst.op = OpCode::WAIT;
        inst.arg1 = std::stoi(args[0]);
        return inst;
    };

    factory["CLICK"] = [](const std::vector<std::string> &args) {
        auto inst = Instruction();
        inst.op = OpCode::CLICK;
        inst.arg1 = (args[0] == "RIGHT") ? 1 : 0;
        return inst;
    };

    factory["PRESS"] = [](const std::vector<std::string> &args) {
        auto inst = Instruction();
        inst.op = OpCode::PRESS_KEY;
        if (!args[0].empty() && isalpha(args[0][0])) {
            const short vk = VkKeyScanA(args[0][0]);
            inst.arg1 = vk & 0xFF;
        } else {
            inst.arg1 = std::stoi(args[0]);
        }
        return inst;
    };

    factory["LOG"] = [](const std::vector<std::string> &args) {
        auto inst = Instruction();
        inst.op = OpCode::LOG;
        std::string text = args[0];
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
            text = text.substr(1, text.size() - 2);
        }
        inst.arg1 = text;
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