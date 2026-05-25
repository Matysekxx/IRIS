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
        a.mov(x86::rcx, reg);
        a.sub(x86::rsp, 32); a.call((uint64_t)releaseValueHelper); a.add(x86::rsp, 32);
    };

    auto emitRetain = [&](x86::Gp reg) {
        a.mov(x86::rcx, reg);
        a.sub(x86::rsp, 32); a.call((uint64_t)retainValueHelper); a.add(x86::rsp, 32);
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
                a.mov(x86::edx, regB.r32()); a.add(x86::edx, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::rdx);
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
                a.mov(x86::edx, regB.r32()); a.sub(x86::edx, regC.r32());
                a.mov(regA, intTag); a.or_(regA, x86::rdx);
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
                a.mov(x86::edx, regB.r32()); a.add(x86::edx, (int8_t)C);
                a.mov(regA, intTag); a.or_(regA, x86::rdx);
                if (A >= 5) a.mov(x86::qword_ptr(rBase, A * 8), regA);
                break;
            }
            case OpCode::OP_SUBI: {
                x86::Gp regB = (B < 5) ? vRegs[B] : x86::rax;
                if (B >= 5) a.mov(regB, x86::qword_ptr(rBase, B * 8));
                x86::Gp regA = (A < 5) ? vRegs[A] : x86::rdx;
                if (A >= 5) a.mov(regA, x86::qword_ptr(rBase, A * 8));
                emitRelease(regA);
                a.mov(x86::edx, regB.r32()); a.sub(x86::edx, (int8_t)C);
                a.mov(regA, intTag); a.or_(regA, x86::rdx);
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
                    for(int j = 0; j < 5; j++) a.mov(vRegs[j], x86::qword_ptr(rBase, j * 8));
                } else return nullptr;
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
            default: {
                std::cout << "[JIT] Unsupported OpCode: " << (int)op << " (" << (int)(instr >> 24) << ")" << std::endl;
                return nullptr;
            }
        }
    }
    a.bind(labels[chunk.code.size()]); JITFunc func;
    if (rt.add(&func, &code) != kErrorOk) return nullptr;
    return func;
}
