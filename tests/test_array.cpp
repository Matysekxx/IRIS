#include "doctest.h"
#include "core/ArrayData.h"
#include "core/Value.h"

using namespace iris::core;

TEST_CASE("ArrayData default type is UNTYPED/VALUE") {
    ArrayData arr(10);
    CHECK(arr.length == 10);
    CHECK(arr.elemType == ArrayData::VALUE);
    CHECK(arr.valData != nullptr);
}

TEST_CASE("ArrayData int array") {
    ArrayData arr(25, ArrayData::INT);
    CHECK(arr.length == 25);
    CHECK(arr.elemType == ArrayData::INT);
    CHECK(arr.intData != nullptr);
    // All elements should be zero-initialized
    for (size_t i = 0; i < arr.length; i++) {
        CHECK(arr.intData[i] == 0);
    }
}

TEST_CASE("ArrayData double array") {
    ArrayData arr(10, ArrayData::DOUBLE);
    CHECK(arr.length == 10);
    CHECK(arr.elemType == ArrayData::DOUBLE);
    CHECK(arr.dblData != nullptr);
    for (size_t i = 0; i < arr.length; i++) {
        CHECK(arr.dblData[i] == 0.0);
    }
}

TEST_CASE("ArrayData value array") {
    ArrayData arr(5, ArrayData::VALUE);
    CHECK(arr.length == 5);
    CHECK(arr.elemType == ArrayData::VALUE);
    for (size_t i = 0; i < arr.length; i++) {
        CHECK(arr.valData[i].isNull());
    }
}

TEST_CASE("ArrayData int read/write") {
    ArrayData arr(10, ArrayData::INT);
    arr.intData[0] = 42;
    arr.intData[3] = -7;
    arr.intData[9] = 100;
    CHECK(arr.intData[0] == 42);
    CHECK(arr.intData[3] == -7);
    CHECK(arr.intData[9] == 100);
}

TEST_CASE("ArrayData double read/write") {
    ArrayData arr(10, ArrayData::DOUBLE);
    arr.dblData[0] = 3.14;
    arr.dblData[5] = -2.5;
    CHECK(arr.dblData[0] == doctest::Approx(3.14));
    CHECK(arr.dblData[5] == doctest::Approx(-2.5));
}

TEST_CASE("ArrayData value read/write") {
    ArrayData arr(5, ArrayData::VALUE);
    arr.valData[0] = Value(42);
    arr.valData[1] = Value(3.14);
    arr.valData[2] = Value(true);
    CHECK(arr.valData[0].asInt() == 42);
    CHECK(arr.valData[1].asDouble() == doctest::Approx(3.14));
    CHECK(arr.valData[2].asBool() == true);
    CHECK(arr.valData[3].isNull());
}

TEST_CASE("ArrayData int/val union: intData and valData share same address") {
    ArrayData arr(10, ArrayData::INT);
    CHECK((void*)arr.intData == (void*)arr.valData);
    CHECK((void*)arr.intData == (void*)arr.dblData);
}

TEST_CASE("ArrayData elemType sizes correct") {
    // INT: 4 bytes per element
    // DOUBLE: 8 bytes per element
    // VALUE: 8 bytes per element
    int size = 25;
    ArrayData intArr(size, ArrayData::INT);
    ArrayData dblArr(size, ArrayData::DOUBLE);
    ArrayData valArr(size, ArrayData::VALUE);

    // intData uses calloc(n, 4)
    // dblData uses calloc(n, 8)
    // valData uses malloc(n * 8)
    // Just verify no crash
    intArr.intData[24] = 1;
    dblArr.dblData[24] = 1.0;
    valArr.valData[24] = Value(1);
    CHECK(intArr.intData[24] == 1);
    CHECK(dblArr.dblData[24] == 1.0);
    CHECK(valArr.valData[24].asInt() == 1);
}

TEST_CASE("ArrayData zero-size") {
    ArrayData arr(0, ArrayData::INT);
    CHECK(arr.length == 0);
    CHECK(arr.elemType == ArrayData::INT);
}

TEST_CASE("ArrayData copy") {
    ArrayData original(5, ArrayData::INT);
    original.intData[0] = 10;
    original.intData[1] = 20;
    original.intData[2] = 30;

    ArrayData copy(original);
    CHECK(copy.length == 5);
    CHECK(copy.elemType == ArrayData::INT);
    CHECK(copy.intData[0] == 10);
    CHECK(copy.intData[1] == 20);
    CHECK(copy.intData[2] == 30);

    // Verify deep copy (independent pointers)
    CHECK((void*)copy.intData != (void*)original.intData);
    copy.intData[0] = 99;
    CHECK(original.intData[0] == 10);
}

TEST_CASE("ArrayData move") {
    ArrayData original(5, ArrayData::INT);
    original.intData[0] = 42;
    int* origPtr = original.intData;

    ArrayData moved(std::move(original));
    CHECK(moved.length == 5);
    CHECK(moved.elemType == ArrayData::INT);
    CHECK(moved.intData == origPtr);
    CHECK(moved.intData[0] == 42);

    // Original should be empty after move
    CHECK(original.length == 0);
    CHECK(original.intData == nullptr);
}

TEST_CASE("ArrayData: ensure int buffer is smaller than value buffer for same size") {
    // Verify that an int array of size n uses 4*n bytes for data,
    // while a value array uses 8*n bytes.
    // This is the root cause of the IDX_SET crash before the fix.
    const int size = 25;
    ArrayData intArr(size, ArrayData::INT);
    ArrayData valArr(size, ArrayData::VALUE);

    // intData uses calloc(25, 4) = 100 bytes
    // valData uses malloc(25 * 8) = 200 bytes
    // Writing valData[14] into intData buffer writes at offset 112,
    // which is 12 bytes past the 100-byte buffer.
    CHECK(sizeof(int) == 4);
    CHECK(sizeof(Value) == 8);
    // This test just documents the size difference; actual buffer overflow
    // detection needs address sanitizer or page heap.
}
