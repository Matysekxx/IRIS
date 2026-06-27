#ifndef TRACE_H
#define TRACE_H

#include <vector>
#include <cstdint>
#include <unordered_map>
#include "ir/OpCode.h"
#include "ir/Chunk.h"
#include "core/Value.h"


namespace iris::bytecode {
    /**
     * @brief Represents a recorded execution trace.
     */
    struct Trace {
        struct Entry {
            uint32_t instr;
            const uint32_t* pc;
            bool branchTaken;
            uint16_t typeA; // Observed type of register A (top 16 bits)
            uint16_t typeB; // Observed type of register B
            uint16_t typeC; // Observed type of register C
            const std::vector<iris::core::Value>* constants = nullptr;
            int registerBaseOffset = 0; // Cumulative offset of R from start of trace
            
            // Optimization flags
            bool skipGuardA = false;
            bool skipGuardB = false;
            bool skipGuardC = false;
        };

        std::vector<Entry> entries;
        std::vector<Entry> preamble;
        const uint32_t* startPC = nullptr;
        JITFunc compiledFunc = nullptr;
        int hotness = 0;
        bool isCompiling = false;

        Trace() = default;
        uint16_t initialTypes[8] = {0};
    };

    /**
     * @brief Manages trace recording and compilation.
     * OPTIMIZATION: Uses a lightweight tracing flag to avoid function-call overhead in NEXT().
     */
    class TraceManager {
        std::unordered_map<const uint32_t*, Trace> traces;
        Trace* currentTrace = nullptr;
        const uint32_t* traceStartPC = nullptr;
        int tracingStartFrameCount = 0;
        iris::core::Value* tracingStartBase = nullptr;

    public:
        static constexpr int HOT_THRESHOLD = 100;
        static constexpr int MAX_TRACE_ENTRIES = 200;

        // Lightweight flag checked by VM dispatch loop (avoids virtual call)
        bool tracingFlag = false;

        bool isTracing() const { return currentTrace != nullptr; }

        void startTracing(const uint32_t* pc, iris::core::Value* R = nullptr, int frameCount = 0) {
            traceStartPC = pc;
            currentTrace = &traces[pc];
            currentTrace->entries.clear();
            currentTrace->startPC = pc;
            tracingStartFrameCount = frameCount;
            tracingStartBase = R;
            tracingFlag = true;
            if (R) {
                for (int i = 0; i < 8; i++) {
                    currentTrace->initialTypes[i] = (uint16_t)(R[i].bits >> 48);
                }
            } else {
                for (int i = 0; i < 8; i++) {
                    currentTrace->initialTypes[i] = 0;
                }
            }
        }

        void stopTracing() {
            if (currentTrace) currentTrace->isCompiling = true;
            currentTrace = nullptr;
            tracingFlag = false;
        }

        int getTracingStartFrameCount() const { return tracingStartFrameCount; }
        const uint32_t* getTracingStartPC() const { return traceStartPC; }
        iris::core::Value* getTracingStartBase() const { return tracingStartBase; }

        // Fast path: no type tags (expensive type reads deferred to JIT compilation time)
        void recordFast(uint32_t instr, const uint32_t* pc, bool branchTaken = false, uint16_t tA = 0, uint16_t tB = 0, uint16_t tC = 0) {
            if (currentTrace) {
                if (currentTrace->entries.size() >= MAX_TRACE_ENTRIES) {
                    currentTrace = nullptr;
                    tracingFlag = false;
                    return;
                }
                currentTrace->entries.push_back({instr, pc, branchTaken, tA, tB, tC, nullptr, 0});
            }
        }

        void record(uint32_t instr, const uint32_t* pc, const std::vector<iris::core::Value>* constants, int baseOffset, uint16_t tA = 0, uint16_t tB = 0, uint16_t tC = 0, bool branchTaken = false) {
            if (currentTrace) {
                currentTrace->entries.push_back({instr, pc, branchTaken, tA, tB, tC, constants, baseOffset});
            }
        }

        void updateLastEntry(bool branchTaken) {
            if (currentTrace && !currentTrace->entries.empty()) {
                currentTrace->entries.back().branchTaken = branchTaken;
            }
        }

        Trace* getTrace(const uint32_t* pc) {
            auto it = traces.find(pc);
            if (it != traces.end()) return &it->second;
            return nullptr;
        }

        Trace& getOrCreateTrace(const uint32_t* pc) {
            return traces[pc];
        }
    };
}

#endif //TRACE_H
