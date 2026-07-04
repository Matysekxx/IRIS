#ifndef TRACE_OPTIMIZER_H
#define TRACE_OPTIMIZER_H

#include "Trace.h"
#include <set>
#include <vector>

namespace iris::bytecode {
    /**
     * @brief Performs optimizations on execution traces before JIT compilation.
     */
    class TraceOptimizer {
    public:
        /**
         * @brief Optimizes a trace in-place.
         * @param trace The trace to optimize.
         */
        static void optimize(Trace &trace);

    private:
        /** @brief Performs Loop-Invariant Code Motion. */
        static void performLICM(Trace &trace);

        /** @brief Performs Dead Code Elimination. */
        static void performDCE(Trace &trace);

        /** @brief Eliminates redundant type guards. */
        static void performGuardElimination(Trace &trace);

        /** @brief Folds constant expressions at trace compilation time. */
        static void performConstantFolding(Trace &trace);
    };
}

#endif //TRACE_OPTIMIZER_H
