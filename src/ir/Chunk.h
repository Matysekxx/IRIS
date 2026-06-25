#ifndef CHUNK_H
#define CHUNK_H

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include "core/Value.h"
#include "OpCode.h"
#include <iostream>

namespace iris::bytecode {
    struct VMState {
        iris::core::Value* rBase;
        iris::core::Value* constants;
        void* vm;
        iris::core::Value* globals;
    };

    /**
     * @brief Type for JIT-compiled functions.
     */
    typedef uint64_t (*JITFunc)(VMState* state, uint64_t arg0, uint64_t arg1, uint64_t arg2);

    /**
     * @brief Inline Cache entry for polymorphic method calls.
     * Stores up to 2 recent (class, method) pairs for fast lookup.
     */
    struct InlineCacheEntry {
        static constexpr size_t MAX_STATES = 2;

        struct CacheSlot {
            uint16_t classId = 0xFFFF;
            uint16_t funcIdx = 0xFFFF;
        };

        CacheSlot slots[MAX_STATES];
        uint8_t hitCount = 0;

        bool lookup(uint16_t classId, uint16_t &funcIdx) const {
            for (size_t i = 0; i < MAX_STATES; i++) {
                if (slots[i].classId == classId) {
                    funcIdx = slots[i].funcIdx;
                    return true;
                }
            }
            return false;
        }

        void update(uint16_t classId, uint16_t funcIdx) {
            // Shift old entries
            if (slots[0].classId != 0xFFFF && slots[0].classId != classId) {
                slots[1] = slots[0];
            }
            slots[0] = {classId, funcIdx};
            hitCount++;
        }
    };

    /**
     * @brief Cache entry for Polymorphic Inline Caching (PIC).
     * Stores up to 2 recent (classId, fid) pairs for fast dispatch.
     */
    struct MethodCacheEntry {
        struct Slot { uint16_t classId = 0xFFFF; uint16_t fid = 0xFFFF; };
        Slot slots[2];
        uint8_t methodNameIdx;
        uint8_t argCount;

        bool lookup(uint16_t classId, uint16_t &fid) const {
            for (size_t i = 0; i < 2; i++)
                if (slots[i].classId == classId) { fid = slots[i].fid; return true; }
            return false;
        }

        void update(uint16_t classId, uint16_t fid) {
            if (slots[0].classId != 0xFFFF && slots[0].classId != classId)
                slots[1] = slots[0];
            slots[0] = {classId, fid};
        }
    };

    /**
     * @brief A block of bytecode instructions and constants.
     * Represents a compiled function or the main program body.
     *
     * OPTIMIZATION: Enhanced inline caching for polymorphic method calls.
     */
    struct Chunk {
        std::vector<uint32_t> code;
        std::vector<iris::core::Value> constants;
        std::unordered_map<std::string, uint16_t> stringIntern;
        std::unordered_map<uint64_t, uint16_t> nonStringDedup;
        std::unordered_map<size_t, InlineCacheEntry> inlineCache;
        std::vector<MethodCacheEntry> methodCaches;

        // JIT related
        void* jitFunc = nullptr;
        uint32_t callCount = 0;
        bool jitAttempted = false;


        /** @brief Appends a 32-bit instruction to the chunk. */
        void emit(uint32_t instr) {
            code.push_back(instr);
        }

        /**
         * @brief Adds a constant to the pool, reusing strings if possible.
         * @return The index of the constant in the pool.
         */
        uint16_t addConstant(const iris::core::Value &value) {
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
            auto it = nonStringDedup.find(value.bits);
            if (it != nonStringDedup.end()) {
                return it->second;
            }
            constants.push_back(value);
            const auto idx = static_cast<uint16_t>(constants.size() - 1);
            nonStringDedup[value.bits] = idx;
            return idx;
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
            OpCode op = decodeOp(old);
            uint8_t a = decodeA(old);
            // std::cout << "[DEBUG PATCHJUMP] instrIdx=" << instrIdx << " op=" << (int)op << " offset=" << offset << " codeSize=" << code.size() << std::endl;
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
}

#endif //CHUNK_H
