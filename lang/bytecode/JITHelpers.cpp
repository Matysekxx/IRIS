#include "JITHelpers.h"
#include "JITCompiler.h"
#include "Compiler.h"
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

    void* compileJITFunc(void* functions_ptr, int funcIdx, void* native_functions) {
        auto* functions = static_cast<std::vector<iris::bytecode::FunctionObject>*>(functions_ptr);
        iris::bytecode::FunctionObject& f = (*functions)[funcIdx];
        if (!f.chunk.jitFunc) {
            iris::bytecode::JITCompiler jit;
            f.chunk.jitFunc = (void*) jit.compile(f.chunk, functions_ptr, native_functions);
        }
        return f.chunk.jitFunc;
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
}
