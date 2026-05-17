#include "JITCompiler.h"
#include "../core/Value.h"
#include <iostream>
#include <vector>

#include <asmjit/core.h>
#include <asmjit/x86.h>

using namespace iris::bytecode;
using namespace asmjit;

JITFunc JITCompiler::compile(Chunk &chunk) {
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
                a.mov(x86::byte_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, tag)),
                      (uint8_t) iris::core::Value::TAG_INT);
                a.mov(x86::dword_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)), val);
                break;
            }
            case OpCode::OP_MOVE: {
                a.mov(x86::rax, x86::qword_ptr(rBase, B * sizeof(iris::core::Value)));
                a.mov(x86::qword_ptr(rBase, A * sizeof(iris::core::Value)), x86::rax);
                a.mov(x86::rax, x86::qword_ptr(rBase, B * sizeof(iris::core::Value) + 8));
                a.mov(x86::qword_ptr(rBase, A * sizeof(iris::core::Value) + 8), x86::rax);
                break;
            }
            case OpCode::OP_ADD_INT: {
                a.mov(x86::eax, x86::dword_ptr(
                          rBase, B * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)));
                a.add(x86::eax, x86::dword_ptr(
                          rBase, C * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)));
                a.mov(x86::dword_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)),
                      x86::eax);
                a.mov(x86::byte_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, tag)),
                      (uint8_t) iris::core::Value::TAG_INT);
                break;
            }
            case OpCode::OP_GT_INT: {
                a.mov(x86::eax, x86::dword_ptr(
                          rBase, B * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)));
                a.cmp(x86::eax, dword_ptr(rBase, C * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)));
                a.setg(x86::al);
                a.movzx(x86::eax, x86::al);
                a.mov(x86::byte_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, asBool)),
                      x86::al);
                a.mov(x86::byte_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, tag)),
                      (uint8_t) iris::core::Value::TAG_BOOL);
                break;
            }
            case OpCode::OP_JMP: {
                int32_t offset = decodeSBx(instr);
                a.jmp(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_JMPF: {
                int32_t offset = decodeSBx(instr);
                a.cmp(x86::byte_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, asBool)), 0);
                a.je(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_INC: {
                a.inc(x86::dword_ptr(rBase, A * sizeof(iris::core::Value) + offsetof(iris::core::Value, asInt)));
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
