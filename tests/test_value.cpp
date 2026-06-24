#include "doctest.h"
#include "core/Value.h"
#include <cmath>

using namespace iris::core;

TEST_CASE("Value default is null") {
    Value v;
    CHECK(v.isNull());
    CHECK_FALSE(v.isInt());
    CHECK_FALSE(v.isDouble());
    CHECK_FALSE(v.isBool());
}

TEST_CASE("Value int construction") {
    Value v(42);
    CHECK(v.isInt());
    CHECK_FALSE(v.isDouble());
    CHECK(v.asInt() == 42);
}

TEST_CASE("Value double construction") {
    Value v(3.14);
    CHECK(v.isDouble());
    CHECK_FALSE(v.isInt());
    CHECK(v.asDouble() == doctest::Approx(3.14));
}

TEST_CASE("Value bool construction") {
    Value v(true);
    CHECK(v.isBool());
    CHECK(v.asBool() == true);
    CHECK_FALSE(v.isInt());

    Value v2(false);
    CHECK(v2.asBool() == false);
}

TEST_CASE("Value NaN boxing: int tag preserved") {
    Value v(-1);
    CHECK(v.isInt());
    CHECK(v.asInt() == -1);
}

TEST_CASE("Value NaN boxing: double representation") {
    Value v(42.0);
    CHECK(v.isDouble());
    CHECK(v.asDouble() == 42.0);
}

TEST_CASE("Value equality") {
    CHECK(Value(5) == Value(5));
    CHECK(Value(5) != Value(6));
    CHECK(Value(3.14) == Value(3.14));
    CHECK(Value(true) == Value(true));
    CHECK(Value() == Value());
}

TEST_CASE("Value int vs double equality") {
    CHECK(Value(5) == Value(5.0));
    CHECK(Value(0) == Value(0.0));
    CHECK(Value(42) != Value(42.5));
}

TEST_CASE("Value numeric arithmetic") {
    Value a(10), b(3);
    CHECK(numericAdd(a, b) == Value(13));
    CHECK(numericSub(a, b) == Value(7));
    CHECK(numericMul(a, b) == Value(30));

    Value da(10.5), db(2.0);
    CHECK(numericAdd(da, db) == Value(12.5));
    CHECK(numericSub(da, db) == Value(8.5));
}

TEST_CASE("Value numeric division") {
    CHECK(numericDiv(Value(10), Value(3)).asDouble() == doctest::Approx(10.0 / 3.0));
    Value zero(0);
    CHECK(numericDiv(Value(10), zero).isNull());
}

TEST_CASE("Value integer division with mod") {
    CHECK(numericMod(Value(10), Value(3)) == Value(1));
    CHECK(numericMod(Value(10), Value(0)).isNull());
}

TEST_CASE("Value numericNegate") {
    CHECK(numericNegate(Value(5)) == Value(-5));
    CHECK(numericNegate(Value(-3)) == Value(3));
    CHECK(numericNegate(Value(2.5)) == Value(-2.5));
}

TEST_CASE("Value comparisons") {
    CHECK(numericLT(Value(1), Value(2)));
    CHECK_FALSE(numericLT(Value(5), Value(3)));
    CHECK(numericGT(Value(5), Value(3)));
    CHECK(numericLE(Value(2), Value(3)));
    CHECK(numericLE(Value(3), Value(3)));
    CHECK(numericGE(Value(5), Value(3)));
    CHECK(numericGE(Value(5), Value(5)));
}

TEST_CASE("Value mixed int/double comparisons") {
    CHECK(numericLT(Value(3), Value(4.5)));
    CHECK(numericGT(Value(10.0), Value(5)));
}

TEST_CASE("Value toString") {
    CHECK(toString(Value(42)) == "42");
    CHECK(toString(Value(-5)) == "-5");
    CHECK(toString(Value(3.14)) == "3.14");
    CHECK(toString(Value(true)) == "true");
    CHECK(toString(Value(false)) == "false");
    CHECK(toString(Value()) == "null");
}

TEST_CASE("Value isNumeric") {
    CHECK(isNumeric(Value(5)));
    CHECK(isNumeric(Value(3.14)));
    CHECK_FALSE(isNumeric(Value(true)));
    CHECK_FALSE(isNumeric(Value()));
}

TEST_CASE("Value string SSO for short strings") {
    Value s(std::string("hi"));
    CHECK(s.isString());
    CHECK(s.str() == "hi");
}

TEST_CASE("Value string heap for long strings") {
    Value s(std::string("hello world, this is a long string"));
    CHECK(s.isString());
    CHECK(s.str() == "hello world, this is a long string");
}

TEST_CASE("Value string equality") {
    Value a(std::string("hello")), b(std::string("hello"));
    CHECK(a == b);

    Value c(std::string("world"));
    CHECK_FALSE(a == c);
}

TEST_CASE("Value string append") {
    Value a(std::string("hello"));
    a.append(Value(std::string(" world")));
    CHECK(a.str() == "hello world");
}
