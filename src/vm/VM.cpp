#include "VM.h"
#include "ir/Compiler.h"
#include "jit/JITCompiler.h"
#include "frontend/ASTNode.h"
#include "core/ArrayData.h"
#include "TraceOptimizer.h"
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _MSC_VER
static uint64_t safeCallJITFunc(iris::bytecode::JITFunc func, iris::bytecode::VMState* state) {
    __try {
        return func(state, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hErr && hErr != INVALID_HANDLE_VALUE) {
            const char* msg = "[JIT CRASH] Exception in compiled trace\n";
            DWORD w;
            WriteFile(hErr, msg, (DWORD)strlen(msg), &w, NULL);
        }
        fflush(stdout);
        return 0; // return NULL -> continue interpreted
    }
}

static uint64_t safeCallJITFuncArgs(iris::bytecode::JITFunc func, iris::bytecode::VMState* state, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    __try {
        return func(state, arg0, arg1, arg2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hErr && hErr != INVALID_HANDLE_VALUE) {
            const char* msg = "[JIT CRASH] Exception in compiled function\n";
            DWORD w;
            WriteFile(hErr, msg, (DWORD)strlen(msg), &w, NULL);
        }
        fflush(stdout);
        return 0;
    }
}
#endif

using namespace iris::bytecode;
using namespace iris::core;
using namespace iris::device;

namespace iris::std_lib {
    thread_local std::vector<ClassMeta>* activeClassMetas = nullptr;

    std::string getClassNameById(uint16_t classId) {
        if (activeClassMetas && classId < activeClassMetas->size()) {
            return (*activeClassMetas)[classId].name;
        }
        return "object";
    }
}

void VM::execute(Chunk &ch, IDeviceDriver *drv, iris::log::Logger *log,
                 std::vector<FunctionObject> *funcs,
                 std::vector<ClassMeta> *clss,
                 std::vector<NativeFunction *> *nativeFuncs) {
    iris::std_lib::activeClassMetas = clss;
    chunk = &ch;
    driver = drv;
    logger = log;
    if (registerFile.empty()) registerFile.resize(STACK_MAX);
    base = registerFile.data();
    frameCount = 0;
    globals.clear();
    int maxGlobalSlot = -1;
    auto scanChunk = [&](const Chunk& c) {
        for (uint32_t instr : c.code) {
            OpCode op = decodeOp(instr);
            if (op == OpCode::OP_GGLOB || op == OpCode::OP_SGLOB || op == OpCode::OP_DGLOB) {
                int slot = (int)(instr & 0xFFFF);
                if (slot > maxGlobalSlot) maxGlobalSlot = slot;
            }
        }
    };
    scanChunk(ch);
    if (funcs) {
        for (const auto& f : *funcs) {
            scanChunk(f.chunk);
        }
    }
    if (maxGlobalSlot >= 0) {
        globals.resize(maxGlobalSlot + 1);
    }

    functions = funcs;
    classMetas = clss;
    nativeFunctions = nativeFuncs;
    if (!jit) jit = new JITCompiler();

    iris::core::activeConstantPools.clear();
    iris::core::activeConstantPools.push_back(&ch.constants);
    if (funcs) {
        for (size_t idx = 0; idx < funcs->size(); ++idx) {
            iris::core::activeConstantPools.push_back(&(*funcs)[idx].chunk.constants);
        }
    }

    if (!ch.jitAttempted) {
        ch.jitAttempted = true;
        ch.jitFunc = (void*)jit->compile(ch, functions, nativeFunctions);
    }

    if (ch.jitFunc) {
        JITFunc jf = (JITFunc)ch.jitFunc;
        VMState state = { base, ch.constants.data(), this, (Value*)globals.data() };
#ifdef _MSC_VER
        safeCallJITFuncArgs(jf, &state, 0, 0, 0);
#else
        jf(&state, 0, 0, 0);
#endif
        return;
    }

    ip = ch.code.data();
    run();
}

void VM::invokeMethod(Value* rBase, int methodIdx, int argCount, Value* constants) {
    Value* R = rBase;
    int mid = methodIdx;
    int ac = argCount;

    if (R[0].isPtr() && R[0].asPtr()->type == ManagedType::Native) {
        std::string methodName = constants[mid].isSSO() ? constants[mid].asSSO() : constants[mid].asStringRef();
        R[0] = static_cast<NativeObject *>(R[0].asPtr())->callMethod(
            methodName, R + 1, ac);
        return;
    } else {
        if (R[0].isNull()) throw std::runtime_error("Null pointer access in method invoke");
        ObjectData *o = static_cast<ObjectData *>(R[0].asPtr());
        
        std::string methodName = constants[mid].isSSO() ? constants[mid].asSSO() : constants[mid].asStringRef();
        auto it = (*classMetas)[o->classId].methodIndex.find(methodName);
        if (it == (*classMetas)[o->classId].methodIndex.end()) throw std::runtime_error(
            "Method not found: " + methodName);
        uint16_t fid = it->second;
        FunctionObject &f = (*functions)[fid];

        if (!f.chunk.jitAttempted && ++f.chunk.callCount >= 1) {
            f.chunk.jitAttempted = true;
            if (!jit) jit = new JITCompiler();
            f.chunk.jitFunc = (void*)jit->compile(f.chunk, functions, nativeFunctions);
        }

        if (f.chunk.jitFunc) {
            if (frameCount >= (int)FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
            frameCount++;
            JITFunc jf = (JITFunc)f.chunk.jitFunc;
            VMState state = { R, f.chunk.constants.data(), this, (Value*)globals.data() };
            uint64_t nullBits = Value::QNAN | Value::TAG_NULL;
            uint64_t arg0 = (f.arity > 0) ? R[0].bits : nullBits;
            uint64_t arg1 = (f.arity > 1) ? R[1].bits : nullBits;
            uint64_t arg2 = (f.arity > 2) ? R[2].bits : nullBits;
            uint64_t retBits = jf(&state, arg0, arg1, arg2);
            R[0].bits = retBits;
            frameCount--;
            return;
        }
        
        if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
        CallFrame &fr = frames[frameCount++];
        fr.returnIp = nullptr;
        fr.returnChunk = chunk;
        fr.returnBase = R;
        fr.returnReg = 0;
        
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
    }
}

uint64_t VM::callFunction(int funcIdx, iris::core::Value* rBaseA) {
    FunctionObject &f = (*functions)[funcIdx];

        if (!f.chunk.jitAttempted && ++f.chunk.callCount >= 1) {
            f.chunk.jitAttempted = true;
            if (!jit) jit = new JITCompiler();
            f.chunk.jitFunc = (void*)jit->compile(f.chunk, functions, nativeFunctions);
        }

        if (f.chunk.jitFunc) {
        if (frameCount >= (int)FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
        frameCount++;
        JITFunc jf = (JITFunc)f.chunk.jitFunc;
        VMState state = { rBaseA, f.chunk.constants.data(), this, (Value*)globals.data() };
        uint64_t nullBits = Value::QNAN | Value::TAG_NULL;
        uint64_t arg0 = (f.arity > 0) ? rBaseA[0].bits : nullBits;
        uint64_t arg1 = (f.arity > 1) ? rBaseA[1].bits : nullBits;
        uint64_t arg2 = (f.arity > 2) ? rBaseA[2].bits : nullBits;
#ifdef _MSC_VER
        uint64_t retBits = safeCallJITFuncArgs(jf, &state, arg0, arg1, arg2);
#else
        uint64_t retBits = jf(&state, arg0, arg1, arg2);
#endif
        frameCount--;
        return retBits;
    }
    
    if (frameCount >= (int)FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
    CallFrame &fr = frames[frameCount++];
    fr.returnIp = nullptr;
    fr.returnChunk = chunk;
    fr.returnBase = rBaseA;
    fr.returnReg = 0;
    
    Chunk* oldChunk = chunk;
    const uint32_t* oldIp = ip;
    Value* oldBase = base;
    
    chunk = &f.chunk;
    ip = f.chunk.code.data();
    base = rBaseA;
    
    run();
    
    uint64_t retBits = rBaseA[0].bits;
    
    chunk = oldChunk;
    ip = oldIp;
    base = oldBase;
    // frameCount is decremented by OP_RET inside run()
    
    return retBits;
}

void VM::compileFunction(int funcIdx) {
    auto& f = (*functions)[funcIdx];
    if (f.chunk.jitAttempted) return;
    f.chunk.jitAttempted = true;
    if (!jit) jit = new JITCompiler();
    f.chunk.jitFunc = (void*)jit->compile(f.chunk, functions, nativeFunctions);
}

iris::core::Value VM::createObject(int classId) {
    if (!classMetas) throw std::runtime_error("classMetas is null");
    return Value(new ObjectData(classId, (uint16_t)(*classMetas)[classId].fields.size()));
}

iris::core::Value VM::getGlobal(int slot) {
    if (slot >= (int)globals.size()) return Value();
    return globals[slot].value;
}

void VM::setGlobal(int slot, iris::core::Value val) {
    if (slot >= (int)globals.size()) globals.resize(slot + 1);
    globals[slot] = {val, true};
}

void VM::jitSleep(int ms) {
    if (driver) driver->sleep(ms);
}

void VM::jitIncField(iris::core::Value* objVal, int fieldIdx) {
    auto* obj = static_cast<ObjectData*>(objVal->asPtr());
    Value& fld = obj->getField(fieldIdx);
    fld.bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(fld.asInt() + 1));
}

void VM::jitDecField(iris::core::Value* objVal, int fieldIdx) {
    auto* obj = static_cast<ObjectData*>(objVal->asPtr());
    Value& fld = obj->getField(fieldIdx);
    fld.bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(fld.asInt() - 1));
}

void VM::jitTailInvoke(iris::core::Value* rBase, int methodIdx, int argCount, iris::core::Value* constants) {
    Value receiver = rBase[0];
    if (receiver.isPtr() && receiver.asPtr()->type == ManagedType::Native) {
        std::string mname = constants[methodIdx].isSSO() ? constants[methodIdx].asSSO() : constants[methodIdx].asStringRef();
        Value res = static_cast<NativeObject*>(receiver.asPtr())->callMethod(mname, rBase + 1, argCount);
        if (frameCount == 0) return;
        frameCount--;
        const CallFrame& f = frames[frameCount];
        base = f.returnBase;
        chunk = f.returnChunk;
        if (f.returnIp == nullptr) {
            base[0] = std::move(res);
            return;
        }
        ip = f.returnIp;
        base[f.returnReg] = std::move(res);
    } else {
        if (receiver.isNull()) throw std::runtime_error("Null pointer access in tail invoke");
        ObjectData* o = static_cast<ObjectData*>(receiver.asPtr());
        std::string mname = constants[methodIdx].isSSO() ? constants[methodIdx].asSSO() : constants[methodIdx].asStringRef();
        auto it = (*classMetas)[o->classId].methodIndex.find(mname);
        if (it == (*classMetas)[o->classId].methodIndex.end()) {
            throw std::runtime_error("Method not found in tail invoke: " + mname);
        }
        uint16_t fid = it->second;
        FunctionObject& f = (*functions)[fid];
        for (uint8_t i = 0; i < argCount; ++i) base[i] = rBase[i];
        chunk = &f.chunk;
        ip = f.chunk.code.data();
    }
}

void VM::jitPushHandler(int bytecodeOffset, uint32_t instr, uint8_t catchVarReg) {
    ExceptionHandler h;
    h.catchIp = chunk->code.data() + bytecodeOffset + (int32_t)(instr & 0xFFFF) - 32767;
    h.chunk = chunk;
    h.base = base;
    h.frameCount = frameCount;
    h.catchVarReg = catchVarReg;
    handlerStack.push_back(h);
}

void VM::jitPopHandler() {
    if (!handlerStack.empty()) handlerStack.pop_back();
}

void VM::jitThrow(const std::string& msg) {
    if (!handlerStack.empty()) {
        ExceptionHandler h = handlerStack.back();
        handlerStack.pop_back();
        while (frameCount > h.frameCount) {
            frameCount--;
            const CallFrame& f = frames[frameCount];
            base = f.returnBase;
            chunk = f.returnChunk;
        }
        ip = h.catchIp;
        chunk = h.chunk;
        base = h.base;
        base[h.catchVarReg] = Value(new StringData(msg));
    } else {
        throw std::runtime_error(msg);
    }
}

#ifdef __GNUC__
#define HOT_FUNC __attribute__((hot))
#else
#define HOT_FUNC
#endif

void HOT_FUNC VM::run() {
    Value * __restrict R = base;
    const uint32_t * __restrict PC = ip;
    uint32_t instr;
    uint8_t A, B, C;

#ifdef __GNUC__
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif


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
        &&OP_INVOKE, &&OP_INVOKE_MONO, &&OP_TAIL_INVOKE,
        &&OP_NEW_ARRAY, &&OP_IDX_GET, &&OP_IDX_SET,
        &&OP_IDX_GET_DBL, &&OP_IDX_SET_DBL, &&OP_IDX_GET_INT, &&OP_IDX_SET_INT,
        &&OP_COLL_LEN,
        &&OP_PUSH_HANDLER, &&OP_POP_HANDLER, &&OP_THROW,
        &&OP_HALT, 
        &&OP_JLT_INT, &&OP_JGT_INT, &&OP_JLE_INT, &&OP_JGE_INT, &&OP_JNE_INT,
        &&OP_ADDI_W, &&OP_SUBI_W,
        &&OP_JLT_INT_IMM, &&OP_JGT_INT_IMM, &&OP_JLE_INT_IMM, &&OP_JGE_INT_IMM, &&OP_JEQ_INT_IMM, &&OP_JNE_INT_IMM,
        &&OP_ADD_K, &&OP_SUB_K, &&OP_MUL_K, &&OP_DIV_K, &&OP_LT_K, &&OP_GT_K, &&OP_EQ_K,
        &&OP_LOADDBL,
        &&OP_COUNT
    };
#endif

#ifdef __GNUC__
#define NEXT() do { \
    instr = *PC++; \
    if (UNLIKELY(traceManager.tracingFlag)) { \
        int depth = (int)frameCount - traceManager.getTracingStartFrameCount(); \
        if (depth == 0) { \
            uint16_t tA = (uint16_t)(R[(instr >> 16) & 0xFF].bits >> 48); \
            uint16_t tB = (uint16_t)(R[(instr >> 8) & 0xFF].bits >> 48); \
            uint16_t tC = (uint16_t)(R[instr & 0xFF].bits >> 48); \
            traceManager.recordFast(instr, PC - 1, false, tA, tB, tC); \
        } else if (depth < 0) { \
            traceManager.stopTracing(); \
        } \
    } \
    goto *d[instr >> 24]; \
} while(0)
#define NEXT_TRACE() NEXT()
#define CASE(op) OP_##op:
#else
#define NEXT() goto next_instr
#define NEXT_TRACE() goto next_instr
#define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op):
#endif

#define DECODE_ABC() A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; C = instr & 0xFF

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
            PC = h.catchIp;
            chunk = h.chunk;
            base = h.base;
            R = base;
            R[h.catchVarReg] = Value(new StringData(msg));
        } else {
            throw std::runtime_error(msg);
        }
    };

#ifdef __GNUC__
    instr = *PC++; goto *d[instr >> 24];
#else
    while (1) {
        next_instr:
        instr = *PC++;
        if (traceManager.tracingFlag) {
            int depth = (int)frameCount - traceManager.getTracingStartFrameCount();
            if (depth == 0) {
                uint16_t tA = (uint16_t)(R[(instr >> 16) & 0xFF].bits >> 48);
                uint16_t tB = (uint16_t)(R[(instr >> 8) & 0xFF].bits >> 48);
                uint16_t tC = (uint16_t)(R[instr & 0xFF].bits >> 48);
                traceManager.recordFast(instr, PC - 1, false, tA, tB, tC);
            } else if (depth < 0) {
                traceManager.stopTracing();
            }
        }
        switch (instr >> 24) {
#endif

// Macros defined at the top of VM::run()


#define CHECK_GC() do { \
    if (UNLIKELY(--gcCheckCounter <= 0)) { \
        gcCheckCounter = GC_CHECK_INTERVAL; \
        if (gcAllocated > gcThreshold) collectGC(registerFile.data(), registerFile.size(), globals); \
    } \
} while(0)

            CASE(LOADK) {
                A = (instr >> 16) & 0xFF;
                R[A].bits = chunk->constants[instr & 0xFFFF].bits;
                NEXT();
            }
            CASE(LOADINT) {
                A = (instr >> 16) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)((int) (instr & 0xFFFF) - 32767));
                NEXT();
            }
            CASE(LOADBOOL) {
                A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_BOOL | (B != 0 ? 1 : 0));
                NEXT();
            }
            CASE(LOADNULL) {
                A = (instr >> 16) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_NULL);
                NEXT();
            }
            CASE(LOADDBL) {
                A = (instr >> 16) & 0xFF;
                R[A] = Value(float16ToDouble((uint16_t)(instr & 0xFFFF)));
                NEXT();
            }
            CASE(MOVE) {
                A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF;
                R[A] = R[B];
                NEXT();
            }
            CASE(MOVE_INT) {
                A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)R[B].asInt());
                NEXT();
            }

            CASE(ADD_INT) {
                DECODE_ABC();
                int res = R[B].asInt() + R[C].asInt();
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }
            CASE(SUB_INT) {
                DECODE_ABC();
                int res = R[B].asInt() - R[C].asInt();
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }
            CASE(MUL_INT) {
                DECODE_ABC();
                int res = R[B].asInt() * R[C].asInt();
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }
            CASE(ADDI) {
                DECODE_ABC();
                int res = R[B].asInt() + (int8_t) C;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }
            CASE(INC) {
                DECODE_ABC();
                int res = R[A].asInt() + 1;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }
            CASE(DEC) {
                DECODE_ABC();
                int res = R[A].asInt() - 1;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)res);
                NEXT();
            }

            CASE(LT_INT) {
                DECODE_ABC();
                R[A].bits = (Value::QNAN | Value::TAG_BOOL | (R[B].asInt() < R[C].asInt() ? 1 : 0));
                NEXT();
            }
            CASE(GT_INT) {
                DECODE_ABC();
                R[A].bits = (Value::QNAN | Value::TAG_BOOL | (R[B].asInt() > R[C].asInt() ? 1 : 0));
                NEXT();
            }
            CASE(EQ_INT) {
                DECODE_ABC();
                R[A].bits = (Value::QNAN | Value::TAG_BOOL | (R[B].asInt() == R[C].asInt() ? 1 : 0));
                NEXT();
            }

            CASE(JLT_INT) {
                DECODE_ABC();
                bool taken = R[A].asInt() < R[B].asInt();
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) {
                    PC += static_cast<int32_t>(static_cast<int8_t>(C));
                }
                NEXT();
            }
            CASE(JGT_INT) {
                DECODE_ABC();
                bool taken = R[A].asInt() > R[B].asInt();
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) {
                    PC += static_cast<int32_t>(static_cast<int8_t>(C));
                }
                NEXT();
            }
            CASE(JLE_INT) {
                DECODE_ABC();
                bool taken = R[A].asInt() <= R[B].asInt();
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) {
                    PC += static_cast<int32_t>(static_cast<int8_t>(C));
                }
                NEXT();
            }
            CASE(JGE_INT) {
                DECODE_ABC();
                bool taken = R[A].asInt() >= R[B].asInt();
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) {
                    PC += static_cast<int32_t>(static_cast<int8_t>(C));
                }
                NEXT();
            }
            CASE(JNE_INT) {
                DECODE_ABC();
                bool taken = R[A].asInt() != R[B].asInt();
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) {
                    PC += static_cast<int32_t>(static_cast<int8_t>(C));
                }
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
                if (traceManager.isTracing() && frameCount == traceManager.getTracingStartFrameCount()) traceManager.updateLastEntry(taken);
                if (taken) PC += (int32_t) (instr & 0xFFFF) - 32767;
                NEXT();
            }
            CASE(LOOP) {
                CHECK_GC();
                PC += (int32_t) (instr & 0xFFFF) - 32767;
                if (LIKELY(TraceManager::HOT_THRESHOLD >= 99999999)) {
                    NEXT();
                }
                Trace& tr = traceManager.getOrCreateTrace(PC);
                if (++tr.hotness >= TraceManager::HOT_THRESHOLD && !tr.isCompiling) {
                    if (traceManager.isTracing()) {
                        Trace* startTrace = traceManager.getTrace(traceManager.getTracingStartPC());
                        if (startTrace) {
                            startTrace->isCompiling = true;
                            TraceOptimizer::optimize(*startTrace);
                            startTrace->compiledFunc = jit->compileTrace(*startTrace, functions, nativeFunctions);
                        }
                        traceManager.stopTracing();
                    } else if (!traceManager.isTracing()) {
                        traceManager.startTracing(PC, R, frameCount);
                    }
                }
                if (tr.compiledFunc) {
                    VMState state = { R, chunk->constants.data(), this, (Value*)globals.data() };
                    const uint32_t* retPC;
#ifdef _MSC_VER
                    retPC = (const uint32_t*)safeCallJITFunc(tr.compiledFunc, &state);
#else
                    retPC = (const uint32_t*)tr.compiledFunc(&state, 0, 0, 0);
#endif
                    if (retPC) {
                        PC = retPC;
                    }
                }
                NEXT();
            }

            CASE(CALL) {
                CHECK_GC();
                DECODE_ABC();
                FunctionObject &f = (*functions)[B];
                
        if (!f.chunk.jitAttempted && ++f.chunk.callCount >= 1) {
                    f.chunk.jitAttempted = true;
                    if (!jit) jit = new JITCompiler();
                    f.chunk.jitFunc = (void*)jit->compile(f.chunk, functions, nativeFunctions);
                }

                if (f.chunk.jitFunc) {
                    JITFunc jf = (JITFunc)f.chunk.jitFunc;
                    Value* newBase = R + A;
                    VMState state = { newBase, f.chunk.constants.data(), this, (Value*)globals.data() };
                    uint64_t nullBits = Value::QNAN | Value::TAG_NULL;
                    uint64_t arg0 = (f.arity > 0) ? newBase[0].bits : nullBits;
                    uint64_t arg1 = (f.arity > 1) ? newBase[1].bits : nullBits;
                    uint64_t arg2 = (f.arity > 2) ? newBase[2].bits : nullBits;
                    uint64_t retBits = jf(&state, arg0, arg1, arg2);
                    R[A].bits = retBits;
                    NEXT();
                }

                if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
                CallFrame &fr = frames[frameCount++];
                fr.returnIp = PC;
                fr.returnChunk = chunk;
                fr.returnBase = base;
                fr.returnReg = A;
                base = R + A;
                R = base;
                chunk = &f.chunk;
                PC = f.chunk.code.data();
                NEXT();
            }
            CASE(RET) {
                A = (instr >> 16) & 0xFF;
                {
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
                    R[f.returnReg] = std::move(res);
                }
                NEXT();
            }

            CASE(GET_FIELD) {
                DECODE_ABC();
                Value obj = R[B];
                if (obj.isNull()) throw std::runtime_error("GetField on null object");
                ObjectData *o = static_cast<ObjectData*>(obj.asPtr());
                R[A] = o->getField(C);
                NEXT();
            }
            CASE(SET_FIELD) {
                DECODE_ABC();
                Value obj = R[B];
                if (obj.isNull()) throw std::runtime_error("SetField on null object");
                ObjectData *o = static_cast<ObjectData*>(obj.asPtr());
                o->getField(C) = R[A];
                NEXT();
            }
            CASE(IDX_GET_INT) {
                DECODE_ABC();
                R[A] = Value(static_cast<ArrayData *>(R[B].asPtr())->getIntData()[R[C].asInt()]);
                NEXT();
            }
            CASE(IDX_SET_INT) {
                DECODE_ABC();
                static_cast<ArrayData *>(R[B].asPtr())->getIntData()[R[C].asInt()] = R[A].asInt();
                NEXT();
            }

            CASE(NEW_OBJ) {
                A = (instr >> 16) & 0xFF;
                uint16_t cid = instr & 0xFFFF;
                R[A] = Value(new ObjectData(cid, (uint16_t)(*classMetas)[cid].fields.size()));
                NEXT();
            }
            CASE(NEW_ARRAY) {
                DECODE_ABC();
                ArrayData::ElementType t = (C == 1) ? ArrayData::INT : (C == 2 ? ArrayData::DOUBLE : ArrayData::VALUE);
                R[A] = Value(ArrayData::create((size_t) R[B].asInt(), t));
                NEXT();
            }
            CASE(LOG) {
                A = (instr >> 16) & 0xFF;
                std::cout << toString(R[A]) << std::endl;
                NEXT();
            }
            CASE(HALT) return;

            CASE(ADD) {
                DECODE_ABC();
                if (R[B].isInt() && R[C].isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() + R[C].asInt())); }
                else if (R[B].isDouble() && R[C].isDouble()) { R[A] = Value(R[B].asDouble() + R[C].asDouble()); }
                else { R[A] = numericAdd(R[B], R[C]); }
                NEXT();
            }
            CASE(SUB) {
                DECODE_ABC();
                if (R[B].isInt() && R[C].isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() - R[C].asInt())); }
                else if (R[B].isDouble() && R[C].isDouble()) { R[A] = Value(R[B].asDouble() - R[C].asDouble()); }
                else { R[A] = numericSub(R[B], R[C]); }
                NEXT();
            }
            CASE(MUL) {
                DECODE_ABC();
                if (R[B].isInt() && R[C].isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() * R[C].asInt())); }
                else if (R[B].isDouble() && R[C].isDouble()) { R[A] = Value(R[B].asDouble() * R[C].asDouble()); }
                else { R[A] = numericMul(R[B], R[C]); }
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
                DECODE_ABC();
                if (R[B].isString()) R[A] = Value((int)R[B].stringLength());
                else if (R[B].isArray()) R[A] = Value((int)static_cast<ArrayData*>(R[B].asPtr())->length);
                NEXT();
            }
            CASE(CALL_NATIVE) {
                DECODE_ABC();
                R[A] = (*nativeFunctions)[B]->fn(R + A, C);
                NEXT();
            }
            CASE(INVOKE) {
                DECODE_ABC();
                invokeMethod(R + A, B, C, chunk->constants.data());
                NEXT();
            }

            CASE(ADD_DOUBLE) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() + R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) + toDouble(R[C]));
                }
                NEXT();
            }
            CASE(SUB_DOUBLE) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() - R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) - toDouble(R[C]));
                }
                NEXT();
            }
            CASE(MUL_DOUBLE) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() * R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) * toDouble(R[C]));
                }
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
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() / R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) / toDouble(R[C]));
                }
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
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() < R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) < toDouble(R[C]));
                }
                NEXT();
            }
            CASE(GT_DBL) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() > R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) > toDouble(R[C]));
                }
                NEXT();
            }
            CASE(LE_DBL) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() <= R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) <= toDouble(R[C]));
                }
                NEXT();
            }
            CASE(GE_DBL) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() >= R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) >= toDouble(R[C]));
                }
                NEXT();
            }
            CASE(EQ_DBL) {
                DECODE_ABC();
                if (R[B].isDouble() && R[C].isDouble()) {
                    R[A] = Value(R[B].asDouble() == R[C].asDouble());
                } else {
                    R[A] = Value(toDouble(R[B]) == toDouble(R[C]));
                }
                NEXT();
            }

            CASE(ADD_K) {
                DECODE_ABC();
                const Value& c = chunk->constants[C];
                if (R[B].isInt() && c.isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() + c.asInt())); }
                else if (R[B].isDouble() && c.isDouble()) { R[A] = Value(R[B].asDouble() + c.asDouble()); }
                else if (R[B].isDouble() && c.isInt()) { R[A] = Value(R[B].asDouble() + (double)c.asInt()); }
                else if (R[B].isInt() && c.isDouble()) { R[A] = Value((double)R[B].asInt() + c.asDouble()); }
                else { R[A] = numericAdd(R[B], c); }
                NEXT();
            }
            CASE(SUB_K) {
                DECODE_ABC();
                const Value& c = chunk->constants[C];
                if (R[B].isInt() && c.isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() - c.asInt())); }
                else if (R[B].isDouble() && c.isDouble()) { R[A] = Value(R[B].asDouble() - c.asDouble()); }
                else if (R[B].isDouble() && c.isInt()) { R[A] = Value(R[B].asDouble() - (double)c.asInt()); }
                else if (R[B].isInt() && c.isDouble()) { R[A] = Value((double)R[B].asInt() - c.asDouble()); }
                else { R[A] = numericSub(R[B], c); }
                NEXT();
            }
            CASE(MUL_K) {
                DECODE_ABC();
                const Value& c = chunk->constants[C];
                if (R[B].isInt() && c.isInt()) { R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[B].asInt() * c.asInt())); }
                else if (R[B].isDouble() && c.isDouble()) { R[A] = Value(R[B].asDouble() * c.asDouble()); }
                else if (R[B].isDouble() && c.isInt()) { R[A] = Value(R[B].asDouble() * (double)c.asInt()); }
                else if (R[B].isInt() && c.isDouble()) { R[A] = Value((double)R[B].asInt() * c.asDouble()); }
                else { R[A] = numericMul(R[B], c); }
                NEXT();
            }
            CASE(DIV_K) {
                DECODE_ABC();
                const Value& c = chunk->constants[C];
                if (R[B].isInt() && c.isInt()) {
                    if (c.asInt() == 0) throw std::runtime_error("DivByZero");
                    R[A] = Value(R[B].asInt() / c.asInt());
                } else { R[A] = numericDiv(R[B], c); }
                NEXT();
            }
            CASE(LT_K) {
                DECODE_ABC();
                R[A] = Value(numericLT(R[B], chunk->constants[C]));
                NEXT();
            }
            CASE(GT_K) {
                DECODE_ABC();
                R[A] = Value(numericGT(R[B], chunk->constants[C]));
                NEXT();
            }
            CASE(EQ_K) {
                DECODE_ABC();
                R[A] = Value(R[B] == chunk->constants[C]);
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
                if (R[A].asBool()) PC += (int32_t) (instr & 0xFFFF) - 32767;
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
                R[A] = Value(static_cast<ObjectData *>(R[B].asPtr())->getField(C).asInt());
                NEXT();
            }
            CASE(GET_FIELD_DBL) {
                DECODE_ABC();
                R[A] = Value(static_cast<ObjectData *>(R[B].asPtr())->getField(C).asDouble());
                NEXT();
            }
            CASE(INC_FIELD) {
                DECODE_ABC();
                Value& fld = static_cast<ObjectData *>(R[A].asPtr())->getField(B);
                fld.bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(fld.asInt() + 1));
                NEXT();
            }
            CASE(DEC_FIELD) {
                DECODE_ABC();
                Value& fld = static_cast<ObjectData *>(R[A].asPtr())->getField(B);
                fld.bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(fld.asInt() - 1));
                NEXT();
            }
            CASE(TAIL_INVOKE) {
                DECODE_ABC();
                Value receiver = R[A];
                bool isNative = receiver.isPtr() && receiver.asPtr()->type == ManagedType::Native;
                if (isNative) {
                    Value res;
                    {
                        Value& nameVal = chunk->constants[B];
                        std::string mname = nameVal.isSSO() ? nameVal.asSSO() : nameVal.asStringRef();
                        res = static_cast<NativeObject *>(receiver.asPtr())->callMethod(mname, R + A + 1, C);
                    }
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
                    R[f.returnReg] = std::move(res);
                    NEXT();
                } else {
                    if (receiver.isNull()) throw std::runtime_error("Null pointer access in tail invoke");
                    ObjectData *o = static_cast<ObjectData *>(receiver.asPtr());
                    uint16_t fid;
                    {
                        Value& nameVal = chunk->constants[B];
                        std::string mname = nameVal.isSSO() ? nameVal.asSSO() : nameVal.asStringRef();
                        auto it = (*classMetas)[o->classId].methodIndex.find(mname);
                        if (it == (*classMetas)[o->classId].methodIndex.end()) {
                            throw std::runtime_error("Method not found in tail invoke: " + mname);
                        }
                        fid = it->second;
                    }
                    FunctionObject &f = (*functions)[fid];
                    for (uint8_t i = 0; i < C; ++i) base[i] = R[A + i];
                    chunk = &f.chunk;
                    PC = f.chunk.code.data();
                    NEXT();
                }
            }
            CASE(IDX_GET) {
                DECODE_ABC();
                ArrayData* arr_g = static_cast<ArrayData *>(R[B].asPtr());
                switch (arr_g->elemType) {
                    case ArrayData::INT:    R[A] = Value(arr_g->getIntData()[R[C].asInt()]); break;
                    case ArrayData::DOUBLE: R[A] = Value(arr_g->getDblData()[R[C].asInt()]); break;
                    default:                R[A] = arr_g->getValData()[R[C].asInt()]; break;
                }
                NEXT();
            }
            CASE(IDX_SET) {
                DECODE_ABC();
                ArrayData* arr_s = static_cast<ArrayData *>(R[B].asPtr());
                switch (arr_s->elemType) {
                    case ArrayData::INT:    arr_s->getIntData()[R[C].asInt()] = R[A].asInt(); break;
                    case ArrayData::DOUBLE: arr_s->getDblData()[R[C].asInt()] = R[A].asDouble(); break;
                    default:                arr_s->getValData()[R[C].asInt()] = R[A]; break;
                }
                NEXT();
            }
            CASE(IDX_GET_DBL) {
                DECODE_ABC();
                R[A] = Value(static_cast<ArrayData *>(R[B].asPtr())->getDblData()[R[C].asInt()]);
                NEXT();
            }
            CASE(IDX_SET_DBL) {
                DECODE_ABC();
                static_cast<ArrayData *>(R[B].asPtr())->getDblData()[R[C].asInt()] = R[A].asDouble();
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
                if (R[B].isInt()) {
                    R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(-R[B].asInt()));
                } else {
                    R[A] = Value(-toDouble(R[B]));
                }
                NEXT();
            }
            CASE(WAIT) {
                A = (instr >> 16) & 0xFF;
                driver->sleep(R[A].asInt());
                NEXT();
            }

            CASE(ADDI_W) {
                A = (instr >> 16) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[A].asInt() + decodeSBx(instr)));
                NEXT();
            }
            CASE(SUBI_W) {
                A = (instr >> 16) & 0xFF;
                R[A].bits = (Value::QNAN | Value::TAG_INT | (uint32_t)(R[A].asInt() - decodeSBx(instr)));
                NEXT();
            }
            CASE(JLT_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() < decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }
            CASE(JGT_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() > decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }
            CASE(JLE_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() <= decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }
            CASE(JGE_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() >= decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }
            CASE(JEQ_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() == decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }
            CASE(JNE_INT_IMM) {
                A = (instr >> 16) & 0xFF;
                if (R[A].asInt() != decodeSBx(instr)) {
                    PC++; PC += (int32_t)(PC[-1] & 0xFFFF) - 32767;
                } else PC++;
                NEXT();
            }

            CASE(COUNT) { NEXT(); }

            CASE(INVOKE_MONO) {
                DECODE_ABC();
                uint16_t cacheIdx = (B << 8) | C;
                auto& entry = chunk->methodCaches[cacheIdx];
                Value receiver = R[A];
                if (receiver.isPtr() && receiver.asPtr()->type == ManagedType::Native) {
                    Value res;
                    {
                        Value& nameVal = chunk->constants[entry.methodNameIdx];
                        std::string mname = nameVal.isSSO() ? nameVal.asSSO() : nameVal.asStringRef();
                        res = static_cast<NativeObject *>(receiver.asPtr())->callMethod(mname, R + A + 1, entry.argCount - 1);
                    }
                    R[A] = std::move(res);
                    NEXT();
                }
                if (receiver.isNull()) {
                    throw std::runtime_error("Null pointer access in method invoke");
                }
                ObjectData *o = static_cast<ObjectData *>(receiver.asPtr());
                uint16_t fid;
                if (entry.lookup(o->classId, fid)) {
                    FunctionObject &f = (*functions)[fid];
                    if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
                    CallFrame &fr = frames[frameCount++];
                    fr.returnIp = PC; fr.returnChunk = chunk; fr.returnBase = base; fr.returnReg = A;
                    base = R + A; R = base; chunk = &f.chunk; PC = f.chunk.code.data();
                } else {
                    Value& nameVal = chunk->constants[entry.methodNameIdx];
                    std::string mname = nameVal.isSSO() ? nameVal.asSSO() : nameVal.asStringRef();
                    auto& meta = (*classMetas)[o->classId];
                    auto it = meta.methodIndex.find(mname);
                    if (it == meta.methodIndex.end()) throw std::runtime_error("Method not found: " + mname);
                    fid = it->second;
                    entry.update(o->classId, fid);
                    FunctionObject &f = (*functions)[fid];
                    if (frameCount >= FRAMES_MAX) throw std::runtime_error("StackOverflow at frameCount=" + std::to_string(frameCount));
                    CallFrame &fr = frames[frameCount++];
                    fr.returnIp = PC; fr.returnChunk = chunk; fr.returnBase = base; fr.returnReg = A;
                    base = R + A; R = base; chunk = &f.chunk; PC = f.chunk.code.data();
                }
                NEXT();
            }
#ifndef __GNUC__
            default: __assume(0);
#endif

#ifndef __GNUC__
        }
    }
#endif
}
