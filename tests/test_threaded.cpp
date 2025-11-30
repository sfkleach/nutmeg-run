#include <catch2/catch_test_macros.hpp>
#include "../src/machine.hpp"
#include "../src/value.hpp"
#include "../src/instruction.hpp"

using namespace nutmeg;

TEST_CASE("Threaded interpreter can execute simple function", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    // Create a simple function: push 42, push 100.
    auto func = std::make_unique<FunctionObject>();
    func->nlocals = 0;
    func->nparams = 0;
    
    // Compile to threaded code: PUSH_INT 42, PUSH_INT 100, HALT.
    InstructionWord w1, w2, w3, w4, w5;
    w1.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w2.i64 = 42;
    w3.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w4.i64 = 100;
    w5.label_addr = opcode_map.at(Opcode::HALT);
    func->code = {w1, w2, w3, w4, w5};
    
    FunctionObject* func_ptr = func.get();
    machine.execute(func_ptr);
    
    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_int(machine.pop()) == 100);
    REQUIRE(as_int(machine.pop()) == 42);
}

TEST_CASE("Threaded interpreter can handle strings", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    auto func = std::make_unique<FunctionObject>();
    func->nlocals = 0;
    func->nparams = 0;
    
    // Compile to threaded code: PUSH_STRING "hello", HALT.
    static std::string test_str = "hello";
    InstructionWord w1, w2, w3;
    w1.label_addr = opcode_map.at(Opcode::PUSH_STRING);
    w2.str_ptr = &test_str;
    w3.label_addr = opcode_map.at(Opcode::HALT);
    func->code = {w1, w2, w3};
    
    FunctionObject* func_ptr = func.get();
    machine.execute(func_ptr);
    
    REQUIRE(machine.stack_size() == 1);
    Cell str = machine.pop();
    REQUIRE(is_ptr(str));
    REQUIRE(*machine.get_string(str) == "hello");
}
