#ifndef VM_H
#define VM_H

#include <vector>
#include "Chunk.h"
#include "../core/Variable.h"
#include "../core/ArrayData.h"
#include "../device/IDeviceDriver.h"
#include "../log/Logger.h"

struct FunctionObject;
struct ClassMeta;

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
 * @brief Represents an active exception handler (try/catch block).
 */
struct ExceptionHandler {
    const uint32_t* catchIp;   // instruction pointer to the catch block
    Chunk* chunk;              // chunk the handler belongs to
    Value* base;               // register base at time of push
    int frameCount;            // call frame depth at time of push
    uint8_t catchVarReg;       // register to store the caught exception message
};

/**
 * @brief Register-based Virtual Machine.
 * Executes bytecode instructions from a Chunk.
 * 
 * OPTIMIZATION: Uses Register Windowing / Flat Call Stack for extreme performance.
 * Instead of allocating separate stack frames, we use one large pre-allocated
 * register file and just move the base pointer. This eliminates memory allocation
 * overhead for function calls and enables O(1) call/return operations.
 */
class VM {
    static constexpr size_t STACK_MAX = 16384;
    static constexpr size_t FRAMES_MAX = 256;
    static constexpr size_t REG_WINDOW_SIZE = 64;  // registers per function frame

    // OPTIMIZATION: Single large register file for all function calls
    // This eliminates allocation overhead and improves cache locality
    Value registerFile[STACK_MAX];
    Value* base = registerFile;

    const uint32_t* ip = nullptr;
    Chunk* chunk = nullptr;

    CallFrame frames[FRAMES_MAX];
    int frameCount = 0;

    IDeviceDriver* driver = nullptr;
    Logger* logger = nullptr;

    std::vector<Variable> globals;
    std::vector<FunctionObject>* functions = nullptr;
    std::vector<ClassMeta>* classMetas = nullptr;
    std::vector<ExceptionHandler> handlerStack;
    
    // OPTIMIZATION: String Interning for O(1) string comparisons
    // Each unique string exists only once in memory
    std::unordered_map<std::string, StringData*> stringInterner;
    
    // OPTIMIZATION: Memory Pools disabled for stability
    // MemoryPool<StringData, 128> stringPool;
    // MemoryPool<ObjectData, 64> objectPool;
    // MemoryPool<ArrayData, 32> arrayPool;

public:
    /**
     * @brief Executes the given bytecode chunk.
     */
    void execute(Chunk& ch, IDeviceDriver* drv, Logger* log,
                 std::vector<FunctionObject>* funcs = nullptr,
                 std::vector<ClassMeta>* classes = nullptr);

private:
    void run();
    
    // OPTIMIZATION: String interning for O(1) comparisons
    StringData* internString(const std::string& s);
};

#endif //VM_H
