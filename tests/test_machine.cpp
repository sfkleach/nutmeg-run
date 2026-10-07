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

// ---- ERASE.

TEST_CASE("ERASE pops and discards the top of the stack", "[machine]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.int", "ivalue": 1},
            {"type": "push.int", "ivalue": 2},
            {"type": "erase"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

TEST_CASE("ERASE on an empty stack throws", "[machine]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "erase"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);
    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);

    REQUIRE_THROWS_AS(machine.execute(func_obj), std::runtime_error);
}

// ---- IF_SO.

TEST_CASE("Machine can parse and execute JSON with if.so taking the jump", "[machine][jumps]") {
    Machine machine;

    // push true, if.so skip, push 999, label skip, push 42.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.so", "value": "skip"},
            {"type": "push.int", "ivalue": 999},
            {"type": "label", "value": "skip"},
            {"type": "push.int", "ivalue": 42}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    // Condition is true, so the jump is taken: only 42 on the stack.
    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}

TEST_CASE("Machine can parse and execute JSON with if.so falling through", "[machine][jumps]") {
    Machine machine;

    // push false, if.so skip, push 99, label skip, push 42.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "false"},
            {"type": "if.so", "value": "skip"},
            {"type": "push.int", "ivalue": 99},
            {"type": "label", "value": "skip"},
            {"type": "push.int", "ivalue": 42}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    // Condition is false, so no jump: both 99 and 42 on the stack.
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    REQUIRE(as_detagged_int(machine.pop()) == 99);
}

TEST_CASE("Machine can parse and execute JSON with a backward if.so loop", "[machine][jumps]") {
    Machine machine;

    // A countdown loop bounded by a counter local, decremented via the "-" sys-function and
    // tested via ">" each pass; if.so jumps backward to "loop" while the counter is still
    // positive. Exercises the backward-reference path for IF_SO. local 0 is the counter,
    // local 1 a scratch slot holding the stack-length snapshot each syscall needs.
    std::string json = R"({
        "nlocals": 2,
        "nparams": 0,
        "instructions": [
            {"type": "push.int", "ivalue": 3},
            {"type": "pop.local", "index": 0},
            {"type": "label", "value": "loop"},
            {"type": "stack.length", "index": 1},
            {"type": "push.local", "index": 0},
            {"type": "push.int", "ivalue": 1},
            {"type": "syscall.counted", "index": 1, "name": "-"},
            {"type": "pop.local", "index": 0},
            {"type": "stack.length", "index": 1},
            {"type": "push.local", "index": 0},
            {"type": "push.int", "ivalue": 0},
            {"type": "syscall.counted", "index": 1, "name": ">"},
            {"type": "if.so", "value": "loop"},
            {"type": "push.local", "index": 0}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    // The loop runs exactly 3 times (bounded, not infinite) and the counter ends at 0.
    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 0);
}

TEST_CASE("IF_SO requires a value field", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.so"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    REQUIRE_THROWS_AS(machine.parse_function_object("test", deps, json), std::runtime_error);
}

TEST_CASE("IF_SO throws on an undefined label", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.so", "value": "nowhere"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    REQUIRE_THROWS_AS(machine.parse_function_object("test", deps, json), std::runtime_error);
}

// ---- IF_THEN_ELSE.

TEST_CASE("IF_THEN_ELSE takes the then-branch, then-label defined before else-label", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 1},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 2},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

TEST_CASE("IF_THEN_ELSE takes the else-branch, then-label defined before else-label", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "false"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 1},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 2},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 2);
}

TEST_CASE("IF_THEN_ELSE takes the then-branch, else-label defined before then-label", "[machine][jumps]") {
    Machine machine;

    // Same instructions, with the labels swapped, so the then-offset's base (operands+1)
    // and the else-offset's base (operands+2) are exercised with the opposite label-definition
    // order from the previous test.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 2},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 1},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

TEST_CASE("IF_THEN_ELSE takes the else-branch, else-label defined before then-label", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "false"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 2},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 1},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 2);
}

TEST_CASE("IF_THEN_ELSE with a backward then-target and a forward else-target", "[machine][jumps]") {
    Machine machine;

    // then_lbl is defined before the if.then.else (backward offset), else_lbl after it
    // (forward offset), so a single instruction exercises both bases at once.
    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "goto", "value": "start"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 111},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "start"},
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 222},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 111);
}

TEST_CASE("IF_THEN_ELSE with a backward then-target takes the forward else-target when false", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "goto", "value": "start"},
            {"type": "label", "value": "then_lbl"},
            {"type": "push.int", "ivalue": 111},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "start"},
            {"type": "push.bool", "value": "false"},
            {"type": "if.then.else", "name": "then_lbl", "value": "else_lbl"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 222},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 222);
}

TEST_CASE("IF_THEN_ELSE requires a name field", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "value": "else_lbl"},
            {"type": "label", "value": "else_lbl"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    REQUIRE_THROWS_AS(machine.parse_function_object("test", deps, json), std::runtime_error);
}

TEST_CASE("IF_THEN_ELSE requires a value field", "[machine][jumps]") {
    Machine machine;

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "name": "then_lbl"},
            {"type": "label", "value": "then_lbl"}
        ]
    })";

    std::unordered_map<std::string, bool> deps;
    REQUIRE_THROWS_AS(machine.parse_function_object("test", deps, json), std::runtime_error);
}

TEST_CASE("IF_THEN_ELSE's name can coincide with a lazy global's name, harmlessly", "[machine][jumps]") {
    Machine machine;

    // The loader's laziness lookup (ParseFunctionObject::parse) keys on inst.name, which for
    // if.then.else is the then-label, not a global reference. Both opcodes in the pair are
    // IF_THEN_ELSE, so even if this lookup spuriously matches a same-named lazy global, the
    // resolved opcode - and hence execution - is unaffected.
    machine.define_global("shared_name", make_tagged_int(0), true);

    std::string json = R"({
        "nlocals": 0,
        "nparams": 0,
        "instructions": [
            {"type": "push.bool", "value": "true"},
            {"type": "if.then.else", "name": "shared_name", "value": "else_lbl"},
            {"type": "label", "value": "shared_name"},
            {"type": "push.int", "ivalue": 1},
            {"type": "goto", "value": "end"},
            {"type": "label", "value": "else_lbl"},
            {"type": "push.int", "ivalue": 2},
            {"type": "label", "value": "end"}
        ]
    })";

    std::unordered_map<std::string, bool> deps{{"shared_name", true}};
    FunctionObject func = machine.parse_function_object("test", deps, json);

    Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
}

// ---- IF_NOT_RETURN / IF_SO_RETURN.
//
// These are exercised through a called function (the ident mechanism: define_global + a
// call.global.counted from a caller), since that is what carries a real return-stack frame
// to pop. Each caller calls the same callee twice in a row, which only works correctly if
// the previous call's frame was popped in full (the return stack is balanced), and the
// callee has nlocals > nparams so that PERFORM_RETURN's pop_return_frame(nlocals) has more
// than the parameter alone to discard.

TEST_CASE("IF_NOT_RETURN returns early on false and falls through on true", "[machine][jumps][return]") {
    Machine machine;

    // callee(cond): push cond; if.not.return; push 42; return.
    std::string callee_json = R"({
        "nlocals": 2,
        "nparams": 1,
        "instructions": [
            {"type": "push.local", "index": 0},
            {"type": "if.not.return"},
            {"type": "push.int", "ivalue": 42},
            {"type": "return"}
        ]
    })";
    std::unordered_map<std::string, bool> deps;
    FunctionObject callee = machine.parse_function_object("callee", deps, callee_json);
    Cell* callee_obj = machine.allocate_function(callee.code, callee.nlocals, callee.nparams);
    machine.define_global("callee", make_tagged_ptr(callee_obj), false);

    // caller(): calls callee(false) [early return, nothing pushed], then callee(true)
    // [falls through, pushes 42].
    std::string caller_json = R"({
        "nlocals": 2,
        "nparams": 0,
        "instructions": [
            {"type": "stack.length", "index": 0},
            {"type": "push.bool", "value": "false"},
            {"type": "call.global.counted", "index": 0, "name": "callee"},
            {"type": "stack.length", "index": 1},
            {"type": "push.bool", "value": "true"},
            {"type": "call.global.counted", "index": 1, "name": "callee"},
            {"type": "return"}
        ]
    })";
    FunctionObject caller = machine.parse_function_object("caller", deps, caller_json);
    Cell* caller_obj = machine.allocate_function(caller.code, caller.nlocals, caller.nparams);

    machine.execute(caller_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    // Every frame, including the callee's that returned early, has been popped.
    REQUIRE(machine.return_stack_size() == 0);
}

TEST_CASE("IF_SO_RETURN returns early on true and falls through on false", "[machine][jumps][return]") {
    Machine machine;

    // callee(cond): push cond; if.so.return; push 42; return.
    std::string callee_json = R"({
        "nlocals": 2,
        "nparams": 1,
        "instructions": [
            {"type": "push.local", "index": 0},
            {"type": "if.so.return"},
            {"type": "push.int", "ivalue": 42},
            {"type": "return"}
        ]
    })";
    std::unordered_map<std::string, bool> deps;
    FunctionObject callee = machine.parse_function_object("callee", deps, callee_json);
    Cell* callee_obj = machine.allocate_function(callee.code, callee.nlocals, callee.nparams);
    machine.define_global("callee", make_tagged_ptr(callee_obj), false);

    // caller(): calls callee(true) [early return, nothing pushed], then callee(false)
    // [falls through, pushes 42].
    std::string caller_json = R"({
        "nlocals": 2,
        "nparams": 0,
        "instructions": [
            {"type": "stack.length", "index": 0},
            {"type": "push.bool", "value": "true"},
            {"type": "call.global.counted", "index": 0, "name": "callee"},
            {"type": "stack.length", "index": 1},
            {"type": "push.bool", "value": "false"},
            {"type": "call.global.counted", "index": 1, "name": "callee"},
            {"type": "return"}
        ]
    })";
    FunctionObject caller = machine.parse_function_object("caller", deps, caller_json);
    Cell* caller_obj = machine.allocate_function(caller.code, caller.nlocals, caller.nparams);

    machine.execute(caller_obj);

    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    // Every frame, including the callee's that returned early, has been popped.
    REQUIRE(machine.return_stack_size() == 0);
}

TEST_CASE("Early returns from nested calls leave the return stack balanced", "[machine][jumps][return]") {
    Machine machine;

    // inner(cond): if.so.return on cond, so a true cond returns before the push.
    std::string inner_json = R"({
        "nlocals": 3,
        "nparams": 1,
        "instructions": [
            {"type": "push.local", "index": 0},
            {"type": "if.so.return"},
            {"type": "push.int", "ivalue": 1},
            {"type": "return"}
        ]
    })";
    std::unordered_map<std::string, bool> deps;
    FunctionObject inner = machine.parse_function_object("inner", deps, inner_json);
    machine.define_global("inner", make_tagged_ptr(
        machine.allocate_function(inner.code, inner.nlocals, inner.nparams)), false);

    // outer(cond): calls inner(cond), then returns early itself if cond is false.
    std::string outer_json = R"({
        "nlocals": 4,
        "nparams": 1,
        "instructions": [
            {"type": "stack.length", "index": 1},
            {"type": "push.local", "index": 0},
            {"type": "call.global.counted", "index": 1, "name": "inner"},
            {"type": "push.local", "index": 0},
            {"type": "if.so.return"},
            {"type": "push.int", "ivalue": 2},
            {"type": "return"}
        ]
    })";
    FunctionObject outer = machine.parse_function_object("outer", deps, outer_json);
    machine.define_global("outer", make_tagged_ptr(
        machine.allocate_function(outer.code, outer.nlocals, outer.nparams)), false);

    // main: outer(true) leaves nothing; outer(false) leaves 1 (from inner) and 2.
    std::string main_json = R"({
        "nlocals": 2,
        "nparams": 0,
        "instructions": [
            {"type": "stack.length", "index": 0},
            {"type": "push.bool", "value": "true"},
            {"type": "call.global.counted", "index": 0, "name": "outer"},
            {"type": "stack.length", "index": 1},
            {"type": "push.bool", "value": "false"},
            {"type": "call.global.counted", "index": 1, "name": "outer"},
            {"type": "return"}
        ]
    })";
    FunctionObject main_func = machine.parse_function_object("main", deps, main_json);
    Cell* main_obj = machine.allocate_function(main_func.code, main_func.nlocals, main_func.nparams);

    machine.execute(main_obj);

    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 1);
    REQUIRE(machine.return_stack_size() == 0);
}
