#include "JITHelpers.h"
#include "JITCompiler.h"
#include "VM.h"
#include <iostream>
#include <vector>

extern "C" {
    uint64_t createArrayHelper(int size, int type) {
        iris::core::Value val(new iris::core::ArrayData(size, static_cast<iris::core::ArrayData::ElementType>(type)));
        uint64_t b = val.bits;
        val.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    uint64_t callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount) {
        iris::core::Value val = nf->fn(args, argCount);
        uint64_t b = val.bits;
        val.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    void logHelper(iris::core::Value* val) {
        std::cout << iris::core::toString(*val) << "\n";
    }

    uint64_t addHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        iris::core::Value res = iris::core::numericAdd(valB, valC);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    uint64_t subHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        iris::core::Value res = iris::core::numericSub(valB, valC);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    uint64_t mulHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        iris::core::Value res = iris::core::numericMul(valB, valC);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    uint64_t divHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        iris::core::Value res = iris::core::numericDiv(valB, valC);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    uint64_t createObjectHelper(int classId, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::core::Value v = vm->createObject(classId);
        uint64_t b = v.bits;
        v.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    void invokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->invokeMethod(base, methodIdx, argCount, constants);
    }
}
