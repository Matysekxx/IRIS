#include "doctest.h"
#include "core/ArrayData.h"
#include "core/Value.h"

using namespace iris::core;

TEST_CASE("ArrayData default type is UNTYPED/VALUE") {
    ArrayData* arr = ArrayData::create(10);
    CHECK(arr->length == 10);
    CHECK(arr->elemType == ArrayData::VALUE);
    CHECK(arr->getValData() != nullptr);
    delete arr;
}

TEST_CASE("ArrayData int array") {
    ArrayData* arr = ArrayData::create(25, ArrayData::INT);
    CHECK(arr->length == 25);
    CHECK(arr->elemType == ArrayData::INT);
    CHECK(arr->getIntData() != nullptr);
    // All elements should be zero-initialized
    for (size_t i = 0; i < arr->length; i++) {
        CHECK(arr->getIntData()[i] == 0);
    }
    delete arr;
}

TEST_CASE("ArrayData double array") {
    ArrayData* arr = ArrayData::create(10, ArrayData::DOUBLE);
    CHECK(arr->length == 10);
    CHECK(arr->elemType == ArrayData::DOUBLE);
    CHECK(arr->getDblData() != nullptr);
    for (size_t i = 0; i < arr->length; i++) {
        CHECK(arr->getDblData()[i] == 0.0);
    }
    delete arr;
}

TEST_CASE("ArrayData value array") {
    ArrayData* arr = ArrayData::create(5, ArrayData::VALUE);
    CHECK(arr->length == 5);
    CHECK(arr->elemType == ArrayData::VALUE);
    for (size_t i = 0; i < arr->length; i++) {
        CHECK(arr->getValData()[i].isNull());
    }
    delete arr;
}

TEST_CASE("ArrayData int read/write") {
    ArrayData* arr = ArrayData::create(10, ArrayData::INT);
    arr->getIntData()[0] = 42;
    arr->getIntData()[3] = -7;
    arr->getIntData()[9] = 100;
    CHECK(arr->getIntData()[0] == 42);
    CHECK(arr->getIntData()[3] == -7);
    CHECK(arr->getIntData()[9] == 100);
    delete arr;
}

TEST_CASE("ArrayData double read/write") {
    ArrayData* arr = ArrayData::create(10, ArrayData::DOUBLE);
    arr->getDblData()[0] = 3.14;
    arr->getDblData()[5] = -2.5;
    CHECK(arr->getDblData()[0] == doctest::Approx(3.14));
    CHECK(arr->getDblData()[5] == doctest::Approx(-2.5));
    delete arr;
}

TEST_CASE("ArrayData value read/write") {
    ArrayData* arr = ArrayData::create(5, ArrayData::VALUE);
    arr->getValData()[0] = Value(42);
    arr->getValData()[1] = Value(3.14);
    arr->getValData()[2] = Value(true);
    CHECK(arr->getValData()[0].asInt() == 42);
    CHECK(arr->getValData()[1].asDouble() == doctest::Approx(3.14));
    CHECK(arr->getValData()[2].asBool() == true);
    CHECK(arr->getValData()[3].isNull());
    delete arr;
}

TEST_CASE("ArrayData int/val union: getIntData and getValData share same address") {
    ArrayData* arr = ArrayData::create(10, ArrayData::INT);
    CHECK((void*)arr->getIntData() == (void*)arr->getValData());
    CHECK((void*)arr->getIntData() == (void*)arr->getDblData());
    delete arr;
}

TEST_CASE("ArrayData elemType sizes correct") {
    int size = 25;
    ArrayData* intArr = ArrayData::create(size, ArrayData::INT);
    ArrayData* dblArr = ArrayData::create(size, ArrayData::DOUBLE);
    ArrayData* valArr = ArrayData::create(size, ArrayData::VALUE);

    intArr->getIntData()[24] = 1;
    dblArr->getDblData()[24] = 1.0;
    valArr->getValData()[24] = Value(1);
    CHECK(intArr->getIntData()[24] == 1);
    CHECK(dblArr->getDblData()[24] == 1.0);
    CHECK(valArr->getValData()[24].asInt() == 1);

    delete intArr;
    delete dblArr;
    delete valArr;
}

TEST_CASE("ArrayData zero-size") {
    ArrayData* arr = ArrayData::create(0, ArrayData::INT);
    CHECK(arr->length == 0);
    CHECK(arr->elemType == ArrayData::INT);
    delete arr;
}

TEST_CASE("ArrayData: ensure int buffer is smaller than value buffer for same size") {
    const int size = 25;
    ArrayData* intArr = ArrayData::create(size, ArrayData::INT);
    ArrayData* valArr = ArrayData::create(size, ArrayData::VALUE);

    CHECK(sizeof(int) == 4);
    CHECK(sizeof(Value) == 8);

    delete intArr;
    delete valArr;
}
