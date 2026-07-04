#include "JITCompiler.h"
#include "vm/Trace.h"
#include "core/Value.h"
#include "core/Variable.h"
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
            case OpCode::OP_LOADK: {
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8));
                else { a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8)); a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADINT: {
                if (A < NUM_VREGS) a.mov(vRegs[A], intTag | (uint32_t)decodeSBx(instr));
                else { a.mov(x86::rax, intTag | (uint32_t)decodeSBx(instr)); a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADBOOL: {
                if (A < NUM_VREGS) a.mov(vRegs[A], boolTag | (B != 0 ? 1ULL : 0ULL));
                else { a.mov(x86::rax, boolTag | (B != 0 ? 1ULL : 0ULL)); a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADNULL: {
                if (A < NUM_VREGS) a.mov(vRegs[A], nullTag);
                else { a.mov(x86::rax, nullTag); a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADDBL: {
                iris::core::Value dv(iris::core::float16ToDouble((uint16_t)(instr & 0xFFFF)));
                if (A < NUM_VREGS) a.mov(vRegs[A], dv.bits);
                else { a.mov(x86::rax, dv.bits); a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_MOVE: {
                if (A < NUM_VREGS && B < NUM_VREGS) a.mov(vRegs[A], vRegs[B]);
                else { loadReg(B, x86::rax); storeReg(A, x86::rax); }
                break;
            }
            case OpCode::OP_MOVE_INT: { loadReg(B, x86::rax); a.and_(x86::eax, x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_GGLOB: { a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rax, (uint64_t)(instr & 0xFFFF) * sizeof(iris::core::Variable))); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADD_INT: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    if (A == C && A != B) {
                        a.mov(x86::r11d, vRegs[C].r32());
                        a.mov(vRegs[A], vRegs[B]);
                        a.add(vRegs[A], x86::r11);
                    } else {
                        if (A != B) a.mov(vRegs[A], vRegs[B]);
                        if (C < NUM_VREGS) a.mov(x86::r11d, vRegs[C].r32());
                        else a.mov(x86::r11d, x86::dword_ptr(rBase, (uint64_t)C * 8));
                        a.add(vRegs[A], x86::r11);
                    }
                } else {
                    loadReg(B, x86::rax);
                    if (C < NUM_VREGS) a.add(x86::eax, vRegs[C].r32());
                    else a.add(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                    a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                    storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_SUB_INT: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    if (A == C && A != B) {
                        a.mov(x86::r11d, vRegs[C].r32());
                        a.mov(vRegs[A].r32(), vRegs[B].r32());
                        a.sub(vRegs[A].r32(), x86::r11d);
                    } else {
                        if (A != B) a.mov(vRegs[A].r32(), vRegs[B].r32());
                        if (C < NUM_VREGS) a.sub(vRegs[A].r32(), vRegs[C].r32());
                        else a.sub(vRegs[A].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                    }
                    a.mov(x86::r11, intTag); a.or_(vRegs[A].r64(), x86::r11);
                } else {
                    loadReg(B, x86::rax);
                    if (C < NUM_VREGS) a.sub(x86::eax, vRegs[C].r32());
                    else a.sub(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                    a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                    storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_MUL_INT: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    if (A == C && A != B) {
                        a.mov(x86::r11d, vRegs[C].r32());
                        a.mov(vRegs[A].r32(), vRegs[B].r32());
                        a.imul(vRegs[A].r32(), x86::r11d);
                    } else {
                        if (A != B) a.mov(vRegs[A].r32(), vRegs[B].r32());
                        if (C < NUM_VREGS) a.imul(vRegs[A].r32(), vRegs[C].r32());
                        else a.imul(vRegs[A].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                    }
                    a.mov(x86::r11, intTag); a.or_(vRegs[A].r64(), x86::r11);
                } else {
                    loadReg(B, x86::rax);
                    if (C < NUM_VREGS) a.imul(x86::eax, vRegs[C].r32());
                    else a.imul(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                    a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                    storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_DIV_INT: {
                loadReg(B, x86::rax);
                loadReg(C, x86::rcx);
                a.movsxd(x86::rax, x86::eax);
                a.movsxd(x86::rcx, x86::ecx);
                a.cdq();
                a.idiv(x86::ecx);
                a.mov(x86::rcx, intTag);
                a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax);
                break;
            }
            case OpCode::OP_ADDI: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    if (A != B) a.mov(vRegs[A], vRegs[B]);
                    a.mov(x86::r11d, (int32_t)(int8_t)C);
                    a.add(vRegs[A], x86::r11);
                } else {
                    loadReg(B, x86::rax); a.add(x86::eax, (int32_t)(int8_t)C); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_SUBI: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    if (A != B) a.mov(vRegs[A].r32(), vRegs[B].r32());
                    a.sub(vRegs[A].r32(), (int32_t)(int8_t)C);
                    a.mov(x86::r11, intTag); a.or_(vRegs[A].r64(), x86::r11);
                } else {
                    loadReg(B, x86::rax); a.sub(x86::eax, (int32_t)(int8_t)C); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_INC: {
                if (A < NUM_VREGS) {
                    a.inc(vRegs[A]);
                } else {
                    loadReg(A, x86::rax); a.inc(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_DEC: {
                if (A < NUM_VREGS) {
                    a.dec(vRegs[A].r32());
                    a.mov(x86::r11, intTag); a.or_(vRegs[A].r64(), x86::r11);
                } else {
                    loadReg(A, x86::rax); a.dec(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_BIT_XOR: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.xor_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_BIT_AND: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.and_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_BIT_OR:  { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.or_(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SHL: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.shl(x86::eax, x86::cl); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_SHR: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.shr(x86::eax, x86::cl); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADDI_W: {
                if (A < NUM_VREGS) {
                    a.mov(x86::r11d, (int32_t)(decodeBx(instr) - 32767));
                    a.add(vRegs[A], x86::r11);
                } else {
                    loadReg(A, x86::rax); a.add(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_SUBI_W: {
                if (A < NUM_VREGS) {
                    a.sub(vRegs[A].r32(), (int32_t)(decodeBx(instr) - 32767));
                    a.mov(x86::r11, intTag); a.or_(vRegs[A].r64(), x86::r11);
                } else {
                    loadReg(A, x86::rax); a.sub(x86::eax, (int32_t)(decodeBx(instr) - 32767)); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_NOT: { loadReg(B, x86::rax); a.xor_(x86::rax, 1); storeReg(A, x86::rax); break; }
            case OpCode::OP_ADD_DOUBLE:
            case OpCode::OP_SUB_DOUBLE:
            case OpCode::OP_MUL_DOUBLE:
            case OpCode::OP_DIV_DOUBLE: {
                Label L_bInt = a.new_label(), L_bLoad = a.new_label();
                Label L_cInt = a.new_label(), L_doOp = a.new_label();
                uint16_t intPrefix = (uint16_t)(intTag >> 48);
                // --- Load B into xmm0 ---
                loadReg(B, x86::rax);
                a.mov(x86::r11, x86::rax); a.shr(x86::r11, 48);
                a.cmp(x86::r11w, intPrefix);
                a.je(L_bInt);
                a.movq(x86::xmm0, x86::rax); a.jmp(L_bLoad);
                a.bind(L_bInt);
                a.and_(x86::eax, x86::eax); a.cvtsi2sd(x86::xmm0, x86::eax);
                a.bind(L_bLoad);
                // --- Load C into xmm1 ---
                loadReg(C, x86::rax);
                a.mov(x86::r11, x86::rax); a.shr(x86::r11, 48);
                a.cmp(x86::r11w, intPrefix);
                a.je(L_cInt);
                a.movq(x86::xmm1, x86::rax); a.jmp(L_doOp);
                a.bind(L_cInt);
                a.and_(x86::eax, x86::eax); a.cvtsi2sd(x86::xmm1, x86::eax);
                a.bind(L_doOp);
                // --- SSE operation ---
                if (op == OpCode::OP_ADD_DOUBLE) a.addsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_SUB_DOUBLE) a.subsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_MUL_DOUBLE) a.mulsd(x86::xmm0, x86::xmm1);
                else a.divsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); break;
            }
            case OpCode::OP_LT_INT: {
                if (B < NUM_VREGS && C < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), vRegs[C].r32());
                } else if (B < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                } else if (C < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)B * 8), vRegs[C].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                }
                a.setl(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break;
            }
            case OpCode::OP_GT_INT: {
                if (B < NUM_VREGS && C < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), vRegs[C].r32());
                } else if (B < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                } else if (C < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)B * 8), vRegs[C].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                }
                a.setg(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break;
            }
            case OpCode::OP_JMPF: {
                if (A < NUM_VREGS) { a.test(vRegs[A].r32(), 1); a.je(labels[i + 1 + decodeSBx(instr)]); }
                else { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 0); a.je(labels[i + 1 + decodeSBx(instr)]); }
                break;
            }
            case OpCode::OP_JMPT: {
                if (A < NUM_VREGS) { a.test(vRegs[A].r32(), 1); a.jne(labels[i + 1 + decodeSBx(instr)]); }
                else { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 1); a.je(labels[i + 1 + decodeSBx(instr)]); }
                break;
            }
            case OpCode::OP_GET_FIELD: { loadReg(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeReg(A, x86::rax); break; }
            case OpCode::OP_GET_FIELD_INT: { loadReg(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                a.mov(x86::ecx, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); break; }
            case OpCode::OP_GET_FIELD_DBL: { loadReg(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8)); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeReg(A, x86::rax); break; }
            case OpCode::OP_SET_FIELD: { loadReg(B, x86::rdx); a.shl(x86::rdx, 16); a.shr(x86::rdx, 16); loadReg(A, x86::rax);
                a.mov(x86::byte_ptr(x86::rdx, offsetof(iris::core::Managed, dirty)), 1);
                if (C < 4) { a.mov(x86::qword_ptr(x86::rdx, 32 + C * 8), x86::rax); }
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 24)); a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8), x86::rax); }
                break; }
            case OpCode::OP_INVOKE: {
                // Dynamic invoke: B = nameId (constant pool index), C = argCount
                flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8)); a.mov(x86::edx, (uint32_t)B); a.mov(x86::r8d, (uint32_t)C);
                a.mov(x86::r9, constants); a.mov(x86::rax, vmPtr); a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
                a.call((uint64_t)&invokeHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                break;
            }
            case OpCode::OP_INVOKE_MONO: {
                // INVOKE_MONO: B = (cacheIdx >> 8) & 0xFF, C = cacheIdx & 0xFF
                uint16_t cacheIdx = ((uint16_t)B << 8) | C;
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.mov(x86::edx, (uint32_t)cacheIdx);
                a.mov(x86::r8, constants); a.mov(x86::r9, vmPtr);
                a.mov(x86::rax, (uint64_t)&chunk); a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
                a.call((uint64_t)&invokeMonoHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                break;
            }
            case OpCode::OP_TYPECHECK: {
                // Type check is currently a no-op in the VM (placeholder for future use)
                break;
            }
            case OpCode::OP_CALL:
            case OpCode::OP_TAILCALL: {
                flushRegs(); a.mov(x86::rcx, (uint64_t)B); a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)A * 8)); a.mov(x86::r8, vmPtr);
                a.call((uint64_t)&callFunctionHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                if (op == OpCode::OP_TAILCALL) emitEpilogue();
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
            case OpCode::OP_IDX_GET: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();
                Label L_value_path = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Check if elemType is UNTYPED (0) or VALUE (3)
                a.movzx(x86::eax, x86::byte_ptr(x86::r10, offsetof(iris::core::ArrayData, elemType)));
                a.cmp(x86::al, 0);
                a.je(L_value_path);
                a.cmp(x86::al, 3);
                a.jne(L_slow);

                a.bind(L_value_path);
                // 3. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 4. Load Value element
                a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)));
                storeReg(A, x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.call((uint64_t)&idxGetHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeReg(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load int element
                a.mov(x86::r9d, x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)));

                // 4. Construct Value (QNAN | TAG_INT | r9d)
                a.mov(x86::rax, 0x7FF8000000000000ULL);
                a.or_(x86::rax, x86::r9);
                storeReg(A, x86::rax);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.call((uint64_t)&idxGetIntHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeReg(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load double element
                a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)));

                // 4. Construct Value (ensure canonical NaN if bits are NaN)
                a.mov(x86::rax, x86::r9);
                a.mov(x86::r11, 0x7FF8000000000000ULL);
                a.and_(x86::rax, x86::r11);
                a.cmp(x86::rax, x86::r11);
                a.cmove(x86::r9, x86::r11);

                storeReg(A, x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.call((uint64_t)&idxGetDblHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeReg(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();
                Label L_value_path = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Check if elemType is UNTYPED (0) or VALUE (3)
                a.movzx(x86::eax, x86::byte_ptr(x86::r10, offsetof(iris::core::ArrayData, elemType)));
                a.cmp(x86::al, 0);
                a.je(L_value_path);
                a.cmp(x86::al, 3);
                a.jne(L_slow);

                a.bind(L_value_path);
                // 3. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 4. Load value to set from A
                loadReg(A, x86::r9);

                // 5. Store element
                a.mov(x86::byte_ptr(x86::r10, offsetof(iris::core::Managed, dirty)), 1);
                a.mov(x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)), x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.call((uint64_t)&idxSetHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET_INT: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load value to set from A
                loadReg(A, x86::r9);

                // 4. Store int element
                a.mov(x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)), x86::r9d);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.call((uint64_t)&idxSetIntHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET_DBL: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadReg(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadReg(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load value to set from A
                loadReg(A, x86::r9);

                // Verify the value is indeed a double (expon != 0x7FF)
                a.mov(x86::rax, x86::r9);
                a.shr(x86::rax, 52);
                a.and_(x86::rax, 0x7FF);
                a.cmp(x86::rax, 0x7FF);
                a.je(L_slow);

                // 4. Store double element
                a.mov(x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)), x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)C * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.call((uint64_t)&idxSetDblHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_COLL_LEN: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)B * 8));
                a.call((uint64_t)&collLenHelper);
                if (A < NUM_VREGS) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, (uint64_t)A * 8), x86::rax);
                break;
            }
            case OpCode::OP_ADD: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();

                loadReg(B, x86::rcx); loadReg(C, x86::rdx);

                // B is int?
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                // B is double?
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);

                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);

                a.bind(L_C_int);
                a.add(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);

                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.addsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);

                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.addsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);

                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.addsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);

                a.bind(L_helper);
                a.call((uint64_t)&addHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_SUB: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); loadReg(C, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.sub(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.subsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.subsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.subsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&subHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_MUL: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); loadReg(C, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.imul(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.mulsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.mulsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.mulsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&mulHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_EQ:
            case OpCode::OP_NEQ: {
                // Inline int+int, double+double, int+double, double+int
                // Other type combinations fall through to eqHelper
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                bool neg = (op == OpCode::OP_NEQ);
                loadReg(B, x86::rcx); loadReg(C, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.cmp(x86::r8d, x86::edx);
                a.sete(x86::al); a.movzx(x86::eax, x86::al);
                if (neg) a.xor_(x86::eax, 1);
                a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                a.movzx(x86::eax, x86::al);
                if (neg) a.xor_(x86::eax, 1);
                a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                a.movzx(x86::eax, x86::al);
                if (neg) a.xor_(x86::eax, 1);
                a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.cvtsi2sd(x86::xmm1, x86::edx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                a.movzx(x86::eax, x86::al);
                if (neg) a.xor_(x86::eax, 1);
                a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&eqHelper);
                if (neg) a.xor_(x86::rax, 1);
                storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_LE:
            case OpCode::OP_GE:
            case OpCode::OP_LT_DBL:
            case OpCode::OP_GT_DBL:
            case OpCode::OP_LE_DBL:
            case OpCode::OP_GE_DBL: {
                bool swap = (op == OpCode::OP_GT || op == OpCode::OP_LE ||
                             op == OpCode::OP_GT_DBL || op == OpCode::OP_LE_DBL);
                bool neg = (op == OpCode::OP_LE || op == OpCode::OP_GE ||
                            op == OpCode::OP_LE_DBL || op == OpCode::OP_GE_DBL);
                int leftReg = swap ? C : B;
                int rightReg = swap ? B : C;
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_L_int = a.new_label(), L_L_double = a.new_label();
                Label L_R_int = a.new_label(), L_R_double = a.new_label(), L_R_int_in_dbl = a.new_label();
                loadReg(leftReg, x86::rcx); loadReg(rightReg, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_L_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_L_double);
                a.jmp(L_helper);
                a.bind(L_L_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_R_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_R_double);
                a.jmp(L_helper);
                a.bind(L_R_int);
                a.cmp(x86::r8d, x86::edx); a.setl(x86::al);
                if (neg) a.xor_(x86::al, 1);
                a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_R_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                if (neg) a.xor_(x86::al, 1);
                a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_L_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_R_int_in_dbl);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                if (neg) a.xor_(x86::al, 1);
                a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_R_int_in_dbl);
                a.cvtsi2sd(x86::xmm1, x86::edx);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                if (neg) a.xor_(x86::al, 1);
                a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&ltHelper);
                if (neg) a.xor_(x86::rax, 1);
                storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_NEG: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_int = a.new_label(), L_double = a.new_label(), L_bool = a.new_label();
                loadReg(B, x86::rcx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_double);
                // NaN-boxed: check bool tag
                a.mov(x86::rax, x86::rcx); a.and_(x86::eax, 0xFFFF);
                a.cmp(x86::eax, 1); a.je(L_bool);
                a.jmp(L_helper);
                a.bind(L_int);
                a.mov(x86::r8d, x86::ecx); a.neg(x86::r8d);
                a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_double);
                a.mov(x86::rax, 0x8000000000000000ULL);
                a.movq(x86::xmm0, x86::rcx); a.movq(x86::xmm1, x86::rax);
                a.xorpd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_bool);
                a.mov(x86::eax, x86::ecx); a.xor_(x86::eax, 1);
                a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&negHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_DIV: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); loadReg(C, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                // int/int division using signed integer division
                a.movsxd(x86::rax, x86::r8d); a.movsxd(x86::rcx, x86::edx);
                a.cdq(); a.idiv(x86::ecx);
                a.mov(x86::r8d, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.divsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.divsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.divsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&divHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_MOD: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_C_int = a.new_label();
                loadReg(B, x86::rcx); loadReg(C, x86::rdx);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.movsxd(x86::rax, x86::r8d); a.movsxd(x86::rcx, x86::edx);
                a.cdq(); a.idiv(x86::ecx); // remainder in edx
                a.mov(x86::r8d, x86::edx); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&modHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_JLT_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.jl(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            case OpCode::OP_JGT_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.jg(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            case OpCode::OP_JLE_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.jle(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            case OpCode::OP_JGE_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.jge(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            case OpCode::OP_JEQ_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.je(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            case OpCode::OP_JNE_INT_IMM: {
                int32_t targetOffset = (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767;
                loadReg(A, x86::rax);
                a.cmp(x86::eax, decodeSBx(instr));
                a.jne(labels[i + 2 + targetOffset]);
                i++;
                break;
            }
            // Boolean logic
            case OpCode::OP_AND: { loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.and_(x86::ecx, x86::edx); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            case OpCode::OP_OR:  { loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.or_(x86::ecx, x86::edx); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
            // Integer comparisons (register result)
            case OpCode::OP_LE_INT: {
                if (B < NUM_VREGS && C < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), vRegs[C].r32());
                } else if (B < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                } else if (C < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)B * 8), vRegs[C].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                }
                a.setle(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break;
            }
            case OpCode::OP_GE_INT: {
                if (B < NUM_VREGS && C < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), vRegs[C].r32());
                } else if (B < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                } else if (C < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)B * 8), vRegs[C].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                }
                a.setge(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break;
            }
            case OpCode::OP_EQ_INT: {
                if (B < NUM_VREGS && C < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), vRegs[C].r32());
                } else if (B < NUM_VREGS) {
                    a.cmp(vRegs[B].r32(), x86::dword_ptr(rBase, (uint64_t)C * 8));
                } else if (C < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)B * 8), vRegs[C].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)C * 8));
                }
                a.sete(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break;
            }
            case OpCode::OP_EQ_DBL: { loadReg(B, x86::rcx); loadReg(C, x86::rdx); a.call((uint64_t)&eqHelper); storeReg(A, x86::rax); break; }
            // Global store/delete
            case OpCode::OP_SGLOB: { flushRegs(); loadReg(A, x86::rax); a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 32)); a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(instr & 0xFFFF) * sizeof(iris::core::Variable)), x86::rax); break; }
            case OpCode::OP_DGLOB: {
                uint16_t slot = ((uint16_t)B << 8) | C;
                flushRegs();
                loadReg(A, x86::rax);
                a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 32));
                a.mov(x86::qword_ptr(x86::rcx, (uint64_t)slot * sizeof(iris::core::Variable)), x86::rax);
                a.mov(x86::byte_ptr(x86::rcx, (uint64_t)slot * sizeof(iris::core::Variable) + 8), 1);
                break;
            }
            case OpCode::OP_JLT_INT:
            case OpCode::OP_JGT_INT:
            case OpCode::OP_JLE_INT:
            case OpCode::OP_JGE_INT:
            case OpCode::OP_JNE_INT: {
                if (A < NUM_VREGS && B < NUM_VREGS) {
                    a.cmp(vRegs[A].r32(), vRegs[B].r32());
                } else if (A < NUM_VREGS) {
                    a.cmp(vRegs[A].r32(), x86::dword_ptr(rBase, (uint64_t)B * 8));
                } else if (B < NUM_VREGS) {
                    a.cmp(x86::dword_ptr(rBase, (uint64_t)A * 8), vRegs[B].r32());
                } else {
                    a.mov(x86::eax, x86::dword_ptr(rBase, (uint64_t)A * 8));
                    a.cmp(x86::eax, x86::dword_ptr(rBase, (uint64_t)B * 8));
                }
                if (op == OpCode::OP_JLT_INT) a.jl(labels[i + 1 + (int8_t)C]);
                else if (op == OpCode::OP_JGT_INT) a.jg(labels[i + 1 + (int8_t)C]);
                else if (op == OpCode::OP_JLE_INT) a.jle(labels[i + 1 + (int8_t)C]);
                else if (op == OpCode::OP_JGE_INT) a.jge(labels[i + 1 + (int8_t)C]);
                else a.jne(labels[i + 1 + (int8_t)C]);
                break;
            }
            // K operations (constant fused)
            case OpCode::OP_ADD_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.add(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.addsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.addsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.addsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&addHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_SUB_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.sub(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.subsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.subsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.subsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&subHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_MUL_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.imul(x86::r8d, x86::edx); a.mov(x86::rax, intTag);
                a.or_(x86::rax, x86::r8); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.mulsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.mulsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.mulsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&mulHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_DIV_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.movsxd(x86::rax, x86::r8d); a.movsxd(x86::rcx, x86::edx);
                a.cdq(); a.idiv(x86::ecx);
                a.mov(x86::r8d, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.divsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.divsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.divsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0);
                storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&divHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_LT_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.cmp(x86::r8d, x86::edx); a.setl(x86::al); a.movzx(x86::eax, x86::al);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1); a.setb(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.ucomisd(x86::xmm0, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.setb(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.ucomisd(x86::xmm0, x86::xmm1); a.setb(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&ltHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_GT_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.cmp(x86::r8d, x86::edx); a.setg(x86::al); a.movzx(x86::eax, x86::al);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1); a.seta(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.ucomisd(x86::xmm0, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.seta(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.ucomisd(x86::xmm0, x86::xmm1); a.seta(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&gtHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_EQ_K: {
                Label L_done = a.new_label(), L_helper = a.new_label();
                Label L_B_int = a.new_label(), L_B_double = a.new_label();
                Label L_C_int = a.new_label(), L_C_double = a.new_label(), L_C_int_dblB = a.new_label();
                loadReg(B, x86::rcx); a.mov(x86::rdx, x86::qword_ptr(constants, (uint64_t)C * 8));
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_B_int);
                a.mov(x86::rax, x86::rcx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_B_double);
                a.jmp(L_helper);
                a.bind(L_B_int);
                a.mov(x86::r8d, x86::ecx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.jne(L_C_double);
                a.jmp(L_helper);
                a.bind(L_C_int);
                a.cmp(x86::r8d, x86::edx); a.sete(x86::al); a.movzx(x86::eax, x86::al);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_double);
                a.cvtsi2sd(x86::xmm0, x86::r8d); a.movq(x86::xmm1, x86::rdx);
                a.ucomisd(x86::xmm0, x86::xmm1); a.sete(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_B_double);
                a.movq(x86::xmm0, x86::rcx);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 48);
                a.cmp(x86::ax, (uint16_t)(intTag >> 48)); a.je(L_C_int_dblB);
                a.mov(x86::rax, x86::rdx); a.shr(x86::rax, 52);
                a.and_(x86::eax, 0x7FF); a.cmp(x86::eax, 0x7FF); a.je(L_helper);
                a.movq(x86::xmm1, x86::rdx); a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_C_int_dblB);
                a.mov(x86::r8d, x86::edx); a.cvtsi2sd(x86::xmm1, x86::r8d);
                a.ucomisd(x86::xmm0, x86::xmm1); a.sete(x86::al); a.setnp(x86::cl); a.and_(x86::eax, x86::ecx);
                a.or_(x86::rax, boolTag); storeReg(A, x86::rax); a.jmp(L_done);
                a.bind(L_helper);
                a.call((uint64_t)&eqHelper); storeReg(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_LOG: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.call((uint64_t)&logHelper); break;
            }
            case OpCode::OP_WAIT: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.mov(x86::rdx, vmPtr);
                a.call((uint64_t)&waitHelper); break;
            }
            case OpCode::OP_TAIL_INVOKE: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.mov(x86::edx, (uint32_t)B);
                a.mov(x86::r8d, (uint32_t)C);
                a.mov(x86::r9, constants);
                a.mov(x86::rax, vmPtr);
                a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
                a.call((uint64_t)&tailInvokeHelper);
                for (int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                a.mov(x86::rax, x86::qword_ptr(rBase, 0));
                emitEpilogue(); break;
            }
            case OpCode::OP_PUSH_HANDLER: {
                flushRegs();
                a.mov(x86::rcx, vmPtr);
                a.mov(x86::edx, (uint32_t)i);
                a.mov(x86::r8d, instr);
                a.mov(x86::r9d, (uint32_t)A);
                a.call((uint64_t)&pushHandlerHelper); break;
            }
            case OpCode::OP_POP_HANDLER: {
                flushRegs();
                a.mov(x86::rcx, vmPtr);
                a.call((uint64_t)&popHandlerHelper); break;
            }
            case OpCode::OP_THROW: {
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                a.mov(x86::rdx, vmPtr);
                a.call((uint64_t)&throwHelper);
                emitEpilogue(); break;
            }
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
            case OpCode::OP_LOADDBL:
            case OpCode::OP_GGLOB:
            case OpCode::OP_NOT:
            case OpCode::OP_AND:
            case OpCode::OP_OR:
            case OpCode::OP_MOVE:
            case OpCode::OP_MOVE_INT:
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT:
            case OpCode::OP_DIV_INT:
            case OpCode::OP_ADD_DOUBLE:
            case OpCode::OP_SUB_DOUBLE:
            case OpCode::OP_MUL_DOUBLE:
            case OpCode::OP_DIV_DOUBLE:
            case OpCode::OP_ADDI:
            case OpCode::OP_SUBI:
            case OpCode::OP_LE_INT:
            case OpCode::OP_GE_INT:
            case OpCode::OP_EQ_INT:
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
            case OpCode::OP_LE:
            case OpCode::OP_GE:
            case OpCode::OP_LT_DBL:
            case OpCode::OP_GT_DBL:
            case OpCode::OP_LE_DBL:
            case OpCode::OP_GE_DBL:
            case OpCode::OP_DIV:
            case OpCode::OP_MOD:
            case OpCode::OP_BIT_AND:
            case OpCode::OP_BIT_OR:
            case OpCode::OP_BIT_XOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR:
            case OpCode::OP_JLT_INT:
            case OpCode::OP_JGT_INT:
            case OpCode::OP_JLE_INT:
            case OpCode::OP_JGE_INT:
            case OpCode::OP_JNE_INT:
            case OpCode::OP_SGLOB:
            case OpCode::OP_DGLOB:
            case OpCode::OP_ADDI_W:
            case OpCode::OP_SUBI_W:
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
            case OpCode::OP_DIV_K:
            case OpCode::OP_EQ_K:
            case OpCode::OP_COUNT:
                return true;
            case OpCode::OP_RET:
                return baseOff == 0;
            default:
                return false;
        }
    };

    for (const auto& entry : trace.preamble) {
        OpCode op = decodeOp(entry.instr);
        if (!isSupported(op, entry.registerBaseOffset)) { return nullptr; }
    }
    for (size_t eidx = 0; eidx < trace.entries.size(); eidx++) {
        const auto& entry = trace.entries[eidx];
        OpCode op = decodeOp(entry.instr);
        if (!isSupported(op, entry.registerBaseOffset)) {
            return nullptr;
        }
    }

    // SSA prepass: build live interval info per abs value.
    // For each abs (virtual register), we track first def and last use index.
    // This enables smart reload: after a C helper call, only restore abs values
    // that are actually still live.
    struct AbsInfo { int firstDef = -1; int lastUse = -1; };
    std::unordered_map<int, AbsInfo> absInfo;
    auto processEntry = [&](const Trace::Entry& entry, int idx) {
        uint32_t instr = entry.instr;
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr), B = decodeB(instr), C = decodeC(instr);
        int bo = entry.registerBaseOffset;
        // Determine which abs values are read (used) and which are written (defined)
        int absB = bo + B, absA = bo + A, absC = bo + C;
        // Abs is "defined" if it's the destination (except non-producing ops)
        bool defA = true;
        switch (op) {
            case OpCode::OP_SGLOB: case OpCode::OP_DGLOB: case OpCode::OP_JMPF:
            case OpCode::OP_JMPT: case OpCode::OP_RET: case OpCode::OP_LOOP:
            case OpCode::OP_SET_FIELD: case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_INT: case OpCode::OP_IDX_SET_DBL:
            case OpCode::OP_JLT_INT: case OpCode::OP_JGT_INT:
            case OpCode::OP_JLE_INT: case OpCode::OP_JGE_INT:
            case OpCode::OP_JNE_INT: case OpCode::OP_COUNT:
                defA = false; break;
            default: break;
        }
        // B is always read (or just used as position); except some ops
        // C is read by most ops except immediate-only ones
        bool readC = true;
        switch (op) {
            case OpCode::OP_LOADK: case OpCode::OP_LOADINT: case OpCode::OP_LOADBOOL:
            case OpCode::OP_LOADNULL: case OpCode::OP_LOADDBL: case OpCode::OP_GGLOB:
            case OpCode::OP_LOOP: case OpCode::OP_COUNT: case OpCode::OP_INC:
            case OpCode::OP_DEC: case OpCode::OP_NEG: case OpCode::OP_NOT:
            case OpCode::OP_MOVE: case OpCode::OP_RET: case OpCode::OP_NEW_OBJ:
            case OpCode::OP_SGLOB: case OpCode::OP_DGLOB:
            case OpCode::OP_ADDI: case OpCode::OP_SUBI:
            case OpCode::OP_ADDI_W: case OpCode::OP_SUBI_W:
            case OpCode::OP_JMPF: case OpCode::OP_JMPT:
            case OpCode::OP_NEW_ARRAY:
                readC = false; break;
            default: break;
        }
        if (defA) { absInfo[absA].firstDef = idx; absInfo[absA].lastUse = idx; }
        absInfo[absB].lastUse = idx;
        if (readC) absInfo[absC].lastUse = idx;
    };
    int totalEntries = (int)(trace.preamble.size() + trace.entries.size());
    for (int i = 0; i < (int)trace.preamble.size(); i++) processEntry(trace.preamble[i], i);
    for (int i = 0; i < (int)trace.entries.size(); i++) processEntry(trace.entries[i], (int)trace.preamble.size() + i);
    // Precompute isLiveAfter[i] for each abs and instruction index
    // For smart reload: if abs is live-after at instruction idx, it needs restoring after a call at that point
    // Helper: check if a VM register slot (abs) is live after a given instruction index
    auto isLiveAfter = [&](int abs, int instrIdx) -> bool {
        auto it = absInfo.find(abs);
        return it != absInfo.end() && it->second.lastUse > instrIdx;
    };

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
    std::vector<bool> isPointer(NUM_VREGS, false);
    std::vector<bool> dirty(NUM_VREGS, false);

    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint16_t intPrefix = (uint16_t)(intTag >> 48);
    uint64_t ptrTag = iris::core::Value::QNAN | iris::core::Value::TAG_PTR;
    uint16_t ptrPrefix = (uint16_t)(ptrTag >> 48);
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;

    for(int i = 0; i < NUM_VREGS; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    Label loopEntry = a.new_label(); 

    auto flushRegs = [&]() {
        for(int i = 0; i < NUM_VREGS; i++) {
            if (dirty[i]) {
                if (isUnboxed[i]) {
                    a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, (uint64_t)i * 8), x86::rax);
                } else { a.mov(x86::qword_ptr(rBase, (uint64_t)i * 8), vRegs[i]); }
                dirty[i] = false;
            }
        }
    };
    auto emitEpilogue = [&]() { flushRegs(); a.xor_(x86::eax, x86::eax); a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };

    for (int i = 0; i < NUM_VREGS; i++) {
        if (trace.initialTypes[i] == intPrefix) {
            a.mov(x86::rax, vRegs[i]); a.shr(x86::rax, 48); a.cmp(x86::ax, intPrefix);
            emitGuard(x86::CondCode::kEqual, trace.startPC);
            a.mov(vRegs[i].r32(), vRegs[i].r32()); isUnboxed[i] = true;
        }
        if (trace.initialTypes[i] == ptrPrefix) {
            a.mov(x86::rax, vRegs[i]); a.shr(x86::rax, 48); a.cmp(x86::ax, ptrPrefix);
            emitGuard(x86::CondCode::kEqual, trace.startPC);
        }
    }

    auto emitEntry = [&](const Trace::Entry& entry, int instrIdx) {
        (void)instrIdx;
        uint32_t instr = entry.instr; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        (void)entry;
        int baseOff = entry.registerBaseOffset;

        auto loadRegAbs = [&](uint8_t reg, x86::Gp dest) {
            int abs = baseOff + reg;
            if (abs < NUM_VREGS) a.mov(dest, vRegs[abs]);
            else a.mov(dest, x86::qword_ptr(rBase, (uint64_t)abs * 8));
        };
        auto storeRegAbs = [&](uint8_t reg, x86::Gp src) {
            int abs = baseOff + reg;
            if (abs < NUM_VREGS) { a.mov(vRegs[abs], src); dirty[abs] = true; isUnboxed[abs] = false; }
            else a.mov(x86::qword_ptr(rBase, (uint64_t)abs * 8), src);
        };
        auto isUnboxedAbs = [&](uint8_t reg) { int abs = baseOff + reg; return (abs < NUM_VREGS && isUnboxed[abs]); };
        auto loadBoxed = [&](uint8_t reg, x86::Gp dest) { loadRegAbs(reg, dest); if (isUnboxedAbs(reg)) { a.mov(x86::r11, intTag); a.or_(dest, x86::r11); } };
        auto storeUnboxed = [&](uint8_t reg, x86::Gp src32) {
            int abs = baseOff + reg;
            if (abs < NUM_VREGS) {
                a.mov(vRegs[abs].r32(), src32.r32());
                isUnboxed[abs] = true; dirty[abs] = true;
            } else {
                a.mov(x86::rax, intTag); a.or_(x86::rax, src32);
                a.mov(x86::qword_ptr(rBase, (uint64_t)abs * 8), x86::rax);
            }
        };

        switch (op) {
            case OpCode::OP_LOADK:
                a.mov(x86::rax, x86::qword_ptr(x86::rsi, (uint64_t)(instr & 0xFFFF) * 8));
                storeRegAbs(A, x86::rax); break;
            case OpCode::OP_LOADINT:
                a.mov(x86::eax, (uint32_t)decodeSBx(instr));
                storeUnboxed(A, x86::eax); break;
            case OpCode::OP_LOADBOOL:
                a.mov(x86::rax, boolTag | (B != 0 ? 1ULL : 0ULL));
                storeRegAbs(A, x86::rax); break;
            case OpCode::OP_LOADNULL:
                a.mov(x86::rax, (uint64_t)(iris::core::Value::QNAN | iris::core::Value::TAG_NULL));
                storeRegAbs(A, x86::rax); break;
            case OpCode::OP_LOADDBL: {
                iris::core::Value dv(iris::core::float16ToDouble((uint16_t)(instr & 0xFFFF)));
                a.mov(x86::rax, dv.bits);
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_GGLOB: {
                a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 40));
                a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(instr & 0xFFFF) * sizeof(iris::core::Variable)));
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_SGLOB: {
                // Direct store to globals array
                loadRegAbs(A, x86::rax);
                a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 40));
                a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(instr & 0xFFFF) * sizeof(iris::core::Variable)), x86::rax); break;
            }
            case OpCode::OP_DGLOB: {
                uint16_t slot = ((uint16_t)B << 8) | C;
                loadRegAbs(A, x86::rax);
                a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 40));
                a.mov(x86::qword_ptr(x86::rcx, (uint64_t)slot * sizeof(iris::core::Variable)), x86::rax);
                a.mov(x86::byte_ptr(x86::rcx, (uint64_t)slot * sizeof(iris::core::Variable) + 8), 1);
                break;
            }
            case OpCode::OP_NOT: {
                if (isUnboxedAbs(B)) {
                    loadRegAbs(B, x86::eax); a.xor_(x86::eax, 1);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadRegAbs(B, x86::rax); a.xor_(x86::rax, 1);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_AND:
            case OpCode::OP_OR: {
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::eax); loadRegAbs(C, x86::ecx);
                    if (op == OpCode::OP_AND) a.and_(x86::eax, x86::ecx);
                    else a.or_(x86::eax, x86::ecx);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    a.and_(x86::eax, x86::ecx);
                    if (op == OpCode::OP_OR) a.or_(x86::eax, x86::ecx);
                    a.mov(x86::r11, intTag); a.or_(x86::rax, x86::r11);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_MOVE: {
                if (isUnboxedAbs(B)) {
                    loadRegAbs(B, x86::eax);
                    int absA = baseOff + A;
                    if (absA < NUM_VREGS) {
                        storeUnboxed(A, x86::eax);
                    } else {
                        a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                        a.mov(x86::qword_ptr(rBase, (uint64_t)absA * 8), x86::rax);
                    }
                } else {
                    loadRegAbs(B, x86::rax); storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT: {
                loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF);
                if (op == OpCode::OP_ADD_INT) a.add(x86::eax, x86::ecx);
                else if (op == OpCode::OP_SUB_INT) a.sub(x86::eax, x86::ecx);
                else a.imul(x86::eax, x86::ecx);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_ADDI: {
                loadRegAbs(B, x86::rax); if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                a.add(x86::eax, (int32_t)(int8_t)C);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_SUBI: {
                loadRegAbs(B, x86::rax); if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                a.sub(x86::eax, (int32_t)(int8_t)C);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_ADDI_W: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.add(x86::eax, (int32_t)(decodeBx(instr) - 32767));
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_SUBI_W: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.sub(x86::eax, (int32_t)(decodeBx(instr) - 32767));
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_LT_INT:
            case OpCode::OP_GT_INT:
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K: {
                loadRegAbs(B, x86::rax); if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (op == OpCode::OP_LT_K || op == OpCode::OP_GT_K) {
                    a.mov(x86::rcx, x86::qword_ptr(x86::rsi, (uint64_t)C * 8)); a.and_(x86::rcx, 0xFFFFFFFF);
                } else { loadRegAbs(C, x86::rcx); if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF); }
                a.cmp(x86::eax, x86::ecx);
                x86::CondCode cond = (op == OpCode::OP_LT_INT || op == OpCode::OP_LT_K) ? x86::CondCode::kSignedLT : x86::CondCode::kSignedGT;
                a.set(cond, x86::al); a.movzx(x86::eax, x86::al);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_JMPF:
            case OpCode::OP_JMPT: {
                bool opIsF = op == OpCode::OP_JMPF;
                if (isUnboxedAbs(A)) {
                    loadRegAbs(A, x86::eax);
                    if (!entry.branchTaken) { a.cmp(x86::eax, 0); emitGuard(opIsF ? x86::CondCode::kNotEqual : x86::CondCode::kEqual, entry.pc); }
                    else { a.cmp(x86::eax, 0); emitGuard(opIsF ? x86::CondCode::kEqual : x86::CondCode::kNotEqual, entry.pc); }
                } else {
                    loadRegAbs(A, x86::rax); a.and_(x86::eax, 1);
                    if (!entry.branchTaken) { a.cmp(x86::eax, opIsF ? 1 : 0); emitGuard(x86::CondCode::kEqual, entry.pc); }
                    else { a.cmp(x86::eax, opIsF ? 0 : 1); emitGuard(x86::CondCode::kEqual, entry.pc); }
                }
                break;
            }
            case OpCode::OP_GET_FIELD: {
                loadRegAbs(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_GET_FIELD_INT: {
                loadRegAbs(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                a.mov(x86::ecx, x86::eax); a.mov(x86::rax, intTag); a.or_(x86::rax, x86::rcx);
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_GET_FIELD_DBL: {
                loadRegAbs(B, x86::rax); a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                if (C < 4) a.mov(x86::rax, x86::qword_ptr(x86::rax, 32 + C * 8));
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 24)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8)); }
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_SET_FIELD: {
                loadRegAbs(B, x86::rdx); loadBoxed(A, x86::rax); a.shl(x86::rdx, 16); a.shr(x86::rdx, 16);
                a.mov(x86::byte_ptr(x86::rdx, offsetof(iris::core::Managed, dirty)), 1);
                if (C < 4) a.mov(x86::qword_ptr(x86::rdx, 32 + C * 8), x86::rax);
                else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 24)); a.mov(x86::qword_ptr(x86::rcx, (uint64_t)(C - 4) * 8), x86::rax); }
                break;
            }
            case OpCode::OP_NEW_OBJ: {
                flushRegs(); a.mov(x86::ecx, (uint32_t)decodeBx(instr)); a.mov(x86::rdx, vmPtr);
                a.call((uint64_t)&createObjectHelper); for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_NEW_ARRAY: {
                flushRegs();
                loadRegAbs(B, x86::rcx); a.mov(x86::ecx, x86::ecx);
                a.mov(x86::edx, (uint32_t)C);
                a.call((uint64_t)&createArrayHelper);
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_IDX_GET: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();
                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Dispatch on elemType
                a.movzx(x86::eax, x86::byte_ptr(x86::r10, offsetof(iris::core::ArrayData, elemType)));
                a.cmp(x86::al, 1); // INT
                Label L_int_path = a.new_label();
                a.je(L_int_path);
                a.cmp(x86::al, 2); // DOUBLE
                Label L_double_path = a.new_label();
                a.je(L_double_path);
                // fall through: UNTYPED (0) or VALUE (3)
                // 3. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 4a. Load Value element
                a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)));
                storeRegAbs(A, x86::r9);
                a.jmp(L_done);

                // 4b. INT path — load 4 bytes, tag as int
                a.bind(L_int_path);
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);
                a.mov(x86::r9d, x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)));
                a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r9);
                storeRegAbs(A, x86::rax);
                a.jmp(L_done);

                // 4c. DOUBLE path — load 8 bytes (same layout as raw double)
                a.bind(L_double_path);
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);
                a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)));
                storeRegAbs(A, x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.call((uint64_t)&idxGetHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load int element
                a.mov(x86::r9d, x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)));

                // 4. Construct Value (QNAN | TAG_INT | r9d)
                a.mov(x86::rax, 0x7FF8000000000000ULL);
                a.or_(x86::rax, x86::r9);
                storeRegAbs(A, x86::rax);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.call((uint64_t)&idxGetIntHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load double element
                a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)));

                // 4. Construct Value (ensure canonical NaN if bits are NaN)
                a.mov(x86::rax, x86::r9);
                a.mov(x86::r11, 0x7FF8000000000000ULL);
                a.and_(x86::rax, x86::r11);
                a.cmp(x86::rax, x86::r11);
                a.cmove(x86::r9, x86::r11);

                storeRegAbs(A, x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.call((uint64_t)&idxGetDblHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));
                storeRegAbs(A, x86::rax);

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Dispatch on elemType
                a.movzx(x86::eax, x86::byte_ptr(x86::r10, offsetof(iris::core::ArrayData, elemType)));
                a.cmp(x86::al, 1); // INT
                Label L_int_path = a.new_label();
                a.je(L_int_path);
                a.cmp(x86::al, 2); // DOUBLE
                Label L_double_path = a.new_label();
                a.je(L_double_path);
                // fall through: UNTYPED (0) or VALUE (3)

                // 3. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 4a. VALUE store — load value from A, write full 8 bytes
                loadRegAbs(A, x86::r9);
                a.mov(x86::byte_ptr(x86::r10, offsetof(iris::core::Managed, dirty)), 1);
                a.mov(x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)), x86::r9);
                a.jmp(L_done);

                // 4b. INT store — extract int32, write 4 bytes
                a.bind(L_int_path);
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);
                loadRegAbs(A, x86::r9);
                a.and_(x86::r9d, 0xFFFFFFFF);
                a.mov(x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)), x86::r9d);
                a.jmp(L_done);

                // 4c. DOUBLE store — write 8 bytes
                a.bind(L_double_path);
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);
                loadRegAbs(A, x86::r9);
                a.mov(x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)), x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)(baseOff + A) * 8));
                a.call((uint64_t)&idxSetHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET_INT: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load value to set from A
                loadRegAbs(A, x86::r9);

                // 4. Store int element
                a.mov(x86::dword_ptr(x86::r10, x86::r8, 2, sizeof(iris::core::ArrayData)), x86::r9d);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)(baseOff + A) * 8));
                a.call((uint64_t)&idxSetIntHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_IDX_SET_DBL: {
                Label L_slow = a.new_label();
                Label L_done = a.new_label();

                // 1. Get array pointer from B
                loadRegAbs(B, x86::rax);
                a.mov(x86::r10, x86::rax);
                a.shl(x86::r10, 16);
                a.shr(x86::r10, 16);

                a.test(x86::r10, x86::r10);
                a.jz(L_slow);

                // 2. Get index from C
                loadRegAbs(C, x86::r11);
                a.movsxd(x86::r8, x86::r11d);

                // Bounds check
                a.cmp(x86::r8, 0);
                a.jl(L_slow);
                a.cmp(x86::r8, x86::qword_ptr(x86::r10, offsetof(iris::core::ArrayData, length)));
                a.jae(L_slow);

                // 3. Load value to set from A
                loadRegAbs(A, x86::r9);

                // Verify value is double
                a.mov(x86::rax, x86::r9);
                a.shr(x86::rax, 52);
                a.and_(x86::rax, 0x7FF);
                a.cmp(x86::rax, 0x7FF);
                a.je(L_slow);

                // 4. Store double element
                a.mov(x86::qword_ptr(x86::r10, x86::r8, 3, sizeof(iris::core::ArrayData)), x86::r9);
                a.jmp(L_done);

                // Slow path
                a.bind(L_slow);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.lea(x86::rdx, x86::qword_ptr(rBase, (uint64_t)(baseOff + C) * 8));
                a.lea(x86::r8, x86::qword_ptr(rBase, (uint64_t)(baseOff + A) * 8));
                a.call((uint64_t)&idxSetDblHelper);
                for(int j = 0; j < NUM_VREGS; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, (uint64_t)j * 8));

                a.bind(L_done);
                break;
            }
            case OpCode::OP_COLL_LEN: {
                Label L_helper = a.new_label(), L_done = a.new_label();
                loadRegAbs(B, x86::rax);
                // Fast path: check if it's an array (most common case)
                a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 48);
                a.cmp(x86::cx, ptrPrefix);
                a.jne(L_helper);
                a.shl(x86::rax, 16); a.shr(x86::rax, 16);
                a.movzx(x86::ecx, x86::byte_ptr(x86::rax, offsetof(iris::core::Managed, type)));
                a.cmp(x86::cl, (uint8_t)iris::core::ManagedType::Array);
                a.jne(L_helper);
                // Array: load length directly
                a.mov(x86::rax, x86::qword_ptr(x86::rax, offsetof(iris::core::ArrayData, length)));
                a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                storeRegAbs(A, x86::rax); a.jmp(L_done);
                // Slow path: call C++ helper (handles strings and other types)
                a.bind(L_helper);
                flushRegs();
                a.lea(x86::rcx, x86::qword_ptr(rBase, (uint64_t)(baseOff + B) * 8));
                a.call((uint64_t)&collLenHelper);
                storeRegAbs(A, x86::rax);
                a.bind(L_done);
                break;
            }
            case OpCode::OP_RET: { if (baseOff == 0) { loadBoxed(A, x86::rax); emitEpilogue(); } break; }
            case OpCode::OP_LOOP: {
                    const uint32_t* targetPC = entry.pc + 1 + decodeSBx(instr);
                    if (targetPC == trace.startPC) {
                        a.jmp(loopEntry);
                    } else {
                        a.mov(x86::rax, (uint64_t)targetPC);
                        a.jmp(sideExitTrampoline);
                    }
                    break;
                }
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL: {
                bool ubB = isUnboxedAbs(B);
                bool ubC = isUnboxedAbs(C);
                if (ubB && ubC) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    if (op == OpCode::OP_ADD) a.add(x86::eax, x86::ecx);
                    else if (op == OpCode::OP_SUB) a.sub(x86::eax, x86::ecx);
                    else a.imul(x86::eax, x86::ecx);
                    storeUnboxed(A, x86::eax);
                } else if (ubB || ubC) {
                    Label L_helper = a.new_label(), L_done = a.new_label();
                    int ubReg = ubB ? B : C;
                    int bxReg = ubB ? C : B;
                    bool ubLeft = ubB;
                    loadRegAbs(ubReg, x86::r8);
                    loadRegAbs(bxReg, x86::rax);
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 48);
                    a.cmp(x86::cx, (uint16_t)(intTag >> 48));
                    Label L_int = a.new_label();
                    a.je(L_int);
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 52);
                    a.and_(x86::ecx, 0x7FF); a.cmp(x86::ecx, 0x7FF);
                    Label L_double = a.new_label();
                    a.jne(L_double);
                    a.jmp(L_helper);
                    a.bind(L_int);
                    if (op == OpCode::OP_ADD) a.add(x86::r8d, x86::eax);
                    else if (op == OpCode::OP_SUB) { if (ubLeft) a.sub(x86::r8d, x86::eax); else { a.mov(x86::ecx, x86::eax); a.sub(x86::ecx, x86::r8d); a.mov(x86::r8d, x86::ecx); } }
                    else { if (ubLeft) a.imul(x86::r8d, x86::eax); else { a.mov(x86::ecx, x86::eax); a.imul(x86::ecx, x86::r8d); a.mov(x86::r8d, x86::ecx); } }
                    a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8); storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_double);
                    a.movq(x86::xmm0, x86::rax);
                    a.cvtsi2sd(x86::xmm2, x86::r8d);
                    if (op == OpCode::OP_ADD) { if (ubLeft) a.addsd(x86::xmm2, x86::xmm0); else a.addsd(x86::xmm0, x86::xmm2); }
                    else if (op == OpCode::OP_SUB) { if (ubLeft) a.subsd(x86::xmm2, x86::xmm0); else a.subsd(x86::xmm0, x86::xmm2); }
                    else { if (ubLeft) a.mulsd(x86::xmm2, x86::xmm0); else a.mulsd(x86::xmm0, x86::xmm2); }
                    if (ubLeft) a.movq(x86::rax, x86::xmm2); else a.movq(x86::rax, x86::xmm0);
                    storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_helper);
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call(op == OpCode::OP_ADD ? (uint64_t)&addHelper : (op == OpCode::OP_SUB ? (uint64_t)&subHelper : (uint64_t)&mulHelper));
                    storeRegAbs(A, x86::rax);
                    a.bind(L_done);
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call(op == OpCode::OP_ADD ? (uint64_t)&addHelper : (op == OpCode::OP_SUB ? (uint64_t)&subHelper : (uint64_t)&mulHelper));
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_EQ:
            case OpCode::OP_NEQ: {
                bool neg = (op == OpCode::OP_NEQ);
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    a.cmp(x86::eax, x86::ecx);
                    a.sete(x86::al);
                    a.movzx(x86::eax, x86::al);
                    if (neg) a.xor_(x86::eax, 1);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call((uint64_t)&eqHelper);
                    if (neg) a.xor_(x86::rax, 1);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_LE:
            case OpCode::OP_GE: {
                bool swap = (op == OpCode::OP_GT || op == OpCode::OP_LE);
                bool neg = (op == OpCode::OP_LE || op == OpCode::OP_GE);
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    int leftReg = swap ? C : B;
                    int rightReg = swap ? B : C;
                    loadRegAbs(leftReg, x86::rax); loadRegAbs(rightReg, x86::rcx);
                    a.cmp(x86::eax, x86::ecx);
                    a.setl(x86::al);
                    if (neg) a.xor_(x86::al, 1);
                    a.movzx(x86::eax, x86::al);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(swap ? C : B, x86::rcx); loadBoxed(swap ? B : C, x86::rdx);
                    a.call((uint64_t)&ltHelper);
                    if (neg) a.xor_(x86::rax, 1);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_LT_DBL:
            case OpCode::OP_GT_DBL:
            case OpCode::OP_LE_DBL:
            case OpCode::OP_GE_DBL: {
                bool swap = (op == OpCode::OP_GT_DBL || op == OpCode::OP_LE_DBL);
                bool neg = (op == OpCode::OP_LE_DBL || op == OpCode::OP_GE_DBL);
                loadRegAbs(swap ? C : B, x86::rax);
                a.movq(x86::xmm0, x86::rax);
                loadRegAbs(swap ? B : C, x86::rax);
                a.movq(x86::xmm1, x86::rax);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al); a.setnp(x86::ah); a.and_(x86::al, x86::ah);
                if (neg) a.xor_(x86::al, 1);
                a.movzx(x86::eax, x86::al);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_INC: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.inc(x86::eax);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_DEC: {
                loadRegAbs(A, x86::rax); if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                a.dec(x86::eax);
                storeUnboxed(A, x86::eax); break;
            }
            case OpCode::OP_NEG: {
                if (isUnboxedAbs(B)) {
                    loadRegAbs(B, x86::eax);
                    a.neg(x86::eax);
                    storeUnboxed(A, x86::eax);
                } else {
                    Label L_done = a.new_label(), L_helper = a.new_label();
                    loadRegAbs(B, x86::rax);
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 48);
                    a.cmp(x86::cx, (uint16_t)(intTag >> 48));
                    Label L_int = a.new_label();
                    a.je(L_int);
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 52);
                    a.and_(x86::ecx, 0x7FF); a.cmp(x86::ecx, 0x7FF);
                    Label L_double = a.new_label();
                    a.jne(L_double);
                    a.jmp(L_helper);
                    a.bind(L_int);
                    a.neg(x86::eax);
                    a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx);
                    storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_double);
                    a.mov(x86::rcx, 0x8000000000000000); a.xor_(x86::rax, x86::rcx);
                    storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_helper);
                    a.mov(x86::rcx, x86::rax); a.call((uint64_t)&negHelper);
                    storeRegAbs(A, x86::rax);
                    a.bind(L_done);
                }
                break;
            }
            // K operations (constant fused with operation)
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
            case OpCode::OP_DIV_K: {
                if (isUnboxedAbs(B)) {
                    Label L_done = a.new_label(), L_helper = a.new_label();
                    loadRegAbs(B, x86::r8);
                    a.mov(x86::rax, x86::qword_ptr(x86::rsi, (uint64_t)C * 8));
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 48);
                    a.cmp(x86::cx, (uint16_t)(intTag >> 48));
                    Label L_int = a.new_label();
                    a.je(L_int);
                    a.mov(x86::rcx, x86::rax); a.shr(x86::rcx, 52);
                    a.and_(x86::ecx, 0x7FF); a.cmp(x86::ecx, 0x7FF);
                    Label L_double = a.new_label();
                    a.jne(L_double);
                    a.jmp(L_helper);
                    a.bind(L_int);
                    if (op == OpCode::OP_ADD_K) a.add(x86::r8d, x86::eax);
                    else if (op == OpCode::OP_SUB_K) a.sub(x86::r8d, x86::eax);
                    else if (op == OpCode::OP_MUL_K) a.imul(x86::r8d, x86::eax);
                    else { a.movsxd(x86::rax, x86::eax); a.movsxd(x86::rcx, x86::r8d); a.cdq(); a.idiv(x86::ecx); a.mov(x86::r8d, x86::eax); }
                    a.mov(x86::rax, intTag); a.or_(x86::rax, x86::r8); storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_double);
                    a.movq(x86::xmm0, x86::rax);
                    a.cvtsi2sd(x86::xmm2, x86::r8d);
                    if (op == OpCode::OP_ADD_K) a.addsd(x86::xmm0, x86::xmm2);
                    else if (op == OpCode::OP_SUB_K) { a.movq(x86::xmm1, x86::xmm0); a.movq(x86::xmm0, x86::xmm2); a.subsd(x86::xmm0, x86::xmm1); }
                    else if (op == OpCode::OP_MUL_K) a.mulsd(x86::xmm0, x86::xmm2);
                    else a.divsd(x86::xmm0, x86::xmm2);
                    a.movq(x86::rax, x86::xmm0); storeRegAbs(A, x86::rax);
                    a.jmp(L_done);
                    a.bind(L_helper);
                    loadBoxed(B, x86::rcx);
                    a.mov(x86::rdx, x86::qword_ptr(x86::rsi, (uint64_t)C * 8));
                    if (op == OpCode::OP_ADD_K) a.call((uint64_t)&addHelper);
                    else if (op == OpCode::OP_SUB_K) a.call((uint64_t)&subHelper);
                    else if (op == OpCode::OP_MUL_K) a.call((uint64_t)&mulHelper);
                    else a.call((uint64_t)&divHelper);
                    storeRegAbs(A, x86::rax);
                    a.bind(L_done);
                } else {
                    loadBoxed(B, x86::rcx);
                    a.mov(x86::rdx, x86::qword_ptr(x86::rsi, (uint64_t)C * 8));
                    if (op == OpCode::OP_ADD_K) a.call((uint64_t)&addHelper);
                    else if (op == OpCode::OP_SUB_K) a.call((uint64_t)&subHelper);
                    else if (op == OpCode::OP_MUL_K) a.call((uint64_t)&mulHelper);
                    else a.call((uint64_t)&divHelper);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_EQ_K: {
                if (isUnboxedAbs(B)) {
                    loadRegAbs(B, x86::eax);
                    a.mov(x86::rcx, x86::qword_ptr(x86::rsi, (uint64_t)C * 8)); a.and_(x86::ecx, 0xFFFFFFFF);
                    a.cmp(x86::eax, x86::ecx);
                    a.sete(x86::al); a.movzx(x86::eax, x86::al);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(B, x86::rcx);
                    a.mov(x86::rdx, x86::qword_ptr(x86::rsi, (uint64_t)C * 8));
                    a.call((uint64_t)&eqHelper);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            // Double arithmetic — inline with recorded type info
            case OpCode::OP_ADD_DOUBLE:
            case OpCode::OP_SUB_DOUBLE:
            case OpCode::OP_MUL_DOUBLE:
            case OpCode::OP_DIV_DOUBLE: {
                bool bIsInt = entry.typeB == intPrefix;
                bool cIsInt = entry.typeC == intPrefix;
                loadRegAbs(B, x86::rax);
                if (bIsInt) {
                    if (!isUnboxedAbs(B)) a.and_(x86::eax, 0xFFFFFFFF);
                    a.cvtsi2sd(x86::xmm0, x86::eax);
                } else {
                    a.movq(x86::xmm0, x86::rax);
                }
                loadRegAbs(C, x86::rax);
                if (cIsInt) {
                    if (!isUnboxedAbs(C)) a.and_(x86::eax, 0xFFFFFFFF);
                    a.cvtsi2sd(x86::xmm1, x86::eax);
                } else {
                    a.movq(x86::xmm1, x86::rax);
                }
                if (op == OpCode::OP_ADD_DOUBLE) a.addsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_SUB_DOUBLE) a.subsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_MUL_DOUBLE) a.mulsd(x86::xmm0, x86::xmm1);
                else a.divsd(x86::xmm0, x86::xmm1);
                a.movq(x86::rax, x86::xmm0);
                storeRegAbs(A, x86::rax); break;
            }
            case OpCode::OP_DIV_INT: {
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    a.movsxd(x86::rax, x86::eax);
                    a.movsxd(x86::rcx, x86::ecx);
                    a.cdq();
                    a.idiv(x86::ecx);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call((uint64_t)&divHelper);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            // Bitwise operations
            case OpCode::OP_BIT_AND:
            case OpCode::OP_BIT_OR:
            case OpCode::OP_BIT_XOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR: {
                loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF);
                if (op == OpCode::OP_BIT_AND) a.and_(x86::eax, x86::ecx);
                else if (op == OpCode::OP_BIT_OR) a.or_(x86::eax, x86::ecx);
                else if (op == OpCode::OP_BIT_XOR) a.xor_(x86::eax, x86::ecx);
                else if (op == OpCode::OP_SHL) a.shl(x86::eax, x86::cl);
                else a.shr(x86::eax, x86::cl);
                storeUnboxed(A, x86::eax); break;
            }
            // Integer comparisons
            case OpCode::OP_LE_INT:
            case OpCode::OP_GE_INT:
            case OpCode::OP_EQ_INT: {
                loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                if (!isUnboxedAbs(B)) a.and_(x86::rax, 0xFFFFFFFF);
                if (!isUnboxedAbs(C)) a.and_(x86::rcx, 0xFFFFFFFF);
                a.cmp(x86::eax, x86::ecx);
                x86::CondCode cond = (op == OpCode::OP_LE_INT) ? x86::CondCode::kSignedLE : (op == OpCode::OP_GE_INT ? x86::CondCode::kSignedGE : x86::CondCode::kEqual);
                a.set(cond, x86::al); a.movzx(x86::eax, x86::al);
                storeUnboxed(A, x86::eax); break;
            }
            // Fused integer compare-and-branch (peephole optimized)
            case OpCode::OP_JLT_INT:
            case OpCode::OP_JGT_INT:
            case OpCode::OP_JLE_INT:
            case OpCode::OP_JGE_INT:
            case OpCode::OP_JNE_INT: {
                loadRegAbs(A, x86::rax); loadRegAbs(B, x86::rcx);
                if (!isUnboxedAbs(A)) a.and_(x86::rax, 0xFFFFFFFF);
                if (!isUnboxedAbs(B)) a.and_(x86::rcx, 0xFFFFFFFF);
                a.cmp(x86::eax, x86::ecx);
                if (!entry.branchTaken) {
                    if (op == OpCode::OP_JLT_INT) emitGuard(x86::CondCode::kSignedGE, entry.pc);
                    else if (op == OpCode::OP_JGT_INT) emitGuard(x86::CondCode::kSignedLE, entry.pc);
                    else if (op == OpCode::OP_JLE_INT) emitGuard(x86::CondCode::kSignedGT, entry.pc);
                    else if (op == OpCode::OP_JGE_INT) emitGuard(x86::CondCode::kSignedLT, entry.pc);
                    else emitGuard(x86::CondCode::kEqual, entry.pc); // JNE_INT
                } else {
                    if (op == OpCode::OP_JLT_INT) emitGuard(x86::CondCode::kSignedLT, entry.pc);
                    else if (op == OpCode::OP_JGT_INT) emitGuard(x86::CondCode::kSignedGT, entry.pc);
                    else if (op == OpCode::OP_JLE_INT) emitGuard(x86::CondCode::kSignedLE, entry.pc);
                    else if (op == OpCode::OP_JGE_INT) emitGuard(x86::CondCode::kSignedGE, entry.pc);
                    else emitGuard(x86::CondCode::kNotEqual, entry.pc); // JNE_INT
                }
                break;
            }
            case OpCode::OP_DIV: {
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    a.movsxd(x86::rax, x86::eax); a.movsxd(x86::rcx, x86::ecx);
                    a.cdq(); a.idiv(x86::ecx);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call((uint64_t)&divHelper);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_MOD: {
                if (isUnboxedAbs(B) && isUnboxedAbs(C)) {
                    loadRegAbs(B, x86::rax); loadRegAbs(C, x86::rcx);
                    a.movsxd(x86::rax, x86::eax); a.movsxd(x86::rcx, x86::ecx);
                    a.cdq(); a.idiv(x86::ecx);
                    a.mov(x86::eax, x86::edx);
                    storeUnboxed(A, x86::eax);
                } else {
                    loadBoxed(B, x86::rcx); loadBoxed(C, x86::rdx);
                    a.call((uint64_t)&modHelper);
                    storeRegAbs(A, x86::rax);
                }
                break;
            }
            case OpCode::OP_COUNT: break; // NOP
            default: break;
        }
    };

    for (int i = 0; i < (int)trace.preamble.size(); i++) emitEntry(trace.preamble[i], i);
    a.bind(loopEntry);
    bool hasLoop = !trace.entries.empty() && decodeOp(trace.entries.back().instr) == OpCode::OP_LOOP;
    const int UNROLL_FACTOR = 2;
    if (hasLoop && UNROLL_FACTOR > 1) {
        for (int u = 0; u < UNROLL_FACTOR - 1; u++) {
            for (int i = 0; i < (int)trace.entries.size() - 1; i++) {
                emitEntry(trace.entries[i], 0);
            }
        }
        for (int i = 0; i < (int)trace.entries.size(); i++) {
            emitEntry(trace.entries[i], 0);
        }
    } else {
        for (int i = 0; i < (int)trace.entries.size(); i++) {
            emitEntry(trace.entries[i], (int)trace.preamble.size() + i);
        }
        if (!hasLoop) a.jmp(loopEntry);
    }

    a.bind(sideExitTrampoline);
    a.mov(x86::qword_ptr(x86::rsp, 48), x86::rax);
    flushRegs();
    a.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
    a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) { return nullptr; }
    return func;
}
