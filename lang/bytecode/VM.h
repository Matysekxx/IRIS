#ifndef VM_H
#define VM_H

#include <vector>
#include "Chunk.h"
#include "../core/Variable.h"
#include "../device/IDeviceDriver.h"
#include "../log/Logger.h"

struct FunctionObject;

/**
 * @brief Represents a function call frame on the stack.
 */
struct CallFrame {
    const FunctionObject* function;
    const uint32_t* returnIp;
    Chunk* returnChunk;
    Value* returnBase;
};

/**
 * @brief Register-based Virtual Machine.
 * Executes bytecode instructions from a Chunk.
 */
class VM {
    static constexpr size_t STACK_MAX = 16384;
    static constexpr size_t FRAMES_MAX = 256;

    Value stack[STACK_MAX];
    Value* base = stack;

    const uint32_t* ip = nullptr;
    Chunk* chunk = nullptr;

    CallFrame frames[FRAMES_MAX];
    int frameCount = 0;

    IDeviceDriver* driver = nullptr;
    Logger* logger = nullptr;

    std::vector<Variable> globals;
    std::vector<FunctionObject>* functions = nullptr;

public:
    /**
     * @brief Executes the given bytecode chunk.
     */
    void execute(Chunk& ch, IDeviceDriver* drv, Logger* log,
                 std::vector<FunctionObject>* funcs = nullptr);

private:
    void run();
};

#endif //VM_H
