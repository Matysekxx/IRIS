#ifndef JIT_COMPILER_H
#define JIT_COMPILER_H

#include <asmjit/core.h>
#include <asmjit/x86.h>
#include "Chunk.h"

namespace iris::bytecode {
    /**
     * @brief Type for JIT-compiled functions.
     * Takes the register base, constants array, and VM instance.
     * Returns the bit representation of the result Value.
     */
    typedef uint64_t (*JITFunc)(iris::core::Value *registers, iris::core::Value *constants, void* vm);

    /**
     * @brief JIT Compiler using AsmJit.
     * Translates IRIS bytecode to native x64 machine code.
     */
    class JITCompiler {
        asmjit::JitRuntime rt;

    public:
        /**
         * @brief Compiles a hot chunk to native code.
         */
        JITFunc compile(Chunk &chunk, void* functions = nullptr, void* native_functions = nullptr);
    };
}

#endif //JIT_COMPILER_H
