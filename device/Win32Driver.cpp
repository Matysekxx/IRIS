#include "Win32Driver.h"
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

static void sendKey(const int vk) {
    if (vk == 0) return;
    keybd_event(static_cast<BYTE>(vk), 0, 0, 0);
    keybd_event(static_cast<BYTE>(vk), 0, KEYEVENTF_KEYUP, 0);
}

void Win32Driver::moveMouse(int x, int y) {
    SetCursorPos(x, y);
}

void Win32Driver::clickMouse(bool left) {
    if (left) {
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    } else {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    }
}

void Win32Driver::pressKey(const std::string &key) {
    const int vk = resolveKey(key);
    sendKey(vk);
}

void Win32Driver::typeText(const std::string &text) {
    for (const char c : text) {
        const SHORT vk = VkKeyScanA(c);
        if ((vk >> 8) & 1) keybd_event(VK_SHIFT, 0, 0, 0);
        sendKey(vk & 0xFF);
        if ((vk >> 8) & 1) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    }
}

void Win32Driver::sleep(const int milliseconds) {
    Sleep(milliseconds);
}