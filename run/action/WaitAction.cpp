#include "WaitAction.h"
#include <windows.h>
#include <variant>

void WaitAction::run(Instruction instruction) {
    if (instruction.op != OpCode::WAIT) return;
    
    if (std::holds_alternative<int>(instruction.arg1)) {
        const int ms = std::get<int>(instruction.arg1);
        Sleep(ms);
    }
}