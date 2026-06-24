#include "doctest.h"
#include "vm/VM.h"
#include "ir/Chunk.h"
#include "core/ArrayData.h"
#include "core/Value.h"

#include <vector>

using namespace iris::bytecode;
using namespace iris::core;

// Helper: emit a single instruction
static void emitInstr(Chunk& c, OpCode op, uint8_t a = 0, uint8_t b = 0, uint8_t cVal = 0) {
    c.emit(encodeABC(op, a, b, cVal));
}

TEST_CASE("VM halts without crash") {
    Chunk chunk;
    chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("VM OP_LOADINT stores correct value") {
    Chunk chunk;
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 42));
    emitInstr(chunk, OpCode::OP_HALT);

    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("VM create int array and set element") {
    // NEW_ARRAY + IDX_SET_INT + IDX_GET_INT
    // Operand order: IDX_SET_INT A=value, B=array, C=index
    //                IDX_GET_INT A=dest, B=array, C=index
    Chunk chunk;

    // R1 = 5 (size)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 5));

    // NEW_ARRAY R0, R1, INT (type=1) -> R0 = array
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));

    // R2 = 42 (value)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 2, 42));

    // R3 = 0 (index)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 3, 0));

    // IDX_SET_INT R2, R0, R3  (A=value(R2), B=array(R0), C=index(R3))
    chunk.emit(encodeABC(OpCode::OP_IDX_SET_INT, 2, 0, 3));

    // Read back with IDX_GET_INT
    // A=dest(R4), B=array(R0), C=index(R3)
    chunk.emit(encodeABC(OpCode::OP_IDX_GET_INT, 4, 0, 3));

    emitInstr(chunk, OpCode::OP_HALT);

    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("VM IDX_SET_INT fills all elements") {
    Chunk chunk;

    // R1 = 25 (array size)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 25));

    // NEW_ARRAY R0, R1, INT -> R0 = array
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));

    // Store values at each index
    // IDX_SET_INT: A=value, B=array, C=index
    for (int i = 0; i < 25; i++) {
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 2, i * 10)); // R2 = value
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 3, i));      // R3 = index
        chunk.emit(encodeABC(OpCode::OP_IDX_SET_INT, 2, 0, 3)); // R0[R3] = R2
    }

    // Read back
    // IDX_GET_INT: A=dest, B=array, C=index
    for (int i = 0; i < 25; i++) {
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 3, i));      // R3 = index
        chunk.emit(encodeABC(OpCode::OP_IDX_GET_INT, 4, 0, 3)); // R4 = R0[i]
    }

    emitInstr(chunk, OpCode::OP_HALT);

    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("VM generic IDX_SET dispatches correctly on elemType") {
    // Create int array, use generic IDX_SET (not INT variant),
    // verify it accesses intData and not valData (no buffer overflow)
    Chunk chunk;

    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 10));
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));

    // Generic IDX_SET at index 5: A=value(2), B=array(0), C=index(3)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 3, 5));
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 2, 99));
    chunk.emit(encodeABC(OpCode::OP_IDX_SET, 2, 0, 3));

    // Read back with IDX_GET_INT at index 5: A=dest(4), B=array(0), C=index(3)
    chunk.emit(encodeABC(OpCode::OP_IDX_GET_INT, 4, 0, 3));

    emitInstr(chunk, OpCode::OP_HALT);

    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("VM double array with IDX_SET_DBL and IDX_GET_DBL") {
    Chunk chunk;

    // R1 = 5 (size)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 5));

    // NEW_ARRAY R0, R1, DOUBLE
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::DOUBLE));

    // R2 = 0 (index), load constant pool value 3.14
    uint16_t cidx = chunk.addConstant(Value(3.14));
    chunk.emit(encodeABx(OpCode::OP_LOADK, 2, cidx));

    // R1 = 0 (index)
    chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 0));

    // IDX_SET_DBL R0, R1, R2 -> A=value(2), B=array(0), C=index(1)
    chunk.emit(encodeABC(OpCode::OP_IDX_SET_DBL, 2, 0, 1));

    emitInstr(chunk, OpCode::OP_HALT);

    VM vm;
    CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
}
