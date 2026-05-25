#include "JITCompiler.h"
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

    a.mov(x86::rdi, x86::rcx); // rBase = rdi
    x86::Gp rBase = x86::rdi;
    std::vector<x86::Gp> vRegs = { x86::r8, x86::r9, x86::r10, x86::r11, x86::r12, x86::r13 };

    for(int i = 0; i < 6; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    uint64_t intTag = iris::core::Value::QNAN | iris::core::Value::TAG_INT;
    uint64_t boolTag = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL;

    auto flushRegs = [&]() {
        for(int i = 0; i < 6; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
    };

    auto emitEpilogue = [&]() {
        flushRegs();
        a.add(x86::rsp, 8);
        a.pop(x86::rbx); a.pop(x86::rbp); a.pop(x86::rsi); a.pop(x86::rdi);
        a.pop(x86::r15); a.pop(x86::r14); a.pop(x86::r13); a.pop(x86::r12);
        a.ret();
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]);
        uint32_t instr = chunk.code[i];
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADK: {
                uint64_t bits = chunk.constants[instr & 0xFFFF].bits;
                if (A < 6) a.mov(vRegs[A], bits); else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADBOOL: {
                uint64_t bits = boolTag | (B ? 1 : 0);
                if (A < 6) a.mov(vRegs[A], bits); else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADINT: {
                uint64_t bits = intTag | (uint32_t)decodeSBx(instr);
                if (A < 6) a.mov(vRegs[A], bits); else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_MOVE: {
                if (A < 6 && B < 6) a.mov(vRegs[A], vRegs[B]);
                else if (A < 6) a.mov(vRegs[A], x86::qword_ptr(rBase, B * 8));
                else if (B < 6) a.mov(x86::qword_ptr(rBase, A * 8), vRegs[B]);
                else { a.mov(x86::rax, x86::qword_ptr(rBase, B * 8)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_ADD_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rdx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::edx, regB.r32()); a.add(x86::edx, regC.r32());
                a.mov(x86::rax, intTag); a.movzx(x86::r14, x86::edx); a.or_(x86::rax, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_INC: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.inc(regA.r32()); a.mov(x86::r14, intTag); a.or_(regA, x86::r14);
                if (A >= 6) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_ADDI: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.add(regB.r32(), (int8_t)C); a.mov(x86::r14, intTag); a.or_(regB, x86::r14);
                if (A < 6) a.mov(vRegs[A], regB); else a.mov(x86::qword_ptr(rBase, A * 8), regB);
                break;
            }
            case OpCode::OP_SUBI: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.sub(regB.r32(), (int8_t)C); a.mov(x86::r14, intTag); a.or_(regB, x86::r14);
                if (A < 6) a.mov(vRegs[A], regB); else a.mov(x86::qword_ptr(rBase, A * 8), regB);
                break;
            }
            case OpCode::OP_LT_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rdx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setl(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, boolTag); a.or_(x86::rax, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_LOOP:
            case OpCode::OP_JMP: a.jmp(labels[(int32_t)i + 1 + decodeSBx(instr)]); break;
            case OpCode::OP_JMPF: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); a.je(labels[(int32_t)i + 1 + decodeSBx(instr)]);
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::rdx, regB); a.and_(x86::rdx, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::rsi, regC.r32());
                a.mov(x86::rdx, x86::qword_ptr(x86::rdx, 16));
                a.movsxd(x86::rax, x86::dword_ptr(x86::rdx, x86::rsi, 2));
                a.mov(x86::r14, intTag); a.or_(x86::rax, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_SET_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::rdx, regB); a.and_(x86::rdx, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rax;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::rsi, regC.r32());
                a.mov(x86::rdx, x86::qword_ptr(x86::rdx, 16));
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::dword_ptr(x86::rdx, x86::rsi, 2), regA.r32());
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t funcIdx = B;
                if (!(*functions)[funcIdx].chunk.jitFunc && !(*functions)[funcIdx].chunk.jitAttempted) {
                    (*functions)[funcIdx].chunk.jitAttempted = true;
                    (*functions)[funcIdx].chunk.jitFunc = (void*) this->compile((*functions)[funcIdx].chunk, functions_ptr, native_functions);
                }
                void* jitFunc = (*functions)[funcIdx].chunk.jitFunc;
                void* constsPtr = (*functions)[funcIdx].chunk.constants.data();
                if (jitFunc || &(*functions)[funcIdx].chunk == &chunk) {
                    flushRegs();
                    a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8);
                    a.mov(x86::rdx, (uint64_t)constsPtr);
                    a.sub(x86::rsp, 32);
                    if (jitFunc) a.call((uint64_t)jitFunc); else a.call(funcEntry);
                    a.add(x86::rsp, 32);
                    if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                    for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                } else return nullptr;
                break;
            }
            case OpCode::OP_RET: {
                if (A < 6) a.mov(x86::rax, vRegs[A]); else a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                emitEpilogue();
                break;
            }
            case OpCode::OP_LOG: {
                flushRegs(); a.mov(x86::rcx, rBase); a.add(x86::rcx, A * 8);
                a.sub(x86::rsp, 32); a.call((uint64_t)logHelper); a.add(x86::rsp, 32);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_HALT: emitEpilogue(); break;
            default: return nullptr;
        }
    }
    a.bind(labels[chunk.code.size()]);
    JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
