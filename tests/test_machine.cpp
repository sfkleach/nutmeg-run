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
    REQUIRE(std::string(machine.get_string(str1)) == "hello");
    REQUIRE(std::string(machine.get_string(str2)) == "world");
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
    const auto& opcode_map = machine.get_opcode_map();
    
    // Create a simple function: push 42, push 100.
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // Compile to threaded code: PUSH_INT 42, PUSH_INT 100, HALT.
    InstructionWord w1, w2, w3, w4, w5;
    w1.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w2.i64 = 42;
    w3.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w4.i64 = 100;
    w5.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2, w3, w4, w5};
    
    Cell func_cell = machine.allocate_function(func.code, func.nlocals, func.nparams);
    HeapCell* func_ptr = machine.get_function_ptr(func_cell);
    machine.execute(func_ptr);
    
    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_int(machine.pop()) == 100);
    REQUIRE(as_int(machine.pop()) == 42);
}
