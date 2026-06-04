#ifndef OPCODE_H
#define OPCODE_H

#include <cstdint>

namespace iris::bytecode {
    /**
     * @brief VM Instruction Set.
     * Each instruction is 4 bytes (uint32_t) encoded with OpCode and operands.
     */
    enum class OpCode : uint8_t {
        OP_LOADK = 0,
        OP_LOADINT = 1,
        OP_LOADBOOL = 2,
        OP_LOADNULL = 3,
        OP_MOVE = 4,
        OP_MOVE_INT = 5,

        OP_ADD = 6,
        OP_SUB = 7,
        OP_MUL = 8,
        OP_DIV = 9,
        OP_MOD = 10,
        OP_NEG = 11,

        OP_ADD_INT = 12,
        OP_ADD_DOUBLE = 13,
        OP_SUB_INT = 14,
        OP_SUB_DOUBLE = 15,
        OP_MUL_INT = 16,
        OP_MUL_DOUBLE = 17,
        OP_DIV_INT = 18,
        OP_DIV_DOUBLE = 19,

        OP_ADDI = 20,
        OP_SUBI = 21,
        OP_INC = 22,
        OP_DEC = 23,

        OP_NOT = 24,
        OP_AND = 25,
        OP_OR = 26,

        OP_EQ = 27,
        OP_NEQ = 28,
        OP_LT = 29,
        OP_GT = 30,
        OP_LE = 31,
        OP_GE = 32,

        OP_LT_INT = 33,
        OP_GT_INT = 34,
        OP_LE_INT = 35,
        OP_GE_INT = 36,
        OP_LT_DBL = 37,
        OP_GT_DBL = 38,
        OP_LE_DBL = 39,
        OP_GE_DBL = 40,
        OP_EQ_INT = 41,
        OP_EQ_DBL = 42,

        OP_BIT_AND = 43,
        OP_BIT_OR = 44,
        OP_BIT_XOR = 45,
        OP_SHL = 46,
        OP_SHR = 47,

        OP_GGLOB = 48,
        OP_SGLOB = 49,
        OP_DGLOB = 50,

        OP_JMP = 51,
        OP_JMPF = 52,
        OP_JMPT = 53,
        OP_LOOP = 54,

        OP_CALL = 55,
        OP_TAILCALL = 56,
        OP_CALL_NATIVE = 57,
        OP_RET = 58,

        OP_LOG = 59,
        OP_WAIT = 60,

        OP_TYPECHECK = 61,

        OP_NEW_OBJ = 62,
        OP_GET_FIELD = 63,
        OP_GET_FIELD_INT = 64,
        OP_GET_FIELD_DBL = 65,
        OP_SET_FIELD = 66,
        OP_INC_FIELD = 67,
        OP_DEC_FIELD = 68,
        OP_INVOKE = 69,
        OP_INVOKE_MONO = 70,
        OP_TAIL_INVOKE = 71,

        OP_NEW_ARRAY = 72,
        OP_IDX_GET = 73,
        OP_IDX_SET = 74,
        OP_IDX_GET_DBL = 75,
        OP_IDX_SET_DBL = 76,
        OP_IDX_GET_INT = 77,
        OP_IDX_SET_INT = 78,

        OP_COLL_LEN = 79,

        OP_PUSH_HANDLER = 80,
        OP_POP_HANDLER = 81,
        OP_THROW = 82,

        OP_HALT = 83,

        OP_JLT_INT = 84,
        OP_JGT_INT = 85,
        OP_JLE_INT = 86,
        OP_JGE_INT = 87,
        OP_JNE_INT = 88,

        OP_ADDI_W = 89,
        OP_SUBI_W = 90,

        OP_JLT_INT_IMM = 91,
        OP_JGT_INT_IMM = 92,
        OP_JLE_INT_IMM = 93,
        OP_JGE_INT_IMM = 94,
        OP_JEQ_INT_IMM = 95,
        OP_JNE_INT_IMM = 96,

        OP_COUNT = 97
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