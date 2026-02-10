#ifndef WIN32DRIVER_H
#define WIN32DRIVER_H
#include "IDeviceDriver.h"

class Win32Driver : public IDeviceDriver {
public:
    void moveMouse(int x, int y) override;
    void clickMouse(bool left) override;
    void pressKey(const std::string &key) override;
    void typeText(const std::string &text) override;
    void sleep(int milliseconds) override;
};

#endif //WIN32DRIVER_H