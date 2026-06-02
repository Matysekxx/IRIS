#ifndef TRACE_H
#define TRACE_H

#include <vector>
#include <cstdint>
#include <unordered_map>
#include "OpCode.h"
#include "Chunk.h"

namespace iris::bytecode {
    /**
     * @brief Represents a recorded execution trace.
     */
    struct Trace {
        struct Entry {
            uint32_t instr;
            const uint32_t* pc;
            bool branchTaken;
        };

        std::vector<Entry> entries;
        const uint32_t* startPC = nullptr;
        JITFunc compiledFunc = nullptr;
        int hotness = 0;
        bool isCompiling = false;

        Trace() = default;
    };

    /**
     * @brief Manages trace recording and compilation.
     */
    class TraceManager {
        std::unordered_map<const uint32_t*, Trace> traces;
        Trace* currentTrace = nullptr;
        const uint32_t* traceStartPC = nullptr;

    public:
        static constexpr int HOT_THRESHOLD = 100;

        bool isTracing() const { return currentTrace != nullptr; }

        void startTracing(const uint32_t* pc) {
            traceStartPC = pc;
            currentTrace = &traces[pc];
            currentTrace->entries.clear();
            currentTrace->startPC = pc;
        }

        void stopTracing() {
            currentTrace = nullptr;
        }

        const uint32_t* getTracingStartPC() const { return traceStartPC; }

        void record(uint32_t instr, const uint32_t* pc, bool branchTaken = false) {
            if (currentTrace) {
                currentTrace->entries.push_back({instr, pc, branchTaken});
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
