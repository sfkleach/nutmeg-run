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
    w2 = make_tagged_int(42);
    w3.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w4 = make_tagged_int(100);
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

TEST_CASE("Threaded interpreter can handle GOTO", "[threaded][jumps]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    // Test unconditional jump: PUSH_INT 1, GOTO skip, PUSH_INT 999, label skip:, PUSH_INT 2, HALT.
    // Should push 1 and 2, skipping 999.
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    Cell w1, w2, w3, w4, w5, w6, w7, w8, w9;
    w1.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT
    w2.i64 = 1 << 2;                                    // 1 (tagged int)
    w3.label_addr = opcode_map.at(Opcode::GOTO);       // GOTO
    w4.i64 = 2;                                         // offset to skip 2 cells (PUSH_INT + operand)
    w5.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (should be skipped)
    w6.i64 = 999 << 2;                                  // 999 (tagged int, should be skipped)
    w7.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (label skip:)
    w8.i64 = 2 << 2;                                    // 2 (tagged int)
    w9.label_addr = opcode_map.at(Opcode::HALT);       // HALT
    func.code = {w1, w2, w3, w4, w5, w6, w7, w8, w9};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    // Should have 2 values on stack: 1 and 2 (not 999).
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

TEST_CASE("Threaded interpreter can handle IF_NOT with false", "[threaded][jumps]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    // Test conditional jump with false: PUSH_BOOL false, IF_NOT skip, PUSH_INT 999, label skip:, PUSH_INT 42, HALT.
    // Should push 42 only, skipping 999 because condition is false.
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    Cell w1, w2, w3, w4, w5, w6, w7, w8, w9;
    w1.label_addr = opcode_map.at(Opcode::PUSH_BOOL);  // PUSH_BOOL
    w2 = SPECIAL_FALSE;                                 // false
    w3.label_addr = opcode_map.at(Opcode::IF_NOT);     // IF_NOT
    w4.i64 = 2;                                         // offset to skip 2 cells (PUSH_INT + operand)
    w5.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (should be skipped)
    w6.i64 = 999 << 2;                                  // 999 (tagged int, should be skipped)
    w7.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (label skip:)
    w8.i64 = 42 << 2;                                   // 42 (tagged int)
    w9.label_addr = opcode_map.at(Opcode::HALT);       // HALT
    func.code = {w1, w2, w3, w4, w5, w6, w7, w8, w9};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    // Should have 1 value on stack: 42 (not 999).
    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}

TEST_CASE("Threaded interpreter can handle IF_NOT with true", "[threaded][jumps]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    // Test conditional jump with true: PUSH_BOOL true, IF_NOT skip, PUSH_INT 99, label skip:, PUSH_INT 42, HALT.
    // Should push 99 and 42 because condition is true (no jump).
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    Cell w1, w2, w3, w4, w5, w6, w7, w8, w9;
    w1.label_addr = opcode_map.at(Opcode::PUSH_BOOL);  // PUSH_BOOL
    w2 = SPECIAL_TRUE;                                  // true
    w3.label_addr = opcode_map.at(Opcode::IF_NOT);     // IF_NOT
    w4.i64 = 2;                                         // offset (not taken)
    w5.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (should NOT be skipped)
    w6.i64 = 99 << 2;                                   // 99 (tagged int, should be pushed)
    w7.label_addr = opcode_map.at(Opcode::PUSH_INT);   // PUSH_INT (label skip:)
    w8.i64 = 42 << 2;                                   // 42 (tagged int)
    w9.label_addr = opcode_map.at(Opcode::HALT);       // HALT
    func.code = {w1, w2, w3, w4, w5, w6, w7, w8, w9};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    // Should have 2 values on stack: 99 and 42.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    REQUIRE(as_detagged_int(machine.pop()) == 99);
}
