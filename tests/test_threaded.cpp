#include <catch2/catch_test_macros.hpp>
#include "../src/machine.hpp"
#include "../src/value.hpp"
#include "../src/instruction.hpp"

using namespace nutmeg;

TEST_CASE("Threaded interpreter can execute simple function", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    // Create a simple function: push 42, push 100.
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // Compile to threaded code: PUSH_INT 42, PUSH_INT 100, HALT.
    Cell w1, w2, w3, w4, w5;
    w1.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w2.i64 = 42;
    w3.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w4.i64 = 100;
    w5.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2, w3, w4, w5};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 100);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}

TEST_CASE("Threaded interpreter can handle strings", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // Pre-allocate string in heap and compile to threaded code: PUSH_STRING cell, HALT.
    Cell str_cell = machine.allocate_string("hello");
    Cell w1, w2, w3;
    w1.label_addr = opcode_map.at(Opcode::PUSH_STRING);
    w2 = str_cell;
    w3.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2, w3};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    REQUIRE(machine.stack_size() == 1);
    Cell str = machine.pop();
    REQUIRE(is_tagged_ptr(str));
    Cell* str_ptr = static_cast<Cell*>(as_detagged_ptr(str));
    REQUIRE(std::string(machine.get_heap().get_string_data(str_ptr)) == "hello");
}
