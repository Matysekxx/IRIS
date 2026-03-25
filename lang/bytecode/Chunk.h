#ifndef CHUNK_H
#define CHUNK_H

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include "../core/Value.h"
#include "OpCode.h"

/**
 * @brief A block of bytecode instructions and constants.
 * Represents a compiled function or the main program body.
 */
struct Chunk {
    std::vector<uint32_t> code;
    std::vector<Value> constants;
    std::unordered_map<std::string, uint16_t> stringIntern;

    /** @brief Appends a 32-bit instruction to the chunk. */
    void emit(uint32_t instr) {
        code.push_back(instr);
    }

    /**
     * @brief Adds a constant to the pool, reusing strings if possible.
     * @return The index of the constant in the pool.
     */
    uint16_t addConstant(const Value& value) {
        if (value.isString()) {
            auto it = stringIntern.find(value.str());
            if (it != stringIntern.end()) {
                return it->second;
            }
            constants.push_back(value);
            const auto idx = static_cast<uint16_t>(constants.size() - 1);
            stringIntern[value.str()] = idx;
            return idx;
        }
        constants.push_back(value);
        return static_cast<uint16_t>(constants.size() - 1);
    }

    /**
     * @brief Emits a jump instruction with a placeholder offset.
     * @return Index of the instruction to patch later.
     */
    size_t emitJump(OpCode op, uint8_t a = 0) {
        emit(encodeAsBx(op, a, 0));
        return code.size() - 1;
    }

    /**
     * @brief Updates a previous jump instruction with the correct offset.
     * Calculates the offset from the jump instruction to the current end of code.
     */
    void patchJump(size_t instrIdx) {
        int16_t offset = static_cast<int16_t>(code.size() - instrIdx - 1);
        uint32_t old = code[instrIdx];
        OpCode op = DECODE_OP(old);
        uint8_t a = DECODE_A(old);
        code[instrIdx] = encodeAsBx(op, a, offset);
    }

    /**
     * @brief Emits a backward jump (loop).
     * Calculates the negative offset to jump back to loopStart.
     */
    void emitLoop(size_t loopStart) {
        int16_t offset = -static_cast<int16_t>(code.size() - loopStart + 1);
        emit(encodesBx(OpCode::OP_LOOP, offset));
    }
};

#endif //CHUNK_H
