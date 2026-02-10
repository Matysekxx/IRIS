#ifndef RUNTIMECONTEXT_H
#define RUNTIMECONTEXT_H

#include "../log/Logger.h"
#include "../device/IDeviceDriver.h"

struct RuntimeContext {
    Logger* logger;
    IDeviceDriver* driver;
    //TODO: std::map<std::string, Value> variables;
};

#endif //RUNTIMECONTEXT_H