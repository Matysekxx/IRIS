#ifndef VM_H
#define VM_H

#include <unordered_map>
#include "Chunk.h"
#include "../core/Variable.h"
#include "../device/IDeviceDriver.h"
#include "../log/Logger.h"

/**
 * @brief Stack-based Virtual Machine for executing IRIS bytecode.
 */
class VM {
    static constexpr size_t STACK_MAX = 256;
    /** The operand stack. */
    Value stack[STACK_MAX];
    /** Pointer to the next free slot on the stack. */
    Value* stackTop = stack;

    /** Instruction Pointer - points to the next byte to be executed. */
    const uint8_t* ip = nullptr;
    /** The chunk of code currently being executed. */
    Chunk* chunk = nullptr;

    IDeviceDriver* driver = nullptr;
    Logger* logger = nullptr;
    /** Global variables storage. */
    std::unordered_map<std::string, Variable> globals;

public:
    /**
     * @brief Executes the given bytecode chunk.
     * @param ch The chunk containing bytecode and constants.
     * @param drv The device driver for hardware interaction.
     * @param log The logger instance.
     */
    void execute(Chunk& ch, IDeviceDriver* drv, Logger* log);

private:
    /** The main execution loop (fetch-decode-execute). */
    void run();

    /** Pushes a value onto the stack.
     * @param v The value to push.
     * @throws std::runtime_error if stack overflow. */
    void push(const Value& v) {
        *stackTop++ = v;
    }

    /** Pops a value from the stack.
     * @return The popped value.
     * @throws std::runtime_error if stack underflow. */
    Value pop() {
        return *--stackTop;
    }

    /** Returns a reference to a value on the stack without popping it.
     * @param distance 0 is the top, 1 is the one below, etc. */
    Value& peek(int distance = 0) {
        return *(stackTop - 1 - distance);
    }

    /** Reads the next byte from the bytecode and advances IP. */
    uint8_t readByte() {
        return *ip++;
    }

    /** Reads the next 2 bytes as a 16-bit integer (Big Endian) and advances IP. */
    uint16_t readShort() {
        const uint16_t hi = *ip++;
        const uint16_t lo = *ip++;
        return static_cast<uint16_t>((hi << 8) | lo);
    }
};

#endif //VM_H
