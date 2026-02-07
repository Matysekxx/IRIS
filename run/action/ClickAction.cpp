#include "ClickAction.h"
#include <windows.h>
#include <variant>

void ClickAction::run(Instruction instruction) {
    if (instruction.op != OpCode::CLICK) return;

    const int button = std::get<int>(instruction.arg1);
    
    if (button == 0) {
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    } else {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    }
}