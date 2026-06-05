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
    std::vector<Label> labels(chunk.code.size() + 1);
    for (size_t i = 0; i <= chunk.code.size(); ++i) labels[i] = a.new_label();

    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 72);
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r9, x86::r10, x86::r11 };
    
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    
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
                case OpCode::OP_ADD: {
                    x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 8) ? vRegs[C] : x86::rdx;
                    if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 8) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                    x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    Label callHelper = a.new_label(); Label done = a.new_label();
                    a.mov(x86::r10, regB); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(intTag >> 48)); a.jne(callHelper);
                    a.mov(x86::r10, regC); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(intTag >> 48)); a.jne(callHelper);
                    a.mov(x86::r8d, regB.r32()); a.add(x86::r8d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r8);
                    a.jmp(done);
                    a.bind(callHelper); flushRegs(); a.mov(x86::rcx, regB); a.mov(x86::rdx, regC); a.call((uint64_t)addHelper); a.mov(regA, x86::rax);
                    a.bind(done); if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
                }
                case OpCode::OP_SUB: {
                    x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 8) ? vRegs[C] : x86::rdx;
                    if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 8) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                    x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    Label callHelper = a.new_label(); Label done = a.new_label();
                    a.mov(x86::r10, regB); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(intTag >> 48)); a.jne(callHelper);
                    a.mov(x86::r10, regC); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(intTag >> 48)); a.jne(callHelper);
                    a.mov(x86::r8d, regB.r32()); a.sub(x86::r8d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r8);
                    a.jmp(done);
                    a.bind(callHelper); flushRegs(); a.mov(x86::rcx, regB); a.mov(x86::rdx, regC); a.call((uint64_t)subHelper); a.mov(regA, x86::rax);
                    a.bind(done); if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
                }
                case OpCode::OP_INVOKE_MONO: {
                    uint16_t cacheIdx = (B << 8) | C;
                    auto& entry = chunk.methodCaches[cacheIdx];
                    x86::Gp obj = (A < 8) ? vRegs[A] : x86::rax; if (A >= 8) a.mov(obj, x86::qword_ptr(rBase, A * 8));
                    Label fallback = a.new_label(); Label done = a.new_label();
                    a.mov(x86::r10, obj); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                    a.cmp(x86::word_ptr(x86::r10, 8), entry.classId); a.jne(fallback);
                    FunctionObject &f = (*functions)[entry.fid];
                    if (f.chunk.jitFunc) {
                        for(int i=0; i<8; i++) a.push(vRegs[i]);
                        a.mov(x86::rdx, x86::qword_ptr(rBase, (uint64_t)A * 8));
                        a.mov(x86::r8, x86::qword_ptr(rBase, (uint64_t)(A+1) * 8));
                        a.mov(x86::r9, x86::qword_ptr(rBase, (uint64_t)(A+2) * 8));
                        a.call((uint64_t)f.chunk.jitFunc);
                        for(int i=7; i>=0; i--) a.pop(vRegs[i]);
                        if (A < 8) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                        a.jmp(done);
                    }
                    a.bind(fallback); flushRegs(); a.mov(x86::rcx, (uint64_t)&chunk); a.mov(x86::rdx, (uint32_t)i); a.call((uint64_t)invokeHelper); 
                    if (A < 8) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                    a.bind(done); break;
                }
                case OpCode::OP_RET: { 
                    if (A < 8) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8)); 
                    if (isFast) a.ret(); else emitEpilogue(); break; 
                }
                case OpCode::OP_JMP:
                case OpCode::OP_LOOP: { a.jmp(labels[i + 1 + decodeSBx(instr)]); break; }
                case OpCode::OP_LOG: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8); a.call((uint64_t)logHelper); break; }
                case OpCode::OP_HALT: if (isFast) a.ret(); else emitEpilogue(); break;
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

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions) {
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 40);
    
    a.mov(x86::rdi, x86::qword_ptr(x86::rcx, 0)); // rBase
    a.mov(x86::rsi, x86::qword_ptr(x86::rcx, 8)); // constants
    a.mov(x86::r12, x86::qword_ptr(x86::rcx, 16)); // vmPtr
    a.mov(x86::rax, x86::qword_ptr(x86::rcx, 24)); // globalsPtr
    a.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);

    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r12;
    std::vector<x86::Gp> vRegs = { x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r9, x86::r10, x86::r11 };
    std::vector<bool> isUnboxed(8, false);
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint16_t intPrefix = (uint16_t)(intTag >> 48);

    for(int i = 0; i < 8; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    Label loopEntry = a.new_label(); 

    auto flushRegs = [&](bool preserveRax = false) {
        for(int i = 0; i < 8; i++) {
            if (isUnboxed[i]) {
                x86::Gp tmp = preserveRax ? (vRegs[i] == x86::r8 ? x86::r10 : x86::r8) : x86::rax;
                a.mov(tmp, intTag); a.or_(tmp, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, i * 8), tmp);
            } else { a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); }
        }
    };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 40); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };

    auto ensureUnboxed = [&](uint8_t reg, bool skipGuard, const uint32_t* pc) {
        if (reg < 8 && !isUnboxed[reg]) {
            if (!skipGuard) {
                a.mov(x86::rax, vRegs[reg]); a.shr(x86::rax, 48); a.cmp(x86::ax, intPrefix); 
                emitGuard(x86::CondCode::kEqual, pc);
            }
            a.mov(vRegs[reg].r32(), vRegs[reg].r32()); // Strip tag
            isUnboxed[reg] = true;
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
                else if (B < 8) { flushRegs(true); a.mov(x86::qword_ptr(rBase, A * 8), vRegs[B]); }
                break;
            case OpCode::OP_ADD:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.add(x86::r8d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SUB:
                if (A < 8 && B < 8 && C < 8) {
                    ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.sub(x86::r8d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
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
            case OpCode::OP_BIT_AND:
                if (A < 8 && B < 8 && C < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.and_(x86::r8d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_BIT_OR:
                if (A < 8 && B < 8 && C < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.or_(x86::r8d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_BIT_XOR:
                if (A < 8 && B < 8 && C < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.xor_(x86::r8d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SHL:
                if (A < 8 && B < 8 && C < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.mov(x86::ecx, vRegs[C].r32()); a.shl(x86::r8d, x86::cl); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SHR:
                if (A < 8 && B < 8 && C < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); ensureUnboxed(C, entry.skipGuardC, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.mov(x86::ecx, vRegs[C].r32()); a.shr(x86::r8d, x86::cl); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_GGLOB: {
                uint16_t slot = (B << 8) | C; a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));
                a.mov(x86::r10, x86::qword_ptr(x86::rax, (uint64_t)slot * 16));
                if (A < 8) { a.mov(vRegs[A], x86::r10); isUnboxed[A] = false; }
                else { a.mov(x86::qword_ptr(rBase, A * 8), x86::r10); }
                break;
            }
            case OpCode::OP_GET_FIELD: {
                x86::Gp obj = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(obj, x86::qword_ptr(rBase, B * 8));
                if (!entry.skipGuardB) { a.mov(x86::r10, obj); a.shr(x86::r10, 48); a.cmp(x86::r10w, 0x7FFC); emitGuard(x86::CondCode::kEqual, entry.pc); }
                a.mov(x86::r10, obj); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) a.mov(x86::r10, x86::qword_ptr(x86::r10, 24 + (uint64_t)C * 8));
                else { a.mov(x86::r11, x86::qword_ptr(x86::r10, 16)); a.mov(x86::r10, x86::qword_ptr(x86::r11, (uint64_t)(C - 4) * 8)); }
                if (A < 8) { a.mov(vRegs[A], x86::r10); isUnboxed[A] = false; }
                else { a.mov(x86::qword_ptr(rBase, A * 8), x86::r10); }
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp obj = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(obj, x86::qword_ptr(rBase, B * 8));
                if (!entry.skipGuardB) { a.mov(x86::r10, obj); a.shr(x86::r10, 48); a.cmp(x86::r10w, 0x7FFC); emitGuard(x86::CondCode::kEqual, entry.pc); }
                x86::Gp val = (A < 8) ? vRegs[A] : x86::r11; if (A >= 8) a.mov(val, x86::qword_ptr(rBase, A * 8));
                if (A < 8 && isUnboxed[A]) { a.mov(x86::r11, intTag); a.or_(x86::r11, val.r64()); val = x86::r11; }
                a.mov(x86::r10, obj); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) a.mov(x86::qword_ptr(x86::r10, 24 + (uint64_t)C * 8), val);
                else { a.mov(x86::r10, x86::qword_ptr(x86::r10, 16)); a.mov(x86::qword_ptr(x86::r10, (uint64_t)(C - 4) * 8), val); }
                break;
            }
            case OpCode::OP_JMPF: {
                if (A < 8) { a.mov(x86::r8, vRegs[A]); if (isUnboxed[A]) a.and_(x86::r8d, 1); else a.and_(x86::r8, 1); }
                else { a.mov(x86::r8, x86::qword_ptr(rBase, A * 8)); a.and_(x86::r8, 1); }
                if (entry.branchTaken) { a.cmp(x86::r8d, 0); emitGuard(x86::CondCode::kEqual, entry.pc); }
                else { a.cmp(x86::r8d, 1); emitGuard(x86::CondCode::kEqual, entry.pc); }
                break;
            }
            case OpCode::OP_LOOP: a.jmp(loopEntry); break;
            case OpCode::OP_RET:
                if (A < 8) { if (isUnboxed[A]) { a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[A].r64()); } else { a.mov(x86::rax, vRegs[A]); } }
                else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                emitEpilogue(); break;
            case OpCode::OP_LOG: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8); a.call((uint64_t)logHelper); break; }
            case OpCode::OP_ADD_K:
                if (A < 8 && B < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)C * 8));
                    a.mov(x86::r10, x86::rax); a.shr(x86::r10, 48); a.cmp(x86::r10w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.add(x86::r8d, x86::eax); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_SUB_K:
                if (A < 8 && B < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)C * 8));
                    a.mov(x86::r10, x86::rax); a.shr(x86::r10, 48); a.cmp(x86::r10w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.sub(x86::r8d, x86::eax); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_MUL_K:
                if (A < 8 && B < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)C * 8));
                    a.mov(x86::r10, x86::rax); a.shr(x86::r10, 48); a.cmp(x86::r10w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.mov(x86::r8d, vRegs[B].r32()); a.imul(x86::r8d, x86::eax); a.mov(vRegs[A].r32(), x86::r8d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            case OpCode::OP_LT_K:
                if (A < 8 && B < 8) { ensureUnboxed(B, entry.skipGuardB, entry.pc); a.mov(x86::rax, x86::qword_ptr(constants, (uint64_t)C * 8));
                    a.mov(x86::r10, x86::rax); a.shr(x86::r10, 48); a.cmp(x86::r10w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.cmp(vRegs[B].r32(), x86::eax); a.setl(x86::r8b); a.movzx(x86::r8d, x86::r8b); a.mov(vRegs[A], boolTag); a.or_(vRegs[A], x86::r8); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            default: a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); break;
        }
    };

    for (const auto& entry : trace.preamble) emitEntry(entry);
    a.bind(loopEntry);
    for (const auto& entry : trace.entries) emitEntry(entry);

    a.bind(sideExitTrampoline); a.push(x86::rax); flushRegs(true); a.mov(x86::rcx, x86::rax); a.call((uint64_t)sideExitDiagnostic); a.pop(x86::rax);
    a.add(x86::rsp, 40); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
