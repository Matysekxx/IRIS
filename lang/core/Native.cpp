#include "Native.h"
#include "Value.h"

namespace iris::core {
    Value NativeObject::callMethod(const std::string& name, Value* args, int argCount) {
        return Value(); // Default: return null
    }
}
