#ifndef RUNTIMECONTEXT_H
#define RUNTIMECONTEXT_H


#include <unordered_map>

#include "../log/Logger.h"
#include "../device/IDeviceDriver.h"
#include "../core/Variable.h"

struct RuntimeContext {
    Logger* logger;
    IDeviceDriver* driver;
    std::unordered_map<std::string, Variable> variables;
};

#endif //RUNTIMECONTEXT_H