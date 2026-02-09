#include "LTSNode.h"

#include "../log/Logger.h"
#include <windows.h>

static int resolveKey(const std::string& key) {
    if (key == "enter") return VK_RETURN;
    if (key == "escape") return VK_ESCAPE;
    if (key == "space") return VK_SPACE;
    if (key == "backspace") return VK_BACK;
    if (key == "tab") return VK_TAB;
    if (key == "shift") return VK_SHIFT;
    if (key == "ctrl") return VK_CONTROL;
    if (key == "alt") return VK_MENU;

    if (key.length() == 1) {
        return VkKeyScanA(key[0]) & 0xFF;
    }
    return 0;
}

static void pressKey(const int vk) {
    if (vk == 0) return;
    keybd_event(static_cast<BYTE>(vk), 0, 0, 0);
    keybd_event(static_cast<BYTE>(vk), 0, KEYEVENTF_KEYUP, 0);
}

void ProgramNode::execute(Logger* logger) {
    for (const auto& stmt : statements) {
        stmt->execute(logger);
    }
}

void WaitNode::execute(Logger* logger) {
    logger->info("Waiting " + std::to_string(duration) + "ms");
    Sleep(duration);
}

void MouseBlockNode::execute(Logger* logger) {
    for (const auto& action : actions) {
        action->execute(logger);
    }
}

void ClickNode::execute(Logger* logger) {
    if (x.has_value() && y.has_value()) {
        SetCursorPos(x.value(), y.value());
    }
    
    if (button == Left) {
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        logger->info("Click Left");
    } else {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        logger->info("Click Right");
    }
}

void MoveNode::execute(Logger* logger) {
    SetCursorPos(x, y);
    logger->info("Move to " + std::to_string(x) + ", " + std::to_string(y));
}

void KeyboardBlockNode::execute(Logger* logger) {
    for (const auto& action : actions) {
        action->execute(logger);
    }
}

void TypeNode::execute(Logger* logger) {
    logger->info("Typing: " + text);
    for (const char c : text) {
        const SHORT vk = VkKeyScanA(c);
        if ((vk >> 8) & 1) keybd_event(VK_SHIFT, 0, 0, 0);
        pressKey(vk & 0xFF);
        if ((vk >> 8) & 1) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    }
}

void PressNode::execute(Logger* logger) {
    const int vk = resolveKey(key);
    logger->info("Pressing key: " + key);
    pressKey(vk);
}

void HybridClickNode::execute(Logger* logger) {
    ClickNode(button).execute(logger);
}

void HybridPressNode::execute(Logger* logger) {
    PressNode(key).execute(logger);
}