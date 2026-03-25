#include "VM.h"
#include "Compiler.h"
#include "../node/ASTNode.h"
#include <iostream>
#include <stdexcept>

void VM::execute(Chunk& ch, IDeviceDriver* drv, Logger* log,
                 std::vector<FunctionObject>* funcs) {
    chunk = &ch;
    ip = ch.code.data();
    driver = drv;
    logger = log;
    base = stack;
    frameCount = 0;
    globals.clear();
    functions = funcs;
    run();
}

// Use Computed GOTO on GCC/Clang for performance.
// This allows jumping directly to the instruction handler address
// stored in a table, avoiding the overhead of a switch statement.
#if defined(__GNUC__) || defined(__clang__)
#define USE_COMPUTED_GOTO
#endif

void VM::run() {
    Value* R = base;

#ifdef USE_COMPUTED_GOTO

    /*
     * The Dispatch Table holds the memory addresses of the labels (e.g., L_ADD).
     * The '&&' operator is a GCC/Clang extension to get the address of a label.
     */
    static void* dispatchTable[] = {
        &&L_LOADK, &&L_LOADINT, &&L_LOADBOOL, &&L_LOADNULL, &&L_MOVE,
        &&L_ADD, &&L_SUB, &&L_MUL, &&L_DIV, &&L_MOD, &&L_NEG,
        &&L_NOT, &&L_AND, &&L_OR,
        &&L_EQ, &&L_NEQ, &&L_LT, &&L_GT, &&L_LE, &&L_GE,
        &&L_BIT_AND, &&L_BIT_OR, &&L_BIT_XOR, &&L_SHL, &&L_SHR,
        &&L_GGLOB, &&L_SGLOB, &&L_DGLOB,
        &&L_JMP, &&L_JMPF, &&L_LOOP,
        &&L_CALL, &&L_RET,
        &&L_LOG, &&L_WAIT,
        &&L_TYPECHECK,
        &&L_HALT,
    };

    uint32_t instr;
    uint8_t A, B, C;

    // Reads the next instruction word and advances the instruction pointer.
    #define FETCH() instr = *ip++

    // Extracts operands A (8-bit), B (8-bit), and C (8-bit) from the instruction.
    #define DECODE_ABC() A = DECODE_A(instr); B = DECODE_B(instr); C = DECODE_C(instr)

    // 1. Fetches the next instruction.
    // 2. Extracts the OpCode (first 8 bits: instr >> 24).
    // 3. Looks up the address in dispatchTable.
    // 4. Jumps directly to that address (goto *ptr).
    #define DISPATCH() FETCH(); goto *dispatchTable[instr >> 24]

    // Defines a label for the computed goto.
    // The '##' operator pastes 'L_' and the op name together.
    // Example: CASE(ADD) becomes L_ADD:
    #define CASE(op) L_##op

    // Start execution by dispatching the first instruction.
    DISPATCH();

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
        //logger->info("Waiting " + std::to_string(ms) + "ms");
        driver->sleep(ms);
        DISPATCH();
    }

    CASE(TYPECHECK): {
        DECODE_ABC();
        // A = register to check, B = expected TypeAnnotation tag (1-4)
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
            // Determine actual type name for the error message
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

    #undef FETCH
    #undef DECODE_ABC
    #undef DISPATCH
    #undef CASE
#endif


}
