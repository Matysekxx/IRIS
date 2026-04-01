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
    globals.clear();
    functions = funcs;
    classMetas = classes;
    run();
}

void VM::run() {
    Value* R = base;

    uint32_t instr;
    uint8_t A, B, C;

    #define FETCH() instr = *ip++
    #define DECODE_ABC() A = DECODE_A(instr); B = DECODE_B(instr); C = DECODE_C(instr)


    #define DISPATCH() break
    #define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op)

    while (true) {
        FETCH();
        switch (instr >> 24) {

    CASE(LOADK): {
        A = DECODE_A(instr);
        R[A] = chunk->constants[DECODE_Bx(instr)];
        DISPATCH();
    }
    CASE(LOADINT): {
        A = DECODE_A(instr);
        R[A] = Value(DECODE_sBx(instr));
        DISPATCH();
    }
    CASE(LOADBOOL): {
        DECODE_ABC();
        R[A] = Value(B != 0);
        DISPATCH();
    }
    CASE(LOADNULL): {
        R[DECODE_A(instr)] = Value();
        DISPATCH();
    }
    CASE(MOVE): {
        DECODE_ABC();
        R[A] = R[B];
        DISPATCH();
    }

    CASE(ADD): {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) {
            R[A] = Value(vb.asInt + vc.asInt);
        } else if (isNumeric(vb) && isNumeric(vc)) {
            R[A] = numericAdd(vb, vc);
        } else {
            R[A] = Value(toString(vb) + toString(vc));
        }
        DISPATCH();
    }
    CASE(SUB): {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) R[A] = Value(vb.asInt - vc.asInt);
        else R[A] = numericSub(vb, vc);
        DISPATCH();
    }
    CASE(MUL): {
        DECODE_ABC();
        const Value& vb = R[B];
        const Value& vc = R[C];
        if (vb.isInt() && vc.isInt()) R[A] = Value(vb.asInt * vc.asInt);
        else R[A] = numericMul(vb, vc);
        DISPATCH();
    }
    CASE(DIV): {
        DECODE_ABC();
        if (toDouble(R[C]) == 0.0) throw std::runtime_error("Division by zero");
        R[A] = numericDiv(R[B], R[C]);
        DISPATCH();
    }
    CASE(MOD): {
        DECODE_ABC();
        R[A] = numericMod(R[B], R[C]);
        DISPATCH();
    }
    CASE(NEG): {
        DECODE_ABC();
        R[A] = numericNegate(R[B]);
        DISPATCH();
    }
    CASE(NOT): {
        DECODE_ABC();
        if (R[B].isBool()) R[A] = Value(!R[B].asBool);
        else throw std::runtime_error("Operator '!' requires boolean.");
        DISPATCH();
    }

    CASE(AND): { DECODE_ABC(); R[A] = Value(R[B].asBool && R[C].asBool); DISPATCH(); }
    CASE(OR): { DECODE_ABC(); R[A] = Value(R[B].asBool || R[C].asBool); DISPATCH(); }

    CASE(EQ): { DECODE_ABC(); R[A] = Value(R[B] == R[C]); DISPATCH(); }
    CASE(NEQ): { DECODE_ABC(); R[A] = Value(R[B] != R[C]); DISPATCH(); }
    CASE(LT): {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) R[A] = Value(R[B].asInt < R[C].asInt);
        else R[A] = Value(numericLT(R[B], R[C]));
        DISPATCH();
    }
    CASE(GT): {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) R[A] = Value(R[B].asInt > R[C].asInt);
        else R[A] = Value(numericGT(R[B], R[C]));
        DISPATCH();
    }
    CASE(LE): {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) R[A] = Value(R[B].asInt <= R[C].asInt);
        else R[A] = Value(numericLE(R[B], R[C]));
        DISPATCH();
    }
    CASE(GE): {
        DECODE_ABC();
        if (R[B].isInt() && R[C].isInt()) R[A] = Value(R[B].asInt >= R[C].asInt);
        else R[A] = Value(numericGE(R[B], R[C]));
        DISPATCH();
    }

    CASE(BIT_AND): { DECODE_ABC(); R[A] = Value(R[B].asInt & R[C].asInt); DISPATCH(); }
    CASE(BIT_OR): { DECODE_ABC(); R[A] = Value(R[B].asInt | R[C].asInt); DISPATCH(); }
    CASE(BIT_XOR): { DECODE_ABC(); R[A] = Value(R[B].asInt ^ R[C].asInt); DISPATCH(); }
    CASE(SHL): { DECODE_ABC(); R[A] = Value(R[B].asInt << R[C].asInt); DISPATCH(); }
    CASE(SHR): { DECODE_ABC(); R[A] = Value(R[B].asInt >> R[C].asInt); DISPATCH(); }

    CASE(GGLOB): {
        A = DECODE_A(instr);
        uint16_t slot = DECODE_Bx(instr);
        if (slot >= globals.size()) throw std::runtime_error("Undefined global slot " + std::to_string(slot));
        R[A] = globals[slot].value;
        DISPATCH();
    }
    CASE(SGLOB): {
        A = DECODE_A(instr);
        uint16_t slot = DECODE_Bx(instr);
        if (slot >= globals.size()) throw std::runtime_error("Undefined global slot " + std::to_string(slot));
        if (!globals[slot].isMutable) throw std::runtime_error("Global is immutable.");
        globals[slot].value = R[A];
        DISPATCH();
    }
    CASE(DGLOB): {
        DECODE_ABC();
        uint16_t slot = static_cast<uint16_t>((B << 8) | C);
        if (slot >= globals.size()) globals.resize(slot + 1);
        globals[slot] = {R[A], true};
        DISPATCH();
    }

    CASE(JMP): {
        ip += DECODE_sBx(instr);
        DISPATCH();
    }
    CASE(JMPF): {
        A = DECODE_A(instr);
        if (R[A].isBool() && !R[A].asBool) ip += DECODE_sBx(instr);
        DISPATCH();
    }
    CASE(LOOP): {
        ip += DECODE_sBx(instr);
        DISPATCH();
    }

    CASE(CALL): {
        DECODE_ABC();
        uint16_t funcIdx = B;
        uint8_t argCount = C;
        uint8_t callBase = A;

        if (!functions || funcIdx >= functions->size())
            throw std::runtime_error("Invalid function index");

        FunctionObject& func = (*functions)[funcIdx];
        if (argCount != static_cast<uint8_t>(func.arity))
            throw std::runtime_error("Function '" + func.name + "' expects " +
                std::to_string(func.arity) + " args, got " + std::to_string(argCount));

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

    CASE(RET): {
        A = DECODE_A(instr);
        Value result = R[A];

        frameCount--;
        const CallFrame& frame = frames[frameCount];
        base = frame.returnBase;
        R = base;
        ip = frame.returnIp;
        chunk = frame.returnChunk;

        R[DECODE_A(*(ip - 1))] = result;
        DISPATCH();
    }

    CASE(LOG): {
        A = DECODE_A(instr);
        std::cout << toString(R[A]) << "\n";
        DISPATCH();
    }
    CASE(WAIT): {
        A = DECODE_A(instr);
        int ms;
        if (R[A].isInt()) ms = R[A].asInt;
        else if (R[A].isDouble()) ms = static_cast<int>(R[A].asDouble);
        else throw std::runtime_error("wait() expects number");
        driver->sleep(ms);
        DISPATCH();
    }

    CASE(TYPECHECK): {
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

    CASE(HALT): return;

    CASE(NEW_OBJ): {
        A = DECODE_A(instr);
        uint16_t clsId = DECODE_Bx(instr);
        auto obj = std::make_shared<ObjectData>();
        obj->classId = clsId;
        if (classMetas && clsId < classMetas->size()) {
            obj->fields.resize((*classMetas)[clsId].fields.size());
        }
        R[A] = Value(std::move(obj));
        DISPATCH();
    }
    CASE(GET_FIELD): {
        DECODE_ABC();
        if (!R[B].isObject()) throw std::runtime_error("GET_FIELD on non-object");
        R[A] = R[B].objPtr->fields[C];
        DISPATCH();
    }
    CASE(SET_FIELD): {
        DECODE_ABC();
        if (!R[B].isObject()) throw std::runtime_error("SET_FIELD on non-object");
        R[B].objPtr->fields[C] = R[A];
        DISPATCH();
    }

    // ====== Collection opcodes ======

    CASE(NEW_ARRAY): {
        DECODE_ABC();
        int size;
        if (R[B].isInt()) size = R[B].asInt;
        else throw std::runtime_error("array() size must be int");
        if (size < 0) throw std::runtime_error("array() size must be non-negative");

        ArrayData::ElementType type = ArrayData::UNTYPED;
        if (C == 1) type = ArrayData::INT;
        else if (C == 2) type = ArrayData::DOUBLE;
        else if (C == 3) type = ArrayData::VALUE;

        R[A] = Value(std::make_shared<ArrayData>(static_cast<size_t>(size), type));
        DISPATCH();
    }

    CASE(IDX_GET): {
        DECODE_ABC();
        const Value& coll = R[B];
        const Value& idx = R[C];

        if (coll.isArray()) {
            if (!idx.isInt()) throw std::runtime_error("Array index must be int");
            auto* arr = coll.arrPtr.get();
            int i = idx.asInt;
            if (i < 0 || static_cast<size_t>(i) >= arr->length)
                throw std::runtime_error("Array index out of bounds");
            if (arr->elemType == ArrayData::INT)
                R[A] = Value(arr->intData[i]);
            else if (arr->elemType == ArrayData::DOUBLE)
                R[A] = Value(arr->dblData[i]);
            else if (arr->elemType == ArrayData::VALUE)
                R[A] = arr->valData[i];
        } else {
            throw std::runtime_error("IDX_GET on non-array");
        }
        DISPATCH();
    }

    CASE(IDX_SET): {
        DECODE_ABC();
        const Value& coll = R[B];
        const Value& idx = R[C];

        if (coll.isArray()) {
            if (!idx.isInt()) throw std::runtime_error("Array index must be int");
            auto* arr = coll.arrPtr.get();
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

    CASE(COLL_LEN): {
        DECODE_ABC();
        const Value& coll = R[B];
        if (coll.isArray()) R[A] = Value(static_cast<int>(coll.arrPtr->length));
        else if (coll.isString()) R[A] = Value(static_cast<int>(coll.str().size()));
        else throw std::runtime_error("len() on non-array/string");
        DISPATCH();
    }

        default:
            throw std::runtime_error("VM: unknown opcode " + std::to_string(instr >> 24));
        }
    }

    #undef FETCH
    #undef DECODE_ABC
    #undef DISPATCH
    #undef CASE
}
