//
// Created by chalo on 07.02.2026.
//

#include "Runner.h"

#include "action/KeyPressAction.h"
#include "action/MoveMouseAction.h"

Runner::Runner(Logger *log) {
    this->log = log;
}

void Runner::init() {
    actions[OpCode::MOVE_MOUSE] = std::make_unique<MoveMouseAction()>;
    actions[OpCode::PRESS_KEY] = std::make_unique<KeyPressAction()>;


}

void Runner::run(const std::vector<Instruction> &instructions) {
    for (const Instruction instruction : instructions) {
        runInstruction(instruction);
    }
}

void Runner::runInstruction(const Instruction &instruction) {
    if (!actions.contains(instruction.op)) return;
    actions[instruction.op]->run(instruction);
}


