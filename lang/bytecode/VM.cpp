#include "VM.h"
#include "Compiler.h"
#include "../node/ASTNode.h"
#include "../core/ArrayData.h"
#include <iostream>
#include <stdexcept>

using namespace iris::bytecode;
using namespace iris::core;
using namespace iris::device;

// OPTIMIZATION: String Interning implementation
/**
 * @brief Interns a string to ensure O(1) comparison and save memory.
 * 
 * @param s The string to intern.
 * @return StringData* A pointer to the interned string data.
 */
StringData* VM::internString(const std::string& s) {
    auto it = stringInterner.find(s);
    if (it != stringInterner.end()) {
        return it->second;
    }
    StringData* data = new StringData(s);
    // Keep one reference for the interner itself so it's not deleted by Values
    data->refCount++; 
    stringInterner[s] = data;
    return data;
}

/**
 * @brief Main entry point for bytecode execution.
 * 
 * Initializes the VM state, registers, and global tables, then starts the execution loop.
 */
void VM::execute(Chunk& ch, IDeviceDriver* drv, iris::log::Logger* log,
                std::vector<FunctionObject>* funcs,
                std::vector<ClassMeta>* clss,
                std::vector<NativeFunction*>* nativeFuncs) {

    chunk = &ch;
    ip = ch.code.data();
    driver = drv;
    logger = log;
    
    // Allocate or reuse register file
    if (registerFile.empty()) registerFile.resize(STACK_MAX);
    base = registerFile.data();
    
    frameCount = 0;
    handlerStack.clear();
    globals.clear();
    functions = funcs;
    classMetas = clss;
    nativeFunctions = nativeFuncs;
    
    // Clear previous strings safely to avoid memory leaks
    for (auto& pair : stringInterner) {
        if (--pair.second->refCount == 0) {
            delete pair.second;
        }
    }
    stringInterner.clear();

    // Zero out registers for safety (important for reference counting)
    for (int i = 0; i < STACK_MAX; i++) registerFile[i] = Value();

    run();
}

/**
 * @brief The Virtual Machine's hot loop.
 * 
 * Uses direct threaded dispatch (labels as values) on supported compilers (GCC/Clang)
 * or a highly optimized switch statement on others (MSVC).
 */
void VM::run() {
    Value* R = base;

    uint32_t instr;
    uint8_t A, B, C;

#ifdef __GNUC__
    // OPTIMIZATION: Direct Threaded Dispatch table for O(1) instruction decoding.
    static const void* dispatchTable[] = {
        &&OP_LOADK, &&OP_LOADINT, &&OP_LOADBOOL, &&OP_LOADNULL, &&OP_MOVE,
        &&OP_ADD, &&OP_SUB, &&OP_MUL, &&OP_DIV, &&OP_MOD, &&OP_NEG,
        &&OP_ADD_INT, &&OP_ADD_DOUBLE, &&OP_SUB_INT, &&OP_SUB_DOUBLE,
        &&OP_MUL_INT, &&OP_MUL_DOUBLE, &&OP_DIV_INT, &&OP_DIV_DOUBLE,
        &&OP_NOT, &&OP_AND, &&OP_OR,
        &&OP_EQ, &&OP_NEQ, &&OP_LT, &&OP_GT, &&OP_LE, &&OP_GE,
        &&OP_LT_INT, &&OP_GT_INT, &&OP_LE_INT, &&OP_GE_INT,
        &&OP_LT_DBL, &&OP_GT_DBL, &&OP_LE_DBL, &&OP_GE_DBL,
        &&OP_EQ_INT, &&OP_EQ_DBL,
        &&OP_BIT_AND, &&OP_BIT_OR, &&OP_BIT_XOR, &&OP_SHL, &&OP_SHR,
        &&OP_GGLOB, &&OP_SGLOB, &&OP_DGLOB,
        &&OP_JMP, &&OP_JMPF, &&OP_LOOP,
        &&OP_CALL, &&OP_TAILCALL, &&OP_CALL_NATIVE, &&OP_RET,
        &&OP_LOG, &&OP_WAIT,
        &&OP_TYPECHECK,
        &&OP_NEW_OBJ, &&OP_GET_FIELD, &&OP_SET_FIELD,
        &&OP_INVOKE, &&OP_TAIL_INVOKE,
        &&OP_NEW_ARRAY, &&OP_IDX_GET, &&OP_IDX_SET, 
        &&OP_IDX_GET_DBL, &&OP_IDX_SET_DBL, &&OP_IDX_GET_INT, &&OP_IDX_SET_INT,
        &&OP_COLL_LEN,
        &&OP_PUSH_HANDLER, &&OP_POP_HANDLER, &&OP_THROW,
        &&OP_HALT, &&OP_COUNT
    };

#define FETCH() instr = *ip++
#define DECODE_ABC() A = decodeA(instr); B = decodeB(instr); C = decodeC(instr)
#define DISPATCH() do { FETCH(); goto *dispatchTable[static_cast<uint8_t>(decodeOp(instr))]; } while(0)
#define CASE(op) OP_##op:
#define VM_LOOP() DISPATCH();
#define VM_LOOP_END()
#else
// Fallback for MSVC/others
#define FETCH() instr = *ip++
#define DECODE_ABC() A = decodeA(instr); B = decodeB(instr); C = decodeC(instr)
#define DISPATCH() break
#define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op):
#define VM_LOOP() while (true) { FETCH(); switch (static_cast<uint8_t>(decodeOp(instr))) {
#define VM_LOOP_END() } }
#endif

    /** @brief Handles exceptions by looking up the handler stack. */
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
        A = decodeA(instr);
        R[A] = chunk->constants[decodeBx(instr)];
        DISPATCH();
    }
    CASE(LOADINT) {
        A = decodeA(instr);
        R[A] = Value(static_cast<int>(decodeSBx(instr)));
        DISPATCH();
    }
    CASE(LOADBOOL) {
        DECODE_ABC();
        R[A] = Value(B != 0);
        DISPATCH();
    }
    CASE(LOADNULL) {
        A = decodeA(instr);
        R[A] = Value();
        DISPATCH();
    }
    CASE(MOVE) {
        DECODE_ABC();
        R[A] = R[B];
        DISPATCH();
    }

    CASE(ADD) {
        DECODE_ABC();
        Value& va = R[A];
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            va = Value(vb.asInt + vc.asInt);
        } else if (isNumeric(vb) && isNumeric(vc)) {
            va = numericAdd(vb, vc);
        } else {
            if (A == B && vb.tag == Value::TAG_STRING_HEAP && vb.asPtr->refCount == 1) {
                va.append(vc);
            } else {
                va = Value(vb.str() + toString(vc));
            }
        }
        DISPATCH();
    }

    CASE(ADD_INT) {
        DECODE_ABC();
        int res = R[B].asInt + R[C].asInt;
        R[A] = Value(res);
        DISPATCH();
    }
    
    CASE(ADD_DOUBLE) {
        DECODE_ABC();
        double res = toDouble(R[B]) + toDouble(R[C]);
        R[A] = Value(res);
        DISPATCH();
    }
    CASE(SUB) {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A] = Value(vb.asInt - vc.asInt);
        } else {
            R[A] = numericSub(vb, vc);
        }
        DISPATCH();
    }
    
    CASE(SUB_INT) {
        DECODE_ABC();
        int res = R[B].asInt - R[C].asInt;
        R[A] = Value(res);
        DISPATCH();
    }
    
    CASE(SUB_DOUBLE) {
        DECODE_ABC();
        double res = toDouble(R[B]) - toDouble(R[C]);
        R[A] = Value(res);
        DISPATCH();
    }
    CASE(MUL) {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A] = Value(vb.asInt * vc.asInt);
        } else {
            R[A] = numericMul(vb, vc);
        }
        DISPATCH();
    }
    
    CASE(MUL_INT) {
        DECODE_ABC();
        int res = R[B].asInt * R[C].asInt;
        R[A] = Value(res);
        DISPATCH();
    }
    
    CASE(MUL_DOUBLE) {
        DECODE_ABC();
        double res = toDouble(R[B]) * toDouble(R[C]);
        R[A] = Value(res);
        DISPATCH();
    }
    CASE(DIV) {
        DECODE_ABC();
        if (toDouble(R[C]) == 0.0) { dispatchException("Division by zero"); DISPATCH(); }
        R[A] = numericDiv(R[B], R[C]);
        DISPATCH();
    }
    
    CASE(DIV_INT) {
        DECODE_ABC();
        if (R[C].asInt == 0) { dispatchException("Division by zero"); DISPATCH(); }
        int res = R[B].asInt / R[C].asInt;
        R[A] = Value(res);
        DISPATCH();
    }
    
    CASE(DIV_DOUBLE) {
        DECODE_ABC();
        if (toDouble(R[C]) == 0.0) { dispatchException("Division by zero"); DISPATCH(); }
        double res = toDouble(R[B]) / toDouble(R[C]);
        R[A] = Value(res);
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
            R[A] = Value(!R[B].asBool);
        } else {
            throw std::runtime_error("Operator '!' requires boolean.");
        }
        DISPATCH();
    }

    CASE(AND) {
        DECODE_ABC();
        R[A] = Value(R[B].asBool && R[C].asBool);
        DISPATCH();
    }
    CASE(OR) {
        DECODE_ABC();
        R[A] = Value(R[B].asBool || R[C].asBool);
        DISPATCH();
    }

    CASE(EQ) {
        DECODE_ABC();
        const Value& a = R[B];
        const Value& b = R[C];
        if (a.isInt() && b.isInt()) {
            R[A] = Value(a.asInt == b.asInt);
        } else {
            R[A] = Value(a == b);
        }
        DISPATCH();
    }
    
    CASE(EQ_INT) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt == R[C].asInt);
        DISPATCH();
    }
    
    CASE(EQ_DBL) {
        DECODE_ABC();
        R[A] = Value(toDouble(R[B]) == toDouble(R[C]));
        DISPATCH();
    }
    CASE(NEQ) {
        DECODE_ABC();
        const Value& a = R[B];
        const Value& b = R[C];
        if (a.isInt() && b.isInt()) {
            R[A] = Value(a.asInt != b.asInt);
        } else {
            R[A] = Value(a != b);
        }
        DISPATCH();
    }
    CASE(LT) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A] = Value(R[B].asInt < R[C].asInt);
        } else {
            R[A] = Value(numericLT(R[B], R[C]));
        }
        DISPATCH();
    }
    
    CASE(LT_INT) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt < R[C].asInt);
        DISPATCH();
    }
    
    CASE(LT_DBL) {
        DECODE_ABC();
        R[A] = Value(toDouble(R[B]) < toDouble(R[C]));
        DISPATCH();
    }
    CASE(GT) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A] = Value(R[B].asInt > R[C].asInt);
        } else {
            R[A] = Value(numericGT(R[B], R[C]));
        }
        DISPATCH();
    }
    
    CASE(GT_INT) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt > R[C].asInt);
        DISPATCH();
    }
    
    CASE(GT_DBL) {
        DECODE_ABC();
        R[A] = Value(toDouble(R[B]) > toDouble(R[C]));
        DISPATCH();
    }
    CASE(LE) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A] = Value(R[B].asInt <= R[C].asInt);
        } else {
            R[A] = Value(numericLE(R[B], R[C]));
        }
        DISPATCH();
    }
    
    CASE(LE_INT) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt <= R[C].asInt);
        DISPATCH();
    }
    
    CASE(LE_DBL) {
        DECODE_ABC();
        R[A] = Value(toDouble(R[B]) <= toDouble(R[C]));
        DISPATCH();
    }
    CASE(GE) {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) {
            R[A] = Value(R[B].asInt >= R[C].asInt);
        } else {
            R[A] = Value(numericGE(R[B], R[C]));
        }
        DISPATCH();
    }
    
    CASE(GE_INT) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt >= R[C].asInt);
        DISPATCH();
    }
    
    CASE(GE_DBL) {
        DECODE_ABC();
        R[A] = Value(toDouble(R[B]) >= toDouble(R[C]));
        DISPATCH();
    }

    CASE(BIT_AND) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt & R[C].asInt);
        DISPATCH();
    }
    CASE(BIT_OR) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt | R[C].asInt);
        DISPATCH();
    }
    CASE(BIT_XOR) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt ^ R[C].asInt);
        DISPATCH();
    }
    CASE(SHL) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt << R[C].asInt);
        DISPATCH();
    }
    CASE(SHR) {
        DECODE_ABC();
        R[A] = Value(R[B].asInt >> R[C].asInt);
        DISPATCH();
    }

    CASE(GGLOB) {
        A = decodeA(instr);
        uint16_t slot = decodeBx(instr);
        if (slot >= globals.size()) throw std::runtime_error("Undefined global slot " + std::to_string(slot));
        R[A] = globals[slot].value;
        DISPATCH();
    }
    CASE(SGLOB) {
        A = decodeA(instr);
        uint16_t slot = decodeBx(instr);
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
        ip += decodeSBx(instr);
        DISPATCH();
    }
    CASE(JMPF) {
        A = decodeA(instr);
        if (R[A].isBool() && !R[A].asBool) ip += decodeSBx(instr);
        DISPATCH();
    }
    CASE(JMPT) {
        A = decodeA(instr);
        if (R[A].isBool() && R[A].asBool) ip += decodeSBx(instr);
        DISPATCH();
    }
    CASE(LOOP) {
        ip += decodeSBx(instr);
        DISPATCH();
    }

    CASE(CALL) {
        DECODE_ABC();
        uint16_t funcIdx = B;
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

    CASE(CALL_NATIVE) {
        DECODE_ABC();
        uint8_t callBase = A;
        uint8_t nativeIdx = B;
        uint8_t argCount = C;

        if (!nativeFunctions || nativeIdx >= nativeFunctions->size())
            throw std::runtime_error("Native function index out of bounds");

        NativeFunction* nf = (*nativeFunctions)[nativeIdx];
        Value result = nf->fn(R + callBase, argCount);
        R[callBase] = result; // Place result back into the first register of the call base
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

        if (R[callBase].tag == Value::TAG_NATIVE_OBJ) {
            NativeObject* obj = static_cast<NativeObject*>(R[callBase].asPtr);
            std::string methName = chunk->constants[nameId].str();
            Value result = obj->callMethod(methName, R + callBase + 1, argCount);
            R[callBase] = result;
            DISPATCH();
        }

        ObjectData* obj = static_cast<ObjectData*>(R[callBase].asPtr);
        uint16_t funcIdx;

        size_t ipOffset = (ip - 1) - chunk->code.data();
        auto& cache = chunk->inlineCache[ipOffset];
        
        if (cache.lookup(obj->classId, funcIdx)) {
        } else {
            ClassMeta& meta = (*classMetas)[obj->classId];
            std::string methName = chunk->constants[nameId].str();
            auto it = meta.methodIndex.find(methName);
            funcIdx = it->second;
            cache.update(obj->classId, funcIdx);
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
        auto& cache = chunk->inlineCache[ipOffset];
        
        if (cache.lookup(obj->classId, funcIdx)) {
        } else {
            ClassMeta& meta = (*classMetas)[obj->classId];
            std::string methName = chunk->constants[nameId].str();
            auto it = meta.methodIndex.find(methName);
            funcIdx = it->second;
            cache.update(obj->classId, funcIdx);
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
            A = decodeA(instr);
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

            R[decodeA(*(ip - 1))] = result;
        }
        DISPATCH();
    }

    CASE(LOG) {
        A = decodeA(instr);
        std::cout << toString(R[A]) << "\n";
        DISPATCH();
    }
    CASE(WAIT) {
        A = decodeA(instr);
        int ms;
        if (R[A].isInt()) ms = R[A].asInt;
        else if (R[A].isDouble()) ms = static_cast<int>(R[A].asDouble);
        else throw std::runtime_error("wait() expects number");
        driver->sleep(ms);
        DISPATCH();
    }

    CASE(TYPECHECK) {
        DECODE_ABC();
        const TypeAnnotation expected(static_cast<TypeKind>(B));
        const Value& v = R[A];
        bool ok = false;
        switch (expected.kind) {
            case TypeKind::Int:    ok = v.isInt();    break;
            case TypeKind::Double: ok = v.isDouble(); break;
            case TypeKind::Bool:   ok = v.isBool();   break;
            case TypeKind::String: ok = v.isString(); break;
            default: ok = true; break;
        }
        if (!ok) {
            const char* actual;
            switch (v.tag) {
                case Value::TAG_INT:    actual = "int";    break;
                case Value::TAG_DOUBLE: actual = "double"; break;
                case Value::TAG_BOOL:   actual = "bool";   break;
                case Value::TAG_STRING_SSO:
                case Value::TAG_STRING_HEAP: actual = "string"; break;
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
        A = decodeA(instr);
        uint16_t offset = decodeBx(instr);
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
            A = decodeA(instr);
            std::string msg = toString(R[A]);
            dispatchException(msg);
        }
        DISPATCH();
    }

    CASE(NEW_OBJ) {
        A = decodeA(instr);
        uint16_t clsId = decodeBx(instr);
        auto obj = new ObjectData();
        obj->classId = clsId;
        if (classMetas && clsId < classMetas->size()) {
            obj->fields.resize((*classMetas)[clsId].fields.size());
        }
        R[A] = Value(obj);
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

        ArrayData* arr = new ArrayData(static_cast<size_t>(size), type);
        R[A] = Value(arr);
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
            if (i < 0 || static_cast<size_t>(i) >= arr->length) {
                throw std::runtime_error("Array index out of bounds");
            }
            if (arr->elemType == ArrayData::INT) {
                R[A] = Value(arr->intData[i]);
            } else if (arr->elemType == ArrayData::DOUBLE) {
                R[A] = Value(arr->dblData[i]);
            } else {
                R[A] = arr->valData[i];
            }
        } else {
            throw std::runtime_error("IDX_GET on non-array");
        }
        DISPATCH();
    }

    CASE(IDX_GET_DBL) {
        DECODE_ABC();
        ArrayData* arr = static_cast<ArrayData*>(R[B].asPtr);
        int i = R[C].asInt;
        R[A] = Value(arr->dblData[i]);
        DISPATCH();
    }

    CASE(IDX_GET_INT) {
        DECODE_ABC();
        ArrayData* arr = static_cast<ArrayData*>(R[B].asPtr);
        int i = R[C].asInt;
        R[A] = Value(arr->intData[i]);
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
            if (i < 0 || static_cast<size_t>(i) >= arr->length) {
                throw std::runtime_error("Array index out of bounds");
            }
            
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

    CASE(IDX_SET_DBL) {
        DECODE_ABC();
        ArrayData* arr = static_cast<ArrayData*>(R[B].asPtr);
        int i = R[C].asInt;
        arr->dblData[i] = toDouble(R[A]);
        DISPATCH();
    }

    CASE(IDX_SET_INT) {
        DECODE_ABC();
        ArrayData* arr = static_cast<ArrayData*>(R[B].asPtr);
        int i = R[C].asInt;
        arr->intData[i] = (R[A].isInt() ? R[A].asInt : static_cast<int>(toDouble(R[A])));
        DISPATCH();
    }

    CASE(COLL_LEN) {
        DECODE_ABC();
        const Value& coll = R[B];
        if (coll.isArray()) {
            R[A] = Value(static_cast<int>(static_cast<ArrayData*>(coll.asPtr)->length));
        } else if (coll.isString()) {
            R[A] = Value(static_cast<int>(coll.str().size()));
        } else {
            throw std::runtime_error("len() on non-array/string");
        }
        DISPATCH();
    }

    CASE(COUNT)
#ifndef __GNUC__
    default:
    throw std::runtime_error("VM: unknown opcode " + std::to_string(static_cast<uint8_t>(decodeOp(instr))));
    
#endif

    VM_LOOP_END()

    #undef FETCH
    #undef DECODE_ABC
    #undef DISPATCH
    #undef CASE
    #undef VM_LOOP
    #undef VM_LOOP_END
}
