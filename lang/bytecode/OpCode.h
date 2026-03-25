#ifndef OPCODE_H
#define OPCODE_H

#include <cstdint>

/**
 * @brief VM Instruction Set.
 * Each instruction is 1 byte (uint8_t). Operands follow in subsequent bytes.
 */
enum class OpCode : uint8_t {
    OP_LOADK,    ///< Load constant from pool.
    OP_LOADINT,  ///< Load immediate integer.
    OP_LOADBOOL, ///< Load boolean.
    OP_LOADNULL, ///< Load null.
    OP_MOVE,     ///< Copy value between registers.

    OP_ADD, ///< Addition (+)
    OP_SUB, ///< Subtraction (-)
    OP_MUL, ///< Multiplication (*)
    OP_DIV, ///< Division (/)
    OP_MOD, ///< Modulo (%)
    OP_NEG, ///< Negation (-)

    OP_NOT, ///< Logical NOT (!)
    OP_AND, ///< Logical AND (&&)
    OP_OR,  ///< Logical OR (||)

    OP_EQ,  ///< Equal (==)
    OP_NEQ, ///< Not equal (!=)
    OP_LT,  ///< Less than (<)
    OP_GT,  ///< Greater than (>)
    OP_LE,  ///< Less or equal (<=)
    OP_GE,  ///< Greater or equal (>=)

    OP_BIT_AND, ///< Bitwise AND (&)
    OP_BIT_OR,  ///< Bitwise OR (|)
    OP_BIT_XOR, ///< Bitwise XOR (^)
    OP_SHL,     ///< Shift Left (<<)
    OP_SHR,     ///< Shift Right (>>)

    OP_GGLOB, ///< Get Global.
    OP_SGLOB, ///< Set Global.
    OP_DGLOB, ///< Define Global.

    OP_JMP,   ///< Unconditional Jump.
    OP_JMPF,  ///< Jump if False.
    OP_LOOP,  ///< Jump back (loop).

    OP_CALL,  ///< Call function.
    OP_RET,   ///< Return from function.

    OP_LOG,      ///< Print to console.
    OP_WAIT,     ///< Sleep for N ms.

    OP_TYPECHECK, ///< Runtime type check. A=reg, B=expected TypeAnnotation tag. Throws on mismatch.

    OP_HALT,  ///< Stop VM.

    OP_COUNT
};


/**
 * @brief Encodes an instruction in ABC format.
 * Format: [OpCode:8][A:8][B:8][C:8]
 * Used for arithmetic and logical operations (e.g., ADD R[A], R[B], R[C]).
 */
inline uint32_t encodeABC(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
    return (static_cast<uint32_t>(op) << 24) |
           (static_cast<uint32_t>(a) << 16) |
           (static_cast<uint32_t>(b) << 8) |
           static_cast<uint32_t>(c);
}

/**
 * @brief Encodes an instruction in ABx format.
 * Format: [OpCode:8][A:8][Bx:16]
 * Used for loading constants and global variables (e.g., LOADK R[A], Const[Bx]).
 */
inline uint32_t encodeABx(OpCode op, uint8_t a, uint16_t bx) {
    return (static_cast<uint32_t>(op) << 24) |
           (static_cast<uint32_t>(a) << 16) |
           static_cast<uint32_t>(bx);
}

/**
 * @brief Encodes an instruction in AsBx format (signed Bx).
 * Format: [OpCode:8][A:8][sBx:16]
 * Used for jumps with offsets (e.g., JMP sBx).
 * The signed value is stored with a bias of +32767.
 */
inline uint32_t encodeAsBx(OpCode op, uint8_t a, int16_t sbx) {
    uint16_t bx = static_cast<uint16_t>(sbx + 32767);
    return encodeABx(op, a, bx);
}

/**
 * @brief Encodes an instruction in sBx format (signed Bx, no A operand).
 * Format: [OpCode:8][0:8][sBx:16]
 * Used for unconditional jumps where register A is unused.
 */
inline uint32_t encodesBx(OpCode op, int16_t sbx) {
    return encodeAsBx(op, 0, sbx);
}


/** @brief Extracts the OpCode (bits 24-31). */
#define DECODE_OP(i)  static_cast<OpCode>((i) >> 24)

/** @brief Extracts operand A (bits 16-23). */
#define DECODE_A(i)   static_cast<uint8_t>(((i) >> 16) & 0xFF)

/** @brief Extracts operand B (bits 8-15). */
#define DECODE_B(i)   static_cast<uint8_t>(((i) >> 8) & 0xFF)

/** @brief Extracts operand C (bits 0-7). */
#define DECODE_C(i)   static_cast<uint8_t>((i) & 0xFF)

/** @brief Extracts operand Bx (bits 0-15, unsigned). */
#define DECODE_Bx(i)  static_cast<uint16_t>((i) & 0xFFFF)

/** @brief Extracts operand sBx (bits 0-15, signed). Subtracts bias 32767. */
#define DECODE_sBx(i) (static_cast<int32_t>(DECODE_Bx(i)) - 32767)

#endif //OPCODE_H
