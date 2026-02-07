

#include "Runner.h"

#include "action/KeyPressAction.h"
#include "action/MoveMouseAction.h"
#include "action/WaitAction.h"
#include "action/ClickAction.h"
#include "action/LogAction.h"

Runner::Runner(Logger *log) {
    this->log = log;
    this->init();
}

void Runner::init() {
    actions[OpCode::MOVE_MOUSE] = std::make_unique<MoveMouseAction>();
    actions[OpCode::PRESS_KEY] = std::make_unique<KeyPressAction>();
    actions[OpCode::WAIT] = std::make_unique<WaitAction>();
    actions[OpCode::CLICK] = std::make_unique<ClickAction>();
    actions[OpCode::LOG] = std::make_unique<LogAction>(this->log);
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
