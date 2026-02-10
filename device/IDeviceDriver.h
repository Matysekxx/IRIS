#ifndef IDEVICEDRIVER_H
#define IDEVICEDRIVER_H
#include <string>

class IDeviceDriver {
public:
    virtual ~IDeviceDriver() = default;

    virtual void moveMouse(int x, int y) = 0;
    virtual void clickMouse(bool left) = 0;
    virtual void pressKey(const std::string& key) = 0;
    virtual void typeText(const std::string& text) = 0;
    virtual void sleep(int milliseconds) = 0;
};

#endif //IDEVICEDRIVER_H