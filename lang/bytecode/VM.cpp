#include "VM.h"
#include "Compiler.h"
#include "../node/ASTNode.h"
#include "../core/ArrayData.h"
#include <iostream>
#include <stdexcept>

void VM::execute(Chunk& ch, IDeviceDriver* drv, Logger* log,
                 std::vector<FunctionObject>* funcs,
                 std::vector<ClassMeta>* classes) {
    chunk = &ch;
    ip = ch.code.data();
    driver = drv;
    logger = log;
    base = stack;
    frameCount = 0;
    handlerStack.clear();
    globals.clear();
    functions = funcs;
    classMetas = classes;
    run();
}

void VM::run() {
    Value* R = base;

    uint32_t instr;
    uint8_t A, B, C;

#ifdef __GNUC__
    static const void* dispatchTable[] = {
        &&OP_LOADK, &&OP_LOADINT, &&OP_LOADBOOL, &&OP_LOADNULL, &&OP_MOVE,
        &&OP_ADD, &&OP_SUB, &&OP_MUL, &&OP_DIV, &&OP_MOD, &&OP_NEG,
        &&OP_NOT, &&OP_AND, &&OP_OR,
        &&OP_EQ, &&OP_NEQ, &&OP_LT, &&OP_GT, &&OP_LE, &&OP_GE,
        &&OP_BIT_AND, &&OP_BIT_OR, &&OP_BIT_XOR, &&OP_SHL, &&OP_SHR,
        &&OP_GGLOB, &&OP_SGLOB, &&OP_DGLOB,
        &&OP_JMP, &&OP_JMPF, &&OP_LOOP,
        &&OP_CALL, &&OP_TAILCALL, &&OP_RET,
        &&OP_LOG, &&OP_WAIT,
        &&OP_TYPECHECK,
        &&OP_NEW_OBJ, &&OP_GET_FIELD, &&OP_SET_FIELD,
        &&OP_INVOKE, &&OP_TAIL_INVOKE,
        &&OP_NEW_ARRAY, &&OP_IDX_GET, &&OP_IDX_SET, &&OP_COLL_LEN,
        &&OP_PUSH_HANDLER, &&OP_POP_HANDLER, &&OP_THROW,
        &&OP_HALT, &&OP_COUNT
    };

#define FETCH() instr = *ip++
#define DECODE_ABC() A = DECODE_A(instr); B = DECODE_B(instr); C = DECODE_C(instr)
#define DISPATCH() do { FETCH(); goto *dispatchTable[instr >> 24]; } while(0)
#define CASE(op) OP_##op:
#define VM_LOOP() DISPATCH();
#define VM_LOOP_END()
#else
#define FETCH() instr = *ip++
#define DECODE_ABC() A = DECODE_A(instr); B = DECODE_B(instr); C = DECODE_C(instr)
#define DISPATCH() break
#define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op):
#define VM_LOOP() while (true) { FETCH(); switch (instr >> 24) {
#define VM_LOOP_END() } }
#endif

    auto dispatchException = [&](const std::string& msg) {
        if (!handlerStack.empty()) {
            ExceptionHandler h = handlerStack.back();
            handlerStack.pop_back();
            while (frameCount > h.frameCount) {
                frameCount--;
                const CallFrame& frame = frames[frameCount];
                base = frame.returnBase;
                chunk = frame.returnChunk;
            }
            ip = h.catchIp;
            chunk = h.chunk;
            base = h.base;
            R = base;
            R[h.catchVarReg] = Value(msg);
        } else {
            throw std::runtime_error(msg);
        }
    };

    VM_LOOP()

    CASE(LOADK) {
        A = DECODE_A(instr);
        R[A] = chunk->constants[DECODE_Bx(instr)];
        DISPATCH();
    }
    CASE(LOADINT) {
        A = DECODE_A(instr);
        R[A].tag = Value::TAG_INT;
        R[A].asInt = DECODE_sBx(instr);
        DISPATCH();
    }
    CASE(LOADBOOL) {
        DECODE_ABC();
        R[A].tag = Value::TAG_BOOL;
        R[A].asBool = (B != 0);
        DISPATCH();
    }
    CASE(LOADNULL) {
        A = DECODE_A(instr);
        R[A].tag = Value::TAG_NULL;
        R[A].asInt = 0;
        DISPATCH();
    }
    CASE(MOVE) {
        DECODE_ABC();
        R[A] = R[B];
        DISPATCH();
    }

    CASE(ADD) {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A].tag = Value::TAG_INT;
            R[A].asInt = vb.asInt + vc.asInt;
        } else if (isNumeric(vb) && isNumeric(vc)) {
            R[A] = numericAdd(vb, vc);
        } else {
            R[A] = Value(toString(vb) + toString(vc));
        }
        DISPATCH();
    }
    CASE(SUB) {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A].tag = Value::TAG_INT;
            R[A].asInt = vb.asInt - vc.asInt;
        } else {
            R[A] = numericSub(vb, vc);
        }
        DISPATCH();
    }
    CASE(MUL) {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A].tag = Value::TAG_INT;
            R[A].asInt = vb.asInt * vc.asInt;
        } else {
            R[A] = numericMul(vb, vc);
        }
        DISPATCH();
    }
    CASE(DIV) {
        DECODE_ABC();
        if (toDouble(R[C]) == 0.0) { dispatchException("Division by zero"); DISPATCH(); }
        R[A] = numericDiv(R[B], R[C]);
        DISPATCH();
    }
    CASE(MOD) {
        DECODE_ABC();
        R[A] = numericMod(R[B], R[C]);
        DISPATCH();
    }
    CASE(NEG) {
        DECODE_ABC();
        R[A] = numericNegate(R[B]);
        DISPATCH();
    }
    CASE(NOT) {
        DECODE_ABC();
        if (R[B].isBool()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = !R[B].asBool;
        } else {
            throw std::runtime_error("Operator '!' requires boolean.");
        }
        DISPATCH();
    }

    CASE(AND) {
        DECODE_ABC();
        R[A].tag = Value::TAG_BOOL;
        R[A].asBool = R[B].asBool && R[C].asBool;
        DISPATCH();
    }
    CASE(OR) {
        DECODE_ABC();
        R[A].tag = Value::TAG_BOOL;
        R[A].asBool = R[B].asBool || R[C].asBool;
        DISPATCH();
    }

    CASE(EQ) {
        DECODE_ABC();
        const Value& a = R[B];
        const Value& b = R[C];
        if (a.isInt() && b.isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = a.asInt == b.asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = a == b;
        }
        DISPATCH();
    }
    CASE(NEQ) {
        DECODE_ABC();
        const Value& a = R[B];
        const Value& b = R[C];
        if (a.isInt() && b.isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = a.asInt != b.asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = a != b;
        }
        DISPATCH();
    }
    CASE(LT) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = R[B].asInt < R[C].asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = numericLT(R[B], R[C]);
        }
        DISPATCH();
    }
    CASE(GT) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = R[B].asInt > R[C].asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = numericGT(R[B], R[C]);
        }
        DISPATCH();
    }
    CASE(LE) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = R[B].asInt <= R[C].asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = numericLE(R[B], R[C]);
        }
        DISPATCH();
    }
    CASE(GE) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = R[B].asInt >= R[C].asInt;
        } else {
            R[A].tag = Value::TAG_BOOL;
            R[A].asBool = numericGE(R[B], R[C]);
        }
        DISPATCH();
    }

    CASE(BIT_AND) {
        DECODE_ABC();
        R[A].tag = Value::TAG_INT;
        R[A].asInt = R[B].asInt & R[C].asInt;
        DISPATCH();
    }
    CASE(BIT_OR) {
        DECODE_ABC();
        R[A].tag = Value::TAG_INT;
        R[A].asInt = R[B].asInt | R[C].asInt;
        DISPATCH();
    }
    CASE(BIT_XOR) {
        DECODE_ABC();
        R[A].tag = Value::TAG_INT;
        R[A].asInt = R[B].asInt ^ R[C].asInt;
        DISPATCH();
    }
    CASE(SHL) {
        DECODE_ABC();
        R[A].tag = Value::TAG_INT;
        R[A].asInt = R[B].asInt << R[C].asInt;
        DISPATCH();
    }
    CASE(SHR) {
        DECODE_ABC();
        R[A].tag = Value::TAG_INT;
        R[A].asInt = R[B].asInt >> R[C].asInt;
        DISPATCH();
    }

    CASE(GGLOB) {
        A = DECODE_A(instr);
        uint16_t slot = DECODE_Bx(instr);
        if (slot >= globals.size()) throw std::runtime_error("Undefined global slot " + std::to_string(slot));
        R[A] = globals[slot].value;
        DISPATCH();
    }
    CASE(SGLOB) {
        A = DECODE_A(instr);
        uint16_t slot = DECODE_Bx(instr);
        if (slot >= globals.size()) throw std::runtime_error("Undefined global slot " + std::to_string(slot));
        if (!globals[slot].isMutable) throw std::runtime_error("Global is immutable.");
        globals[slot].value = R[A];
        DISPATCH();
    }
    CASE(DGLOB) {
        DECODE_ABC();
        uint16_t slot = static_cast<uint16_t>((B << 8) | C);
        if (slot >= globals.size()) globals.resize(slot + 1);
        globals[slot] = {R[A], true};
        DISPATCH();
    }

    CASE(JMP) {
        ip += DECODE_sBx(instr);
        DISPATCH();
    }
    CASE(JMPF) {
        A = DECODE_A(instr);
        if (R[A].isBool() && !R[A].asBool) ip += DECODE_sBx(instr);
        DISPATCH();
    }
    CASE(LOOP) {
        ip += DECODE_sBx(instr);
        DISPATCH();
    }

    CASE(CALL) {
        DECODE_ABC();
        uint16_t funcIdx = B;
        uint8_t argCount = C;
        uint8_t callBase = A;

        FunctionObject& func = (*functions)[funcIdx];

        if (frameCount >= static_cast<int>(FRAMES_MAX))
            throw std::runtime_error("Stack overflow");

        CallFrame& frame = frames[frameCount++];
        frame.function = &func;
        frame.returnIp = ip;
        frame.returnChunk = chunk;
        frame.returnBase = base;

        base = R + callBase;
        R = base;
        chunk = &func.chunk;
        ip = func.chunk.code.data();
        DISPATCH();
    }

    CASE(TAILCALL) {
        DECODE_ABC();
        uint16_t funcIdx = B;
        uint8_t argCount = C;
        uint8_t callBase = A;

        FunctionObject& func = (*functions)[funcIdx];

        for (uint8_t i = 0; i < argCount; ++i) {
            base[i] = R[callBase + i];
        }

        chunk = &func.chunk;
        ip = func.chunk.code.data();
        DISPATCH();
    }

    CASE(INVOKE) {
        DECODE_ABC();
        uint8_t callBase = A;
        uint8_t nameId = B;
        uint8_t argCount = C;

        ObjectData* obj = static_cast<ObjectData*>(R[callBase].asPtr);
        uint16_t funcIdx;

        size_t ipOffset = (ip - 1) - chunk->code.data();
        auto cacheIt = chunk->inlineCache.find(ipOffset);
        if (cacheIt != chunk->inlineCache.end() && cacheIt->second.first == obj->classId) {
            funcIdx = cacheIt->second.second;
        } else {
            ClassMeta& meta = (*classMetas)[obj->classId];
            std::string methName = chunk->constants[nameId].str();
            auto it = meta.methodIndex.find(methName);
            funcIdx = it->second;
            chunk->inlineCache[ipOffset] = {obj->classId, funcIdx};
        }

        FunctionObject& func = (*functions)[funcIdx];

        if (frameCount >= static_cast<int>(FRAMES_MAX))
            throw std::runtime_error("Stack overflow");

        CallFrame& frame = frames[frameCount++];
        frame.function = &func;
        frame.returnIp = ip;
        frame.returnChunk = chunk;
        frame.returnBase = base;

        base = R + callBase;
        R = base;
        chunk = &func.chunk;
        ip = func.chunk.code.data();
        DISPATCH();
    }

    CASE(TAIL_INVOKE) {
        DECODE_ABC();
        uint8_t callBase = A;
        uint8_t nameId = B;
        uint8_t argCount = C;

        ObjectData* obj = static_cast<ObjectData*>(R[callBase].asPtr);
        uint16_t funcIdx;

        size_t ipOffset = (ip - 1) - chunk->code.data();
        auto cacheIt = chunk->inlineCache.find(ipOffset);
        if (cacheIt != chunk->inlineCache.end() && cacheIt->second.first == obj->classId) {
            funcIdx = cacheIt->second.second;
        } else {
            ClassMeta& meta = (*classMetas)[obj->classId];
            std::string methName = chunk->constants[nameId].str();
            auto it = meta.methodIndex.find(methName);
            funcIdx = it->second;
            chunk->inlineCache[ipOffset] = {obj->classId, funcIdx};
        }

        FunctionObject& func = (*functions)[funcIdx];

        for (uint8_t i = 0; i < argCount; ++i) {
            base[i] = R[callBase + i];
        }

        chunk = &func.chunk;
        ip = func.chunk.code.data();
        DISPATCH();
    }

    CASE(RET) {
        {
            A = DECODE_A(instr);
            Value result = R[A];

            if (frameCount == 0) {
                return;
            }

            frameCount--;
            const CallFrame& frame = frames[frameCount];
            base = frame.returnBase;
            R = base;
            ip = frame.returnIp;
            chunk = frame.returnChunk;

            R[DECODE_A(*(ip - 1))] = result;
        }
        DISPATCH();
    }

    CASE(LOG) {
        A = DECODE_A(instr);
        std::cout << toString(R[A]) << "\n";
        DISPATCH();
    }
    CASE(WAIT) {
        A = DECODE_A(instr);
        int ms;
        if (R[A].isInt()) ms = R[A].asInt;
        else if (R[A].isDouble()) ms = static_cast<int>(R[A].asDouble);
        else throw std::runtime_error("wait() expects number");
        driver->sleep(ms);
        DISPATCH();
    }

    CASE(TYPECHECK) {
        DECODE_ABC();
        const auto expected = static_cast<TypeAnnotation>(B);
        const Value& v = R[A];
        bool ok = false;
        switch (expected) {
            case TypeAnnotation::Int:    ok = v.isInt();    break;
            case TypeAnnotation::Double: ok = v.isDouble(); break;
            case TypeAnnotation::Bool:   ok = v.isBool();   break;
            case TypeAnnotation::String: ok = v.isString(); break;
            default: ok = true; break;
        }
        if (!ok) {
            const char* actual;
            switch (v.tag) {
                case Value::TAG_INT:    actual = "int";    break;
                case Value::TAG_DOUBLE: actual = "double"; break;
                case Value::TAG_BOOL:   actual = "bool";   break;
                case Value::TAG_STRING: actual = "string"; break;
                default:                actual = "null";   break;
            }
            throw std::runtime_error(
                std::string("Type error: expected ") + typeAnnotationName(expected) +
                ", got " + actual);
        }
        DISPATCH();
    }

    CASE(HALT) return;

    CASE(PUSH_HANDLER) {
        A = DECODE_A(instr);
        uint16_t offset = DECODE_Bx(instr);
        ExceptionHandler h;
        h.catchIp = ip + offset;
        h.chunk = chunk;
        h.base = base;
        h.frameCount = frameCount;
        h.catchVarReg = A;
        handlerStack.push_back(h);
        DISPATCH();
    }

    CASE(POP_HANDLER) {
        if (!handlerStack.empty()) handlerStack.pop_back();
        DISPATCH();
    }

    CASE(THROW) {
        {
            A = DECODE_A(instr);
            std::string msg = toString(R[A]);
            dispatchException(msg);
        }
        DISPATCH();
    }

    CASE(NEW_OBJ) {
        A = DECODE_A(instr);
        uint16_t clsId = DECODE_Bx(instr);
        auto obj = new ObjectData();
        obj->classId = clsId;
        if (classMetas && clsId < classMetas->size()) {
            obj->fields.resize((*classMetas)[clsId].fields.size());
        }
        R[A].tag = Value::TAG_OBJECT;
        R[A].asPtr = reinterpret_cast<Managed*>(obj);
        DISPATCH();
    }
    CASE(GET_FIELD) {
        DECODE_ABC();
        R[A] = static_cast<ObjectData*>(R[B].asPtr)->fields[C];
        DISPATCH();
    }
    CASE(SET_FIELD) {
        DECODE_ABC();
        static_cast<ObjectData*>(R[B].asPtr)->fields[C] = R[A];
        DISPATCH();
    }

    CASE(NEW_ARRAY) {
        DECODE_ABC();
        int size;
        if (R[B].isInt()) size = R[B].asInt;
        else throw std::runtime_error("array() size must be int");
        if (size < 0) throw std::runtime_error("array() size must be non-negative");

        ArrayData::ElementType type = ArrayData::UNTYPED;
        if (C == 1) type = ArrayData::INT;
        else if (C == 2) type = ArrayData::DOUBLE;
        else if (C == 3) type = ArrayData::VALUE;

        R[A] = Value(new ArrayData(static_cast<size_t>(size), type));
        DISPATCH();
    }

    CASE(IDX_GET) {
        DECODE_ABC();
        const Value& coll = R[B];
        const Value& idx = R[C];

        if (coll.isArray()) {
            if (!idx.isInt()) throw std::runtime_error("Array index must be int");
            auto* arr = static_cast<ArrayData*>(coll.asPtr);
            int i = idx.asInt;
            if (i < 0 || static_cast<size_t>(i) >= arr->length)
                throw std::runtime_error("Array index out of bounds");
            if (arr->elemType == ArrayData::INT) {
                R[A].tag = Value::TAG_INT;
                R[A].asInt = arr->intData[i];
            } else if (arr->elemType == ArrayData::DOUBLE) {
                R[A].tag = Value::TAG_DOUBLE;
                R[A].asDouble = arr->dblData[i];
            } else {
                R[A] = arr->valData[i];
            }
        } else {
            throw std::runtime_error("IDX_GET on non-array");
        }
        DISPATCH();
    }

    CASE(IDX_SET) {
        DECODE_ABC();
        const Value& coll = R[B];
        const Value& idx = R[C];

        if (coll.isArray()) {
            if (!idx.isInt()) throw std::runtime_error("Array index must be int");
            auto* arr = static_cast<ArrayData*>(coll.asPtr);
            int i = idx.asInt;
            if (i < 0 || static_cast<size_t>(i) >= arr->length)
                throw std::runtime_error("Array index out of bounds");
            if (arr->elemType == ArrayData::INT) {
                if (R[A].isInt()) {
                    arr->intData[i] = R[A].asInt;
                } else if (R[A].isDouble()) {
                    double* newData = static_cast<double*>(std::malloc(arr->length * sizeof(double)));
                    for (size_t k = 0; k < arr->length; k++) newData[k] = static_cast<double>(arr->intData[k]);
                    std::free(arr->intData);
                    arr->dblData = newData;
                    arr->elemType = ArrayData::DOUBLE;
                    arr->dblData[i] = R[A].asDouble;
                } else {
                    Value* newData = static_cast<Value*>(std::malloc(arr->length * sizeof(Value)));
                    for (size_t k = 0; k < arr->length; k++) new (&newData[k]) Value(arr->intData[k]);
                    std::free(arr->intData);
                    arr->valData = newData;
                    arr->elemType = ArrayData::VALUE;
                    arr->valData[i] = R[A];
                }
            } else if (arr->elemType == ArrayData::DOUBLE) {
                if (R[A].isDouble()) arr->dblData[i] = R[A].asDouble;
                else if (R[A].isInt()) arr->dblData[i] = static_cast<double>(R[A].asInt);
                else {
                    Value* newData = static_cast<Value*>(std::malloc(arr->length * sizeof(Value)));
                    for (size_t k = 0; k < arr->length; k++) new (&newData[k]) Value(arr->dblData[k]);
                    std::free(arr->dblData);
                    arr->valData = newData;
                    arr->elemType = ArrayData::VALUE;
                    arr->valData[i] = R[A];
                }
            } else if (arr->elemType == ArrayData::VALUE) {
                arr->valData[i] = R[A];
            }
        } else {
            throw std::runtime_error("IDX_SET on non-array");
        }
        DISPATCH();
    }

    CASE(COLL_LEN) {
        DECODE_ABC();
        const Value& coll = R[B];
        if (coll.isArray()) {
            R[A].tag = Value::TAG_INT;
            R[A].asInt = static_cast<int>(static_cast<ArrayData*>(coll.asPtr)->length);
        } else if (coll.isString()) {
            R[A].tag = Value::TAG_INT;
            R[A].asInt = static_cast<int>(coll.str().size());
        } else {
            throw std::runtime_error("len() on non-array/string");
        }
        DISPATCH();
    }

    CASE(COUNT)
#ifndef __GNUC__
    default:
    throw std::runtime_error("VM: unknown opcode " + std::to_string(instr >> 24));
#endif

    VM_LOOP_END()

    #undef FETCH
    #undef DECODE_ABC
    #undef DISPATCH
    #undef CASE
    #undef VM_LOOP
    #undef VM_LOOP_END
}