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

    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::r17); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8);
    a.mov(x86::rdi, x86::rcx); a.mov(x86::rsi, x86::rdx); a.mov(x86::r17, x86::r8);
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r17;
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r9, x86::r10 };
    int arity = (currentFuncIdx != -1) ? (int)(*functions)[currentFuncIdx].arity : 0;
    int numRegsToLoad = std::min(arity, 8);
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    for(int i = 0; i < numRegsToLoad; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    for(int i = numRegsToLoad; i < 8; i++) a.mov(vRegs[i], nullTag);

    auto flushRegs = [&]() { for(int i = 0; i < 8; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 8); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r17); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    auto emitRelease = [&](x86::Gp reg) {
        // RC disabled -- GC manages memory. Intentionally empty.
    };
    auto emitRetain = [&](x86::Gp reg) {
        // RC disabled -- GC manages memory. Intentionally empty.
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]); uint32_t instr = chunk.code[i]; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_LOADK: { x86::Gp regA = (A < 7) ? vRegs[A] : x86::rax; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, chunk.constants[instr & 0xFFFF].bits); emitRetain(regA); if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADINT: { x86::Gp regA = (A < 7) ? vRegs[A] : x86::rax; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, intTag | (uint32_t)decodeSBx(instr)); if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADBOOL: { x86::Gp regA = (A < 7) ? vRegs[A] : x86::rax; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, boolTag | (B != 0 ? 1 : 0)); if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADNULL: { x86::Gp regA = (A < 7) ? vRegs[A] : x86::rax; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, nullTag); if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_MOVE: { x86::Gp regB = (B < 7) ? vRegs[B] : x86::rax; if (B >= 7) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 7) ? vRegs[A] : x86::rdx; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, regB); emitRetain(regA); if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_INC: { x86::Gp regA = (A < 7) ? vRegs[A] : x86::rax; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11d, regA.r32()); a.add(x86::r11d, 1); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_ADD_INT: { x86::Gp regB = (B < 7) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 7) ? vRegs[C] : x86::rdx;
                if (B >= 7) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 7) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 7) ? vRegs[A] : x86::rcx; if (A >= 7) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, regC.r32()); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 7) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_ADDI: { x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, (int8_t)C); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_SUBI: { x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, (int8_t)C); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LT_INT: { x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 8) ? vRegs[C] : x86::rdx;
                if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 8) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx; if (A >= 8) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setl(x86::r11b); a.movzx(x86::r11d, x86::r11b); a.mov(regA, boolTag); a.or_(regA, x86::r11);
                if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GET_FIELD: {
                x86::Gp regB = (B < 8) ? vRegs[B] : x86::rax; if (B >= 8) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 8) ? vRegs[A] : x86::rcx;
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL);
                if (C < 4) {
                    uint64_t offset = offsetof(iris::core::ObjectData, inlinedFields) + C * 8;
                    a.mov(regA, x86::qword_ptr(x86::r11, (int32_t)offset));
                } else {
                    // Fallback to helper or complex logic? For now, just skip or handle few more.
                }
                if (A >= 8) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
            }
            case OpCode::OP_RET: { if (A < 7) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8)); emitRetain(x86::rax); emitEpilogue(); break; }
            case OpCode::OP_LOG: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8); a.sub(x86::rsp, 32); a.call((uint64_t)logHelper); a.add(x86::rsp, 32); break; }
            case OpCode::OP_HALT: emitEpilogue(); break;
            default: return nullptr;
        }
    }
    a.bind(labels[chunk.code.size()]);
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions) {
    std::cout << "[JIT] Compiling trace with size: " << trace.entries.size() << "\n";
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::r17); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8);
    a.mov(x86::rdi, x86::rcx); a.mov(x86::rsi, x86::rdx); a.mov(x86::r17, x86::r8);
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r17;

    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::r15, x86::rbp, x86::rbx, x86::r9, x86::r10 };
    std::vector<bool> isUnboxed(8, false); // Expanded to 8
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    uint16_t intPrefix = (uint16_t)(intTag >> 48);

    for(int i = 0; i < 8; i++) {
        a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    }

    Label funcEntry = a.new_label(); a.bind(funcEntry);

    auto flushRegs = [&](bool preserveRax = false) {
        for(int i = 0; i < 8; i++) {
            if (isUnboxed[i]) {
                if (preserveRax) {
                    a.mov(x86::r11, intTag); a.or_(x86::r11, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, i * 8), x86::r11);
                } else {
                    a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[i].r64()); a.mov(x86::qword_ptr(rBase, i * 8), x86::rax);
                }
            } else {
                a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
            }
        }
    };

    auto emitEpilogue = [&]() { flushRegs(); a.add(x86::rsp, 8); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r17); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret(); };
    Label sideExitTrampoline = a.new_label();
    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) { Label ok = a.new_label(); a.j(cond, ok); a.mov(x86::rax, (uint64_t)failPC); a.jmp(sideExitTrampoline); a.bind(ok); };

    for (const auto& entry : trace.entries) { 
        std::cout << "[JIT] Embedding failPC: " << entry.pc << " for instr: " << (int)decodeOp(entry.instr) << "\n";
        uint32_t instr = entry.instr; OpCode op = decodeOp(instr); uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        
        switch (op) {
            case OpCode::OP_LOADINT: {
                if (A < 7) {
                    a.mov(vRegs[A].r32(), (uint32_t)decodeSBx(instr));
                    isUnboxed[A] = true;
                } else { a.mov(x86::rax, intTag | (uint32_t)decodeSBx(instr)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_ADD_INT: {
                if (A < 7 && B < 7 && C < 7) {
                    if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; }
                    if (!isUnboxed[C]) { a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[C].r32(), vRegs[C].r32()); isUnboxed[C] = true; }
                    a.mov(x86::r11d, vRegs[B].r32()); a.add(x86::r11d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_SUB_INT: {
                if (A < 7 && B < 7 && C < 7) {
                    if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; }
                    if (!isUnboxed[C]) { a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[C].r32(), vRegs[C].r32()); isUnboxed[C] = true; }
                    a.mov(x86::r11d, vRegs[B].r32()); a.sub(x86::r11d, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_MUL_INT: {
                if (A < 7 && B < 7 && C < 7) {
                    if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; }
                    if (!isUnboxed[C]) { a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[C].r32(), vRegs[C].r32()); isUnboxed[C] = true; }
                    a.mov(x86::eax, vRegs[B].r32()); a.imul(x86::eax, vRegs[C].r32()); a.mov(vRegs[A].r32(), x86::eax); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_ADD_DOUBLE: {
                if (A < 7 && B < 7 && C < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    if (isUnboxed[C]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.addsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_SUB_DOUBLE: {
                if (A < 7 && B < 7 && C < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    if (isUnboxed[C]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.subsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_MUL_DOUBLE: {
                if (A < 7 && B < 7 && C < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    if (isUnboxed[C]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.mulsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_DIV_DOUBLE: {
                if (A < 7 && B < 7 && C < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    if (isUnboxed[C]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.and_(x86::r11w, 0x7FFC); a.cmp(x86::r11w, 0x7FFC); emitGuard(x86::CondCode::kNotEqual, entry.pc);
                    a.movq(x86::xmm0, vRegs[B]); a.movq(x86::xmm1, vRegs[C]); a.divsd(x86::xmm0, x86::xmm1); a.movq(vRegs[A], x86::xmm0); isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_LOADK: {
                uint16_t constIdx = instr & 0xFFFF;
                if (A < 7) {
                    a.mov(vRegs[A], x86::qword_ptr(constants, constIdx * 8));
                    isUnboxed[A] = false;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_MOVE: {
                if (A < 7 && B < 7) {
                    if (isUnboxed[A] && isUnboxed[B]) { a.mov(vRegs[A].r32(), vRegs[B].r32()); }
                    else if (!isUnboxed[A] && !isUnboxed[B]) { a.mov(vRegs[A], vRegs[B]); }
                    else if (isUnboxed[A] && !isUnboxed[B]) {
                         a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); 
                         a.mov(vRegs[A].r32(), vRegs[B].r32());
                    } else {
                         a.mov(vRegs[A], intTag); a.or_(vRegs[A], vRegs[B].r64());
                    }
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_GET_FIELD: {
                if (A < 7 && B < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.mov(x86::r10, x86::r11); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(iris::core::Value::TAG_PTR >> 48)); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); emitGuard(x86::CondCode::kNotZero, entry.pc);
                    if (C < iris::core::ObjectData::INLINED_FIELDS) {
                        uint64_t offset = offsetof(iris::core::ObjectData, inlinedFields) + C * sizeof(iris::core::Value);
                        a.mov(vRegs[A], x86::qword_ptr(x86::r11, (int32_t)offset));
                        isUnboxed[A] = false;
                    } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_SET_FIELD: {
                if (B < 7) {
                    if (isUnboxed[B]) { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    a.mov(x86::r11, vRegs[B]); a.mov(x86::r10, x86::r11); a.shr(x86::r10, 48); a.cmp(x86::r10w, (uint16_t)(iris::core::Value::TAG_PTR >> 48)); emitGuard(x86::CondCode::kEqual, entry.pc);
                    a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.test(x86::r11, x86::r11); emitGuard(x86::CondCode::kNotZero, entry.pc);
                    if (C < iris::core::ObjectData::INLINED_FIELDS) {
                        uint64_t offset = offsetof(iris::core::ObjectData, inlinedFields) + C * sizeof(iris::core::Value);
                        if (A < 7) {
                            if (isUnboxed[A]) { a.mov(x86::rax, intTag); a.or_(x86::rax, vRegs[A].r64()); a.mov(x86::qword_ptr(x86::r11, (int32_t)offset), x86::rax); }
                            else { a.mov(x86::qword_ptr(x86::r11, (int32_t)offset), vRegs[A]); }
                        } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                    } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_INC: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.inc(vRegs[A].r32());
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_ADDI: { 
                if (A < 7 && B < 7) {
                    if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; }
                    a.mov(x86::r11d, vRegs[B].r32()); a.add(x86::r11d, (int8_t)C); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_SUBI: { 
                if (A < 7 && B < 7) {
                    if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; }
                    a.mov(x86::r11d, vRegs[B].r32()); a.sub(x86::r11d, (int8_t)C); a.mov(vRegs[A].r32(), x86::r11d); isUnboxed[A] = true;
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_LT_INT: {
                x86::Gp valB = x86::eax; x86::Gp valC = x86::edx;
                if (B < 7) { if (!isUnboxed[B]) { a.mov(x86::r11, vRegs[B]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[B].r32(), vRegs[B].r32()); isUnboxed[B] = true; } a.mov(valB, vRegs[B].r32()); } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                if (C < 7) { if (!isUnboxed[C]) { a.mov(x86::r11, vRegs[C]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[C].r32(), vRegs[C].r32()); isUnboxed[C] = true; } a.mov(valC, vRegs[C].r32()); } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                a.cmp(valB, valC); a.setl(x86::r10b); a.movzx(x86::r10d, x86::r10b);
                if (A < 7) { a.mov(vRegs[A], boolTag); a.or_(vRegs[A], x86::r10); isUnboxed[A] = false; }
                else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_JMPF: {
                x86::Gp val = x86::r10d;
                if (A < 7) {
                    if (isUnboxed[A]) { a.mov(x86::r11d, vRegs[A].r32()); a.and_(x86::r11d, 1); a.mov(val, x86::r11d); }
                    else { a.mov(x86::r11, vRegs[A]); a.and_(x86::r11, 1); a.mov(val, x86::r11d); }
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                if (entry.branchTaken) { a.cmp(val, 0); emitGuard(x86::CondCode::kEqual, entry.pc); }
                else { a.cmp(val, 1); emitGuard(x86::CondCode::kEqual, entry.pc); }
                break;
            }
            case OpCode::OP_ADDI_W: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.add(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_SUBI_W: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.sub(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_JGE_INT_IMM: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.cmp(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                    if (entry.branchTaken) emitGuard(x86::CondCode::kGE, entry.pc);
                    else emitGuard(x86::CondCode::kL, entry.pc);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_JLT_INT_IMM: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.cmp(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                    if (entry.branchTaken) emitGuard(x86::CondCode::kL, entry.pc);
                    else emitGuard(x86::CondCode::kGE, entry.pc);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_JLE_INT_IMM: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.cmp(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                    if (entry.branchTaken) emitGuard(x86::CondCode::kLE, entry.pc);
                    else emitGuard(x86::CondCode::kG, entry.pc);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_JGT_INT_IMM: {
                if (A < 7) {
                    if (!isUnboxed[A]) { a.mov(x86::r11, vRegs[A]); a.shr(x86::r11, 48); a.cmp(x86::r11w, intPrefix); emitGuard(x86::CondCode::kEqual, entry.pc); a.mov(vRegs[A].r32(), vRegs[A].r32()); isUnboxed[A] = true; }
                    a.cmp(vRegs[A].r32(), (int32_t)decodeSBx(instr));
                    if (entry.branchTaken) emitGuard(x86::CondCode::kG, entry.pc);
                    else emitGuard(x86::CondCode::kLE, entry.pc);
                } else { a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); }
                break;
            }
            case OpCode::OP_LOG: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8); a.sub(x86::rsp, 40); a.call((uint64_t)logHelper); a.add(x86::rsp, 40); break; }
            case OpCode::OP_LOOP: a.jmp(funcEntry); break;
            default: a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); break;
        }
    }
    a.bind(sideExitTrampoline); 
    a.push(x86::rax); flushRegs(true); a.mov(x86::rcx, x86::rax); a.sub(x86::rsp, 40); a.call((uint64_t)sideExitDiagnostic); a.add(x86::rsp, 40); a.pop(x86::rax);
    a.add(x86::rsp, 8); a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r17); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}