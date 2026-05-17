#include "JITCompiler.h"
#include "../core/Value.h"
#include <iostream>
#include <vector>

#include <asmjit/core.h>
#include <asmjit/x86.h>

using namespace iris::bytecode;
using namespace asmjit;

JITFunc JITCompiler::compile(Chunk& chunk) {
    CodeHolder code;
    code.init(rt.environment());
    x86::Assembler a(&code);

    std::vector<Label> labels(chunk.code.size());
    for (size_t i = 0; i < chunk.code.size(); ++i) { labels[i] = a.new_label(); }

    x86::Gp rBase = x86::rcx;
    std::vector<x86::Gp> vRegs = { x86::r8, x86::r9, x86::r10, x86::r11 };

    for(int i = 0; i < 4; i++) a.mov(vRegs[i], x86::qword_ptr(rBase, i * 8));

    auto flushRegs = [&]() {
        for(int i = 0; i < 4; i++) a.mov(x86::qword_ptr(rBase, i * 8), vRegs[i]);
    };

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]);
        uint32_t instr = chunk.code[i];
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr); uint8_t B = decodeB(instr); uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADINT: {
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)decodeSBx(instr);
                if (A < 4) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_MOVE: {
                if (A < 4 && B < 4) a.mov(vRegs[A], vRegs[B]);
                else if (A < 4) a.mov(vRegs[A], x86::qword_ptr(rBase, B * 8));
                else if (B < 4) a.mov(x86::qword_ptr(rBase, A * 8), vRegs[B]);
                else { a.mov(x86::rax, x86::qword_ptr(rBase, B * 8)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_ADD_INT: {
                x86::Gp regB = (B < 4) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 4) ? vRegs[C] : x86::rbx;
                if (B >= 4) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 4) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r12d, regB.r32()); a.add(x86::r12d, regC.r32());
                a.mov(x86::r13, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::r12, x86::r12d); a.or_(x86::r13, x86::r12);
                if (A < 4) a.mov(vRegs[A], x86::r13);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r13);
                break;
            }
            case OpCode::OP_GT_INT: {
                x86::Gp regB = (B < 4) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 4) ? vRegs[C] : x86::rbx;
                if (B >= 4) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 4) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setg(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r10, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r10, x86::rax);
                if (A < 4) a.mov(vRegs[A], x86::r10);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r10);
                break;
            }
            case OpCode::OP_JMP: a.jmp(labels[i + 1 + decodeSBx(instr)]); break;
            case OpCode::OP_JMPF: {
                x86::Gp regA = (A < 4) ? vRegs[A] : x86::rax;
                if (A >= 4) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); a.je(labels[i + 1 + decodeSBx(instr)]);
                break;
            }
            case OpCode::OP_INC: {
                if (A < 4) a.inc(vRegs[A].r32());
                else a.inc(x86::dword_ptr(rBase, A * 8));
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                // R[B] is ArrayData pointer (bits & mask)
                x86::Gp regB = (B < 4) ? vRegs[B] : x86::rax;
                if (B >= 4) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r12, regB); a.and_(x86::r12, 0x0000FFFFFFFFFFFFULL); // pointer
                // R[C] is index (bits & 0xFFFFFFFF)
                x86::Gp regC = (C < 4) ? vRegs[C] : x86::rbx;
                if (C >= 4) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r13, regC.r32());
                // Load intData[idx]
                a.mov(x86::r14, x86::qword_ptr(x86::r12, 8)); // intData pointer (offset 8 in ArrayData)
                a.mov(x86::r15d, x86::dword_ptr(x86::r14, x86::r13, 2)); // scale 4
                // Store to A
                a.mov(x86::rax, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::r15, x86::r15d); a.or_(x86::rax, x86::r15);
                if (A < 4) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_HALT:
            case OpCode::OP_RET: flushRegs(); a.ret(); break;
            default: flushRegs(); a.ret(); goto end_emit;
        }
    }
end_emit:
    JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
