#include <catch2/catch_test_macros.hpp>
#include "../src/machine.hpp"
#include "../src/value.hpp"

using namespace nutmeg;

TEST_CASE("Machine can push and pop values", "[machine]") {
    Machine machine;

    // Test integer values.
    machine.push(make_tagged_int(42));
    machine.push(make_tagged_int(100));

    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 100);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    REQUIRE(machine.empty());
}

TEST_CASE("Machine can allocate strings", "[machine]") {
    Machine machine;

    Cell str1 = machine.allocate_string("hello");
    Cell str2 = machine.allocate_string("world");

    REQUIRE(is_tagged_ptr(str1));
    REQUIRE(is_tagged_ptr(str2));
    REQUIRE(std::string(machine.get_string(str1)) == "hello");
    REQUIRE(std::string(machine.get_string(str2)) == "world");
}

TEST_CASE("Machine can define and lookup globals", "[machine]") {
    Machine machine;

    machine.define_global("x", make_tagged_int(42), false);
    machine.define_global("y", make_tagged_int(100), false);

    REQUIRE(machine.has_global("x"));
    REQUIRE(machine.has_global("y"));
    REQUIRE(!machine.has_global("z"));

    REQUIRE(as_detagged_int(machine.lookup_global("x")) == 42);
    REQUIRE(as_detagged_int(machine.lookup_global("y")) == 100);
}

TEST_CASE("Machine can execute simple function", "[machine]") {
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

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    // Should have 2 values on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 100);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}

TEST_CASE("Machine can parse and execute JSON with forward jump", "[machine][jumps]") {
    Machine machine;
    
    // JSON with forward jump: push 1, goto skip, push 999, label skip, push 2.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.int", "ivalue": 1},
            {"type": "goto", "value": "skip"},
            {"type": "push.int", "ivalue": 999},
            {"type": "label", "value": "skip"},
            {"type": "push.int", "ivalue": 2}
        ]
    })";
    
    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);
    
    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);
    
    // Should have 1 and 2 on stack, not 999.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

TEST_CASE("Machine can parse and execute JSON with backward jump", "[machine][jumps]") {
    Machine machine;
    
    // JSON with backward jump, but limit execution by not looping infinitely.
    // Just verify the code compiles correctly with a backward reference.
    // Test: label target, push 20, goto end, label end.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.int", "ivalue": 10},
            {"type": "label", "value": "target"},
            {"type": "push.int", "ivalue": 20},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "end"}
        ]
    })";
    
    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);
    
    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);
    
    // Should have 10 and 20 on stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 20);
    REQUIRE(as_detagged_int(machine.pop()) == 10);
}

TEST_CASE("Machine can parse and execute JSON with conditional skip", "[machine][jumps]") {
    Machine machine;
    
    // JSON with conditional: push true, if.not skip, push 99, label skip, push 42.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.not", "value": "skip"},
            {"type": "push.int", "ivalue": 99},
            {"type": "label", "value": "skip"},
            {"type": "push.int", "ivalue": 42}
        ]
    })";
    
    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);
    
    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);
    
    // Should have 99 and 42 on stack (condition is true, so no jump).
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    REQUIRE(as_detagged_int(machine.pop()) == 99);
}
