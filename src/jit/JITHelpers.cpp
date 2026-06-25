#include "JITHelpers.h"
#include "JITCompiler.h"
#include "vm/VM.h"
#include "ir/Compiler.h"
#include <iostream>
#include <vector>

extern "C" {
    uint64_t createArrayHelper(int size, int type) {
        iris::core::Value val(new iris::core::ArrayData(size, (iris::core::ArrayData::ElementType)type));
        val.retain();
        uint64_t b = val.bits;
        val.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    uint64_t callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount) {
        iris::core::Value res = nf->fn(args, argCount);
        res.retain();
        uint64_t b = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
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

    uint64_t modHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        iris::core::Value res = iris::core::numericMod(valB, valC);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    uint64_t eqHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        return (valB == valC) ? (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 1) : (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 0);
    }

    uint64_t ltHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        return iris::core::numericLT(valB, valC) ? (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 1) : (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 0);
    }

    uint64_t gtHelper(uint64_t b, uint64_t c) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value valC; valC.bits = c;
        return iris::core::numericGT(valB, valC) ? (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 1) : (iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | 0);
    }

    uint64_t createObjectHelper(int classId, void* vmPtr) {
        // std::cout << "[JIT HELP] createObjectHelper classId=" << classId << " vmPtr=" << vmPtr << std::endl;
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::core::Value res = vm->createObject(classId);
        // std::cout << "[JIT HELP] createObjectHelper created object bits=" << std::hex << res.bits << std::dec << std::endl;
        res.retain();
        uint64_t b = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    void invokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr) {
        // std::cout << "[JIT HELP] invokeHelper base=" << base << " base[0]=" << std::hex << base[0].bits << " methodIdx=" << methodIdx << " argCount=" << argCount << " constants=" << constants << " vmPtr=" << vmPtr << std::dec << std::endl;
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->invokeMethod(base, methodIdx, argCount, constants);
        // std::cout << "[JIT HELP] invokeHelper returned, base[0]=" << std::hex << base[0].bits << std::dec << std::endl;
    }

    void* compileJITFunc(void* functions_ptr, int funcIdx, void* native_functions) {
        auto* functions = static_cast<std::vector<iris::bytecode::FunctionObject>*>(functions_ptr);
        iris::bytecode::JITCompiler compiler;
        functions->at(funcIdx).chunk.jitFunc = (void*)compiler.compile(functions->at(funcIdx).chunk, functions, native_functions);
        return functions->at(funcIdx).chunk.jitFunc;
    }

    void retainValueHelper(uint64_t bits) {
        iris::core::Value v; v.bits = bits;
        v.retain();
    }

    void releaseValueHelper(uint64_t bits) {
        iris::core::Value v;
        v.bits = bits;
        v.release();
        v.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    }

    uint64_t callFunctionHelper(int funcIdx, iris::core::Value* rBaseA, void* vmPtr) {
        // std::cout << "[JIT HELP] callFunctionHelper funcIdx=" << funcIdx << " rBaseA=" << rBaseA << " vmPtr=" << vmPtr << std::endl;
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        uint64_t res = vm->callFunction(funcIdx, rBaseA);
        // std::cout << "[JIT HELP] callFunctionHelper returned res=" << std::hex << res << std::dec << std::endl;
        return res;
    }

    uint64_t getGlobalHelper(void* vmPtr, uint16_t slot) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::core::Value res = vm->getGlobal(slot);
        res.retain();
        uint64_t b = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    void setGlobalHelper(void* vmPtr, uint16_t slot, uint64_t bits) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::core::Value v; v.bits = bits;
        vm->setGlobal(slot, v);
        v.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    }

    uint64_t idxGetHelper(iris::core::Value* collection, iris::core::Value* index) {
        if (collection->isNull()) return iris::core::Value().bits;
        iris::core::ArrayData* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return iris::core::Value().bits;

        iris::core::Value res;
        switch(arr->elemType) {
            case iris::core::ArrayData::INT: res = iris::core::Value(arr->intData[idx]); break;
            case iris::core::ArrayData::DOUBLE: res = iris::core::Value(arr->dblData[idx]); break;
            default: res = arr->valData[idx]; break;
        }
        res.retain();
        uint64_t b = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return b;
    }

    void idxSetHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value) {
        if (collection->isNull()) return;
        iris::core::ArrayData* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return;

        switch(arr->elemType) {
            case iris::core::ArrayData::INT: arr->intData[idx] = value->asInt(); break;
            case iris::core::ArrayData::DOUBLE: arr->dblData[idx] = value->asDouble(); break;
            default: arr->valData[idx] = *value; break;
        }
    }

    void sideExitDiagnostic(const uint32_t* pc) {
        // Diagnostics: uncomment to see side exit info
        // if (pc) printf("[JIT TRACE] Side exit at PC offset %td\n", (ptrdiff_t)*pc);
    }

    uint64_t collLenHelper(iris::core::Value* val) {
        if (val->isString()) {
            return (iris::core::Value::QNAN | iris::core::Value::TAG_INT | static_cast<int>(val->str().length()));
        } else if (val->isArray()) {
            return (iris::core::Value::QNAN | iris::core::Value::TAG_INT | (int)static_cast<iris::core::ArrayData*>(val->asPtr())->length);
        } else {
            throw std::runtime_error("len() expects a string or array collection");
        }
    }

    uint64_t negHelper(uint64_t b) {
        iris::core::Value valB; valB.bits = b;
        iris::core::Value res = iris::core::numericNegate(valB);
        uint64_t r = res.bits;
        res.bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
        return r;
    }

    void waitHelper(iris::core::Value* val, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->jitSleep(val->asInt());
    }

    void incFieldHelper(iris::core::Value* objVal, int fieldIdx) {
        auto* obj = static_cast<iris::core::ObjectData*>(objVal->asPtr());
        iris::core::Value& fld = obj->getField(fieldIdx);
        fld.bits = (iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)(fld.asInt() + 1));
    }

    void decFieldHelper(iris::core::Value* objVal, int fieldIdx) {
        auto* obj = static_cast<iris::core::ObjectData*>(objVal->asPtr());
        iris::core::Value& fld = obj->getField(fieldIdx);
        fld.bits = (iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)(fld.asInt() - 1));
    }

    void tailInvokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->jitTailInvoke(base, methodIdx, argCount, constants);
    }

    void pushHandlerHelper(void* vmPtr, int bytecodeOffset, uint32_t instr, uint8_t catchVarReg) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->jitPushHandler(bytecodeOffset, instr, catchVarReg);
    }

    void popHandlerHelper(void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->jitPopHandler();
    }

    void throwHelper(iris::core::Value* val, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->jitThrow(iris::core::toString(*val));
    }
}
