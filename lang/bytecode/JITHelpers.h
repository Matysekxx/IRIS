#ifndef JIT_HELPERS_H
#define JIT_HELPERS_H

#include "../core/Value.h"
#include "../core/ArrayData.h"
#include "../core/Native.h"

extern "C" {
    iris::core::Value createArrayHelper(int size, int type);
    iris::core::Value callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount);
}

#endif // JIT_HELPERS_H
