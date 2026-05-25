#ifndef JIT_HELPERS_H
#define JIT_HELPERS_H

#include <cstdint>
#include "../core/Value.h"
#include "../core/ArrayData.h"
#include "../core/Native.h"

extern "C" {
    uint64_t createArrayHelper(int size, int type);
    uint64_t callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount);
    void logHelper(iris::core::Value* val);
    void* compileJITFunc(void* functions_ptr, int funcIdx, void* native_functions);
    uint64_t addHelper(uint64_t b, uint64_t c);
    uint64_t subHelper(uint64_t b, uint64_t c);
    uint64_t mulHelper(uint64_t b, uint64_t c);
    uint64_t divHelper(uint64_t b, uint64_t c);
}

#endif // JIT_HELPERS_H
