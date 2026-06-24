#include "doctest.h"
#include "ir/OpCode.h"

using namespace iris::bytecode;

TEST_CASE("OpCode encode/decode ABC") {
    uint32_t instr = encodeABC(OpCode::OP_ADD_INT, 3, 7, 12);
    CHECK(decodeOp(instr) == OpCode::OP_ADD_INT);
    CHECK(decodeA(instr) == 3);
    CHECK(decodeB(instr) == 7);
    CHECK(decodeC(instr) == 12);
}

TEST_CASE("OpCode encode/decode ABx") {
    uint32_t instr = encodeABx(OpCode::OP_LOADK, 2, 1024);
    CHECK(decodeOp(instr) == OpCode::OP_LOADK);
    CHECK(decodeA(instr) == 2);
    CHECK(decodeBx(instr) == 1024);
}

TEST_CASE("OpCode encode/decode AsBx") {
    uint32_t instr = encodeAsBx(OpCode::OP_JMP, 5, -100);
    CHECK(decodeOp(instr) == OpCode::OP_JMP);
    CHECK(decodeA(instr) == 5);
    CHECK(decodeSBx(instr) == -100);
}

TEST_CASE("OpCode IDX_SET_INT encoding") {
    // Simulate: IDX_SET_INT a=1, b=2, c=3
    uint32_t instr = encodeABC(OpCode::OP_IDX_SET_INT, 1, 2, 3);
    CHECK(decodeOp(instr) == OpCode::OP_IDX_SET_INT);
    CHECK(decodeA(instr) == 1);
    CHECK(decodeB(instr) == 2);
    CHECK(decodeC(instr) == 3);
}

TEST_CASE("OpCode IDX_GET_INT encoding") {
    uint32_t instr = encodeABC(OpCode::OP_IDX_GET_INT, 0, 1, 2);
    CHECK(decodeOp(instr) == OpCode::OP_IDX_GET_INT);
    CHECK(decodeA(instr) == 0);
    CHECK(decodeB(instr) == 1);
    CHECK(decodeC(instr) == 2);
}

TEST_CASE("OpCode OP_COUNT is sentinel") {
    CHECK((int)OpCode::OP_COUNT == 104);
    CHECK(decodeOp(encodeABC(OpCode::OP_COUNT, 0, 0, 0)) == OpCode::OP_COUNT);
}
