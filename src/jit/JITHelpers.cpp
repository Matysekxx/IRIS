#include "JITHelpers.h"
#include "JITCompiler.h"
#include "vm/VM.h"
#include "ir/Compiler.h"
#include "core/Managed.h"
#include "core/MemoryPool.h"
#include <iostream>
#include <vector>

namespace iris::core {
    extern Managed* gcObjects;
    extern size_t gcAllocated;
}

extern "C" {
    uint64_t createArrayHelper(int size, int type) {
        iris::core::Value val(iris::core::ArrayData::create(size, (iris::core::ArrayData::ElementType)type));
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
        auto* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        int fieldCount = (int)(*vm->getClassMetas())[classId].fields.size();
        using namespace iris::core;
        ObjectData* obj = new ObjectData((uint16_t)classId, (uint16_t)fieldCount);
        for (int i = 0; i < ObjectData::INLINED_FIELDS && i < fieldCount; i++)
            obj->inlinedFields[i].bits = Value::QNAN | Value::TAG_NULL;
        return Value::QNAN | Value::TAG_PTR | (uint64_t)obj;
    }

    void invokeHelper(iris::core::Value* base, int methodIdx, int argCount, iris::core::Value* constants, void* vmPtr) {
        // std::cout << "[JIT HELP] invokeHelper base=" << base << " base[0]=" << std::hex << base[0].bits << " methodIdx=" << methodIdx << " argCount=" << argCount << " constants=" << constants << " vmPtr=" << vmPtr << std::dec << std::endl;
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        vm->invokeMethod(base, methodIdx, argCount, constants);
        // std::cout << "[JIT HELP] invokeHelper returned, base[0]=" << std::hex << base[0].bits << std::dec << std::endl;
    }

    void invokeMonoHelper(iris::core::Value* base, int cacheIdx, iris::core::Value* constants, void* vmPtr, void* chunkPtr) {
        iris::bytecode::VM* vm = static_cast<iris::bytecode::VM*>(vmPtr);
        iris::bytecode::Chunk* chunk = static_cast<iris::bytecode::Chunk*>(chunkPtr);
        auto& entry = chunk->methodCaches[cacheIdx];
        iris::core::Value receiver = base[0];
        
        if (receiver.isPtr() && receiver.asPtr()->type == iris::core::ManagedType::Native) {
            std::string mname = constants[entry.methodNameIdx].str();
            base[0] = static_cast<iris::core::NativeObject *>(receiver.asPtr())->callMethod(mname, base + 1, entry.argCount - 1);
            return;
        }
        
        if (receiver.isNull()) {
            throw std::runtime_error("Null pointer access in method invoke");
        }
        
        iris::core::ObjectData *o = static_cast<iris::core::ObjectData *>(receiver.asPtr());
        uint16_t fid;
        if (!entry.lookup(o->classId, fid)) {
            std::string mname = constants[entry.methodNameIdx].str();
            auto& meta = (*vm->getClassMetas())[o->classId];
            auto it = meta.methodIndex.find(mname);
            if (it == meta.methodIndex.end()) throw std::runtime_error("Method not found: " + mname);
            fid = it->second;
            entry.update(o->classId, fid);
        }
        
        auto* functions = vm->getFunctions();
        auto& f = (*functions)[fid];
        vm->compileFunction(fid);
        if (f.chunk.jitFunc) {
            iris::bytecode::JITFunc jf = (iris::bytecode::JITFunc)f.chunk.jitFunc;
            iris::bytecode::VMState state = { base, f.chunk.constants.data(), vm, (iris::core::Value*)vm->getGlobals().data() };
            uint64_t nullBits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
            uint64_t arg0 = (f.arity > 0) ? base[0].bits : nullBits;
            uint64_t arg1 = (f.arity > 1) ? base[1].bits : nullBits;
            uint64_t arg2 = (f.arity > 2) ? base[2].bits : nullBits;
            base[0].bits = jf(&state, arg0, arg1, arg2);
        } else {
            base[0].bits = vm->callFunction(fid, base);
        }
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
            case iris::core::ArrayData::INT: res = iris::core::Value(arr->getIntData()[idx]); break;
            case iris::core::ArrayData::DOUBLE: res = iris::core::Value(arr->getDblData()[idx]); break;
            default: res = arr->getValData()[idx]; break;
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
            case iris::core::ArrayData::INT: arr->getIntData()[idx] = value->asInt(); break;
            case iris::core::ArrayData::DOUBLE: arr->getDblData()[idx] = value->asDouble(); break;
            default: arr->dirty = true; arr->getValData()[idx] = *value; break;
        }
    }

    uint64_t idxGetIntHelper(iris::core::Value* collection, iris::core::Value* index) {
        if (collection->isNull()) return iris::core::Value().bits;
        auto* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return iris::core::Value().bits;
        iris::core::Value res(arr->getIntData()[idx]);
        res.retain();
        uint64_t b = res.bits;
        return b;
    }

    uint64_t idxGetDblHelper(iris::core::Value* collection, iris::core::Value* index) {
        if (collection->isNull()) return iris::core::Value().bits;
        auto* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return iris::core::Value().bits;
        iris::core::Value res(arr->getDblData()[idx]);
        res.retain();
        uint64_t b = res.bits;
        return b;
    }

    void idxSetIntHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value) {
        if (collection->isNull()) return;
        auto* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return;
        arr->getIntData()[idx] = value->asInt();
    }

    void idxSetDblHelper(iris::core::Value* collection, iris::core::Value* index, iris::core::Value* value) {
        if (collection->isNull()) return;
        auto* arr = static_cast<iris::core::ArrayData*>(collection->asPtr());
        int idx = index->asInt();
        if (idx < 0 || idx >= (int)arr->length) return;
        arr->getDblData()[idx] = value->asDouble();
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
        obj->dirty = true;
        iris::core::Value& fld = obj->getField(fieldIdx);
        fld.bits = (iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)(fld.asInt() + 1));
    }

    void decFieldHelper(iris::core::Value* objVal, int fieldIdx) {
        auto* obj = static_cast<iris::core::ObjectData*>(objVal->asPtr());
        obj->dirty = true;
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
