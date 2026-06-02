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
    Label funcEntry = a.new_label(); a.bind(funcEntry);

    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8); // Align to 16 bytes (8 pushes + 8 return addr + 8 sub = 80 bytes)
    a.mov(x86::rdi, x86::rcx); a.mov(x86::rsi, x86::rdx); a.mov(x86::r15, x86::r8);
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r15;
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::rbp, x86::rbx };
    int arity = (currentFuncIdx != -1) ? (int)(*functions)[currentFuncIdx].arity : 0;
    int numRegsToLoad = std::min(arity, 5);
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    for(int i = numRegsToLoad; i < 5; i++) a.mov(vRegs[i], nullTag);

    auto flushRegs = [&]() { for(int i = 0; i < 5; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() {
        flushRegs();
        a.add(x86::rsp, 8);
        a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    };
    auto emitRelease = [&](x86::Gp reg) {
        Label done = a.new_label();
        a.mov(x86::r11, reg); a.shr(x86::r11, 48); a.cmp(x86::r11, 0xFFFC); a.jne(done);
        a.mov(x86::r11, reg); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); a.je(done);
        a.dec(x86::dword_ptr(x86::r11, 0)); a.jnz(done);
        a.mov(x86::dword_ptr(x86::r11, 0), 1);
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8); a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40); a.mov(x86::rcx, reg); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8); a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
        a.bind(done);
    };
    auto emitRetain = [&](x86::Gp reg) {
        Label done = a.new_label();
        a.mov(x86::r11, reg); a.shr(x86::r11, 48); a.cmp(x86::r11, 0xFFFC); a.jne(done);
        a.mov(x86::r11, reg); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); a.je(done);
        a.inc(x86::dword_ptr(x86::r11, 0));
        a.bind(done);
    };
    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]); uint32_t instr = chunk.code[i]; OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_LOADK: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, chunk.constants[instr & 0xFFFF].bits); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADINT: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, intTag | (uint32_t)decodeSBx(instr)); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_MOVE: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, regB); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_INC: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11d, regA.r32()); a.add(x86::r11d, 1); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_ADD_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_SUB_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_MUL_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.imul(x86::r11d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_ADDI: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, (int8_t)C); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_SUBI: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, (int8_t)C); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LT_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setl(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GT_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setg(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LE_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setle(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GE_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setge(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_EQ_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.sete(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_JMP:
            case OpCode::OP_LOOP: { a.jmp(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_JMPF: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r10, regA); a.and_(x86::r10, 1); a.cmp(x86::r10, 0); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_JLT_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; x86::Gp regB = (B < 5) ? vRegs[B] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.cmp(regA.r32(), regB.r32()); a.jl(labels[i + 2 + decodeSBx(chunk.code[i+1])]); i++; break;
            }
            case OpCode::OP_JGT_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; x86::Gp regB = (B < 5) ? vRegs[B] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.cmp(regA.r32(), regB.r32()); a.jg(labels[i + 2 + decodeSBx(chunk.code[i+1])]); i++; break;
            }
            case OpCode::OP_JLE_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; x86::Gp regB = (B < 5) ? vRegs[B] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.cmp(regA.r32(), regB.r32()); a.jle(labels[i + 2 + decodeSBx(chunk.code[i+1])]); i++; break;
            }
            case OpCode::OP_JGE_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; x86::Gp regB = (B < 5) ? vRegs[B] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.cmp(regA.r32(), regB.r32()); a.jge(labels[i + 2 + decodeSBx(chunk.code[i+1])]); i++; break;
            }
            case OpCode::OP_JNE_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; x86::Gp regB = (B < 5) ? vRegs[B] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.cmp(regA.r32(), regB.r32()); a.jne(labels[i + 2 + decodeSBx(chunk.code[i+1])]); i++; break;
            }
            case OpCode::OP_CALL: {
                int arity = (functions && B < functions->size()) ? (*functions)[B].arity : 5;
                for (int i = 0; i < arity && (A + i) < 5; ++i) a.mov(x86::qword_ptr(rBase, (A + i) * 8), vRegs[A + i]);
                flushRegs();
                
                // FAST PATH: Direct JIT call if available
                if (functions && B < functions->size() && (*functions)[B].chunk.jitFunc) {
                    a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8);
                    // Pass callee's constants!
                    a.mov(x86::rdx, (uint64_t)(*functions)[B].chunk.constants.data());
                    a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32);
                    a.call((uint64_t)(*functions)[B].chunk.jitFunc);
                    a.add(x86::rsp, 32);
                } else {
                    a.mov(x86::rcx, (uint64_t)B); a.mov(x86::rdx, rBase); a.add(x86::rdx, (uint64_t)A * 8); a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32); a.call((uint64_t)callFunctionHelper); a.add(x86::rsp, 32);
                }
                
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
            }
            case OpCode::OP_CALL_NATIVE: {
                auto* nativeFuncs = static_cast<std::vector<iris::core::NativeFunction*>*>(native_functions);
                void* nfPtr = (nativeFuncs && B < nativeFuncs->size()) ? (*nativeFuncs)[B] : nullptr;
                flushRegs(); a.mov(x86::rcx, (uint64_t)nfPtr); a.lea(x86::rdx, x86::qword_ptr(rBase, A * 8)); a.mov(x86::r8, (uint64_t)C);
                a.sub(x86::rsp, 32); a.call((uint64_t)callNativeHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
            }
            case OpCode::OP_RET: { if (A < 5) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8)); emitRetain(x86::rax); emitEpilogue(); break; }
            case OpCode::OP_NEW_OBJ: { flushRegs(); a.mov(x86::rcx, (uint64_t)(instr & 0xFFFF)); a.mov(x86::rdx, vmPtr); a.sub(x86::rsp, 32); a.call((uint64_t)createObjectHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GET_FIELD:
            case OpCode::OP_GET_FIELD_INT:
            case OpCode::OP_GET_FIELD_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) {
                    a.mov(regA, x86::qword_ptr(x86::rcx, 24 + C * 8));
                } else {
                    a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 16));
                    a.mov(regA, x86::qword_ptr(x86::rcx, (C - 4) * 8));
                }
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) {
                    a.lea(x86::r11, x86::qword_ptr(x86::r11, 24 + C * 8));
                } else {
                    a.mov(x86::r11, x86::qword_ptr(x86::r11, 16));
                    a.lea(x86::r11, x86::qword_ptr(x86::r11, (C - 4) * 8));
                }
                a.mov(x86::rcx, x86::qword_ptr(x86::r11)); // old field value
                a.push(x86::r11); a.push(regA); a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper);
                a.add(x86::rsp, 32); a.pop(regA); a.pop(x86::r11);
                a.mov(x86::qword_ptr(x86::r11), regA);
                emitRetain(regA);
                break;
            }
            case OpCode::OP_LOG: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8); a.sub(x86::rsp, 32); a.call((uint64_t)logHelper); a.add(x86::rsp, 32); break; }
            case OpCode::OP_HALT: emitEpilogue(); break;
            default: break;
        }
    }
    a.bind(labels[chunk.code.size()]); 
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions) {
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    Label funcEntry = a.new_label(); a.bind(funcEntry);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8); // Align to 16 bytes (8 pushes + 8 return addr + 8 sub = 80 bytes)
    a.mov(x86::rdi, x86::rcx); a.mov(x86::rsi, x86::rdx); a.mov(x86::r15, x86::r8);
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r15;
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::rbp, x86::rbx };
    for(int i = 0; i < 5; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    auto flushRegs = [&]() { for(int i = 0; i < 5; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 8); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };
    auto emitRelease = [&](x86::Gp reg) {
        Label done = a.new_label();
        a.mov(x86::r11, reg); a.shr(x86::r11, 48); a.cmp(x86::r11, 0xFFFC); a.jne(done);
        a.mov(x86::r11, reg); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); a.je(done);
        a.dec(x86::dword_ptr(x86::r11, 0)); a.jnz(done);
        a.mov(x86::dword_ptr(x86::r11, 0), 1);
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8); a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40); a.mov(x86::rcx, reg); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8); a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
        a.bind(done);
    };
    auto emitRetain = [&](x86::Gp reg) {
        Label done = a.new_label();
        a.mov(x86::r11, reg); a.shr(x86::r11, 48); a.cmp(x86::r11, 0xFFFC); a.jne(done);
        a.mov(x86::r11, reg); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); a.je(done);
        a.inc(x86::dword_ptr(x86::r11, 0));
        a.bind(done);
    };
    for (const auto& entry : trace.entries) { uint32_t instr = entry.instr; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_LOADK: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); emitRelease(regA); a.mov(x86::rax, (uint64_t)instr & 0xFFFF); a.mov(regA, x86::qword_ptr(constants, x86::rax, 3)); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADINT: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); emitRelease(regA); a.mov(regA, intTag | (uint32_t)decodeSBx(instr)); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_MOVE: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8)); emitRelease(regA); a.mov(regA, regB); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_ADD_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32)); emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32)); emitGuard(x86::CondCode::kEqual, entry.pc);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOOP: a.jmp(funcEntry); break;
            case OpCode::OP_NEW_OBJ: { flushRegs(); a.mov(x86::rcx, (uint64_t)(instr & 0xFFFF)); a.mov(x86::rdx, vmPtr); a.sub(x86::rsp, 32); a.call((uint64_t)createObjectHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GET_FIELD:
            case OpCode::OP_GET_FIELD_INT:
            case OpCode::OP_GET_FIELD_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) {
                    a.mov(regA, x86::qword_ptr(x86::rcx, 24 + C * 8));
                } else {
                    a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 16));
                    a.mov(regA, x86::qword_ptr(x86::rcx, (C - 4) * 8));
                }
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) {
                    a.lea(x86::r11, x86::qword_ptr(x86::r11, 24 + C * 8));
                } else {
                    a.mov(x86::r11, x86::qword_ptr(x86::r11, 16));
                    a.lea(x86::r11, x86::qword_ptr(x86::r11, (C - 4) * 8));
                }
                a.mov(x86::rcx, x86::qword_ptr(x86::r11)); // old field value
                a.push(x86::r11); a.push(regA); a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper);
                a.add(x86::rsp, 32); a.pop(regA); a.pop(x86::r11); a.mov(x86::qword_ptr(x86::r11), regA); emitRetain(regA); break; }
            default: a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); break;
        }
    }
    a.bind(sideExitTrampoline); emitEpilogue();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
