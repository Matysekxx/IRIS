//
// Created by chalo on 07.02.2026.
//

#include "KeyPressAction.h"
#include <windows.h>
#include <variant>

void KeyPressAction::run(Instruction instruction) {
    if (instruction.op != OpCode::PRESS_KEY) {
        return;
    }
    if (std::holds_alternative<int>(instruction.arg1)) {
        const int vkCode = std::get<int>(instruction.arg1);

        keybd_event(static_cast<BYTE>(vkCode), 0, 0, 0);
        keybd_event(static_cast<BYTE>(vkCode), 0, KEYEVENTF_KEYUP, 0);
    }
}