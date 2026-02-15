#ifndef OPCODE_H
#define OPCODE_H

#include <cstdint>

/**
 * @brief Instruction Set Architecture (ISA) for the IRIS Virtual Machine.
 * Each opcode represents a single byte instruction.
 */
enum class OpCode : uint8_t {
    /** [uint16 index] Push constant from pool */
    OP_CONST,
    /** Push true */
    OP_TRUE,
    /** Push false */
    OP_FALSE,
    /** Push null (monostate) */
    OP_NULL,

    /** Arithmetic */
    /** Pop b, pop a -> push a+b (int add or string concat) */
    OP_ADD,
    /** Pop b, pop a -> push a-b */
    OP_SUB,
    /** Pop b, pop a -> push a*b */
    OP_MUL,
    /** Pop b, pop a -> push a/b */
    OP_DIV,
    /** Pop a -> push -a */
    OP_NEGATE,

    /** Logical */
    /** Pop a -> push !a */
    OP_NOT,
    /** Pop b, pop a -> push a && b */
    OP_AND,
    /** Pop b, pop a -> push a || b */
    OP_OR,

    /** Comparison */
    /** Pop b, pop a -> push a == b */
    OP_EQ,
    /** Pop b, pop a -> push a != b */
    OP_NEQ,
    /** Pop b, pop a -> push a < b */
    OP_LT,
    /** Pop b, pop a -> push a > b */
    OP_GT,
    /** Pop b, pop a -> push a <= b */
    OP_LE,
    /** Pop b, pop a -> push a >= b */
    OP_GE,

    /** Bitwise */
    /** Pop b, pop a -> push a & b */
    OP_BIT_AND,
    /** Pop b, pop a -> push a | b */
    OP_BIT_OR,
    /** Pop b, pop a -> push a ^ b */
    OP_BIT_XOR,
    /** Pop b, pop a -> push a << b */
    OP_SHL,
    /** Pop b, pop a -> push a >> b */
    OP_SHR,

    /** Variables */
    /** [uint16 name_idx] Push variable value */
    OP_GET_VAR,
    /** [uint16 name_idx] Pop value, set variable */
    OP_SET_VAR,
    /** [uint16 name_idx] [uint8 mutable] Pop value, declare var */
    OP_DECL_VAR,

    /** Stack */
    /** Discard top of stack */
    OP_POP,

    /** Control flow */
    /** [uint16 offset] Unconditional forward jump */
    OP_JUMP,
    /** [uint16 offset] Jump if top is false (peeks, no pop) */
    OP_JUMP_IF_FALSE,
    /** [uint16 offset] Unconditional backward jump */
    OP_LOOP,

    /** Built-in commands */
    /** Pop value -> print to console */
    OP_LOG,
    /** Pop int -> sleep(ms) */
    OP_WAIT,

    /** Stop execution */
    OP_HALT,
};

#endif //OPCODE_H
