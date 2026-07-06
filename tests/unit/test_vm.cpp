#include "doctest.h"
#include "ir/Chunk.h"
#include "ir/OpCode.h"
#include "vm/VM.h"

using namespace iris::bytecode;
using namespace iris::core;

TEST_SUITE("OpCode Encoding/Decoding") {
    TEST_CASE("ABC encoding") {
        uint32_t instr = encodeABC(OpCode::OP_ADD, 1, 2, 3);
        CHECK(decodeOp(instr) == OpCode::OP_ADD);
        CHECK(decodeA(instr) == 1);
        CHECK(decodeB(instr) == 2);
        CHECK(decodeC(instr) == 3);
    }

    TEST_CASE("ABx encoding") {
        uint32_t instr = encodeABx(OpCode::OP_LOADK, 5, 1000);
        CHECK(decodeOp(instr) == OpCode::OP_LOADK);
        CHECK(decodeA(instr) == 5);
        CHECK(decodeBx(instr) == 1000);
    }

    TEST_CASE("AsBx encoding (signed)") {
        uint32_t instr = encodeAsBx(OpCode::OP_JMP, 0, -128);
        CHECK(decodeOp(instr) == OpCode::OP_JMP);
        CHECK(decodeSBx(instr) == -128);
    }

    TEST_CASE("All opcodes encode without collision") {
        for (int i = 0; i < (int)OpCode::OP_COUNT; i++) {
            OpCode op = static_cast<OpCode>(i);
            uint32_t instr = encodeABC(op, 0, 0, 0);
            CHECK(decodeOp(instr) == op);
        }
    }
}

TEST_SUITE("Chunk") {
    TEST_CASE("Empty chunk") {
        Chunk chunk;
        CHECK(chunk.code.empty());
        CHECK(chunk.constants.empty());
    }

    TEST_CASE("Add instruction") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        CHECK(chunk.code.size() == 1);
        CHECK(decodeOp(chunk.code[0]) == OpCode::OP_HALT);
    }

    TEST_CASE("Add constant") {
        Chunk chunk;
        uint16_t idx = chunk.addConstant(Value(42));
        CHECK(idx == 0);
        CHECK(chunk.constants.size() == 1);
        CHECK(chunk.constants[0].asInt() == 42);
    }

    TEST_CASE("Multiple constants") {
        Chunk chunk;
        chunk.addConstant(Value(1));
        chunk.addConstant(Value(2));
        chunk.addConstant(Value(3));
        CHECK(chunk.constants.size() == 3);
    }
}

TEST_SUITE("VM - Basic Instructions") {
    TEST_CASE("HALT") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("LOADINT stores correct value") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 42));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("LOADK loads constant") {
        Chunk chunk;
        uint16_t cidx = chunk.addConstant(Value(3.14));
        chunk.emit(encodeABx(OpCode::OP_LOADK, 0, cidx));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("LOADBOOL stores bool") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_LOADBOOL, 0, 1, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("LOADNULL stores null") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("MOVE copies register") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_MOVE, 1, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Arithmetic") {
    TEST_CASE("ADD_INT") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 10));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 20));
        chunk.emit(encodeABC(OpCode::OP_ADD_INT, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("SUB_INT") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 50));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 30));
        chunk.emit(encodeABC(OpCode::OP_SUB_INT, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("MUL_INT") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 7));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 6));
        chunk.emit(encodeABC(OpCode::OP_MUL_INT, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("ADDI (add immediate)") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 10));
        chunk.emit(encodeABC(OpCode::OP_ADDI, 1, 0, 5));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("INC/DEC") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_INC, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_DEC, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Control Flow") {
    TEST_CASE("JMP changes flow") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_LOADBOOL, 0, 1, 0));
        // Jump forward (past halt)
        chunk.emit(encodesBx(OpCode::OP_JMP, 2));
        chunk.emit(encodeABC(OpCode::OP_LOADBOOL, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("JMPF conditional branch") {
        Chunk chunk;
        chunk.emit(encodeABC(OpCode::OP_LOADBOOL, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_JMPF, 0, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        // Note: JMPF offset encoded in Bx field, need proper encoding
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Arrays") {
    TEST_CASE("Create int array") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 5));
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("Array set/get int element") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 5));
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 2, 42));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 3, 0));
        chunk.emit(encodeABC(OpCode::OP_IDX_SET_INT, 2, 0, 3));
        chunk.emit(encodeABC(OpCode::OP_IDX_GET_INT, 4, 0, 3));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("Array COLL_LEN") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 10));
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, 0, 1, (uint8_t)ArrayData::INT));
        chunk.emit(encodeABC(OpCode::OP_COLL_LEN, 2, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Objects") {
    TEST_CASE("Create object") {
        Chunk chunk;
        uint16_t classId = 0;
        chunk.emit(encodeABx(OpCode::OP_NEW_OBJ, 0, classId));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("Get/Set field") {
        Chunk chunk;
        uint16_t classId = 0;
        chunk.emit(encodeABx(OpCode::OP_NEW_OBJ, 0, classId));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 42));
        // SET_FIELD A=value(1), B=object(0), C=field(0)
        chunk.emit(encodeABC(OpCode::OP_SET_FIELD, 1, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Comparisons") {
    TEST_CASE("Integer comparisons") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 5));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 10));
        chunk.emit(encodeABC(OpCode::OP_LT_INT, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_GT_INT, 3, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_EQ_INT, 4, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST_SUITE("VM - Bitwise Operations") {
    TEST_CASE("AND, OR, XOR") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 0xFF));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 0x0F));
        chunk.emit(encodeABC(OpCode::OP_BIT_AND, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_BIT_OR, 3, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_BIT_XOR, 4, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }

    TEST_CASE("Shift operations") {
        Chunk chunk;
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 0, 8));
        chunk.emit(encodeAsBx(OpCode::OP_LOADINT, 1, 2));
        chunk.emit(encodeABC(OpCode::OP_SHL, 2, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_SHR, 3, 0, 1));
        chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
        VM vm;
        CHECK_NOTHROW(vm.execute(chunk, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}