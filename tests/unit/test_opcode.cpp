#include "doctest.h"
#include "ir/OpCode.h"
#include "ir/Chunk.h"

using namespace iris::bytecode;
using namespace iris::core;

TEST_SUITE("OpCode Encoding") {
    TEST_CASE("encodeABC produces correct bit layout") {
        // Encoding: op (bits 24-31), A (bits 16-23), B (bits 8-15), C (bits 0-7)
        uint32_t instr = encodeABC(OpCode::OP_ADD, 0xAA, 0xBB, 0xCC);
        CHECK(instr == 0x06AABBCC); // OP_ADD = 6
    }

    TEST_CASE("encodeABx") {
        uint32_t instr = encodeABx(OpCode::OP_LOADK, 3, 0xABCD);
        CHECK(instr == 0x03ABCD);
    }

    TEST_CASE("encodeAsBx with signed value") {
        uint32_t instr = encodeAsBx(OpCode::OP_JMP, 0, -32767);
        CHECK(decodeSBx(instr) == -32767);
    }

    TEST_CASE("Roundtrip ABC") {
        for (uint16_t a = 0; a < 256; a += 64) {
            for (uint16_t b = 0; b < 256; b += 64) {
                for (uint16_t c = 0; c < 256; c += 64) {
                    uint32_t instr = encodeABC(OpCode::OP_ADD, (uint8_t)a, (uint8_t)b, (uint8_t)c);
                    CHECK(decodeA(instr) == (uint8_t)a);
                    CHECK(decodeB(instr) == (uint8_t)b);
                    CHECK(decodeC(instr) == (uint8_t)c);
                }
            }
        }
    }

    TEST_CASE("Roundtrip ABx") {
        uint32_t instr = encodeABx(OpCode::OP_LOADK, 255, 0xFFFF);
        CHECK(decodeA(instr) == 255);
        CHECK(decodeBx(instr) == 0xFFFF);
    }

    TEST_CASE("Roundtrip sBx") {
        for (int32_t i = -32767; i <= 32767; i += 4096) {
            uint32_t instr = encodesBx(OpCode::OP_JMP, (int16_t)i);
            CHECK(decodeSBx(instr) == i);
        }
        // Test edge cases
        CHECK(decodeSBx(encodesBx(OpCode::OP_JMP, -32767)) == -32767);
        CHECK(decodeSBx(encodesBx(OpCode::OP_JMP, 32767)) == 32767);
    }

    TEST_CASE("decodeOp extracts high byte") {
        for (int i = 0; i < 256; i++) {
            uint32_t instr = static_cast<uint32_t>(i) << 24;
            CHECK(static_cast<int>(decodeOp(instr)) == i);
        }
    }

    TEST_CASE("OP_COUNT reflects actual number of opcodes") {
        // Every opcode from 0 to OP_COUNT-1 should be valid
        for (uint8_t i = 0; i < (uint8_t)OpCode::OP_COUNT; i++) {
            OpCode op = static_cast<OpCode>(i);
            uint32_t instr = encodeABC(op, 0, 0, 0);
            CHECK(decodeOp(instr) == op);
        }
    }
}

TEST_SUITE("Chunk") {
    TEST_CASE("Chunk stores constants correctly") {
        Chunk c;
        CHECK(c.addConstant(Value(42)) == 0);
        CHECK(c.addConstant(Value(3.14)) == 1);
        CHECK(c.addConstant(Value("hello")) == 2);
        CHECK(c.constants.size() == 3);
        CHECK(c.constants[0].asInt() == 42);
        CHECK(c.constants[1].asDouble() == 3.14);
        CHECK(c.constants[2].asStringRef() == "hello");
    }

    TEST_CASE("Chunk instruction emission") {
        Chunk c;
        c.emit(0x06AABBCC);
        c.emit(0x00123456);
        CHECK(c.code.size() == 2);
        CHECK(c.code[0] == 0x06AABBCC);
        CHECK(c.code[1] == 0x00123456);
    }
}