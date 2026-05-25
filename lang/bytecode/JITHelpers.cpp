#include "JITHelpers.h"

extern "C" {
    iris::core::Value createArrayHelper(int size, int type) {
        return iris::core::Value(new iris::core::ArrayData(size, static_cast<iris::core::ArrayData::ElementType>(type)));
    }

    iris::core::Value callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount) {
        return nf->fn(args, argCount);
    }
}
