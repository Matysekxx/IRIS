#ifndef VM_H
#define VM_H

#include <vector>
#include "ir/Chunk.h"
#include "core/Variable.h"
#include "core/ArrayData.h"
#include "device/IDeviceDriver.h"
#include "log/Logger.h"

#include "core/Native.h"
#include "Trace.h"

namespace iris::bytecode {
    struct FunctionObject;
    struct ClassMeta;
    class JITCompiler;

    /**
     * @brief Represents a function call frame on the stack.
     */
    struct CallFrame {
        const FunctionObject *function;
        const uint32_t *returnIp;
        Chunk *returnChunk;
        iris::core::Value *returnBase;
        uint8_t returnReg;
    };

    /**
     * @brief Represents an active exception handler (try/catch block).
     */
    struct ExceptionHandler {
        const uint32_t *catchIp; // instruction pointer to the catch block
        Chunk *chunk; // chunk the handler belongs to
        iris::core::Value *base; // register base at time of push
        int frameCount; // call frame depth at time of push
        uint8_t catchVarReg; // register to store the caught exception message
    };

    struct RuntimeError : public std::runtime_error {
        RuntimeError(const std::string& msg) : std::runtime_error(msg) {}
    };

    /**
     * @brief Register-based Virtual Machine.
     * Executes bytecode instructions from a Chunk.
     *
     * OPTIMIZATION: Uses Register Windowing / Flat Call Stack for extreme performance.
     */
    class VM {
        static constexpr size_t STACK_MAX = 262144;
        static constexpr size_t FRAMES_MAX = 4096;
        static constexpr size_t REG_WINDOW_SIZE = 64; // registers per function frame

        // OPTIMIZATION: Single large register file for all function calls
        std::vector<iris::core::Value> registerFile;
        iris::core::Value *base = nullptr;

        const uint32_t *ip = nullptr;
        Chunk *chunk = nullptr;

        CallFrame frames[FRAMES_MAX];
        int frameCount = 0;

        iris::device::IDeviceDriver *driver = nullptr;
        iris::log::Logger *logger = nullptr;

        std::vector<iris::core::Variable> globals;
        std::vector<FunctionObject> *functions = nullptr;
        std::vector<ClassMeta> *classMetas = nullptr;
        std::vector<iris::core::NativeFunction *> *nativeFunctions = nullptr;
        std::vector<ExceptionHandler> handlerStack;

        // OPTIMIZATION: String Interning for O(1) string comparisons
        std::unordered_map<std::string, iris::core::StringData *> stringInterner;

        JITCompiler *jit = nullptr;
        TraceManager traceManager;
        int gcCheckCounter = 0;
        static constexpr int GC_CHECK_INTERVAL = 256;

    public:
        /**
         * @brief Executes the given bytecode chunk.
         */
        void execute(Chunk &ch, iris::device::IDeviceDriver *drv, iris::log::Logger *log,
                     std::vector<FunctionObject> *funcs = nullptr,
                     std::vector<ClassMeta> *classes = nullptr,
                     std::vector<iris::core::NativeFunction *> *nativeFuncs = nullptr);

        // JIT Helpers
        void invokeMethod(iris::core::Value* rBase, int methodIdx, int argCount, iris::core::Value* constants);
        iris::core::Value createObject(int classId);
        uint64_t callFunction(int funcIdx, iris::core::Value* rBaseA);
        iris::core::Value getGlobal(int slot);
        void setGlobal(int slot, iris::core::Value val);

    private:
        void run();

        // OPTIMIZATION: String interning for O(1) comparisons
        iris::core::StringData *internString(const std::string &s);
    };
}

#endif //VM_H
