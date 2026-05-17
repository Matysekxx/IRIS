#include "VM.h"
#include "Compiler.h"
#include "../node/ASTNode.h"
#include "../core/ArrayData.h"
#include <iostream>
#include <stdexcept>

using namespace iris::bytecode;
using namespace iris::core;
using namespace iris::device;

StringData* VM::internString(const std::string& s) {
    auto it = stringInterner.find(s);
    if (it != stringInterner.end()) return it->second;
    StringData* data = new StringData(s);
    data->refCount++; 
    stringInterner[s] = data;
    return data;
}

void VM::execute(Chunk& ch, IDeviceDriver* drv, iris::log::Logger* log,
                std::vector<FunctionObject>* funcs,
                std::vector<ClassMeta>* clss,
                std::vector<NativeFunction*>* nativeFuncs) {
    chunk = &ch; ip = ch.code.data(); driver = drv; logger = log;
    if (registerFile.empty()) registerFile.resize(STACK_MAX);
    base = registerFile.data(); frameCount = 0;
    functions = funcs; classMetas = clss; nativeFunctions = nativeFuncs;
    for (int i = 0; i < 512; i++) registerFile[i] = Value();
    run();
}

void VM::run() {
    Value* R = base;
    const uint32_t* PC = ip;
    uint32_t instr;
    uint8_t A, B, C;

    auto dispatchEx = [&](const std::string& msg) {
        if (!handlerStack.empty()) {
            ExceptionHandler h = handlerStack.back(); handlerStack.pop_back();
            while (frameCount > h.frameCount) { frameCount--; const CallFrame& f = frames[frameCount]; base = f.returnBase; chunk = f.returnChunk; }
            ip = h.catchIp; PC = h.catchIp; chunk = h.chunk; base = h.base; R = base; R[h.catchVarReg] = Value(msg);
        } else throw std::runtime_error(msg);
    };

#ifdef __GNUC__
    static const void* d[] = {
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
#define DISPATCH() do { instr = *PC++; goto *d[instr >> 24]; } while(0)
#define DECODE_ABC() A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; C = instr & 0xFF
#define CASE(op) OP_##op:
#else
#define FETCH() instr = *PC++
#define DECODE_ABC() A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; C = instr & 0xFF
#define DISPATCH() break
#define CASE(op) case static_cast<uint8_t>(OpCode::OP_##op):
#endif

#ifndef __GNUC__
    while(1) { FETCH(); switch(instr >> 24) {
#else
    DISPATCH();
#endif

    CASE(LOADK) { DECODE_ABC(); R[A] = chunk->constants[instr & 0xFFFF]; DISPATCH(); }
    CASE(LOADINT) { A = (instr >> 16) & 0xFF; R[A].release(); R[A].tag = Value::TAG_INT; R[A].asInt = (int)(instr & 0xFFFF) - 32767; DISPATCH(); }
    CASE(LOADBOOL) { DECODE_ABC(); R[A] = Value(B != 0); DISPATCH(); }
    CASE(LOADNULL) { A = (instr >> 16) & 0xFF; R[A] = Value(); DISPATCH(); }
    CASE(MOVE) { DECODE_ABC(); R[A] = R[B]; DISPATCH(); }
    CASE(MOVE_INT) { DECODE_ABC(); R[A].asInt = R[B].asInt; R[A].tag = Value::TAG_INT; DISPATCH(); }
    
    CASE(ADD_INT) { DECODE_ABC(); R[A].asInt = R[B].asInt + R[C].asInt; R[A].tag = Value::TAG_INT; DISPATCH(); }
    CASE(SUB_INT) { DECODE_ABC(); R[A].asInt = R[B].asInt - R[C].asInt; R[A].tag = Value::TAG_INT; DISPATCH(); }
    CASE(MUL_INT) { DECODE_ABC(); R[A].asInt = R[B].asInt * R[C].asInt; R[A].tag = Value::TAG_INT; DISPATCH(); }
    CASE(ADDI) { DECODE_ABC(); R[A].asInt = R[B].asInt + (int8_t)C; R[A].tag = Value::TAG_INT; DISPATCH(); }
    CASE(INC) { A = (instr >> 16) & 0xFF; R[A].asInt++; DISPATCH(); }
    CASE(DEC) { A = (instr >> 16) & 0xFF; R[A].asInt--; DISPATCH(); }
    
    CASE(LT_INT) { DECODE_ABC(); R[A].release(); R[A].tag = Value::TAG_BOOL; R[A].asBool = R[B].asInt < R[C].asInt; DISPATCH(); }
    CASE(GT_INT) { DECODE_ABC(); R[A].release(); R[A].tag = Value::TAG_BOOL; R[A].asBool = R[B].asInt > R[C].asInt; DISPATCH(); }
    CASE(EQ_INT) { DECODE_ABC(); R[A].release(); R[A].tag = Value::TAG_BOOL; R[A].asBool = R[B].asInt == R[C].asInt; DISPATCH(); }
    
    CASE(GGLOB) { A = (instr >> 16) & 0xFF; R[A] = globals[instr & 0xFFFF].value; DISPATCH(); }
    CASE(SGLOB) { A = (instr >> 16) & 0xFF; globals[instr & 0xFFFF].value = R[A]; DISPATCH(); }
    CASE(DGLOB) { DECODE_ABC(); uint16_t s = (B << 8) | C; if (s >= globals.size()) globals.resize(s + 1); globals[s] = {R[A], true}; DISPATCH(); }

    CASE(JMP) { PC += (int32_t)(instr & 0xFFFF) - 32767; DISPATCH(); }
    CASE(JMPF) { A = (instr >> 16) & 0xFF; if (!R[A].asBool) PC += (int32_t)(instr & 0xFFFF) - 32767; DISPATCH(); }
    CASE(LOOP) { PC += (int32_t)(instr & 0xFFFF) - 32767; DISPATCH(); }

    CASE(CALL) {
        DECODE_ABC(); FunctionObject& f = (*functions)[B];
        CallFrame& fr = frames[frameCount++]; fr.returnIp = PC; fr.returnChunk = chunk; fr.returnBase = base;
        base = R + A; R = base; chunk = &f.chunk; PC = f.chunk.code.data(); DISPATCH();
    }
    CASE(RET) {
        A = (instr >> 16) & 0xFF; Value res = R[A]; if (frameCount == 0) return;
        frameCount--; const CallFrame& f = frames[frameCount]; base = f.returnBase; R = base; ip = f.returnIp; PC = f.returnIp; chunk = f.returnChunk;
        R[(*(PC - 1) >> 16) & 0xFF] = res; DISPATCH();
    }
    
    CASE(GET_FIELD) { DECODE_ABC(); R[A] = static_cast<ObjectData*>(R[B].asPtr)->fields[C]; DISPATCH(); }
    CASE(SET_FIELD) { DECODE_ABC(); static_cast<ObjectData*>(R[B].asPtr)->fields[C] = R[A]; DISPATCH(); }
    CASE(IDX_GET_INT) { DECODE_ABC(); R[A].release(); R[A].tag = Value::TAG_INT; R[A].asInt = static_cast<ArrayData*>(R[B].asPtr)->intData[R[C].asInt]; DISPATCH(); }
    CASE(IDX_SET_INT) { DECODE_ABC(); static_cast<ArrayData*>(R[B].asPtr)->intData[R[C].asInt] = R[A].asInt; DISPATCH(); }
    
    CASE(NEW_OBJ) { A = (instr >> 16) & 0xFF; uint16_t cid = instr & 0xFFFF; R[A] = Value(new ObjectData(cid, (*classMetas)[cid].fields.size())); DISPATCH(); }
    CASE(NEW_ARRAY) { DECODE_ABC(); ArrayData::ElementType t = (C==1)?ArrayData::INT:(C==2?ArrayData::DOUBLE:ArrayData::VALUE); R[A] = Value(new ArrayData((size_t)R[B].asInt, t)); DISPATCH(); }
    CASE(LOG) { A = (instr >> 16) & 0xFF; std::cout << toString(R[A]) << "\n"; DISPATCH(); }
    CASE(HALT) return;

    CASE(ADD) { DECODE_ABC(); R[A] = numericAdd(R[B], R[C]); DISPATCH(); }
    CASE(SUB) { DECODE_ABC(); R[A] = numericSub(R[B], R[C]); DISPATCH(); }
    CASE(MUL) { DECODE_ABC(); R[A] = numericMul(R[B], R[C]); DISPATCH(); }
    CASE(DIV) { DECODE_ABC(); R[A] = numericDiv(R[B], R[C]); DISPATCH(); }
    CASE(EQ) { DECODE_ABC(); R[A] = Value(R[B] == R[C]); DISPATCH(); }
    CASE(NEQ) { DECODE_ABC(); R[A] = Value(!(R[B] == R[C])); DISPATCH(); }
    CASE(LT) { DECODE_ABC(); R[A] = Value(numericLT(R[B], R[C])); DISPATCH(); }
    CASE(GT) { DECODE_ABC(); R[A] = Value(numericGT(R[B], R[C])); DISPATCH(); }
    CASE(COLL_LEN) { A = (instr >> 16) & 0xFF; B = (instr >> 8) & 0xFF; R[A].release(); R[A].tag = Value::TAG_INT; R[A].asInt = (int)static_cast<ArrayData*>(R[B].asPtr)->length; DISPATCH(); }
    CASE(CALL_NATIVE) { DECODE_ABC(); R[A] = (*nativeFunctions)[B]->fn(R + A, C); DISPATCH(); }
    CASE(INVOKE) {
        DECODE_ABC(); uint8_t cb = A, mid = B, ac = C;
        if (R[cb].tag == Value::TAG_NATIVE_OBJ) { R[cb] = static_cast<NativeObject*>(R[cb].asPtr)->callMethod(chunk->constants[mid].str(), R + cb + 1, ac); DISPATCH(); }
        else {
            ObjectData* o = static_cast<ObjectData*>(R[cb].asPtr); uint16_t fid; size_t off = (PC - 1) - chunk->code.data();
            if (!chunk->inlineCache[off].lookup(o->classId, fid)) { fid = (*classMetas)[o->classId].methodIndex.at(chunk->constants[mid].str()); chunk->inlineCache[off].update(o->classId, fid); }
            FunctionObject& f = (*functions)[fid]; CallFrame& fr = frames[frameCount++]; fr.returnIp = PC; fr.returnChunk = chunk; fr.returnBase = base;
            base = R + cb; R = base; chunk = &f.chunk; PC = f.chunk.code.data();
        }
        DISPATCH();
    }

    CASE(ADD_DOUBLE) { DECODE_ABC(); R[A] = Value(R[B].asDouble + R[C].asDouble); DISPATCH(); }
    CASE(SUB_DOUBLE) { DECODE_ABC(); R[A] = Value(R[B].asDouble - R[C].asDouble); DISPATCH(); }
    CASE(MUL_DOUBLE) { DECODE_ABC(); R[A] = Value(R[B].asDouble * R[C].asDouble); DISPATCH(); }
    CASE(DIV_INT) { DECODE_ABC(); R[A] = Value(R[B].asInt / R[C].asInt); DISPATCH(); }
    CASE(DIV_DOUBLE) { DECODE_ABC(); R[A] = Value(R[B].asDouble / R[C].asDouble); DISPATCH(); }
    CASE(SUBI) { DECODE_ABC(); R[A] = Value(R[B].asInt - (int8_t)C); DISPATCH(); }
    CASE(NOT) { DECODE_ABC(); R[A] = Value(!R[B].asBool); DISPATCH(); }
    CASE(AND) { DECODE_ABC(); R[A] = Value(R[B].asBool && R[C].asBool); DISPATCH(); }
    CASE(OR) { DECODE_ABC(); R[A] = Value(R[B].asBool || R[C].asBool); DISPATCH(); }
    CASE(LE_INT) { DECODE_ABC(); R[A] = Value(R[B].asInt <= R[C].asInt); DISPATCH(); }
    CASE(GE_INT) { DECODE_ABC(); R[A] = Value(R[B].asInt >= R[C].asInt); DISPATCH(); }
    CASE(LT_DBL) { DECODE_ABC(); R[A] = Value(R[B].asDouble < R[C].asDouble); DISPATCH(); }
    CASE(GT_DBL) { DECODE_ABC(); R[A] = Value(R[B].asDouble > R[C].asDouble); DISPATCH(); }
    CASE(LE_DBL) { DECODE_ABC(); R[A] = Value(R[B].asDouble <= R[C].asDouble); DISPATCH(); }
    CASE(GE_DBL) { DECODE_ABC(); R[A] = Value(R[B].asDouble >= R[C].asDouble); DISPATCH(); }
    CASE(EQ_DBL) { DECODE_ABC(); R[A] = Value(R[B].asDouble == R[C].asDouble); DISPATCH(); }
    CASE(BIT_AND) { DECODE_ABC(); R[A] = Value(R[B].asInt & R[C].asInt); DISPATCH(); }
    CASE(BIT_OR) { DECODE_ABC(); R[A] = Value(R[B].asInt | R[C].asInt); DISPATCH(); }
    CASE(BIT_XOR) { DECODE_ABC(); R[A] = Value(R[B].asInt ^ R[C].asInt); DISPATCH(); }
    CASE(SHL) { DECODE_ABC(); R[A] = Value(R[B].asInt << R[C].asInt); DISPATCH(); }
    CASE(SHR) { DECODE_ABC(); R[A] = Value(R[B].asInt >> R[C].asInt); DISPATCH(); }
    CASE(JMPT) { DECODE_ABC(); if (R[A].asBool) PC += (int32_t)(instr & 0xFFFF) - 32767; DISPATCH(); }
    CASE(TAILCALL) { DECODE_ABC(); FunctionObject& f = (*functions)[B]; for (uint8_t i = 0; i < C; ++i) base[i] = R[A + i]; chunk = &f.chunk; PC = f.chunk.code.data(); DISPATCH(); }
    CASE(TYPECHECK) { DECODE_ABC(); DISPATCH(); }
    CASE(GET_FIELD_INT) { DECODE_ABC(); R[A] = Value(static_cast<ObjectData*>(R[B].asPtr)->fields[C].asInt); DISPATCH(); }
    CASE(GET_FIELD_DBL) { DECODE_ABC(); R[A] = Value(static_cast<ObjectData*>(R[B].asPtr)->fields[C].asDouble); DISPATCH(); }
    CASE(INC_FIELD) { DECODE_ABC(); static_cast<ObjectData*>(R[A].asPtr)->fields[B].asInt++; DISPATCH(); }
    CASE(DEC_FIELD) { DECODE_ABC(); static_cast<ObjectData*>(R[A].asPtr)->fields[B].asInt--; DISPATCH(); }
    CASE(TAIL_INVOKE) { DECODE_ABC(); ObjectData* o = static_cast<ObjectData*>(R[A].asPtr); uint16_t fid; size_t off = (PC - 1) - chunk->code.data(); if (!chunk->inlineCache[off].lookup(o->classId, fid)) { fid = (*classMetas)[o->classId].methodIndex.at(chunk->constants[B].str()); chunk->inlineCache[off].update(o->classId, fid); } FunctionObject& f = (*functions)[fid]; for (uint8_t i = 0; i < C; ++i) base[i] = R[A + i]; chunk = &f.chunk; PC = f.chunk.code.data(); DISPATCH(); }
    CASE(IDX_GET) { DECODE_ABC(); R[A] = static_cast<ArrayData*>(R[B].asPtr)->valData[R[C].asInt]; DISPATCH(); }
    CASE(IDX_SET) { DECODE_ABC(); static_cast<ArrayData*>(R[B].asPtr)->valData[R[C].asInt] = R[A]; DISPATCH(); }
    CASE(IDX_GET_DBL) { DECODE_ABC(); R[A] = Value(static_cast<ArrayData*>(R[B].asPtr)->dblData[R[C].asInt]); DISPATCH(); }
    CASE(IDX_SET_DBL) { DECODE_ABC(); static_cast<ArrayData*>(R[B].asPtr)->dblData[R[C].asInt] = R[A].asDouble; DISPATCH(); }
    CASE(PUSH_HANDLER) { DECODE_ABC(); ExceptionHandler h; h.catchIp = PC + (int32_t)(instr & 0xFFFF) - 32767; h.chunk = chunk; h.base = base; h.frameCount = frameCount; h.catchVarReg = A; handlerStack.push_back(h); DISPATCH(); }
    CASE(POP_HANDLER) { handlerStack.pop_back(); DISPATCH(); }
    CASE(THROW) { A = (instr >> 16) & 0xFF; dispatchEx(toString(R[A])); DISPATCH(); }
    CASE(MOD) { DECODE_ABC(); R[A] = numericMod(R[B], R[C]); DISPATCH(); }
    CASE(NEG) { DECODE_ABC(); R[A] = numericNegate(R[B]); DISPATCH(); }
    CASE(WAIT) { A = (instr >> 16) & 0xFF; driver->sleep(R[A].isInt() ? R[A].asInt : (int)R[A].asDouble); DISPATCH(); }
    CASE(COUNT)
#ifndef __GNUC__
    default: DISPATCH();
#endif
#ifdef __GNUC__
    VM_LOOP_END()
#else
    } }
#endif
}
