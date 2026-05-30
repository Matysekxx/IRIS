#include "VM.h"
#include "Compiler.h"
#include "JITCompiler.h"
#include "../node/ASTNode.h"
#include "../core/ArrayData.h"
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace iris::bytecode;
using namespace iris::core;
using namespace iris::device;

void VM::execute(Chunk &ch, IDeviceDriver *drv, iris::log::Logger *log,
                 std::vector<FunctionObject> *funcs,
                 std::vector<ClassMeta> *clss,
                 std::vector<NativeFunction *> *nativeFuncs) {
    chunk = &ch;
    driver = drv;
    logger = log;
    if (registerFile.empty()) registerFile.resize(STACK_MAX);
    base = registerFile.data();
    frameCount = 0;
    globals.clear();
    functions = funcs;
    classMetas = clss;
    nativeFunctions = nativeFuncs;
    if (!jit) jit = new JITCompiler();

    for (int i = 0; i < 512; i++) base[i] = Value();

    if (ch.callCount++ >= 0 && !ch.jitFunc && !ch.jitAttempted) {
        ch.jitAttempted = true;
        ch.jitFunc = (void*) jit->compile(ch, functions, nativeFunctions);
        if (ch.jitFunc) {
            std::cout << "[DEBUG JIT] Method JIT compilation SUCCESS for chunk!" << std::endl;
        } else {
            std::cout << "[DEBUG JIT] Method JIT compilation FAILED for chunk!" << std::endl;
        }
    }
    
    if (ch.jitFunc) {
        ((JITFunc) ch.jitFunc)(base, ch.constants.data(), this);
        return;
    }

    ip = ch.code.data();
    run();
}

void VM::invokeMethod(Value* rBase, int methodIdx, int argCount, Value* constants) {
    uint8_t cb = 0;
    int mid = methodIdx;
    int ac = argCount;
    Value* R = rBase;

    if (R[0].isPtr() && R[0].asPtr()->type == ManagedType::Native) {
        R[0] = static_cast<NativeObject *>(R[0].asPtr())->callMethod(
            constants[mid].str(), R + 1, ac);
        return;
    } else {
        if (R[0].isNull()) throw std::runtime_error("Null pointer access in method invoke");
        ObjectData *o = static_cast<ObjectData *>(R[0].asPtr());
        uint16_t fid = 0xFFFF;
        
        auto it = (*classMetas)[o->classId].methodIndex.find(constants[mid].str());
        if (it == (*classMetas)[o->classId].methodIndex.end()) throw std::runtime_error(
            "Method not found: " + constants[mid].str());
        fid = it->second;
        
        FunctionObject &f = (*functions)[fid];
        if (++f.chunk.callCount >= 1 && !f.chunk.jitFunc && !f.chunk.jitAttempted) {
            f.chunk.jitAttempted = true;
            f.chunk.jitFunc = (void *) jit->compile(f.chunk, functions, nativeFunctions);
        }
        if (f.chunk.jitFunc) {
            R[0].bits = ((JITFunc) f.chunk.jitFunc)(R, f.chunk.constants.data(), this);
            return;
        }
        
        CallFrame &fr = frames[frameCount++];
        fr.returnIp = nullptr;
        fr.returnChunk = chunk;
        fr.returnBase = base;
        
        Chunk* oldChunk = chunk;
        const uint32_t* oldIp = ip;
        Value* oldBase = base;
        
        chunk = &f.chunk;
        ip = f.chunk.code.data();
        base = R;
        run();
        
        chunk = oldChunk;
        ip = oldIp;
        base = oldBase;
        frameCount--;
    }
}

uint64_t VM::callFunction(int funcIdx, iris::core::Value* rBaseA) {
    FunctionObject &f = (*functions)[funcIdx];
    
    if (++f.chunk.callCount >= 1 && !f.chunk.jitFunc && !f.chunk.jitAttempted) {
        f.chunk.jitAttempted = true;
        f.chunk.jitFunc = (void *) jit->compile(f.chunk, functions, nativeFunctions);
    }
    
    uint64_t retBits;
    if (f.chunk.jitFunc) {
        retBits = ((JITFunc) f.chunk.jitFunc)(rBaseA, f.chunk.constants.data(), this);
    } else {
        if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow");
        CallFrame &fr = frames[frameCount++];
        fr.returnIp = nullptr;
        fr.returnChunk = chunk;
        fr.returnBase = base;
        
        Chunk* oldChunk = chunk;
        const uint32_t* oldIp = ip;
        Value* oldBase = base;
        
        chunk = &f.chunk;
        ip = f.chunk.code.data();
        base = rBaseA;
        
        run();
        
        retBits = rBaseA[0].bits;
        
        chunk = oldChunk;
        ip = oldIp;
        base = oldBase;
        frameCount--;
    }
    return retBits;
}

iris::core::Value VM::createObject(int classId) {
    return Value(new ObjectData(classId, (*classMetas)[classId].fields.size()));
}


void VM::run() {
    Value *R = base;
    const uint32_t *PC = ip;
    uint32_t instr;
    uint8_t A, B, C;

#ifdef __GNUC__
    static const void *d[] = {
        &&OP_LOADK, &&OP_LOADINT, &&OP_LOADBOOL, &&OP_LOADNULL, &&OP_MOVE, &&OP_MOVE_INT,
        &&OP_ADD, &&OP_SUB, &&OP_MUL, &&OP_DIV, &&OP_MOD, &&OP_NEG,
        &&OP_ADD_INT, &&OP_ADD_DOUBLE, &&OP_SUB_INT, &&OP_SUB_DOUBLE,
        &&OP_MUL_INT, &&OP_MUL_DOUBLE, &&OP_DIV_INT, &&OP_DIV_DOUBLE,
        &&OP_ADDI, &&OP_SUBI, &&OP_INC, &&OP_DEC,
        &&OP_NOT, &&OP_AND, &&OP_OR,
        &&OP_EQ, &&OP_NEQ, &&OP_LT, &&OP_GT, &&OP_LE, &&OP_GE,
        &&OP_LT_INT, &&OP_GT_INT, &&OP_LE_INT, &&OP_GE_INT,
        &&OP_LT_DBL, &&OP_GT_DBL, &&OP_LE_DBL, &&OP_GE_DBL,
        &&OP_EQ_INT, &&OP_EQ_DBL,
        &&OP_BIT_AND, &&OP_BIT_OR, &&OP_BIT_XOR, &&OP_SHL, &&OP_SHR,
        &&OP_GGLOB, &&OP_SGLOB, &&OP_DGLOB,
        &&OP_JMP, &&OP_JMPF, &&OP_JMPT, &&OP_LOOP,
        &&OP_CALL, &&OP_TAILCALL, &&OP_CALL_NATIVE, &&OP_RET,
        &&OP_LOG, &&OP_WAIT,
        &&OP_TYPECHECK,
        &&OP_NEW_OBJ, &&OP_GET_FIELD, &&OP_GET_FIELD_INT, &&OP_GET_FIELD_DBL, &&OP_SET_FIELD,
        &&OP_INC_FIELD, &&OP_DEC_FIELD,
        &&OP_INVOKE, &&OP_TAIL_INVOKE,
        &&OP_NEW_ARRAY, &&OP_IDX_GET, &&OP_IDX_SET,
        &&OP_IDX_GET_DBL, &&OP_IDX_SET_DBL, &&OP_IDX_GET_INT, &&OP_IDX_SET_INT,
        &&OP_COLL_LEN,
        &&OP_PUSH_HANDLER, &&OP_POP_HANDLER, &&OP_THROW,
        &&OP_HALT, &&OP_COUNT
    };
#define NEXT() do { \
    instr = *PC++; \
    if (traceManager.isTracing()) traceManager.record(instr, PC - 1); \
    goto *d[instr >> 24]; \
} while(0)
#define DECODE_ABC() A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; C = instr & 0xFF
#define CASE(op) OP_##op:
#else
#define NEXT() break
#define DECODE_ABC() A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; C = instr & 0xFF
#define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op):
#endif

    auto dispatchEx = [&](const std::string &msg) {
        if (!handlerStack.empty()) {
            ExceptionHandler h = handlerStack.back();
            handlerStack.pop_back();
            while (frameCount > h.frameCount) {
                frameCount--;
                const CallFrame &f = frames[frameCount];
                base = f.returnBase;
                chunk = f.returnChunk;
            }
            ip = h.catchIp;
            PC = h.catchIp;
            chunk = h.chunk;
            base = h.base;
            R = base;
            R[h.catchVarReg] = Value(new StringData(msg));
        } else throw std::runtime_error(msg);
    };

#ifndef __GNUC__
    while (1) {
        instr = *PC++;
        if (traceManager.isTracing()) traceManager.record(instr, PC - 1);
        switch (instr >> 24) {
#else
                NEXT();
#endif

            CASE(LOADK) {
                DECODE_ABC();
                R[A] = chunk->constants[instr & 0xFFFF];
                NEXT();
            }
            CASE(LOADINT) {
                A = (instr >> 16) & 0xFF;
                R[A] = Value((int) (instr & 0xFFFF) - 32767);
                NEXT();
            }
            CASE(LOADBOOL) {
                DECODE_ABC();
                R[A] = Value(B != 0);
                NEXT();
            }
            CASE(LOADNULL) {
                A = (instr >> 16) & 0xFF;
                R[A] = Value();
                NEXT();
            }
            CASE(MOVE) {
                DECODE_ABC();
                R[A] = R[B];
                NEXT();
            }
            CASE(MOVE_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt());
                NEXT();
            }

            CASE(ADD_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() + R[C].asInt());
                NEXT();
            }
            CASE(SUB_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() - R[C].asInt());
                NEXT();
            }
            CASE(MUL_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() * R[C].asInt());
                NEXT();
            }
            CASE(ADDI) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() + (int8_t) C);
                NEXT();
            }
            CASE(INC) {
                A = (instr >> 16) & 0xFF;
                R[A] = Value(R[A].asInt() + 1);
                NEXT();
            }
            CASE(DEC) {
                A = (instr >> 16) & 0xFF;
                R[A] = Value(R[A].asInt() - 1);
                NEXT();
            }

            CASE(LT_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() < R[C].asInt());
                NEXT();
            }
            CASE(GT_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() > R[C].asInt());
                NEXT();
            }
            CASE(EQ_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() == R[C].asInt());
                NEXT();
            }

            CASE(GGLOB) {
                A = (instr >> 16) & 0xFF;
                R[A] = globals[instr & 0xFFFF].value;
                NEXT();
            }
            CASE(SGLOB) {
                A = (instr >> 16) & 0xFF;
                globals[instr & 0xFFFF].value = R[A];
                NEXT();
            }
            CASE(DGLOB) {
                DECODE_ABC();
                uint16_t s = (B << 8) | C;
                if (s >= globals.size()) globals.resize(s + 1);
                globals[s] = {R[A], true};
                NEXT();
            }

            CASE(JMP) {
                PC += (int32_t) (instr & 0xFFFF) - 32767;
                NEXT();
            }
            CASE(JMPF) {
                A = (instr >> 16) & 0xFF;
                bool taken = !R[A].asBool();
                if (traceManager.isTracing()) traceManager.updateLastEntry(taken);
                if (taken) PC += (int32_t) (instr & 0xFFFF) - 32767;
                NEXT();
            }
            CASE(LOOP) {
                const uint32_t* target = PC + (int32_t) (instr & 0xFFFF) - 32767;
                Trace& t = traceManager.getOrCreateTrace(target);
                if (t.compiledFunc) {
                    uint64_t nextPC = t.compiledFunc(R, chunk->constants.data(), this);
                    if (nextPC) {
                        PC = (const uint32_t*)nextPC;
                        NEXT();
                    }
                } else if (traceManager.isTracing()) {
                    if (target == traceManager.getTracingStartPC()) {
                        traceManager.record(instr, PC - 1);
                        traceManager.stopTracing();
                        t.compiledFunc = jit->compileTrace(t, functions, nativeFunctions);
                    }
                } else if (++t.hotness > TraceManager::HOT_THRESHOLD) {
                    traceManager.startTracing(target);
                }
                PC = target;
                NEXT();
            }

            CASE(CALL) {
                DECODE_ABC();
                FunctionObject &f = (*functions)[B];
                /*if (++f.chunk.callCount >= 1 && !f.chunk.jitFunc && !f.chunk.jitAttempted) {
                    f.chunk.jitAttempted = true;
                    f.chunk.jitFunc = (void *) jit->compile(f.chunk, functions, nativeFunctions);
                }
                if (f.chunk.jitFunc) {
                    ((JITFunc) f.chunk.jitFunc)(R + A, f.chunk.constants.data(), this);
                    NEXT();
                }*/
                if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow");
                CallFrame &fr = frames[frameCount++];
                fr.returnIp = PC;
                fr.returnChunk = chunk;
                fr.returnBase = base;
                base = R + A;
                R = base;
                chunk = &f.chunk;
                PC = f.chunk.code.data();
                NEXT();
            }
            CASE(RET) {
                A = (instr >> 16) & 0xFF;
                Value res = std::move(R[A]);
                if (frameCount == 0) return;
                frameCount--;
                const CallFrame &f = frames[frameCount];
                base = f.returnBase;
                R = base;
                chunk = f.returnChunk;
                if (f.returnIp == nullptr) {
                    R[0] = std::move(res);
                    return;
                }
                PC = f.returnIp;
                R[(*(PC - 1) >> 16) & 0xFF] = std::move(res);
                NEXT();
            }

            CASE(GET_FIELD) {
                DECODE_ABC();
                R[A] = static_cast<ObjectData *>(R[B].asPtr())->fields[C];
                NEXT();
            }
            CASE(SET_FIELD) {
                DECODE_ABC();
                static_cast<ObjectData *>(R[B].asPtr())->fields[C] = R[A];
                NEXT();
            }
            CASE(IDX_GET_INT) {
                DECODE_ABC();
                R[A] = Value(static_cast<ArrayData *>(R[B].asPtr())->intData[R[C].asInt()]);
                NEXT();
            }
            CASE(IDX_SET_INT) {
                DECODE_ABC();
                static_cast<ArrayData *>(R[B].asPtr())->intData[R[C].asInt()] = R[A].asInt();
                NEXT();
            }

            CASE(NEW_OBJ) {
                A = (instr >> 16) & 0xFF;
                uint16_t cid = instr & 0xFFFF;
                R[A] = Value(new ObjectData(cid, (*classMetas)[cid].fields.size()));
                NEXT();
            }
            CASE(NEW_ARRAY) {
                DECODE_ABC();
                ArrayData::ElementType t = (C == 1) ? ArrayData::INT : (C == 2 ? ArrayData::DOUBLE : ArrayData::VALUE);
                R[A] = Value(new ArrayData((size_t) R[B].asInt(), t));
                NEXT();
            }
            CASE(LOG) {
                A = (instr >> 16) & 0xFF;
                std::cout << toString(R[A]) << "\n";
                NEXT();
            }
            CASE(HALT) return;

            CASE(ADD) {
                DECODE_ABC();
                R[A] = numericAdd(R[B], R[C]);
                NEXT();
            }
            CASE(SUB) {
                DECODE_ABC();
                R[A] = numericSub(R[B], R[C]);
                NEXT();
            }
            CASE(MUL) {
                DECODE_ABC();
                R[A] = numericMul(R[B], R[C]);
                NEXT();
            }
            CASE(DIV) {
                DECODE_ABC();
                R[A] = numericDiv(R[B], R[C]);
                NEXT();
            }
            CASE(EQ) {
                DECODE_ABC();
                R[A] = Value(R[B] == R[C]);
                NEXT();
            }
            CASE(NEQ) {
                DECODE_ABC();
                R[A] = Value(!(R[B] == R[C]));
                NEXT();
            }
            CASE(LT) {
                DECODE_ABC();
                R[A] = Value(numericLT(R[B], R[C]));
                NEXT();
            }
            CASE(GT) {
                DECODE_ABC();
                R[A] = Value(numericGT(R[B], R[C]));
                NEXT();
            }
            CASE(LE) {
                DECODE_ABC();
                R[A] = Value(numericLE(R[B], R[C]));
                NEXT();
            }
            CASE(GE) {
                DECODE_ABC();
                R[A] = Value(numericGE(R[B], R[C]));
                NEXT();
            }
            CASE(COLL_LEN) {
                A = (instr >> 16) & 0xFF;
                B = (instr >> 8) & 0xFF;
                R[A] = Value((int) static_cast<ArrayData *>(R[B].asPtr())->length);
                NEXT();
            }
            CASE(CALL_NATIVE) {
                DECODE_ABC();
                R[A] = (*nativeFunctions)[B]->fn(R + A, C);
                NEXT();
            }
            CASE(INVOKE) {
                DECODE_ABC();
                uint8_t cb = A, mid = B, ac = C;
                if (R[cb].isPtr() && R[cb].asPtr()->type == ManagedType::Native) {
                    R[cb] = static_cast<NativeObject *>(R[cb].asPtr())->callMethod(
                        chunk->constants[mid].str(), R + cb + 1, ac);
                    NEXT();
                } else {
                    ObjectData *o = static_cast<ObjectData *>(R[cb].asPtr());
                    uint16_t fid;
                    size_t off = (PC - 1) - chunk->code.data();
                    if (!chunk->inlineCache[off].lookup(o->classId, fid)) {
                        auto it = (*classMetas)[o->classId].methodIndex.find(chunk->constants[mid].str());
                        if (it == (*classMetas)[o->classId].methodIndex.end()) throw std::runtime_error(
                            "Method not found: " + chunk->constants[mid].str());
                        fid = it->second;
                        chunk->inlineCache[off].update(o->classId, fid);
                    }
                    FunctionObject &f = (*functions)[fid];
                    if (++f.chunk.callCount >= 1 && !f.chunk.jitFunc && !f.chunk.jitAttempted) {
                        f.chunk.jitAttempted = true;
                        f.chunk.jitFunc = (void *) jit->compile(f.chunk, functions, nativeFunctions);
                    }
                    if (f.chunk.jitFunc) {
                        ((JITFunc) f.chunk.jitFunc)(R + cb, f.chunk.constants.data(), this);
                        NEXT();
                    }
                    if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow");
                    CallFrame &fr = frames[frameCount++];
                    fr.returnIp = PC;
                    fr.returnChunk = chunk;
                    fr.returnBase = base;
                    base = R + cb;
                    R = base;
                    chunk = &f.chunk;
                    PC = f.chunk.code.data();
                }
                NEXT();
            }

            CASE(ADD_DOUBLE) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() + R[C].asDouble());
                NEXT();
            }
            CASE(SUB_DOUBLE) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() - R[C].asDouble());
                NEXT();
            }
            CASE(MUL_DOUBLE) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() * R[C].asDouble());
                NEXT();
            }
            CASE(DIV_INT) {
                DECODE_ABC();
                if (R[C].asInt() == 0) throw std::runtime_error("DivByZero");
                R[A] = Value(R[B].asInt() / R[C].asInt());
                NEXT();
            }
            CASE(DIV_DOUBLE) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() / R[C].asDouble());
                NEXT();
            }
            CASE(SUBI) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() - (int8_t) C);
                NEXT();
            }
            CASE(NOT) {
                DECODE_ABC();
                R[A] = Value(!R[B].asBool());
                NEXT();
            }
            CASE(AND) {
                DECODE_ABC();
                R[A] = Value(R[B].asBool() && R[C].asBool());
                NEXT();
            }
            CASE(OR) {
                DECODE_ABC();
                R[A] = Value(R[B].asBool() || R[C].asBool());
                NEXT();
            }
            CASE(LE_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() <= R[C].asInt());
                NEXT();
            }
            CASE(GE_INT) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() >= R[C].asInt());
                NEXT();
            }
            CASE(LT_DBL) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() < R[C].asDouble());
                NEXT();
            }
            CASE(GT_DBL) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() > R[C].asDouble());
                NEXT();
            }
            CASE(LE_DBL) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() <= R[C].asDouble());
                NEXT();
            }
            CASE(GE_DBL) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() >= R[C].asDouble());
                NEXT();
            }
            CASE(EQ_DBL) {
                DECODE_ABC();
                R[A] = Value(R[B].asDouble() == R[C].asDouble());
                NEXT();
            }
            CASE(BIT_AND) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() & R[C].asInt());
                NEXT();
            }
            CASE(BIT_OR) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() | R[C].asInt());
                NEXT();
            }
            CASE(BIT_XOR) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() ^ R[C].asInt());
                NEXT();
            }
            CASE(SHL) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() << R[C].asInt());
                NEXT();
            }
            CASE(SHR) {
                DECODE_ABC();
                R[A] = Value(R[B].asInt() >> R[C].asInt());
                NEXT();
            }
            CASE(JMPT) {
                DECODE_ABC();
                bool taken = R[A].asBool();
                if (traceManager.isTracing()) traceManager.updateLastEntry(taken);
                if (taken) PC += (int32_t) (instr & 0xFFFF) - 32767;
                NEXT();
            }
            CASE(TAILCALL) {
                DECODE_ABC();
                FunctionObject &f = (*functions)[B];
                for (uint8_t i = 0; i < C; ++i) base[i] = R[A + i];
                chunk = &f.chunk;
                PC = f.chunk.code.data();
                NEXT();
            }
            CASE(TYPECHECK) { NEXT(); }
            CASE(GET_FIELD_INT) {
                DECODE_ABC();
                R[A] = Value(static_cast<ObjectData *>(R[B].asPtr())->fields[C].asInt());
                NEXT();
            }
            CASE(GET_FIELD_DBL) {
                DECODE_ABC();
                R[A] = Value(static_cast<ObjectData *>(R[B].asPtr())->fields[C].asDouble());
                NEXT();
            }
            CASE(INC_FIELD) {
                DECODE_ABC();
                R[A] = Value(static_cast<ObjectData *>(R[A].asPtr())->fields[B].asInt() + 1);
                NEXT();
            }
            CASE(DEC_FIELD) {
                DECODE_ABC();
                R[A] = Value(static_cast<ObjectData *>(R[A].asPtr())->fields[B].asInt() - 1);
                NEXT();
            }
            CASE(TAIL_INVOKE) {
                DECODE_ABC();
                ObjectData *o = static_cast<ObjectData *>(R[A].asPtr());
                uint16_t fid;
                size_t off = (PC - 1) - chunk->code.data();
                if (!chunk->inlineCache[off].lookup(o->classId, fid)) {
                    fid = (*classMetas)[o->classId].methodIndex.at(chunk->constants[B].str());
                    chunk->inlineCache[off].update(o->classId, fid);
                }
                FunctionObject &f = (*functions)[fid];
                for (uint8_t i = 0; i < C; ++i) base[i] = R[A + i];
                chunk = &f.chunk;
                PC = f.chunk.code.data();
                NEXT();
            }
            CASE(IDX_GET) {
                DECODE_ABC();
                R[A] = static_cast<ArrayData *>(R[B].asPtr())->valData[R[C].asInt()];
                NEXT();
            }
            CASE(IDX_SET) {
                DECODE_ABC();
                static_cast<ArrayData *>(R[B].asPtr())->valData[R[C].asInt()] = R[A];
                NEXT();
            }
            CASE(IDX_GET_DBL) {
                DECODE_ABC();
                R[A] = Value(static_cast<ArrayData *>(R[B].asPtr())->dblData[R[C].asInt()]);
                NEXT();
            }
            CASE(IDX_SET_DBL) {
                DECODE_ABC();
                static_cast<ArrayData *>(R[B].asPtr())->dblData[R[C].asInt()] = R[A].asDouble();
                NEXT();
            }
            CASE(PUSH_HANDLER) {
                DECODE_ABC();
                ExceptionHandler h;
                h.catchIp = PC + (int32_t) (instr & 0xFFFF) - 32767;
                h.chunk = chunk;
                h.base = base;
                h.frameCount = frameCount;
                h.catchVarReg = A;
                handlerStack.push_back(h);
                NEXT();
            }
            CASE(POP_HANDLER) {
                if (!handlerStack.empty()) handlerStack.pop_back();
                NEXT();
            }
            CASE(THROW) {
                A = (instr >> 16) & 0xFF;
                dispatchEx(toString(R[A]));
                NEXT();
            }
            CASE(MOD) {
                DECODE_ABC();
                R[A] = numericMod(R[B], R[C]);
                NEXT();
            }
            CASE(NEG) {
                DECODE_ABC();
                R[A] = numericNegate(R[B]);
                NEXT();
            }
            CASE(WAIT) {
                A = (instr >> 16) & 0xFF;
                driver->sleep(R[A].isInt() ? R[A].asInt() : (int) R[A].asDouble());
                NEXT();
            }
            CASE(COUNT)
#ifndef __GNUC__
            default: NEXT();
#endif

#ifndef __GNUC__
        }
    }
#endif
}
