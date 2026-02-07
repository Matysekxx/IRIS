#include "MoveMouseAction.h"

#include <array>
#include <windows.h>


void MoveMouseAction::run(Instruction instruction) {
    if (instruction.op != OpCode::MOVE_MOUSE) {
        return;
    }
    const int x = std::get<int>(instruction.arg1);
    const int y = std::get<int>(instruction.arg2);

    POINT point;
    if (GetCursorPos(&point)) {
        SetCursorPos(point.x + x, point.y + y);
    }
}

