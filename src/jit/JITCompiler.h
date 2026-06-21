#ifndef JIT_COMPILER_H
#define JIT_COMPILER_H

#include <asmjit/core.h>
#include <asmjit/x86.h>
#include "ir/Chunk.h"

namespace iris::bytecode {
    struct Trace;

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

        /**
         * @brief Compiles a recorded execution trace.
         */
        JITFunc compileTrace(Trace &trace, void* functions = nullptr, void* native_functions = nullptr);
    };
}

#endif //JIT_COMPILER_H
