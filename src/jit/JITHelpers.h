#ifndef JIT_HELPERS_H
#define JIT_HELPERS_H

#include <cstdint>
#include "core/Value.h"
#include "core/ArrayData.h"
#include "core/Native.h"

extern "C" {
    uint64_t createArrayHelper(int size, int type);
    uint64_t callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount);
    void logHelper(iris::core::Value* val);
    void* compileJITFunc(void* functions_ptr, int funcIdx, void* native_functions);
    uint64_t callFunctionHelper(int funcIdx, iris::core::Value* rBaseA, void* vmPtr);
    uint64_t getGlobalHelper(void* vmPtr, uint16_t slot);
    void setGlobalHelper(void* vmPtr, uint16_t slot, uint64_t bits);
    uint64_t idxGetHelper(iris::core::Value* collection, iris::core::Value* index);
    void idxSetHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value);
    uint64_t idxGetIntHelper(iris::core::Value* collection, iris::core::Value* index);
    uint64_t idxGetDblHelper(iris::core::Value* collection, iris::core::Value* index);
    void idxSetIntHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value);
    void idxSetDblHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value);
    uint64_t addHelper(uint64_t b, uint64_t c);
    uint64_t subHelper(uint64_t b, uint64_t c);
    uint64_t mulHelper(uint64_t b, uint64_t c);
    uint64_t divHelper(uint64_t b, uint64_t c);
    uint64_t modHelper(uint64_t b, uint64_t c);
    uint64_t eqHelper(uint64_t b, uint64_t c);
    uint64_t ltHelper(uint64_t b, uint64_t c);
    uint64_t gtHelper(uint64_t b, uint64_t c);
    uint64_t collLenHelper(iris::core::Value* val);
    uint64_t negHelper(uint64_t b);
    
    // OO Helpers
    uint64_t createObjectHelper(int classId, void* vmPtr);
    void invokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr);
    void invokeMonoHelper(iris::core::Value* base, int cacheIdx, iris::core::Value* constants, void* vmPtr, void* chunkPtr);

    void retainValueHelper(uint64_t bits);
    void releaseValueHelper(uint64_t bits);
    void waitHelper(iris::core::Value* val, void* vmPtr);
    void incFieldHelper(iris::core::Value* objVal, int fieldIdx);
    void decFieldHelper(iris::core::Value* objVal, int fieldIdx);
    void tailInvokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr);
    void pushHandlerHelper(void* vmPtr, int bytecodeOffset, uint32_t instr, uint8_t catchVarReg);
    void popHandlerHelper(void* vmPtr);
    void throwHelper(iris::core::Value* val, void* vmPtr);
}

#endif // JIT_HELPERS_H
