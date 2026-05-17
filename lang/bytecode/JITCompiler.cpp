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

    // Labels for every instruction address
    std::vector<Label> labels(chunk.code.size());
    for (size_t i = 0; i < chunk.code.size(); ++i) {
        labels[i] = a.new_label();
    }

    // Function signature: void func(Value* R, Value* K)
    // x64 calling convention (Windows): RCX = R, RDX = K
    x86::Gp rBase = x86::rcx;
    x86::Gp kBase = x86::rdx;

    for (size_t i = 0; i < chunk.code.size(); ++i) {
        a.bind(labels[i]);
        uint32_t instr = chunk.code[i];
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr);
        uint8_t B = decodeB(instr);
        uint8_t C = decodeC(instr);

        switch (op) {
            case OpCode::OP_LOADINT: {
                int val = decodeSBx(instr);
                uint64_t bits = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)val;
                a.mov(x86::rax, bits);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_MOVE: {
                a.mov(x86::rax, x86::qword_ptr(rBase, B * 8));
                a.mov(x86::qword_ptr(rBase, A * 8), x86::rax);
                break;
            }
            case OpCode::OP_ADD_INT: {
                a.mov(x86::eax, x86::dword_ptr(rBase, B * 8));
                a.add(x86::eax, x86::dword_ptr(rBase, C * 8));
                a.mov(x86::r10, iris::core::Value::QNAN | iris::core::Value::TAG_INT);
                a.or_(x86::r10, x86::rax);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::r10);
                break;
            }
            case OpCode::OP_GT_INT: {
                a.mov(x86::eax, x86::dword_ptr(rBase, B * 8));
                a.cmp(x86::eax, x86::dword_ptr(rBase, C * 8));
                a.setg(x86::al);
                a.movzx(x86::eax, x86::al);
                a.mov(x86::r10, iris::core::Value::QNAN | iris::core::Value::TAG_BOOL);
                a.or_(x86::r10, x86::rax);
                a.mov(x86::qword_ptr(rBase, A * 8), x86::r10);
                break;
            }
            case OpCode::OP_JMP: {
                int32_t offset = decodeSBx(instr);
                a.jmp(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_JMPF: {
                int32_t offset = decodeSBx(instr);
                a.mov(x86::rax, x86::qword_ptr(rBase, A * 8));
                a.test(x86::al, 1);
                a.je(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_INC: {
                a.inc(x86::dword_ptr(rBase, A * 8));
                break;
            }
            case OpCode::OP_HALT:
                a.ret();
                break;
            case OpCode::OP_RET:
                a.ret();
                break;
            default:
                a.ret();
                goto end_emit;
        }
    }

end_emit:
    JITFunc func;
    Error err = rt.add(&func, &code);
    if (err != kErrorOk) {
        std::cerr << "JIT Error: " << DebugUtils::error_as_string(err) << std::endl;
        return nullptr;
    }

    return func;
}
