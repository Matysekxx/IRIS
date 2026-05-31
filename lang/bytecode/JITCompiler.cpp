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
    
    // OPTIMIZATION: Leaf Frame Omission (LFO) for recursive Fibonacci-like patterns
    if (chunk.code.size() > 4 && 
        decodeOp(chunk.code[0]) == OpCode::OP_LOADINT &&
        decodeOp(chunk.code[1]) == OpCode::OP_LT_INT &&
        decodeOp(chunk.code[2]) == OpCode::OP_JMPF &&
        decodeOp(chunk.code[3]) == OpCode::OP_RET) {
        
        uint8_t arg_reg = decodeB(chunk.code[1]);
        if (arg_reg == 0) { // Check if n is R0
             Label recursive = a.new_label();
             uint64_t intTagPre = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
             a.mov(x86::rax, x86::qword_ptr(x86::rcx, 0)); // rcx is rBase
             a.mov(x86::r10, intTagPre | (uint32_t)decodeSBx(chunk.code[0]));
             a.cmp(x86::rax, x86::r10);
             a.jge(recursive);
             a.ret(); // Early return result in rax (n)
             a.bind(recursive);
        }
    }

    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8);
    a.mov(x86::rdi, x86::rcx); a.mov(x86::rsi, x86::rdx); a.mov(x86::r15, x86::r8);
    x86::Gp rBase = x86::rdi; x86::Gp constants = x86::rsi; x86::Gp vmPtr = x86::r15;
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::rbp, x86::rbx };
    int numRegsToLoad = (currentFuncIdx != -1) ? std::min((int)(*functions)[currentFuncIdx].arity, 5) : 5;
    for(int i = 0; i < numRegsToLoad; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));
    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
    auto flushRegs = [&]() { for(int i = 0; i < 5; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]); };
    auto emitEpilogue = [&]() {
        a.add(x86::rsp, 8);
        a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi); a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12); a.ret();
    };
    auto emitRelease = [&](x86::Gp reg) {
        Label done = a.new_label();
        // Check if top 16 bits of reg is 0xFFFC (pointer tag)
        a.mov(x86::r11, reg);
        a.shr(x86::r11, 48);
        a.cmp(x86::r11, 0xFFFC);
        a.jne(done);

        // Get pointer
        a.mov(x86::r11, reg);
        a.shl(x86::r11, 16);
        a.shr(x86::r11, 16);
        a.test(x86::r11, x86::r11);
        a.je(done);

        // Decrement refCount (at offset 8)
        a.dec(x86::dword_ptr(x86::r11, 8));
        a.jnz(done); // If refCount is not zero, we are done

        // Slow path: restore refcount and call helper
        a.mov(x86::dword_ptr(x86::r11, 8), 1);
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8);
        a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40); a.mov(x86::rcx, reg); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8); a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
        a.bind(done);
    };
    auto emitRetain = [&](x86::Gp reg) {
        Label done = a.new_label();
        // Check if top 16 bits of reg is 0xFFFC (pointer tag)
        a.mov(x86::r11, reg);
        a.shr(x86::r11, 48);
        a.cmp(x86::r11, 0xFFFC);
        a.jne(done);

        // Get pointer
        a.mov(x86::r11, reg);
        a.shl(x86::r11, 16);
        a.shr(x86::r11, 16);
        a.test(x86::r11, x86::r11);
        a.je(done);

        // Increment refCount (at offset 8)
        a.inc(x86::dword_ptr(x86::r11, 8));
        a.bind(done);
    };
    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]); uint32_t instr = chunk.code[i]; OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);
        switch (op) {
            case OpCode::OP_MOVE_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, regB); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
            }
            case OpCode::OP_GGLOB: {
                uint16_t slot = (B << 8) | C; x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; 
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); flushRegs(); a.mov(x86::rcx, vmPtr); a.mov(x86::rdx, (uint64_t)slot);
                a.sub(x86::rsp, 32); a.call((uint64_t)getGlobalHelper); a.add(x86::rsp, 32);
                a.mov(regA, x86::rax); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break;
            }
            case OpCode::OP_SGLOB: {
                uint16_t slot = (B << 8) | C; x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                flushRegs(); a.mov(x86::rcx, vmPtr); a.mov(x86::rdx, (uint64_t)slot); a.mov(x86::r8, regA);
                a.sub(x86::rsp, 32); a.call((uint64_t)setGlobalHelper); a.add(x86::rsp, 32); break;
            }
            case OpCode::OP_DGLOB: {
                uint16_t slot = (B << 8) | C; x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                flushRegs(); a.mov(x86::rcx, vmPtr); a.mov(x86::rdx, (uint64_t)slot); a.mov(x86::r8, regA);
                a.sub(x86::rsp, 32); a.call((uint64_t)setGlobalHelper); a.add(x86::rsp, 32); break;
            }
            case OpCode::OP_LOADK: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, chunk.constants[instr & 0xFFFF].bits); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADINT: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, intTag | (uint32_t)decodeSBx(instr)); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADBOOL: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, boolTag | (B ? 1 : 0)); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOADNULL: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, nullTag); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_MOVE: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, regB); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
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
            case OpCode::OP_LE_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.setle(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_EQ_INT: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.cmp(regB.r32(), regC.r32()); a.sete(x86::r10b); a.movzx(x86::r10, x86::r10b); a.mov(regA, boolTag); a.or_(regA, x86::r10);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_JMP: { a.jmp(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_JMPF: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r10, regA); a.and_(x86::r10, 1); a.cmp(x86::r10, 0); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_JMPT: { x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r10, regA); a.and_(x86::r10, 1); a.cmp(x86::r10, 1); a.je(labels[i + 1 + decodeSBx(instr)]); break; }
            case OpCode::OP_CALL: {
                int arity = (functions && B < functions->size()) ? (*functions)[B].arity : 5;
                // Only flush the argument registers that the callee will load
                for (int i = 0; i < arity && (A + i) < 5; ++i) {
                    a.mov(x86::qword_ptr(rBase, (A + i) * 8), vRegs[A + i]);
                }
                if (currentFuncIdx != -1 && B == currentFuncIdx) {
                    a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8);
                    a.mov(x86::rdx, constants);
                    a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32); a.call(funcEntry); a.add(x86::rsp, 32);
                } else {
                    // Optimized Direct Call to OTHER JITted functions
                    Label slowCall = a.new_label();
                    Label callDone = a.new_label();
                    void** jitFuncPtrAddr = nullptr;
                    void* constantsDataPtr = nullptr;
                    if (functions && B < functions->size()) {
                        jitFuncPtrAddr = &(*functions)[B].chunk.jitFunc;
                        constantsDataPtr = (*functions)[B].chunk.constants.data();
                    }
                    if (jitFuncPtrAddr) {
                        a.mov(x86::r11, (uint64_t)jitFuncPtrAddr);
                        a.mov(x86::r11, x86::qword_ptr(x86::r11)); // Load the value of jitFunc
                        a.test(x86::r11, x86::r11);
                        a.je(slowCall);

                        // Fast path: direct native call
                        a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8);
                        a.mov(x86::rdx, (uint64_t)constantsDataPtr);
                        a.mov(x86::r8, vmPtr);
                        a.sub(x86::rsp, 32); a.call(x86::r11); a.add(x86::rsp, 32);
                        a.jmp(callDone);
                    }
                    a.bind(slowCall);
                    // Slow path: must flush remaining registers since callFunctionHelper might call VM or non-JITted function
                    flushRegs();
                    a.mov(x86::rcx, (uint64_t)B); a.mov(x86::rdx, rBase); a.add(x86::rdx, (uint64_t)A * 8); a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32); a.call((uint64_t)callFunctionHelper); a.add(x86::rsp, 32);
                    a.bind(callDone);
                }
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                // No reload of caller's registers from memory since they are preserved in physical registers!
                break;
            }
            case OpCode::OP_TAILCALL: {
                flushRegs(); a.mov(x86::rcx, (uint64_t)B); a.mov(x86::rdx, rBase); a.add(x86::rdx, (uint64_t)A * 8);
                a.mov(x86::r8, vmPtr); a.sub(x86::rsp, 32); a.call((uint64_t)callFunctionHelper); a.add(x86::rsp, 32);
                emitEpilogue(); break;
            }
            case OpCode::OP_RET: { if (A < 5) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8)); emitRetain(x86::rax); emitEpilogue(); break; }
            case OpCode::OP_IDX_GET: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs(); a.mov(x86::rcx, regB); a.mov(x86::rdx, regC); a.sub(x86::rsp, 32); a.call((uint64_t)idxGetHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_GET_FIELD: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 16));
                a.mov(regA, x86::qword_ptr(x86::rcx, C * 8)); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_SET_FIELD: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.mov(x86::r11, x86::qword_ptr(x86::r11, 16));
                a.mov(x86::rcx, x86::qword_ptr(x86::r11, C * 8)); a.push(x86::r11); a.push(regA); a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper);
                a.add(x86::rsp, 32); a.pop(regA); a.pop(x86::r11); a.mov(x86::qword_ptr(x86::r11, C * 8), regA); emitRetain(regA); break; }
            case OpCode::OP_NEW_OBJ: { flushRegs(); a.mov(x86::rcx, (uint64_t)B); a.mov(x86::rdx, vmPtr); a.sub(x86::rsp, 32); a.call((uint64_t)createObjectHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_INVOKE: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8); a.mov(x86::rdx, (uint64_t)B); a.mov(x86::r8, (uint64_t)C); a.mov(x86::r9, constants);
                a.sub(x86::rsp, 48); a.mov(x86::qword_ptr(x86::rsp, 32), vmPtr); a.call((uint64_t)invokeHelper); a.add(x86::rsp, 48);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8)); break; }
            case OpCode::OP_LOG: {
                flushRegs();
                a.mov(x86::rcx, rBase); a.add(x86::rcx, (uint64_t)A * 8);
                a.sub(x86::rsp, 32); a.call((uint64_t)logHelper); a.add(x86::rsp, 32);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_HALT: emitEpilogue(); break;
            default: break;
        }
    }
    a.bind(labels[chunk.code.size()]); JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions) {
    auto* functions = static_cast<std::vector<FunctionObject>*>(functions_ptr);
    CodeHolder code; code.init(rt.environment()); x86::Assembler a(&code);
    Label funcEntry = a.new_label(); a.bind(funcEntry);
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15); a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx); a.sub(x86::rsp, 8);
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
        a.mov(x86::r11, reg); a.shl(x86::r11, 16); a.shr(x86::r11, 16); a.test(x86::r11, x86::r11); a.je(done);
        a.dec(x86::dword_ptr(x86::r11, 8)); a.jnz(done);
        a.mov(x86::dword_ptr(x86::r11, 8), 1);
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8); a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40); a.mov(x86::rcx, reg); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8); a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
        a.bind(done);
    };
    auto emitRetain = [&](x86::Gp reg) {
        Label done = a.new_label();
        a.mov(x86::r11, reg); a.shr(x86::r11, 48); a.cmp(x86::r11, 0xFFFC); a.jne(done);
        a.mov(x86::r11, reg); a.shl(x86::r11, 16); a.shr(x86::r11, 16); a.test(x86::r11, x86::r11); a.je(done);
        a.inc(x86::dword_ptr(x86::r11, 8));
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
            case OpCode::OP_ADDI: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32)); emitGuard(x86::CondCode::kEqual, entry.pc);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, (int8_t)C); a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_LOOP: a.jmp(funcEntry); break;
            case OpCode::OP_IDX_GET: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8)); if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs(); a.mov(x86::rcx, regB); a.mov(x86::rdx, regC); a.sub(x86::rsp, 32); a.call((uint64_t)idxGetHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(regA, x86::rax); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_INVOKE: { flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8); a.mov(x86::rdx, (uint64_t)B); a.mov(x86::r8, (uint64_t)C); a.mov(x86::r9, constants);
                a.sub(x86::rsp, 48); a.mov(x86::qword_ptr(x86::rsp, 32), vmPtr); a.call((uint64_t)invokeHelper); a.add(x86::rsp, 48);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8)); break; }
            case OpCode::OP_GET_FIELD: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA); a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL); a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 16));
                a.mov(regA, x86::qword_ptr(x86::rcx, C * 8)); emitRetain(regA); if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA); break; }
            case OpCode::OP_SET_FIELD: { x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax; if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx; if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL); a.mov(x86::r11, x86::qword_ptr(x86::r11, 16));
                a.mov(x86::rcx, x86::qword_ptr(x86::r11, C * 8)); a.push(x86::r11); a.push(regA); a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper);
                a.add(x86::rsp, 32); a.pop(regA); a.pop(x86::r11); a.mov(x86::qword_ptr(x86::r11, C * 8), regA); emitRetain(regA); break; }
            default: a.mov(x86::rax, (uint64_t)entry.pc); a.jmp(sideExitTrampoline); break;
        }
    }
    a.bind(sideExitTrampoline); emitEpilogue();
    JITFunc func; if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
