#ifndef PEEPHOLE_OPTIMIZER_H
#define PEEPHOLE_OPTIMIZER_H

#include "Chunk.h"

namespace iris::bytecode {
    /**
     * @brief Standalone peephole optimizer for IRIS bytecode.
     *
     * Performs local optimizations like:
     * - Redundant MOVE elimination
     * - Instruction fusion (compare+branch, load+arith)
     * - Constant folding for integer sequences
     * - LOADK + arithmetic -> OP_K variants
     */
    class PeepholeOptimizer {
    public:
        static void optimize(Chunk &ch);
    };
}

#endif // PEEPHOLE_OPTIMIZER_H
