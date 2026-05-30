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
    CodeHolder code;
    code.init(rt.environment());
    x86::Assembler a(&code);

    std::vector<Label> labels(chunk.code.size() + 1);
    for (size_t i = 0; i <= chunk.code.size(); ++i) { labels[i] = a.new_label(); }

    Label funcEntry = a.new_label();
    a.bind(funcEntry);

    // Prologue
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15);
    a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx);
    a.sub(x86::rsp, 8); 

    // Windows x64: rcx = rBase, rdx = constants, r8 = VM*
    a.mov(x86::rdi, x86::rcx); // rBase = rdi
    a.mov(x86::rsi, x86::rdx); // constants = rsi
    a.mov(x86::r15, x86::r8);  // vmPtr = r15
    
    x86::Gp rBase = x86::rdi;
    x86::Gp constants = x86::rsi;
    x86::Gp vmPtr = x86::r15;
    
    // vRegs: R0-R4 mapped to non-volatiles
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::rbp, x86::rbx };
    for(int i = 0; i < 5; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;

    auto flushRegs = [&]() {
        for(int i = 0; i < 5; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
    };

    auto emitEpilogue = [&]() {
        flushRegs();
        a.add(x86::rsp, 8);
        a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi);
        a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12);
        a.ret();
    };

    auto emitRelease = [&](x86::Gp reg) {
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8);
        a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40);
        a.mov(x86::rcx, reg);
        a.call((uint64_t)releaseValueHelper);
        a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8);
        a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
    };

    auto emitRetain = [&](x86::Gp reg) {
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8);
        a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40);
        a.mov(x86::rcx, reg);
        a.call((uint64_t)retainValueHelper);
        a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8);
        a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]);
        uint32_t instr = chunk.code[i];
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADK: {
                uint64_t bits = chunk.constants[instr & 0xFFFF].bits;
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, bits);
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADBOOL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, boolTag | (B ? 1 : 0));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADINT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, intTag | (uint32_t)decodeSBx(instr));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADNULL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, nullTag);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MOVE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, regB);
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MOVE_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::eax, regB.r32());
                a.mov(regA, intTag); a.or_(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs();
                a.mov(x86::rcx, regB); a.mov(x86::rdx, regC);
                a.sub(x86::rsp, 32);
                if (op == OpCode::OP_ADD) a.call((uint64_t)addHelper);
                else if (op == OpCode::OP_SUB) a.call((uint64_t)subHelper);
                else if (op == OpCode::OP_MUL) a.call((uint64_t)mulHelper);
                else a.call((uint64_t)divHelper);
                a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_ADD_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUB_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_INC: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.inc(regA.r32()); a.or_(regA, intTag);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_ADDI: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, (int8_t)C);
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUBI: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, (int8_t)C);
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LT_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.cmp(regB.r32(), regC.r32()); a.setl(x86::al); a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOOP:
            case OpCode::OP_JMP: a.jmp(labels[(int32_t)i + 1 + decodeSBx(instr)]); break;
            case OpCode::OP_JMPF: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); a.je(labels[(int32_t)i + 1 + decodeSBx(instr)]);
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t funcIdx = B;
                void* jitFunc = (*functions)[funcIdx].chunk.jitFunc;
                void* constsPtr = (*functions)[funcIdx].chunk.constants.data();
                if (jitFunc || &(*functions)[funcIdx].chunk == &chunk) {
                    flushRegs();
                    a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8);
                    a.mov(x86::rdx, (uint64_t)constsPtr);
                    a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32);
                    if (jitFunc) a.call((uint64_t)jitFunc); else a.call(funcEntry);
                    a.add(x86::rsp, 32);
                    
                    x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                    if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    emitRelease(regA);
                    a.mov(regA, x86::rax);
                    if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                    
                    for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                } else {
                    flushRegs();
                    a.mov(x86::rcx, (uint64_t)funcIdx);
                    a.mov(x86::rdx, rBase); a.add(x86::rdx, A * 8);
                    a.mov(x86::r8, vmPtr);
                    a.sub(x86::rsp, 32);
                    a.call((uint64_t)callFunctionHelper);
                    a.add(x86::rsp, 32);
                    
                    x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                    if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                    emitRelease(regA);
                    a.mov(regA, x86::rax);
                    if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                    
                    for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                }
                break;
            }
            case OpCode::OP_CALL_NATIVE: {
                auto* nfs = static_cast<std::vector<iris::core::NativeFunction*>*>(native_functions);
                iris::core::NativeFunction* nf = (*nfs)[B];
                flushRegs();
                a.mov(x86::rcx, (uint64_t)nf);
                a.mov(x86::rdx, rBase); a.add(x86::rdx, A * 8);
                a.mov(x86::r8, (uint64_t)C);
                a.sub(x86::rsp, 32); a.call((uint64_t)callNativeHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_NEW_OBJ: {
                flushRegs();
                a.mov(x86::rcx, (uint64_t)B); // classId
                a.mov(x86::rdx, vmPtr);
                a.sub(x86::rsp, 32); a.call((uint64_t)createObjectHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24)); // fields pointer
                a.mov(regA, x86::qword_ptr(x86::rcx, C * 8));
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GET_FIELD_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24)); // fields pointer
                a.movsxd(x86::rax, x86::dword_ptr(x86::rcx, C * 8));
                a.or_(x86::rax, intTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r11, x86::qword_ptr(x86::r11, 24)); // fields pointer
                a.mov(x86::rcx, x86::qword_ptr(x86::r11, C * 8)); // old field value
                a.push(x86::r11); a.push(regA);
                a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 32);
                a.pop(regA); a.pop(x86::r11);
                a.mov(x86::qword_ptr(x86::r11, C * 8), regA);
                emitRetain(regA);
                break;
            }
            case OpCode::OP_INVOKE: {
                flushRegs();
                a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8); 
                a.mov(x86::rdx, (uint64_t)B); 
                a.mov(x86::r8, (uint64_t)C);
                a.mov(x86::r9, constants);
                a.sub(x86::rsp, 48); // aligned shadow space
                a.mov(x86::qword_ptr(x86::rsp, 32), vmPtr);
                a.call((uint64_t)invokeHelper);
                a.add(x86::rsp, 48);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_RET: {
                if (A < 5) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                emitRetain(x86::rax);
                emitEpilogue();
                break;
            }
            case OpCode::OP_LOG: {
                flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8);
                a.sub(x86::rsp, 32); a.call((uint64_t)logHelper); a.add(x86::rsp, 32);
                for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_HALT: emitEpilogue(); break;
            case OpCode::OP_ADD_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.addsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUB_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.subsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MUL_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.mulsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_DIV_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.divsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LT_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GT_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.seta(x86::al);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LE_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setbe(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GE_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setae(x86::al);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_EQ_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.movsxd(x86::rax, regC.r32());
                a.mov(x86::r11, x86::qword_ptr(x86::r10, 16));
                a.mov(regA, x86::qword_ptr(x86::r11, x86::rax, 3));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_SET_DBL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.movsxd(x86::r11, regC.r32());
                a.mov(x86::rax, x86::qword_ptr(x86::r10, 16));
                a.mov(x86::qword_ptr(x86::rax, x86::r11, 3), regA);
                break;
            }
            case OpCode::OP_GET_FIELD_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24));
                a.mov(regA, x86::qword_ptr(x86::rcx, C * 8));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_COLL_LEN: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.movsxd(x86::rax, x86::dword_ptr(x86::rcx, 24));
                a.or_(x86::rax, intTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            default: {
                std::cerr << "[DEBUG JIT] compile unsupported opcode: " << (int)op << std::endl;
                return nullptr;
            }
        }
    }
    a.bind(labels[chunk.code.size()]); JITFunc func;
    Error err = rt.add(&func, &code);
    if (err != kErrorOk) {
        std::cerr << "[DEBUG JIT] compile rt.add failed with error code: " << (uint32_t)err << std::endl;
        return nullptr;
    }
    return func;
}

JITFunc JITCompiler::compileTrace(Trace& trace, void* functions_ptr, void* native_functions) {
    auto* functions = static_cast<std::vector<FunctionObject>*>(functions_ptr);
    CodeHolder code;
    code.init(rt.environment());
    x86::Assembler a(&code);

    Label funcEntry = a.new_label();
    a.bind(funcEntry);

    // Prologue
    a.push(x86::r12); a.push(x86::r13); a.push(x86::r14); a.push(x86::r15);
    a.push(x86::rdi); a.push(x86::rsi); a.push(x86::rbp); a.push(x86::rbx);
    a.sub(x86::rsp, 8); 

    // Windows x64: rcx = rBase, rdx = constants, r8 = VM*
    a.mov(x86::rdi, x86::rcx); // rBase = rdi
    a.mov(x86::rsi, x86::rdx); // constants = rsi
    a.mov(x86::r15, x86::r8);  // vmPtr = r15
    
    x86::Gp rBase = x86::rdi;
    x86::Gp constants = x86::rsi;
    x86::Gp vmPtr = x86::r15;
    
    // vRegs: R0-R4 mapped to non-volatiles
    std::vector<x86::Gp> vRegs = { x86::r12, x86::r13, x86::r14, x86::rbp, x86::rbx };
    for(int i = 0; i < 5; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;
    uint64_t nullTag = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;

    auto flushRegs = [&]() {
        for(int i = 0; i < 5; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
    };

    auto emitEpilogue = [&]() {
        flushRegs();
        a.add(x86::rsp, 8);
        a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi);
        a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12);
        a.ret();
    };

    Label sideExitTrampoline = a.new_label();

    auto emitGuard = [&](x86::CondCode cond, const uint32_t* failPC) {
        Label ok = a.new_label();
        a.j(cond, ok);
        a.mov(x86::rax, (uint64_t)failPC);
        a.jmp(sideExitTrampoline);
        a.bind(ok);
    };

    auto emitRelease = [&](x86::Gp reg) {
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8);
        a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40);
        a.mov(x86::rcx, reg);
        a.call((uint64_t)releaseValueHelper);
        a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8);
        a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
    };

    auto emitRetain = [&](x86::Gp reg) {
        a.push(x86::rax); a.push(x86::rcx); a.push(x86::rdx); a.push(x86::r8);
        a.push(x86::r9); a.push(x86::r10); a.push(x86::r11);
        a.sub(x86::rsp, 40);
        a.mov(x86::rcx, reg);
        a.call((uint64_t)retainValueHelper);
        a.add(x86::rsp, 40);
        a.pop(x86::r11); a.pop(x86::r10); a.pop(x86::r9); a.pop(x86::r8);
        a.pop(x86::rdx); a.pop(x86::rcx); a.pop(x86::rax);
    };

    for (const auto& entry : trace.entries) {
        uint32_t instr = entry.instr;
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADK: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::rax, (uint64_t)instr & 0xFFFF);
                a.mov(regA, x86::qword_ptr(constants, x86::rax, 3)); 
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADINT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, intTag | (uint32_t)decodeSBx(instr));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADBOOL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, boolTag | (B ? 1 : 0));
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LOADNULL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, nullTag);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MOVE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, regB);
                emitRetain(regA);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_ADD_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.add(x86::r11d, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUB_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.sub(x86::r11d, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MUL_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::r11d, regB.r32()); a.imul(x86::r11d, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_INC: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r10, regA); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.inc(regA.r32()); a.or_(regA, intTag);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_DEC: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r10, regA); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.dec(regA.r32()); a.or_(regA, intTag);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LT_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.cmp(regB.r32(), regC.r32()); a.setl(x86::al); a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag); a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GT_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.cmp(regB.r32(), regC.r32()); a.setg(x86::al); a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag); a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                
                // Guard: B is array, C is int
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                // Check if it's an array and correct element type
                a.cmp(x86::byte_ptr(x86::r10, 32), 1); // ElementType::INT = 1
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                // Bounds check
                a.movsxd(x86::rax, regC.r32());
                a.cmp(x86::rax, x86::qword_ptr(x86::r10, 24)); // length at offset 24
                emitGuard(x86::CondCode::kB, entry.pc);
                
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                
                a.mov(x86::r11, x86::qword_ptr(x86::r10, 16)); // data pointer at offset 16
                a.movsxd(x86::rax, x86::dword_ptr(x86::r11, x86::rax, 2)); // scale 4 (int)
                a.or_(x86::rax, intTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_SET_INT: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                
                // Guards
                a.mov(x86::r10, regA); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 32), 1); // ElementType::INT = 1
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                a.movsxd(x86::r11, regC.r32());
                a.cmp(x86::r11, x86::qword_ptr(x86::r10, 24));
                emitGuard(x86::CondCode::kB, entry.pc);
                
                a.mov(x86::rax, x86::qword_ptr(x86::r10, 16));
                a.mov(x86::r10d, regA.r32());
                a.mov(x86::dword_ptr(x86::rax, x86::r11, 2), x86::r10d);
                break;
            }
            case OpCode::OP_JMPF: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); 
                if (entry.branchTaken) {
                    emitGuard(x86::CondCode::kEqual, entry.pc); 
                } else {
                    emitGuard(x86::CondCode::kNotEqual, entry.pc);
                }
                break;
            }
            case OpCode::OP_LOOP: {
                a.jmp(funcEntry);
                break;
            }
            case OpCode::OP_ADD_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.addsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUB_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.subsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_MUL_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.mulsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_DIV_DOUBLE: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.divsd(x86::xmm0, x86::xmm1);
                a.movq(regA, x86::xmm0);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LT_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GT_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.seta(x86::al);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_LE_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setbe(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GE_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setae(x86::al);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_EQ_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));

                // Guard: B is double
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is double
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rax;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movq(x86::xmm0, regB);
                a.movq(x86::xmm1, regC);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al);
                Label not_nan = a.new_label();
                a.jnp(not_nan);
                a.xor_(x86::eax, x86::eax);
                a.bind(not_nan);
                a.movzx(x86::rax, x86::al);
                a.or_(x86::rax, boolTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                
                // Guard: C is int
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                // Check if it's an array and correct element type (ElementType::DOUBLE = 2)
                a.cmp(x86::byte_ptr(x86::r10, 32), 2);
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                // Bounds check
                a.movsxd(x86::rax, regC.r32());
                a.cmp(x86::rax, x86::qword_ptr(x86::r10, 24)); // length at offset 24
                emitGuard(x86::CondCode::kB, entry.pc);
                
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                
                a.mov(x86::r11, x86::qword_ptr(x86::r10, 16)); // data pointer at offset 16
                a.mov(regA, x86::qword_ptr(x86::r11, x86::rax, 3)); // scale 8
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_IDX_SET_DBL: {
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rcx;
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 5) ? vRegs[C] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 5) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                
                // Guard: A is double
                a.mov(x86::r10, regA); a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                // Guard: C is int
                a.mov(x86::r10, regC); a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                // Check if it's an array and correct element type (ElementType::DOUBLE = 2)
                a.cmp(x86::byte_ptr(x86::r10, 32), 2);
                emitGuard(x86::CondCode::kEqual, entry.pc);
                
                // Bounds check
                a.movsxd(x86::r11, regC.r32());
                a.cmp(x86::r11, x86::qword_ptr(x86::r10, 24));
                emitGuard(x86::CondCode::kB, entry.pc);
                
                a.mov(x86::rax, x86::qword_ptr(x86::r10, 16));
                a.mov(x86::qword_ptr(x86::rax, x86::r11, 3), regA);
                break;
            }
            case OpCode::OP_NEW_OBJ: {
                flushRegs();
                a.mov(x86::rcx, (uint64_t)B); // classId
                a.mov(x86::rdx, vmPtr);
                a.sub(x86::rsp, 32); a.call((uint64_t)createObjectHelper); a.add(x86::rsp, 32);
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));

                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Guard: B is Object type (type == 1 at offset 12)
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 12), 1);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24)); // fields pointer
                a.mov(regA, x86::qword_ptr(x86::rcx, C * 8));
                emitRetain(regA);

                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));

                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Guard: B is Object type (type == 1 at offset 12)
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 12), 1);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                a.mov(x86::r11, regB); a.and_(x86::r11, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r11, x86::qword_ptr(x86::r11, 24)); // fields pointer
                a.mov(x86::rcx, x86::qword_ptr(x86::r11, C * 8)); // old field value
                a.push(x86::r11); a.push(regA);
                a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 32);
                a.pop(regA); a.pop(x86::r11);
                a.mov(x86::qword_ptr(x86::r11, C * 8), regA);
                emitRetain(regA);
                break;
            }
            case OpCode::OP_GET_FIELD_INT: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));

                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Guard: B is Object type (type == 1 at offset 12)
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 12), 1);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Fields pointer
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24));
                // Guard: the field value is an integer
                a.mov(x86::r10, x86::qword_ptr(x86::rcx, C * 8));
                a.mov(x86::r11, x86::r10);
                a.shr(x86::r10, 32); a.cmp(x86::r10d, (uint32_t)(intTag >> 32));
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movsxd(x86::rax, x86::r11.r32());
                a.or_(x86::rax, intTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_GET_FIELD_DBL: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));

                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Guard: B is Object type (type == 1 at offset 12)
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 12), 1);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Fields pointer
                a.mov(x86::rcx, regB); a.and_(x86::rcx, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::rcx, x86::qword_ptr(x86::rcx, 24));
                // Guard: the field value is a double
                a.mov(x86::r10, x86::qword_ptr(x86::rcx, C * 8));
                a.mov(x86::r11, x86::r10);
                a.shr(x86::r10, 32); a.and_(x86::r10d, 0x7FFC0000); a.cmp(x86::r10d, 0x7FFC0000);
                emitGuard(x86::CondCode::kNotEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.mov(regA, x86::r11);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_COLL_LEN: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));

                // Guard: B is ptr
                a.mov(x86::r10, regB); a.shr(x86::r10, 32); a.cmp(x86::r10d, 0xFFFC0000);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                // Guard: B is Array type (type == 2 at offset 12)
                a.mov(x86::r10, regB); a.and_(x86::r10, 0x0000FFFFFFFFFFFFULL);
                a.cmp(x86::byte_ptr(x86::r10, 12), 2);
                emitGuard(x86::CondCode::kEqual, entry.pc);

                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);

                a.movsxd(x86::rax, x86::dword_ptr(x86::r10, 24)); // length at offset 24
                a.or_(x86::rax, intTag);
                a.mov(regA, x86::rax);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            default: {
                a.mov(x86::rax, (uint64_t)entry.pc);
                a.jmp(sideExitTrampoline);
                break;
            }
        }
    }

    a.bind(sideExitTrampoline);
    emitEpilogue();

    JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
