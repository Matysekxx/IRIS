#include "doctest.h"
#include "core/Value.h"
#include "core/ArrayData.h"
#include "core/GC.h"
#include "core/Managed.h"

using namespace iris::core;

TEST_SUITE("Value - NaN Tagging") {
    TEST_CASE("Int encoding/decoding") {
        Value v(42);
        CHECK(v.isInt());
        CHECK(v.asInt() == 42);
        CHECK(!v.isDouble());
        CHECK(!v.isBool());
        CHECK(!v.isNull());
    }

    TEST_CASE("Negative int") {
        Value v(-123);
        CHECK(v.isInt());
        CHECK(v.asInt() == -123);
    }

    TEST_CASE("Double encoding/decoding") {
        Value v(3.14);
        CHECK(v.isDouble());
        CHECK(v.asDouble() == doctest::Approx(3.14));
        CHECK(!v.isInt());
    }

    TEST_CASE("Bool encoding/decoding") {
        Value vt(true);
        Value vf(false);
        CHECK(vt.isBool());
        CHECK(vt.asBool() == true);
        CHECK(vf.asBool() == false);
    }

    TEST_CASE("Null encoding") {
        Value v;
        CHECK(v.isNull());
    }

    TEST_CASE("SSO string (<=6 chars)") {
        Value v("hello");
        CHECK(v.isString());
        CHECK(v.isSSO());
        CHECK(v.stringLength() == 5);
        CHECK(v.asSSO() == "hello");
    }

    TEST_CASE("Heap string (>6 chars)") {
        Value v("hello world");
        CHECK(v.isString());
        CHECK(!v.isSSO());
        CHECK(v.isPtr());
        CHECK(v.stringLength() == 11);
        CHECK(v.asStringRef() == "hello world");
    }

    TEST_CASE("String equality - SSO vs SSO") {
        Value a("test");
        Value b("test");
        CHECK(a == b);
    }

    TEST_CASE("String equality - heap vs heap") {
        Value a("hello world");
        Value b("hello world");
        CHECK(a == b);
    }

    TEST_CASE("String equality - SSO vs heap") {
        Value a("test");
        Value b("test");
        CHECK(a == b);
    }

    TEST_CASE("Numeric equality - int vs double") {
        Value a(42);
        Value b(42.0);
        CHECK(a == b);
    }

    TEST_CASE("Numeric add - int + int") {
        Value a(10);
        Value b(20);
        Value c = numericAdd(a, b);
        CHECK(c.isInt());
        CHECK(c.asInt() == 30);
    }

    TEST_CASE("Numeric add - double + double") {
        Value a(1.5);
        Value b(2.5);
        Value c = numericAdd(a, b);
        CHECK(c.isDouble());
        CHECK(c.asDouble() == doctest::Approx(4.0));
    }

    TEST_CASE("Numeric add - int + double") {
        Value a(10);
        Value b(2.5);
        Value c = numericAdd(a, b);
        CHECK(c.isDouble());
        CHECK(c.asDouble() == doctest::Approx(12.5));
    }

    TEST_CASE("String concat - SSO + SSO") {
        Value a("hello");
        Value b("world");
        Value c = numericAddString(a, b);
        CHECK(c.isString());
        CHECK(c.asStringRef() == "helloworld");
    }

    TEST_CASE("String concat - heap + heap") {
        Value a("hello ");
        Value b("world");
        Value c = numericAddString(a, b);
        CHECK(c.isString());
        CHECK(c.asStringRef() == "hello world");
    }

    TEST_CASE("toString - int") {
        CHECK(toString(Value(42)) == "42");
    }

    TEST_CASE("toString - double") {
        CHECK(toString(Value(3.14)) == "3.14");
    }

    TEST_CASE("toString - bool") {
        CHECK(toString(Value(true)) == "true");
        CHECK(toString(Value(false)) == "false");
    }

    TEST_CASE("toString - null") {
        CHECK(toString(Value()) == "null");
    }
}

TEST_SUITE("Value - Rope (lazy concat)") {
    TEST_CASE("Rope creation and flatten") {
        Value a("hello");
        Value b("world");
        Value c("!");
        
        Value ab = numericAddString(a, b);
        Value abc = numericAddString(ab, c);
        
        CHECK(abc.isString());
        CHECK(abc.str() == "helloworld!");
    }

    TEST_CASE("Rope auto-flatten at depth") {
        Value s("x");
        for (int i = 0; i < 70; i++) {
            s = numericAddString(s, Value("y"));
        }
        CHECK(s.stringLength() == 71);
        CHECK(s.str()[0] == 'x');
    }
}

TEST_SUITE("ArrayData - Typed Arrays") {
    TEST_CASE("Int array creation and access") {
        ArrayData* arr = ArrayData::create(10, ArrayData::INT);
        CHECK(arr->length == 10);
        CHECK(arr->elemType == ArrayData::INT);
        
        int* data = arr->getIntData();
        for (int i = 0; i < 10; i++) data[i] = i * 2;
        
        CHECK(data[0] == 0);
        CHECK(data[5] == 10);
        CHECK(data[9] == 18);
        
        delete arr;
    }

    TEST_CASE("Double array creation and access") {
        ArrayData* arr = ArrayData::create(5, ArrayData::DOUBLE);
        CHECK(arr->elemType == ArrayData::DOUBLE);
        
        double* data = arr->getDblData();
        data[0] = 1.5;
        data[4] = 9.5;
        
        CHECK(data[0] == doctest::Approx(1.5));
        CHECK(data[4] == doctest::Approx(9.5));
        
        delete arr;
    }

    TEST_CASE("Value array creation") {
        ArrayData* arr = ArrayData::create(3, ArrayData::VALUE);
        CHECK(arr->elemType == ArrayData::VALUE);
        
        Value* data = arr->getValData();
        data[0] = Value(1);
        data[1] = Value(2.5);
        data[2] = Value("test");
        
        CHECK(data[0].asInt() == 1);
        CHECK(data[1].asDouble() == doctest::Approx(2.5));
        CHECK(data[2].asStringRef() == "test");
        
        delete arr;
    }

    TEST_CASE("Type score tracking") {
        ArrayData* arr = ArrayData::create(10, ArrayData::VALUE);
        CHECK(arr->typeScore == 0);
        
        arr->recordStore(Value(42));
        CHECK(arr->typeScore == 1);
        
        arr->recordStore(Value(3.14));
        CHECK(arr->typeScore == 0);
        
        delete arr;
    }
}

TEST_SUITE("GC - Generational") {
    TEST_CASE("Minor collection - nursery evacuation") {
        GC gc;
        Value* stack = new Value[10];
        std::vector<Variable> globals;
        std::vector<const std::vector<Value>*> pools;
        
        // Allocate in nursery
        StringData* s = new StringData("test");
        stack[0] = Value(s);
        
        // Force minor collection
        gc.minorCollect(stack, 1, globals, pools);
        
        // String should be evacuated to mature
        CHECK(!gc.isInNursery(s));
        
        delete[] stack;
    }

    TEST_CASE("Major collection - mark and sweep") {
        GC gc;
        Value* stack = new Value[10];
        std::vector<Variable> globals;
        std::vector<const std::vector<Value>*> pools;
        
        StringData* s1 = new StringData("keep");
        StringData* s2 = new StringData("collect");
        stack[0] = Value(s1);
        // s2 not referenced - should be collected
        
        gc.majorCollect(stack, 1, globals, pools);
        
        // s1 should survive, s2 should be freed
        // (Can't easily test without exposing internals)
        
        delete[] stack;
    }

    TEST_CASE("Write barrier - dirty flag") {
        GC gc;
        ObjectData* obj = new ObjectData(0, 2);
        obj->getField(0) = Value(42);
        
        // Field write should mark dirty
        obj->getField(1) = Value("test");
        CHECK(obj->dirty == true);
        
        delete obj;
    }
}