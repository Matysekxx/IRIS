#ifndef CHUNK_H
#define CHUNK_H

#include <vector>
#include <cstdint>
#include <stdexcept>
#include "../core/Value.h"
#include "OpCode.h"

/**
 * @brief Represents a block of bytecode and its associated constants.
 *
 * A Chunk is a dynamic array of instructions (bytes) and a pool of constants
 * used by those instructions.
 */
struct Chunk {
    /** The raw bytecode instructions. */
    std::vector<uint8_t> code;
    /** The pool of constants (numbers, strings, etc.) used in the bytecode. */
    std::vector<Value> constants;

    /** Appends a single byte to the bytecode. */
    void emit(const uint8_t byte) {
        code.push_back(byte);
    }

    /** Appends an opcode to the bytecode. */
    void emitOp(OpCode op) {
        emit(static_cast<uint8_t>(op));
    }

    /** Appends a 16-bit integer (2 bytes) to the bytecode (Big Endian). */
    void emitShort(const uint16_t value) {
        emit(static_cast<uint8_t>((value >> 8) & 0xFF));
        emit(static_cast<uint8_t>(value & 0xFF));
    }

    /** Adds a constant to the pool and returns its index. */
    uint16_t addConstant(const Value& value) {
        constants.push_back(value);
        return static_cast<uint16_t>(constants.size() - 1);
    }

    /** Emits an OP_CONST instruction followed by the constant's index. */
    void emitConstant(const Value& value) {
        const uint16_t index = addConstant(value);
        emitOp(OpCode::OP_CONST);
        emitShort(index);
    }

    /** Emits a jump instruction with a placeholder offset.
     * @return The index in the code vector where the jump offset begins (for patching later). */
    size_t emitJump(const OpCode op) {
        emitOp(op);
        emit(0xFF);
        emit(0xFF);
        return code.size() - 2;
    }

    /** Patches a previously emitted jump instruction with the correct offset.
     * @param offset The index returned by emitJump. */
    void patchJump(const size_t offset) {
        const size_t jump = code.size() - offset - 2;
        if (jump > UINT16_MAX) {
            throw std::runtime_error("Jump offset too large");
        }
        code[offset] = static_cast<uint8_t>((jump >> 8) & 0xFF);
        code[offset + 1] = static_cast<uint8_t>(jump & 0xFF);
    }

    /** Emits a loop instruction (backward jump).
     * @param loopStart The index in the code vector to jump back to. */
    void emitLoop(const size_t loopStart) {
        emitOp(OpCode::OP_LOOP);
        const size_t offset = code.size() - loopStart + 2;
        if (offset > UINT16_MAX) {
            throw std::runtime_error("Loop body too large");
        }
        emitShort(static_cast<uint16_t>(offset));
    }
};

#endif //CHUNK_H
