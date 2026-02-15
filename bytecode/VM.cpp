#include "VM.h"
#include <iostream>
#include <stdexcept>

void VM::execute(Chunk& ch, IDeviceDriver* drv, Logger* log) {
    chunk = &ch;
    ip = ch.code.data();
    driver = drv;
    logger = log;
    stackTop = stack;
    globals.clear();
    run();
}

void VM::run() {
    for (;;) {
        switch (static_cast<OpCode>(readByte())) {

        case OpCode::OP_CONST: {
            uint16_t idx = readShort();
            push(chunk->constants[idx]);
            break;
        }
        case OpCode::OP_TRUE:  push(true);break;
        case OpCode::OP_FALSE: push(false);break;
        case OpCode::OP_NULL:  push(std::monostate{});break;

        case OpCode::OP_ADD: {
            const Value b = pop();
            const Value a = pop();
            if (const int* pa = std::get_if<int>(&a)) {
                if (const int* pb = std::get_if<int>(&b)) {
                    push(*pa + *pb);
                    break;
                }
            }
            push(toString(a) + toString(b));
            break;
        }
        case OpCode::OP_SUB: {
            Value b = pop(); Value a = pop();
            if (const int* pa = std::get_if<int>(&a)) {
                if (const int* pb = std::get_if<int>(&b)) {
                    push(*pa - *pb);
                    break;
                }
            } else {
                throw std::runtime_error("Operand must be a number");
            }
            break;
        }
        case OpCode::OP_MUL: {
            Value b = pop(); Value a = pop();
            push(std::get<int>(a) * std::get<int>(b));
            break;
        }
        case OpCode::OP_DIV: {
            Value b = pop(); Value a = pop();
            int divisor = std::get<int>(b);
            if (divisor == 0) throw std::runtime_error("Division by zero");
            push(std::get<int>(a) / divisor);
            break;
        }
        case OpCode::OP_NEGATE: {
            Value a = pop();
            push(-std::get<int>(a));
            break;
        }
        case OpCode::OP_NOT: {
            Value a = pop();
            if (const bool* p = std::get_if<bool>(&a)) {
                push(!(*p));
            } else {
                throw std::runtime_error("Operator '!' requires a boolean operand.");
            }
            break;
        }
        case OpCode::OP_AND: { Value b = pop(); Value a = pop(); push(std::get<bool>(a) && std::get<bool>(b)); break; }
        case OpCode::OP_OR:  { Value b = pop(); Value a = pop(); push(std::get<bool>(a) || std::get<bool>(b)); break; }

        case OpCode::OP_EQ:  { Value b = pop(); Value a = pop(); push(a == b);  break; }
        case OpCode::OP_NEQ: { Value b = pop(); Value a = pop(); push(a != b);  break; }

        case OpCode::OP_LT:  { Value b = pop(); Value a = pop(); push(std::get<int>(a) < std::get<int>(b)); break; }
        case OpCode::OP_GT:  { Value b = pop(); Value a = pop(); push(std::get<int>(a) > std::get<int>(b)); break; }
        case OpCode::OP_LE:  { Value b = pop(); Value a = pop(); push(std::get<int>(a) <= std::get<int>(b)); break; }
        case OpCode::OP_GE:  { Value b = pop(); Value a = pop(); push(std::get<int>(a) >= std::get<int>(b)); break; }
        
        case OpCode::OP_BIT_AND: { Value b = pop(); Value a = pop(); push(std::get<int>(a) &  std::get<int>(b)); break; }
        case OpCode::OP_BIT_OR:  { Value b = pop(); Value a = pop(); push(std::get<int>(a) |  std::get<int>(b)); break; }
        case OpCode::OP_BIT_XOR: { Value b = pop(); Value a = pop(); push(std::get<int>(a) ^  std::get<int>(b)); break; }
        case OpCode::OP_SHL:     { Value b = pop(); Value a = pop(); push(std::get<int>(a) << std::get<int>(b)); break; }
        case OpCode::OP_SHR:     { Value b = pop(); Value a = pop(); push(std::get<int>(a) >> std::get<int>(b)); break; }

        case OpCode::OP_GET_VAR: {
            uint16_t idx = readShort();
            const auto& name = std::get<std::string>(chunk->constants[idx]);
            auto it = globals.find(name);
            if (it == globals.end()) {
                throw std::runtime_error("Undefined variable: " + name);
            }
            push(it->second.value);
            break;
        }
        case OpCode::OP_SET_VAR: {
            uint16_t idx = readShort();
            const auto& name = std::get<std::string>(chunk->constants[idx]);
            auto it = globals.find(name);
            if (it == globals.end()) {
                throw std::runtime_error("Variable '" + name + "' is not declared.");
            }
            if (!it->second.isMutable) {
                throw std::runtime_error("Variable '" + name + "' is immutable.");
            }
            it->second.value = pop();
            break;
        }
        case OpCode::OP_DECL_VAR: {
            uint16_t idx = readShort();
            uint8_t mut = readByte();
            const auto& name = std::get<std::string>(chunk->constants[idx]);
            Value val = pop();
            globals[name] = {val, mut != 0};
            break;
        }
        case OpCode::OP_GET_LOCAL: {
            uint8_t slot = readByte();
            push(stack[slot]);
            break;
        }
        case OpCode::OP_SET_LOCAL: {
            uint8_t slot = readByte();
            stack[slot] = pop();
            break;
        }

        case OpCode::OP_POP:
            pop();
            break;

        case OpCode::OP_JUMP: {
            uint16_t offset = readShort();
            ip += offset;
            break;
        }
        case OpCode::OP_JUMP_IF_FALSE: {
            uint16_t offset = readShort();
            Value& top = peek();
            if (const bool* b = std::get_if<bool>(&top)) {
                if (!(*b)) ip += offset;
            }
            break;
        }
        case OpCode::OP_LOOP: {
            uint16_t offset = readShort();
            ip -= offset;
            break;
        }

        case OpCode::OP_LOG: {
            Value val = pop();
            std::cout << toString(val) << "\n";
            break;
        }
        case OpCode::OP_WAIT: {
            Value val = pop();
            if (!std::holds_alternative<int>(val))
                throw std::runtime_error("Wait expects an integer (milliseconds)");
            int ms = std::get<int>(val);
            logger->info("Waiting " + std::to_string(ms) + "ms");
            driver->sleep(ms);
            break;
        }
        case OpCode::OP_HALT:return;
        }
    }
}
