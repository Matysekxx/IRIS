#include "JITCompiler.h"
#include "../core/Value.h"
#include "Compiler.h"
#include <iostream>
#include <vector>

#include <asmjit/core.h>
#include <asmjit/x86.h>

using namespace iris::bytecode;
using namespace asmjit;

JITFunc JITCompiler::compile(Chunk& chunk, void* functions_ptr) {
    auto* functions = static_cast<std::vector<FunctionObject>*>(functions_ptr);
    CodeHolder code;
    code.init(rt.environment());
    x86::Assembler a(&code);

    std::vector<Label> labels(chunk.code.size());
    for (size_t i = 0; i < chunk.code.size(); ++i) { labels[i] = a.new_label(); }

    // Prologue: save non-volatile registers we use
    a.push(x86::r12);
    a.push(x86::r13);
    a.sub(x86::rsp, 8); // 16-byte align stack

    x86::Gp rBase = x86::rcx;
    std::vector<x86::Gp> vRegs = { x86::r8, x86::r9, x86::r10, x86::r11, x86::r12, x86::r13 };

    for(int i = 0; i < 6; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    auto flushRegs = [&]() {
        for(int i = 0; i < 6; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]);
        uint32_t instr = chunk.code[i];
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADINT: {
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)decodeSBx(instr);
                if (A < 6) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
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
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::edx, regB.r32()); a.add(x86::edx, regC.r32());
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::r15, x86::edx); a.or_(x86::r14, x86::r15);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_SUBI: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14d, regB.r32());
                a.sub(x86::r14d, (int8_t)C);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::r14, x86::r14d); a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_MUL_DOUBLE: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.mulsd(x86::xmm0, x86::xmm1);
                if (A < 6) a.movq(vRegs[A], x86::xmm0); else a.movq(x86::qword_ptr(rBase, A * 8), x86::xmm0);
                break;
            }
            case OpCode::OP_ADD_DOUBLE: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.addsd(x86::xmm0, x86::xmm1);
                if (A < 6) a.movq(vRegs[A], x86::xmm0); else a.movq(x86::qword_ptr(rBase, A * 8), x86::xmm0);
                break;
            }
            case OpCode::OP_LT_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setl(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_GT_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setg(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_JMP: a.jmp(labels[i + 1 + decodeSBx(instr)]); break;
            case OpCode::OP_JMPF: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); a.je(labels[i + 1 + decodeSBx(instr)]);
                break;
            }
            case OpCode::OP_INC: {
                if (A < 6) a.inc(vRegs[A].r32());
                else a.inc(x86::dword_ptr(rBase, A * 8));
                break;
            }
            case OpCode::OP_IDX_GET: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 8));
                a.mov(x86::rax, x86::qword_ptr(x86::r14, x86::r15, 3));
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 8));
                a.mov(x86::rax, x86::qword_ptr(x86::r14, x86::r15, 3));
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_CALL: {
                if (!functions) return nullptr; // Abort JIT compilation
                uint8_t funcIdx = B;
                void** jitFuncPtr = &(*functions)[funcIdx].chunk.jitFunc;
                void* constsPtr = (*functions)[funcIdx].chunk.constants.data();

                flushRegs();

                a.mov(x86::rax, (uint64_t)jitFuncPtr);
                a.mov(x86::rax, x86::qword_ptr(x86::rax));

                // If target is not JITted yet, just bail out (we will never reach here for fib)
                Label skipCall = a.new_label();
                a.test(x86::rax, x86::rax);
                a.jz(skipCall);

                a.mov(x86::rcx, rBase);
                a.add(x86::rcx, A * 8);
                a.mov(x86::rdx, (uint64_t)constsPtr);

                a.push(x86::rcx);
                a.push(x86::rdx);
                a.sub(x86::rsp, 32);
                a.call(x86::rax);
                a.add(x86::rsp, 32);
                a.pop(x86::rdx);
                a.pop(x86::rcx);

                a.bind(skipCall);

                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_HALT:
            case OpCode::OP_RET: {
                if (A != 0) {
                    if (A < 6) {
                        a.mov(vRegs[0], vRegs[A]);
                    } else {
                        a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                        a.mov(vRegs[0], x86::rax);
                    }
                }
                flushRegs(); 
                
                // Epilogue
                a.add(x86::rsp, 8);
                a.pop(x86::r13);
                a.pop(x86::r12);

                a.ret(); 
                break;
            }
            default: return nullptr; // ABORT JIT FOR UNSUPPORTED OPCODES
        }
    }
end_emit:
    JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
