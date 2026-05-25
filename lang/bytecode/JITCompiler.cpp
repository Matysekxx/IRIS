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
    std::string name = "unknown";
    if (functions) {
        for (const auto& f : *functions) {
            if (&f.chunk == &chunk) {
                name = f.name;
                break;
            }
        }
    }
    std::cout << "[JIT] Attempting compile: " << name << std::endl;
    CodeHolder code;
    code.init(rt.environment());
    x86::Assembler a(&code);

    std::vector<Label> labels(chunk.code.size());
    for (size_t i = 0; i < chunk.code.size(); ++i) { labels[i] = a.new_label(); }

    Label funcStartLabel = a.new_label();
    a.bind(funcStartLabel);

    // Prologue: save non-volatile registers we use
    a.push(x86::r12);
    a.push(x86::r13);
    a.push(x86::r14);
    a.push(x86::r15);
    a.push(x86::rdi);
    a.push(x86::rsi);
    a.sub(x86::rsp, 8); // 16-byte align stack

    a.mov(x86::rdi, x86::rcx);
    x86::Gp rBase = x86::rdi;
    std::vector<x86::Gp> vRegs = { x86::r8, x86::r9, x86::r10, x86::r11, x86::r12, x86::r13 };

    Label entryLabel = a.new_label();
    a.bind(entryLabel);

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
            case OpCode::OP_LOADK: {
                uint16_t constIdx = instr & 0xFFFF;
                iris::core::Value constVal = chunk.constants[constIdx];
                uint64_t bits = constVal.bits;
                if (A < 6) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADBOOL: {
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (B ? 1 : 0);
                if (A < 6) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADNULL: {
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
                if (A < 6) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_LOADINT: {
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)decodeSBx(instr);
                if (A < 6) a.mov(vRegs[A], bits);
                else { a.mov(x86::rax, bits); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_MOVE_INT:
            case OpCode::OP_MOVE: {
                if (A < 6 && B < 6) a.mov(vRegs[A], vRegs[B]);
                else if (A < 6) a.mov(vRegs[A], x86::qword_ptr(rBase, B * 8));
                else if (B < 6) a.mov(x86::qword_ptr(rBase, A * 8), vRegs[B]);
                else { a.mov(x86::rax, x86::qword_ptr(rBase, B * 8)); a.mov(x86::qword_ptr(rBase, A * 8), x86::rax); }
                break;
            }
            case OpCode::OP_ADD: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs();
                a.mov(x86::rcx, regB);
                a.mov(x86::rdx, regC);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)addHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_SUB: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs();
                a.mov(x86::rcx, regB);
                a.mov(x86::rdx, regC);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)subHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_MUL: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs();
                a.mov(x86::rcx, regB);
                a.mov(x86::rdx, regC);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)mulHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_DIV: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                flushRegs();
                a.mov(x86::rcx, regB);
                a.mov(x86::rdx, regC);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)divHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_ADD_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r14d, regB.r32()); a.add(x86::r14d, regC.r32());
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r14, x86::r15);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_SUB_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r14d, regB.r32()); a.sub(x86::r14d, regC.r32());
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r14, x86::r15);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_MUL_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r14d, regB.r32()); a.imul(x86::r14d, regC.r32());
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r14, x86::r15);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_ADDI: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14d, regB.r32());
                a.add(x86::r14d, (int8_t)C);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_SUBI: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14d, regB.r32());
                a.sub(x86::r14d, (int8_t)C);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r15, x86::r14);
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
            case OpCode::OP_SUB_DOUBLE: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.subsd(x86::xmm0, x86::xmm1);
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
            case OpCode::OP_EQ_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.sete(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_LE_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setle(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_GE_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.cmp(regB.r32(), regC.r32()); a.setge(x86::al); a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
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
            case OpCode::OP_JMPT: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.test(regA.r8(), 1); a.jne(labels[(int32_t)i + 1 + decodeSBx(instr)]);
                break;
            }
            case OpCode::OP_NOT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.test(regB.r8(), 1);
                a.sete(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL);
                a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_AND: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 1);
                a.mov(x86::r15, regC); a.and_(x86::r15, 1);
                a.and_(x86::r14d, x86::r15d);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL);
                a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_OR: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 1);
                a.mov(x86::r15, regC); a.and_(x86::r15, 1);
                a.or_(x86::r14d, x86::r15d);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL);
                a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_INC: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r14d, regA.r32());
                a.inc(x86::r14d);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_DEC: {
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rax;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::r14d, regA.r32());
                a.dec(x86::r14d);
                a.mov(x86::r15, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r15, x86::r14);
                if (A < 6) a.mov(vRegs[A], x86::r15);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r15);
                break;
            }
            case OpCode::OP_GET_FIELD: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 24)); // ObjectData::fields (offset 24)
                a.mov(x86::rax, x86::qword_ptr(x86::r14, (uint32_t)C * 8));
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_GET_FIELD_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 24)); // ObjectData::fields
                a.mov(x86::eax, x86::dword_ptr(x86::r14, (uint32_t)C * 8)); // Value::asInt()
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_SET_FIELD: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 24)); // ObjectData::fields
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rcx;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::qword_ptr(x86::r14, (uint32_t)C * 8), regA);
                break;
            }
            case OpCode::OP_IDX_GET: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 16));
                a.mov(x86::rax, x86::qword_ptr(x86::r14, x86::r15, 3));
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_SET: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 16));
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rcx;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::qword_ptr(x86::r14, x86::r15, 3), regA);
                break;
            }
            case OpCode::OP_IDX_GET_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 32)); // ArrayData::intData (offset 32)
                a.mov(x86::eax, x86::dword_ptr(x86::r14, x86::r15, 2));
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_IDX_SET_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 32)); // ArrayData::intData
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rcx;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::dword_ptr(x86::r14, x86::r15, 2), regA.r32());
                break;
            }
            case OpCode::OP_IDX_GET_DBL: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 24)); // ArrayData::dblData (offset 24)
                a.mov(x86::rax, x86::qword_ptr(x86::r14, x86::r15, 3));
                if (A < 6) a.mov(vRegs[A], x86::rax); else a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_IDX_SET_DBL: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.movsxd(x86::r15, regC.r32());
                a.mov(x86::r14, x86::qword_ptr(x86::r14, 24)); // ArrayData::dblData
                x86::Gp regA = (A < 6) ? vRegs[A] : x86::rcx;
                if (A >= 6) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                a.mov(x86::qword_ptr(x86::r14, x86::r15, 3), regA);
                break;
            }
            case OpCode::OP_COLL_LEN: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::r14, regB); a.and_(x86::r14, 0x0000FFFFFFFFFFFFULL);
                a.mov(x86::r15, x86::qword_ptr(x86::r14, 24));
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::rax, x86::r15d);
                a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14);
                else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_NEW_ARRAY: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                flushRegs();
                a.movsxd(x86::rcx, regB.r32());
                a.mov(x86::rdx, (uint64_t)C);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)createArrayHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_CALL_NATIVE: {
                if (!native_functions) return nullptr;
                auto* nfs = static_cast<std::vector<iris::core::NativeFunction*>*>(native_functions);
                uint8_t funcIdx = B;
                iris::core::NativeFunction* nf = (*nfs)[funcIdx];
                flushRegs();
                a.mov(x86::rcx, (uint64_t)nf);
                a.mov(x86::rdx, rBase);
                a.add(x86::rdx, A * 8);
                a.mov(x86::r8, (uint64_t)C);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)callNativeHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_LT_DBL: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setb(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_GT_DBL: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.seta(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_LE_DBL: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setbe(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_GE_DBL: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.setae(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_EQ_DBL: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.sete(x86::al);
                a.movzx(x86::rax, x86::al);
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL); a.or_(x86::r14, x86::rax);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_DIV_DOUBLE: {
                if (B < 6) a.movq(x86::xmm0, vRegs[B]); else a.movq(x86::xmm0, x86::qword_ptr(rBase, B * 8));
                if (C < 6) a.movq(x86::xmm1, vRegs[C]); else a.movq(x86::xmm1, x86::qword_ptr(rBase, C * 8));
                a.divsd(x86::xmm0, x86::xmm1);
                if (A < 6) a.movq(vRegs[A], x86::xmm0); else a.movq(x86::qword_ptr(rBase, A * 8), x86::xmm0);
                break;
            }
            case OpCode::OP_DIV_INT: {
                x86::Gp regB = (B < 6) ? vRegs[B] : x86::rax;
                x86::Gp regC = (C < 6) ? vRegs[C] : x86::rbx;
                if (B >= 6) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                if (C >= 6) a.mov(regC, x86::qword_ptr(rBase, C * 8));
                a.mov(x86::eax, regB.r32());
                a.cdq();
                a.idiv(regC.r32());
                a.mov(x86::r14, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.movzx(x86::r15, x86::eax); a.or_(x86::r14, x86::r15);
                if (A < 6) a.mov(vRegs[A], x86::r14); else a.mov(x86::qword_ptr(rBase, A * 8), x86::r14);
                break;
            }
            case OpCode::OP_CALL: {
                if (!functions) return nullptr; // Abort JIT compilation
                uint8_t funcIdx = B;
                void* constsPtr = (*functions)[funcIdx].chunk.constants.data();

                flushRegs();

                if (&(*functions)[funcIdx].chunk == &chunk) {
                    // Self-recursive call: call funcStartLabel!
                    a.mov(x86::rcx, rBase);
                    a.add(x86::rcx, A * 8);
                    a.mov(x86::rdx, (uint64_t)constsPtr);

                    a.push(x86::rcx);
                    a.push(x86::rdx);
                    a.sub(x86::rsp, 32);
                    a.call(funcStartLabel);
                    a.add(x86::rsp, 32);
                    a.pop(x86::rdx);
                    a.pop(x86::rcx);
                } else {
                    // Call to another function: compile callee on demand at compile-time!
                    if (!(*functions)[funcIdx].chunk.jitFunc && !(*functions)[funcIdx].chunk.jitAttempted) {
                        (*functions)[funcIdx].chunk.jitAttempted = true;
                        iris::bytecode::JITCompiler calleeJit;
                        (*functions)[funcIdx].chunk.jitFunc = (void*) calleeJit.compile((*functions)[funcIdx].chunk, functions, native_functions);
                    }

                    // If callee cannot be JITted, we MUST abort compilation of the caller as well
                    if (!(*functions)[funcIdx].chunk.jitFunc) {
                        return nullptr;
                    }

                    void* jitFunc = (*functions)[funcIdx].chunk.jitFunc;

                    a.mov(x86::rax, (uint64_t)jitFunc);
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
                }

                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_LOG: {
                flushRegs();
                a.mov(x86::rcx, rBase);
                a.add(x86::rcx, A * 8);
                a.sub(x86::rsp, 48);
                a.mov(x86::rax, (uint64_t)logHelper);
                a.call(x86::rax);
                a.add(x86::rsp, 48);
                for(int j = 0; j < 6; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                break;
            }
            case OpCode::OP_TYPECHECK: {
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
                a.pop(x86::rsi);
                a.pop(x86::rdi);
                a.pop(x86::r15);
                a.pop(x86::r14);
                a.pop(x86::r13);
                a.pop(x86::r12);

                a.ret(); 
                break;
            }
            default: {
                std::cout << "[JIT] Unsupported OpCode in " << name << ": " << (int)(instr >> 24) << std::endl;
                return nullptr;
            }
        }
    }
end_emit:
    JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    std::cout << "[JIT] Compiled successfully: " << name << std::endl;
    return func;
}
