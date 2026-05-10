#ifndef OPCODE_H
#define OPCODE_H

#include <cstdint>

namespace iris::bytecode {
    /**
     * @brief VM Instruction Set.
     * Each instruction is 4 bytes (uint32_t) encoded with OpCode and operands.
     */
    enum class OpCode : uint8_t {
        OP_LOADK,    ///< Load constant from pool.
        OP_LOADINT,  ///< Load immediate integer.
        OP_LOADBOOL, ///< Load boolean.
        OP_LOADNULL, ///< Load null.
        OP_MOVE,     ///< Copy value between registers.

        // === Generic arithmetic (with type dispatch) ===
        OP_ADD, ///< Addition (+)
        OP_SUB, ///< Subtraction (-)
        OP_MUL, ///< Multiplication (*)
        OP_DIV, ///< Division (/)
        OP_MOD, ///< Modulo (%)
        OP_NEG, ///< Negation (-)

        // === OPTIMIZATION: Specialized arithmetic (no type dispatch) ===
        OP_ADD_INT,    ///< Integer addition (fast path)
        OP_ADD_DOUBLE, ///< Double addition (fast path)
        OP_SUB_INT,    ///< Integer subtraction (fast path)
        OP_SUB_DOUBLE, ///< Double subtraction (fast path)
        OP_MUL_INT,    ///< Integer multiplication (fast path)
        OP_MUL_DOUBLE, ///< Double multiplication (fast path)
        OP_DIV_INT,    ///< Integer division (fast path)
        OP_DIV_DOUBLE, ///< Double division (fast path)

        OP_NOT, ///< Logical NOT (!)
        OP_AND, ///< Logical AND (&&)
        OP_OR,  ///< Logical OR (||)

        // === Generic comparisons ===
        OP_EQ,  ///< Equal (==)
        OP_NEQ, ///< Not equal (!=)
        OP_LT,  ///< Less than (<)
        OP_GT,  ///< Greater than (>)
        OP_LE,  ///< Less or equal (<=)
        OP_GE,  ///< Greater or equal (>=)

        // === OPTIMIZATION: Specialized comparisons (no type dispatch) ===
        OP_LT_INT,   ///< Integer less than (fast path)
        OP_GT_INT,   ///< Integer greater than (fast path)
        OP_LE_INT,   ///< Integer less or equal (fast path)
        OP_GE_INT,   ///< Integer greater or equal (fast path)
        OP_LT_DBL,   ///< Double less than (fast path)
        OP_GT_DBL,   ///< Double greater than (fast path)
        OP_LE_DBL,   ///< Double less or equal (fast path)
        OP_GE_DBL,   ///< Double greater or equal (fast path)
        OP_EQ_INT,   ///< Integer equality (fast path)
        OP_EQ_DBL,   ///< Double equality (fast path)

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
        OP_TAILCALL, ///< Tail call function.
        OP_CALL_NATIVE, ///< Call native function. A=base, B=native func idx, C=arg count.
        OP_RET,   ///< Return from function.

        OP_LOG,      ///< Print to console.
        OP_WAIT,     ///< Sleep for N ms.

        OP_TYPECHECK, ///< Runtime type check. A=reg, B=expected TypeAnnotation tag. Throws on mismatch.

        OP_NEW_OBJ,   ///< Create new object instance. A=dst, Bx=classId.
        OP_GET_FIELD, ///< Get object field. A=dst, B=objReg, C=fieldIdx.
        OP_SET_FIELD, ///< Set object field. A=valueReg, B=objReg, C=fieldIdx.
        OP_INVOKE,    ///< Method call. A=base (obj+args), B=method string idx, C=arg count.
        OP_TAIL_INVOKE, ///< Tail method call. A=base, B=method idx, C=arg count.

        // === Collection opcodes ===
        OP_NEW_ARRAY, ///< Create array. A=dst, B=sizeReg.

        OP_IDX_GET,   ///< Get by index. A=dst, B=collection, C=index.
        OP_IDX_SET,   ///< Set by index. A=value, B=collection, C=index.

        OP_COLL_LEN,  ///< Get collection length. A=dst, B=collection.

        OP_PUSH_HANDLER, ///< Push exception handler. A=catchVar reg slot (in catch frame), Bx=jump offset to catch block.
        OP_POP_HANDLER,  ///< Pop exception handler (leave try block normally).
        OP_THROW,        ///< Throw an exception value. A=value reg.

        OP_HALT,  ///< Stop VM.

        OP_COUNT
    };


    /**
     * @brief Encodes an instruction in ABC format.
     */
    inline uint32_t encodeABC(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
        return (static_cast<uint32_t>(op) << 24) |
               (static_cast<uint32_t>(a) << 16) |
               (static_cast<uint32_t>(b) << 8) |
               static_cast<uint32_t>(c);
    }

    /**
     * @brief Encodes an instruction in ABx format.
     */
    inline uint32_t encodeABx(OpCode op, uint8_t a, uint16_t bx) {
        return (static_cast<uint32_t>(op) << 24) |
               (static_cast<uint32_t>(a) << 16) |
               static_cast<uint32_t>(bx);
    }

    /**
     * @brief Encodes an instruction in AsBx format (signed Bx).
     */
    inline uint32_t encodeAsBx(OpCode op, uint8_t a, int16_t sbx) {
        uint16_t bx = static_cast<uint16_t>(sbx + 32767);
        return encodeABx(op, a, bx);
    }

    /**
     * @brief Encodes an instruction in sBx format (signed Bx, no A operand).
     */
    inline uint32_t encodesBx(OpCode op, int16_t sbx) {
        return encodeAsBx(op, 0, sbx);
    }

    /** @brief Extracts the OpCode (bits 24-31). */
    inline OpCode decodeOp(uint32_t i) { return static_cast<OpCode>(i >> 24); }

    /** @brief Extracts operand A (bits 16-23). */
    inline uint8_t decodeA(uint32_t i) { return static_cast<uint8_t>((i >> 16) & 0xFF); }

    /** @brief Extracts operand B (bits 8-15). */
    inline uint8_t decodeB(uint32_t i) { return static_cast<uint8_t>((i >> 8) & 0xFF); }

    /** @brief Extracts operand C (bits 0-7). */
    inline uint8_t decodeC(uint32_t i) { return static_cast<uint8_t>(i & 0xFF); }

    /** @brief Extracts operand Bx (bits 0-15, unsigned). */
    inline uint16_t decodeBx(uint32_t i) { return static_cast<uint16_t>(i & 0xFFFF); }

    /** @brief Extracts operand sBx (bits 0-15, signed). */
    inline int32_t decodeSBx(uint32_t i) { return static_cast<int32_t>(decodeBx(i)) - 32767; }
}

#endif //OPCODE_H