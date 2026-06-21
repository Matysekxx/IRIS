#include "JITCompiler.h"
#include "vm/Trace.h"
#include "core/Value.h"
#include "ir/Compiler.h"
#include "JITHelpers.h"
#include <iostream>
#include <vector>
#include <asmjit/core.h>
#include <asmjit/x86.h>

using namespace iris::bytecode;
using namespace asmjit;

JITFunc JITCompiler::compile(Chunk& chunk, void* functions_ptr, void* native_functions) {
    auto* functions = static_cast<std::vector<FunctionObject>*>(functions_ptr);
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 72);
    
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    
    a.mov(x86::rax, x86::qword_ptr(x86::rcx, 24)); // globals
    a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
    
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx };
    const int NUM_VREGS = 5;
    
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    
    for(int i = 0; i < NUM_VREGS; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    auto flushRegs = [&]() { for(int i = 0; i < NUM_VREGS; i++) a.mov(x86::qword_ptr(rBase, (uint64_t)i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };

    std::vector<Label> labels(chunk.code.size() + 1);
    for (size_t i = 0; i <= chunk.code.size(); ++i) labels[i] = a.new_label();
    
    auto loadReg = [&](uint8_t reg, x86::Gp temp) {
        if (reg < NUM_VREGS) a.mov(temp, vRegs[reg]);
        else a.mov(temp, x86::qword_ptr(rBase, (uint64_t)reg * 8));
    };
    auto storeReg = [&](uint8_t reg, x86::Gp temp) {
        if (reg < NUM_VREGS) a.mov(vRegs[reg], temp);
        else a.mov(x86::qword_ptr(rBase, (uint64_t)reg * 8), temp);
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]); uint32_t instr = chunk.code[i]; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_LOADK: { a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8)); storeReg(A, x86::rax); break; }
            case OpCode::OP_LOADINT: { a.mov(x86::rax, intTag | (uint32_t)decodeSBx(instr)); storeReg(A, x86::rax); break; }
            case OpCode::OP_LOADBOOL: { a.mov(x86::rax, boolTag | (B != 0 ? 1ULL : 0ULL)); storeReg(A, x86::rax); break; }
            case OpCode::OP_LOADNULL: { a.mov(x86::rax, nullTag); storeReg(A, x86::rax); break; }
            case OpCode::OP_MOVE: { loadReg(B, x86::rax); storeReg(A, x86::rax); break; }
            case OpCode::OP_MOVE_INT: { loadReg(B, x86::rax); a.and_(x86::eax, x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_GGLOB: { a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rax, (uint64_t)(instr & 0xFFFF) * 8)); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADD_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.add(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SUB_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.sub(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_MUL_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.imul(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_DIV_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.mov(x86::rdx, x86::rax); a.sar(x86::rdx, 31); a.idiv(x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADDI: { loadReg(B, x86::rax); a.add(x86::eax, (int32_t)(int8_t)C); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_INC: { loadReg(A, x86::rax); a.inc(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_DEC: { loadReg(A, x86::rax); a.dec(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_BIT_XOR: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.xor_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_BIT_AND: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.and_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_BIT_OR:  { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.or_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SHL: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.shl(x86::eax, x86::cl); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SHR: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.shr(x86::eax, x86::cl); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADDI_W: { loadReg(B, x86::rax); a.add(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SUBI_W: { loadReg(B, x86::rax); a.sub(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_NOT: { loadReg(B, x86::rax); a.xor_(x86::rax, 1); storeReg(A, x86::rax); break; }
            case OpCode::OP_LT_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setl(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_GT_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setg(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_JMPF: { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 0); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_JMPT: { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 1); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_GET_FIELD: { loadReg(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeReg(A, x86::rax); break; }
            case OpCode::OP_GET_FIELD_INT: { loadReg(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                a.mov(x86::ecx, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); break; }
            case OpCode::OP_GET_FIELD_DBL: { loadReg(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeReg(A, x86::rax); break; }
            case OpCode::OP_SET_FIELD: { loadReg(B, x86::rdx); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rdx, x86::rcx); loadReg(A, x86::rax);
                if (C < 4) { a.mov(x86::qword_ptr(x86::rdx, 40 + C * 8), x86::rax); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 32)); a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8), x86::rax); }
                break; }
            case OpCode::OP_INVOKE:
            case OpCode::OP_INVOKE_MONO: {
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8)); a.mov(x86::edx, (uint32_t)B); a.mov(x86::r8d, (uint32_t)C);
                a.mov(x86::r9, constants); a.mov(x86::rax, vmPtr); a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
                a.call((uint64_t)&invokeHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                break;
            }
            case OpCode::OP_CALL: {
                flushRegs(); a.mov(x86::rcx, (uint64_t)B); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)A * 8)); a.mov(x86::r8, vmPtr);
                a.call((uint64_t)&callFunctionHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_CALL_NATIVE: {
                auto* native_funcs = static_cast<std::vector<iris::core::NativeFunction*>*>(native_functions);
                flushRegs();
                a.mov(x86::rcx, (uint64_t)(*native_funcs)[B]);
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.mov(x86::r8d, (uint32_t)C);
                a.call((uint64_t)&callNativeHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_RET: { loadReg(A, x86::rax); emitEpilogue(); break; }
            case OpCode::OP_HALT: emitEpilogue(); break;
            case OpCode::OP_JMP:
            case OpCode::OP_LOOP: { a.jmp(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_NEW_OBJ: {
                flushRegs(); a.mov(x86::ecx, (uint32_t)decodeBx(instr)); a.mov(x86::rdx, vmPtr);
                a.call((uint64_t)&createObjectHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_NEW_ARRAY: {
                flushRegs();
                loadReg(B, x86::rcx); a.mov(x86::ecx, x86::ecx);
                a.mov(x86::edx, (uint32_t)C);
                a.call((uint64_t)&createArrayHelper);
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_GET:
            case OpCode::OP_IDX_GET_DBL:
            case OpCode::OP_IDX_GET_INT: {
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8)); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.call((uint64_t)&idxGetHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_DBL:
            case OpCode::OP_IDX_SET_INT: {
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8)); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8)); a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.call((uint64_t)&idxSetHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                break;
            }
            case OpCode::OP_COLL_LEN: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.call((uint64_t)&collLenHelper);
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_ADD: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&addHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_SUB: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&subHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_MUL: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&mulHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_EQ:  { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&eqHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_NEQ: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&eqHelper); a.xor_(x86::rax, 1); storeReg(A, x86::rax); break; }
            case OpCode::OP_LT:  { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&ltHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_GT:  { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&gtHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_NEG: { flushRegs(); loadReg(B, x86::rcx); a.call((uint64_t)&negHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_DIV: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&divHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_MOD: { flushRegs(); loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&modHelper); storeReg(A, x86::rax); break; }
            case OpCode::OP_JLT_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.jge(labels[i + 1]); break; }
            case OpCode::OP_JGT_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.jle(labels[i + 1]); break; }
            case OpCode::OP_JLE_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.jg(labels[i + 1]); break; }
            case OpCode::OP_JGE_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.jl(labels[i + 1]); break; }
            case OpCode::OP_JEQ_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.jne(labels[i + 1]); break; }
            case OpCode::OP_JNE_INT_IMM: { loadReg(B, x86::rax); a.cmp(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.je(labels[i + 1]); break; }
            default: break;
        }
    }
    a.bind(labels[chunk.code.size()]);
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions_ptr) {
    auto isSupported = [](OpCode op, int baseOff) {
        switch (op) {
            case OpCode::OP_LOADK:
            case OpCode::OP_LOADINT:
            case OpCode::OP_LOADBOOL:
            case OpCode::OP_LOADNULL:
            case OpCode::OP_GGLOB:
            case OpCode::OP_NOT:
            case OpCode::OP_MOVE:
            case OpCode::OP_MOVE_INT:
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT:
            case OpCode::OP_ADDI:
            case OpCode::OP_LT_INT:
            case OpCode::OP_GT_INT:
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K:
            case OpCode::OP_JMPF:
            case OpCode::OP_JMPT:
            case OpCode::OP_GET_FIELD:
            case OpCode::OP_GET_FIELD_INT:
            case OpCode::OP_GET_FIELD_DBL:
            case OpCode::OP_SET_FIELD:
            case OpCode::OP_NEW_OBJ:
            case OpCode::OP_NEW_ARRAY:
            case OpCode::OP_IDX_GET:
            case OpCode::OP_IDX_GET_DBL:
            case OpCode::OP_IDX_GET_INT:
            case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_DBL:
            case OpCode::OP_IDX_SET_INT:
            case OpCode::OP_COLL_LEN:
            case OpCode::OP_INC:
            case OpCode::OP_DEC:
            case OpCode::OP_LOOP:
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_EQ:
            case OpCode::OP_NEQ:
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_NEG:
                return true;
            case OpCode::OP_RET:
                return baseOff == 0;
            default:
                return false;
        }
    };

    for (const auto& entry : trace.preamble) {
        OpCode op = decodeOp(entry.instr);
        if (!isSupported(op, entry.registerBaseOffset)) return nullptr;
    }
    for (const auto& entry : trace.entries) {
        OpCode op = decodeOp(entry.instr);
        if (!isSupported(op, entry.registerBaseOffset)) return nullptr;
    }

    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 72);
    
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants (start frame)
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    a.mov(x86::rax, x86::qword_ptr(x86::rcx, 24)); a.mov(x86::qword_ptr(x86::rsp, 40), x86::rax); // globalsPtr

    x86::Gp rBase = x86::rdi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx };
    const int NUM_VREGS = 5;
    std::vector<bool> isUnboxed(NUM_VREGS, false);

    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint16_t intPrefix = (uint16_t)(intTag >> 48);
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;

    for(int i = 0; i < NUM_VREGS; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    Label loopEntry = a.new_label(); 

    auto flushRegs = [&]() {
        for(int i = 0; i < NUM_VREGS; i++) {
            if (isUnboxed[i]) {
                a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, (uint64_t)i * 8), x86::rax);
            } else { a.mov(x86::qword_ptr(rBase, (uint64_t)i * 8), vRegs[i]); }
        }
    };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };

    for (int i = 0; i < NUM_VREGS; i++) {
        if (trace.initialTypes[i] == intPrefix) {
            a.mov(x86::rax, vRegs[i]); a.shr(x86::rax, 48); a.cmp(x86::ax, intPrefix);
            emitGuard(x86::CondCode::kEqual, trace.startPC);
            a.mov(vRegs[i].r32(), vRegs[i].r32()); isUnboxed[i] = true;
        }
    }

    auto emitEntry = [&](const Trace::Entry& entry) {
        uint32_t instr = entry.instr; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        const std::vector<iris::core::Value>* curConstants = entry.constants;
        int baseOff = entry.registerBaseOffset;

        auto loadRegAbs = [&](uint8_t reg, x86::Gp dest) {
            int abs = baseOff + reg;
            if (abs < NUM_VREGS) a.mov(dest, vRegs[abs]);
            else a.mov(dest, x86::qword_ptr(rBase, (uint64_t)abs * 8));
        };
        auto storeRegAbs = [&](uint8_t reg, x86::Gp src) {
            int abs = baseOff + reg;
            if (abs < NUM_VREGS) a.mov(vRegs[abs], src);
            else a.mov(x86::qword_ptr(rBase, (uint64_t)abs * 8), src);
        };
        auto isUnboxedAbs = [&](uint8_t reg) { int abs = baseOff + reg; return (abs < NUM_VREGS && isUnboxed[abs]); };
        auto loadBoxed = [&](uint8_t reg, x86::Gp dest) { loadRegAbs(reg, dest); if (isUnboxedAbs(reg)) { a.mov(x86::r11, intTag); a.or_(dest, x86::r11); } };

        switch (op) {
            case OpCode::OP_LOADK:
                a.mov(x86::rax, (uint64_t)curConstants->data()); a.mov(x86::rax, x86::qword_ptr(x86::rax, (uint64_t)(instr & 0xFFFF) * 8));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            case OpCode::OP_LOADINT:
                a.mov(x86::rax, intTag | (uint32_t)decodeSBx(instr));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true; break;
            case OpCode::OP_LOADBOOL:
                a.mov(x86::rax, boolTag | (B != 0 ? 1ULL : 0ULL));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            case OpCode::OP_LOADNULL:
                a.mov(x86::rax, (uint64_t)(iris::core::Value::QNAN | iris::core::Value::TAG_NULL));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            case OpCode::OP_GGLOB: {
                flushRegs();
                a.mov(x86::rcx, vmPtr); a.mov(x86::edx, (uint32_t)(instr & 0xFFFF));
                a.call((uint64_t)&getGlobalHelper);
                for (int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_NOT: {
                loadRegAbs(B, x86::rax); a.xor_(x86::rax, 1);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_MOVE:
                loadRegAbs(B, x86::rax); storeRegAbs(A, x86::rax);
                if (baseOff + A < NUM_VREGS && baseOff + B < NUM_VREGS) isUnboxed[baseOff + A] = isUnboxed[baseOff + B];
                else if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT: {
                loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF);
                if (op == OpCode::OP_ADD_INT) a.add(x86::eax, x86::ecx);
                else if (op == OpCode::OP_SUB_INT) a.sub(x86::eax, x86::ecx);
                else a.imul(x86::eax, x86::ecx);
                a.mov(x86::rdx, intTag); a.or_(x86::rax, x86::rdx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true; break;
            }
            case OpCode::OP_ADDI: {
                loadRegAbs(B, x86::rax); if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                a.add(x86::eax, (int32_t)(int8_t)C);
                a.mov(x86::rdx, intTag); a.or_(x86::rax, x86::rdx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true; break;
            }
            case OpCode::OP_LT_INT:
            case OpCode::OP_GT_INT:
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K: {
                loadRegAbs(B, x86::rax); if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (op == OpCode::OP_LT_K || op == OpCode::OP_GT_K) {
                    a.mov(x86::rcx, (uint64_t)curConstants->data()); a.mov(x86::rcx, x86::qword_ptr(x86::rcx, (uint64_t)C * 8)); a.and_(x86::rcx, 0xFFFFFFFF);
                } else { loadRegAbs(C, x86::rcx); if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF); }
                a.cmp(x86::eax, x86::ecx);
                x86::CondCode cond = (op == OpCode::OP_LT_INT || op == OpCode::OP_LT_K) ? x86::CondCode::kSignedLT : x86::CondCode::kSignedGT;
                a.set(cond, x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_JMPF:
            case OpCode::OP_JMPT: {
                loadRegAbs(A, x86::rax); a.and_(x86::eax, 1);
                if (!entry.branchTaken) { a.cmp(x86::eax, op == OpCode::OP_JMPF ? 1 : 0); emitGuard(x86::CondCode::kEqual, entry.pc); }
                else { a.cmp(x86::eax, op == OpCode::OP_JMPF ? 0 : 1); emitGuard(x86::CondCode::kEqual, entry.pc); }
                break;
            }
            case OpCode::OP_GET_FIELD: {
                loadRegAbs(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_GET_FIELD_INT: {
                loadRegAbs(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                a.mov(x86::ecx, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_GET_FIELD_DBL: {
                loadRegAbs(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 40 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_SET_FIELD: {
                loadRegAbs(B, x86::rdx); loadBoxed(A, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rdx, x86::rcx);
                if (C < 4) a.mov(x86::qword_ptr(x86::rdx, 40 + C * 8), x86::rax);
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 32)); a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8), x86::rax); }
                break;
            }
            case OpCode::OP_NEW_OBJ: {
                flushRegs(); a.mov(x86::ecx, (uint32_t)decodeBx(instr)); a.mov(x86::rdx, vmPtr);
                a.call((uint64_t)&createObjectHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_NEW_ARRAY: {
                flushRegs();
                loadRegAbs(B, x86::rcx); a.mov(x86::ecx, x86::ecx);
                a.mov(x86::edx, (uint32_t)C);
                a.call((uint64_t)&createArrayHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_IDX_GET:
            case OpCode::OP_IDX_GET_DBL:
            case OpCode::OP_IDX_GET_INT: {
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8)); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.call((uint64_t)&idxGetHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_DBL:
            case OpCode::OP_IDX_SET_INT: {
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8)); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8)); a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)(baseOff + A) * 8));
                a.call((uint64_t)&idxSetHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                break;
            }
            case OpCode::OP_COLL_LEN: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.call((uint64_t)&collLenHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_RET: { if (baseOff == 0) { loadBoxed(A, x86::rax); emitEpilogue(); } break; }
            case OpCode::OP_LOOP: a.jmp(loopEntry); break;
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL: {
                // Fast path: both operands unboxed int -> inline arithmetic
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    if (op == OpCode::OP_ADD) a.add(x86::eax, x86::ecx);
                    else if (op == OpCode::OP_SUB) a.sub(x86::eax, x86::ecx);
                    else a.imul(x86::eax, x86::ecx);
                    a.mov(x86::rdx, intTag); a.or_(x86::rax, x86::rdx);
                    storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true;
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call(op == OpCode::OP_ADD ? (uint64_t)&addHelper : (op == OpCode::OP_SUB ? (uint64_t)&subHelper : (uint64_t)&mulHelper));
                    storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false;
                }
                break;
            }
            case OpCode::OP_EQ: {
                loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                a.call((uint64_t)&eqHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_NEQ: {
                loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                a.call((uint64_t)&eqHelper);
                a.xor_(x86::rax, 1);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_LT: {
                loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                a.call((uint64_t)&ltHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_GT: {
                loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                a.call((uint64_t)&gtHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            case OpCode::OP_INC: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.inc(x86::eax);
                a.mov(x86::rdx, intTag); a.or_(x86::rax, x86::rdx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true; break;
            }
            case OpCode::OP_DEC: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.dec(x86::eax);
                a.mov(x86::rdx, intTag); a.or_(x86::rax, x86::rdx);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = true; break;
            }
            case OpCode::OP_NEG: {
                loadBoxed(B, x86::rcx);
                a.call((uint64_t)&negHelper);
                storeRegAbs(A, x86::rax); if (baseOff + A < NUM_VREGS) isUnboxed[baseOff + A] = false; break;
            }
            default: break;
        }
    };

    for (const auto& entry : trace.preamble) emitEntry(entry);
    a.bind(loopEntry);
    for (const auto& entry : trace.entries) emitEntry(entry);
    if (trace.entries.empty() || decodeOp(trace.entries.back().instr) != OpCode::OP_LOOP) a.jmp(loopEntry);

    a.bind(sideExitTrampoline);
    a.mov(x86::qword_ptr(x86::rsp, 48), x86::rax); 
    flushRegs();
    a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 48)); a.call((uint64_t)&sideExitDiagnostic);
    a.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
    a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
