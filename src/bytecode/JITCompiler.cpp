#include "JITCompiler.h"
#include "Trace.h"
#include "../core/Value.h"
#include "Compiler.h"
#include "JITHelpers.h"
#include <iostream>
#include <vector>
#include <asmjit/core.h>
#include <asmjit/x86.h>

using namespace iris::bytecode;
using namespace asmjit;

JITFunc JITCompiler::compile(Chunk& chunk, void* functions_ptr, void* native_functions) {
    auto* functions = static_cast<std::vector<FunctionObject>*>(functions_ptr);
    int currentFuncIdx = -1;
    if (functions) {
        for (size_t idx = 0; idx < functions->size(); ++idx) {
            if (&(*functions)[idx].chunk == &chunk) {
                currentFuncIdx = (int)idx;
                break;
            }
        }
    }
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 72);
    
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    
    a.mov(x86::rax, x86::qword_ptr(x86::rcx, 24)); // globals
    a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
    
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r9, x86::r10, x86::r11 };
    
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    
    int arity = (currentFuncIdx != -1) ? (int)(*functions)[currentFuncIdx].arity : 0;
    int numRegsToLoad = std::min(arity + 1, 8);
    
    a.mov(vRegs[0], x86::rdx); a.mov(vRegs[1], x86::r8); a.mov(vRegs[2], x86::r9);
    for(int i = 3; i < numRegsToLoad; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    for(int i = numRegsToLoad; i < 8; i++) a.mov(vRegs[i], nullTag);

    auto flushRegs = [&]() { for(int i = 0; i < 8; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 72); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };

    Label fastEntryLabel = a.new_label();
    auto compileBody = [&](bool isFast) {
        std::vector<Label> labels(chunk.code.size() + 1);
        for (size_t i = 0; i <= chunk.code.size(); ++i) labels[i] = a.new_label();
        
        auto loadReg = [&](uint8_t reg, x86::Gp temp) {
            if (reg < 8) a.mov(temp, vRegs[reg]);
            else a.mov(temp, x86::qword_ptr(rBase, reg * 8));
        };
        auto storeReg = [&](uint8_t reg, x86::Gp temp) {
            if (reg < 8) a.mov(vRegs[reg], temp);
            else a.mov(x86::qword_ptr(rBase, reg * 8), temp);
        };

        for (size_t i = 0; i < chunk.code.size(); ++i) {
            a.bind(labels[i]); uint32_t instr = chunk.code[i]; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
            switch (op) {
                case OpCode::OP_LOADK: { x86::Gp regA = (A < 8) ? vRegs[A] : x86::rax; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    a.mov(regA, x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8)); if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
                case OpCode::OP_LOADINT: { x86::Gp regA = (A < 8) ? vRegs[A] : x86::rax; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    a.mov(regA, intTag | (uint32_t)decodeSBx(instr)); if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
                case OpCode::OP_MOVE: { x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                    x86::Gp regA = (A < 8) ? vRegs[A] : x86::rdx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    a.mov(regA, regB); if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
                
                case OpCode::OP_ADD_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.add(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_SUB_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.sub(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_MUL_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.imul(x86::eax, x86::ecx); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_ADDI: { loadReg(B, x86::rax); a.add(x86::eax, (int32_t)(int8_t)C); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_SUBI: { loadReg(B, x86::rax); a.sub(x86::eax, (int32_t)(int8_t)C); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_INC: { loadReg(A, x86::rax); a.inc(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_DEC: { loadReg(A, x86::rax); a.dec(x86::eax); a.mov(x86::rcx, intTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }

                case OpCode::OP_ADD_DOUBLE: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.movq(x86::xmm0, x86::rax); a.movq(x86::xmm1, x86::rcx); a.addsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); break; }
                case OpCode::OP_SUB_DOUBLE: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.movq(x86::xmm0, x86::rax); a.movq(x86::xmm1, x86::rcx); a.subsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); break; }
                case OpCode::OP_MUL_DOUBLE: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.movq(x86::xmm0, x86::rax); a.movq(x86::xmm1, x86::rcx); a.mulsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); break; }
                case OpCode::OP_DIV_DOUBLE: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.movq(x86::xmm0, x86::rax); a.movq(x86::xmm1, x86::rcx); a.divsd(x86::xmm0, x86::xmm1); a.movq(x86::rax, x86::xmm0); storeReg(A, x86::rax); break; }

                case OpCode::OP_LT_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setl(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_GT_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setg(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_LE_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setle(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_GE_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.setge(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }
                case OpCode::OP_EQ_INT: { loadReg(B, x86::rax); loadReg(C, x86::rcx); a.cmp(x86::eax, x86::ecx); a.sete(x86::al); a.movzx(x86::eax, x86::al); a.mov(x86::rcx, boolTag); a.or_(x86::rax, x86::rcx); storeReg(A, x86::rax); break; }

                case OpCode::OP_JMPF: { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 0); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
                case OpCode::OP_JMPT: { loadReg(A, x86::rax); a.and_(x86::eax, 1); a.cmp(x86::eax, 1); a.je(labels[i + 1 + decodeSBx(instr)]); break; }

                case OpCode::OP_GGLOB: { uint16_t slot = instr & 0xFFFF; a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32)); a.mov(x86::rax, x86::qword_ptr(x86::rax, (uint64_t)slot * 16)); storeReg(A, x86::rax); break; }
                case OpCode::OP_SGLOB: { uint16_t slot = instr & 0xFFFF; loadReg(A, x86::rdx); a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32)); a.mov(x86::qword_ptr(x86::rax, (uint64_t)slot * 16), x86::rdx); break; }

                case OpCode::OP_GET_FIELD: { loadReg(B, x86::rax); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                    if (C < 4) { a.mov(x86::rax, x86::qword_ptr(x86::rax, 24 + C * 8)); }
                    else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 16)); a.mov(x86::rax, x86::qword_ptr(x86::rcx, (C - 4) * 8)); }
                    storeReg(A, x86::rax); break; }
                case OpCode::OP_SET_FIELD: { loadReg(B, x86::rdx); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rdx, x86::rcx); loadReg(A, x86::rax);
                    if (C < 4) { a.mov(x86::qword_ptr(x86::rdx, 24 + C * 8), x86::rax); }
                    else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 16)); a.mov(x86::qword_ptr(x86::rcx, (C - 4) * 8), x86::rax); }
                    break; }

                case OpCode::OP_JLT_INT: { loadReg(A, x86::rax); loadReg(B, x86::rcx); a.cmp(x86::eax, x86::ecx); a.jl(labels[i + 2 + (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767]); i++; break; }
                case OpCode::OP_JGT_INT: { loadReg(A, x86::rax); loadReg(B, x86::rcx); a.cmp(x86::eax, x86::ecx); a.jg(labels[i + 2 + (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767]); i++; break; }
                case OpCode::OP_JLE_INT: { loadReg(A, x86::rax); loadReg(B, x86::rcx); a.cmp(x86::eax, x86::ecx); a.jle(labels[i + 2 + (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767]); i++; break; }
                case OpCode::OP_JGE_INT: { loadReg(A, x86::rax); loadReg(B, x86::rcx); a.cmp(x86::eax, x86::ecx); a.jge(labels[i + 2 + (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767]); i++; break; }
                case OpCode::OP_JNE_INT: { loadReg(A, x86::rax); loadReg(B, x86::rcx); a.cmp(x86::eax, x86::ecx); a.jne(labels[i + 2 + (int32_t)(chunk.code[i + 1] & 0xFFFF) - 32767]); i++; break; }

                case OpCode::OP_RET: { if (A < 8) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8)); 
                    if (isFast) a.ret(); else emitEpilogue(); break; }
                case OpCode::OP_HALT: if (isFast) a.ret(); else emitEpilogue(); break;
                case OpCode::OP_JMP:
                case OpCode::OP_LOOP: { a.jmp(labels[i + 1 + decodeSBx(instr)]); break; }
                default: break;
            }
        }
        a.bind(labels[chunk.code.size()]);
    };
    compileBody(false);
    a.bind(fastEntryLabel);
    a.mov(vRegs[0], x86::rdx); a.mov(vRegs[1], x86::r8); a.mov(vRegs[2], x86::r9);
    for(int i = 3; i < 8; i++) a.mov(vRegs[i], nullTag);
    compileBody(true);
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions_ptr) {
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 64);
    
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    a.mov(x86::rax, x86::qword_ptr(x86::rcx, 24)); a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax); // globalsPtr

    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r8, x86::r9, x86::r10 };
    std::vector<bool> isUnboxed(8, false);
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint16_t intPrefix = (uint16_t)(intTag >> 48);
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;

    for(int i = 0; i < 8; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    Label loopEntry = a.new_label(); 

    auto flushRegs = [&]() {
        for(int i = 0; i < 8; i++) {
            if (isUnboxed[i]) {
                a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, i * 8), x86::rax);
            } else { a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); }
        }
    };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 64); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };

    auto ensureUnboxed = [&](uint8_t reg, bool skipGuard, const uint32_t* pc) {
        if (reg < 8 && !isUnboxed[reg]) {
            if (!skipGuard) { a.mov(x86::rax, vRegs[reg]); a.shr(x86::rax, 48); a.cmp(x86::ax, intPrefix); emitGuard(x86::CondCode::kEqual, pc); }
            a.mov(vRegs[reg].r32(), vRegs[reg].r32()); isUnboxed[reg] = true;
        }
    };

    auto emitEntry = [&](const Trace::Entry& entry) {
        uint32_t instr = entry.instr; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_LOADK:
                if (A < 8) { a.mov(vRegs[A], x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8)); isUnboxed[A] = false; }
                else { a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)(instr & 0xFFFF) * 8)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            case OpCode::OP_LOADINT:
                if (A < 8) { a.mov(vRegs[A].r32(), (uint32_t)decodeSBx(instr)); isUnboxed[A] = true; }
                else { a.mov(x86::rax, intTag | (uint32_t)decodeSBx(instr)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            case OpCode::OP_MOVE:
                if (A < 8 && B < 8) { a.mov(vRegs[A], vRegs[B]); isUnboxed[A] = isUnboxed[B]; }
                else if (A < 8) { a.mov(vRegs[A], x86::qword_ptr(rBase, B * 8)); isUnboxed[A] = false; }
                else if (B < 8) { flushRegs(); a.mov(x86::qword_ptr(rBase, A * 8), vRegs[B]); }
                break;
            case OpCode::OP_ADD:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r11d, vRegs[B].r32()); a.add(x86::r11d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SUB:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r11d, vRegs[B].r32()); a.sub(x86::r11d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_MUL:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r11d, vRegs[B].r32()); a.imul(x86::r11d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_INC:
                if (A < 8) { ensureUnboxed(A, entry.skipGuardA, entry.pc); a.inc(vRegs[A].r32()); isUnboxed[A] = true; }
                else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_DEC:
                if (A < 8) { ensureUnboxed(A, entry.skipGuardA, entry.pc); a.dec(vRegs[A].r32()); isUnboxed[A] = true; }
                else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_ADD_K:
                if (A < 8 && B < 8) { 
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); 
                    a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)C * 8));
                    a.mov(x86::r11, x86::rax); a.sub(x86::r11, intTag); emitGuard(x86::CondCode::kZero, entry.pc); // check tag matches intTag
                    a.mov(x86::r11d, vRegs[B].r32()); a.add(x86::r11d, x86::eax); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;

            case OpCode::OP_LT_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.cmp(vRegs[B].r32(), vRegs[C].r32()); a.setl(x86::al); a.movzx(vRegs[A].r32(), x86::al);
                    isUnboxed[A] = false; a.mov(x86::rcx, boolTag); a.or_(vRegs[A], x86::rcx);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_GT_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.cmp(vRegs[B].r32(), vRegs[C].r32()); a.setg(x86::al); a.movzx(vRegs[A].r32(), x86::al);
                    isUnboxed[A] = false; a.mov(x86::rcx, boolTag); a.or_(vRegs[A], x86::rcx);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_LE_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.cmp(vRegs[B].r32(), vRegs[C].r32()); a.setle(x86::al); a.movzx(vRegs[A].r32(), x86::al);
                    isUnboxed[A] = false; a.mov(x86::rcx, boolTag); a.or_(vRegs[A], x86::rcx);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_GE_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.cmp(vRegs[B].r32(), vRegs[C].r32()); a.setge(x86::al); a.movzx(vRegs[A].r32(), x86::al);
                    isUnboxed[A] = false; a.mov(x86::rcx, boolTag); a.or_(vRegs[A], x86::rcx);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_EQ_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.cmp(vRegs[B].r32(), vRegs[C].r32()); a.sete(x86::al); a.movzx(vRegs[A].r32(), x86::al);
                    isUnboxed[A] = false; a.mov(x86::rcx, boolTag); a.or_(vRegs[A], x86::rcx);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;

            case OpCode::OP_ADD_DOUBLE:
                if (A < 8 && B < 8 && C < 8) {
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.addsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SUB_DOUBLE:
                if (A < 8 && B < 8 && C < 8) {
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.subsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;

            case OpCode::OP_GET_FIELD:
                if (A < 8 && B < 8) {
                    a.mov(x86::rax, vRegs[B]); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                    if (C < 4) { a.mov(vRegs[A], x86::qword_ptr(x86::rax, 24 + C * 8)); }
                    else { a.mov(x86::rcx, x86::qword_ptr(x86::rax, 16)); a.mov(vRegs[A], x86::qword_ptr(x86::rcx, (C - 4) * 8)); }
                    isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SET_FIELD:
                if (A < 8 && B < 8) {
                    a.mov(x86::rdx, vRegs[B]); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rdx, x86::rcx); a.mov(x86::rax, vRegs[A]);
                    if (C < 4) { a.mov(x86::qword_ptr(x86::rdx, 24 + C * 8), x86::rax); }
                    else { a.mov(x86::rcx, x86::qword_ptr(x86::rdx, 16)); a.mov(x86::qword_ptr(x86::rcx, (C - 4) * 8), x86::rax); }
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;

            case OpCode::OP_IDX_GET_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::rax, vRegs[B]); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                    a.mov(x86::rcx, x86::qword_ptr(x86::rax, 16));
                    a.mov(x86::r11d, x86::dword_ptr(x86::rcx, vRegs[C].r64(), 2));
                    a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_IDX_SET_INT:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(C, entry.skipGuardC, entry.pc); ensureUnboxed(A, entry.skipGuardA, entry.pc);
                    a.mov(x86::rax, vRegs[B]); a.mov(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.and_(x86::rax, x86::rcx);
                    a.mov(x86::rcx, x86::qword_ptr(x86::rax, 16));
                    a.mov(x86::r11d, vRegs[A].r32());
                    a.mov(x86::dword_ptr(x86::rcx, vRegs[C].r64(), 2), x86::r11d);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;

            case OpCode::OP_JMPF: {
                if (A < 8) { a.mov(x86::r11, vRegs[A]); if (isUnboxed[A]) a.and_(x86::r11d, 1); else a.and_(x86::r11, 1); }
                else { a.mov(x86::r11, x86::qword_ptr(rBase, A * 8)); a.and_(x86::r11, 1); }
                if (entry.branchTaken) { a.cmp(x86::r11d, 0); emitGuard(x86::CondCode::kEqual, entry.pc); }
                else { a.cmp(x86::r11d, 1); emitGuard(x86::CondCode::kEqual, entry.pc); }
                break;
            }
            case OpCode::OP_LOOP: a.jmp(loopEntry); break;
            case OpCode::OP_RET:
                if (A < 8) { if (isUnboxed[A]) { a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[A].r64()); } else { a.mov(x86::rax, vRegs[A]); } }
                else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                emitEpilogue(); break;
            case OpCode::OP_LOG: { flushRegs(); a.lea(x86::rcx, x86::qword_ptr(rBase, A * 8)); a.call((uint64_t)logHelper); break; }
            default: a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); break;
        }
    };

    for (const auto& entry : trace.preamble) emitEntry(entry);
    a.bind(loopEntry);
    for (const auto& entry : trace.entries) emitEntry(entry);

    a.bind(sideExitTrampoline); 
    a.mov(x86::qword_ptr(x86::rsp, 56), x86::rax); // Preserve failPC
    flushRegs(); 
    a.mov(x86::rcx, x86::qword_ptr(x86::rsp, 56)); a.call((uint64_t)sideExitDiagnostic);
    a.mov(x86::rax, x86::qword_ptr(x86::rsp, 56)); // Restore failPC to rax for return
    a.add(x86::rsp, 64); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
