#include <catch2/catch_test_macros.hpp>
#include "../src/machine.hpp"
#include "../src/value.hpp"
#include "../src/instruction.hpp"

using namespace nutmeg;

TEST_CASE("Threaded interpreter can execute simple function", "[threaded]") {
    Machine machine;
    machine.init_threaded();  // Initialize threaded mode.
    
    // Create a simple function: push 42, push 100.
    auto func = std::make_unique<FunctionObject>();
    func->nlocals = 0;
    func->nparams = 0;
    
    Instruction inst1;
    inst1.type = "PushInt";
    inst1.opcode = Opcode::PUSH_INT;
    inst1.index = 42;
    func->instructions.push_back(inst1);
    
    Instruction inst2;
    inst2.type = "PushInt";
    inst2.opcode = Opcode::PUSH_INT;
    inst2.index = 100;
    func->instructions.push_back(inst2);
    
    FunctionObject* func_ptr = func.get();
    machine.execute_threaded(func_ptr);
    
    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_int(machine.pop()) == 100);
    REQUIRE(as_int(machine.pop()) == 42);
}

TEST_CASE("Threaded interpreter can handle strings", "[threaded]") {
    Machine machine;
    machine.init_threaded();
    
    auto func = std::make_unique<FunctionObject>();
    func->nlocals = 0;
    func->nparams = 0;
    
    Instruction inst;
    inst.type = "PushString";
    inst.opcode = Opcode::PUSH_STRING;
    inst.value = "hello";
    func->instructions.push_back(inst);
    
    FunctionObject* func_ptr = func.get();
    machine.execute_threaded(func_ptr);
    
    REQUIRE(machine.stack_size() == 1);
    Cell str = machine.pop();
    REQUIRE(is_ptr(str));
    REQUIRE(*machine.get_string(str) == "hello");
}
