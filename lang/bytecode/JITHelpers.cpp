#include "JITHelpers.h"
#include "JITCompiler.h"
#include "VM.h"
#include "Compiler.h"
#include <iostream>
#include <vector>

extern "C" {
    uint64_t createArrayHelper(int size, int type) {
        iris::core::Value val(new iris::core::ArrayData(size, (iris::core::ArrayData::ElementType)type));
        val.retain();
        return val.bits;
    }

    uint64_t callNativeHelper(iris::core::NativeFunction* nf, iris::core::Value* args, int argCount) {
        iris::core::Value res = nf->fn(args, argCount);
        res.retain();
        return res.bits;
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
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::core::Value res = vm->createObject(classId);
        res.retain();
        return res.bits;
    }

    void invokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr) {
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->invokeMethod(base, methodIdx, argCount, constants);
    }

    void* compileJITFunc(void* functions_ptr, int funcIdx, void* native_functions) {
        auto* functions = static_cast<std::vector<iris::bytecode::FunctionObject>*>(functions_ptr);
        iris::bytecode::JITCompiler compiler;
        functions->at(funcIdx).chunk.jitFunc = (void*)compiler.compile(functions->at(funcIdx).chunk, functions, native_functions);
        return functions->at(funcIdx).chunk.jitFunc;
    }

    void retainValueHelper(uint64_t bits) {
        if ((bits & iris::core::Value::QNAN) == iris::core::Value::QNAN && (bits & iris::core::Value::TAG_PTR)) {
            iris::core::Managed* p = reinterpret_cast<iris::core::Managed*>(bits & 0x0000FFFFFFFFFFFFULL);
            if (p) p->refCount++;
        }
    }

    void releaseValueHelper(uint64_t bits) {
        if ((bits & iris::core::Value::QNAN) == iris::core::Value::QNAN && (bits & iris::core::Value::TAG_PTR)) {
            iris::core::Managed* p = reinterpret_cast<iris::core::Managed*>(bits & 0x0000FFFFFFFFFFFFULL);
            if (p && --p->refCount == 0) delete p;
        }
    }

    uint64_t callFunctionHelper(int funcIdx, iris::core::Value* rBaseA, void* vmPtr) {
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        return vm->callFunction(funcIdx, rBaseA);
    }

    uint64_t idxGetHelper(iris::core::Value* collection, iris::core::Value* index) {
        iris::core::ArrayData* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return iris::core::Value().bits;
        
        switch(arr->elemType) {
            case iris::core::ArrayData::INT: return iris::core::Value(arr->intData[idx]).bits;
            case iris::core::ArrayData::DOUBLE: return iris::core::Value(arr->dblData[idx]).bits;
            default: return arr->valData[idx].bits;
        }
    }

    void idxSetHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value) {
        iris::core::ArrayData* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return;

        switch(arr->elemType) {
            case iris::core::ArrayData::INT: arr->intData[idx] = value->asInt(); break;
            case iris::core::ArrayData::DOUBLE: arr->dblData[idx] = value->asDouble(); break;
            default: arr->valData[idx] = *value; break;
        }
    }
}
