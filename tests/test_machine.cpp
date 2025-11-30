#include <catch2/catch_test_macros.hpp>
#include "../src/machine.hpp"
#include "../src/value.hpp"

using namespace nutmeg;

TEST_CASE("Machine can push and pop values", "[machine]") {
    Machine machine;
    
    // Test integer values.
    machine.push(make_int(42));
    machine.push(make_int(100));
    
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_int(machine.pop()) == 100);
    REQUIRE(as_int(machine.pop()) == 42);
    REQUIRE(machine.empty());
}

TEST_CASE("Machine can allocate strings", "[machine]") {
    Machine machine;
    
    Cell str1 = machine.allocate_string("hello");
    Cell str2 = machine.allocate_string("world");
    
    REQUIRE(is_ptr(str1));
    REQUIRE(is_ptr(str2));
    REQUIRE(*machine.get_string(str1) == "hello");
    REQUIRE(*machine.get_string(str2) == "world");
}

TEST_CASE("Machine can define and lookup globals", "[machine]") {
    Machine machine;
    
    machine.define_global("x", make_int(42));
    machine.define_global("y", make_int(100));
    
    REQUIRE(machine.has_global("x"));
    REQUIRE(machine.has_global("y"));
    REQUIRE(!machine.has_global("z"));
    
    REQUIRE(as_int(machine.lookup_global("x")) == 42);
    REQUIRE(as_int(machine.lookup_global("y")) == 100);
}

TEST_CASE("Machine can execute simple function", "[machine]") {
    Machine machine;
    
    // Create a simple function: push 42, push 100.
    auto func = std::make_unique<FunctionObject>();
    func->nlocals = 0;
    func->nparams = 0;
    
    Instruction inst1;
    inst1.type = "PushInt";
    inst1.index = 42;
    func->instructions.push_back(inst1);
    
    Instruction inst2;
    inst2.type = "PushInt";
    inst2.index = 100;
    func->instructions.push_back(inst2);
    
    FunctionObject* func_ptr = func.get();
    machine.execute(func_ptr);
    
    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_int(machine.pop()) == 100);
    REQUIRE(as_int(machine.pop()) == 42);
}
