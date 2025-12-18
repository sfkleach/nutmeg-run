#include <catch2/catch_test_macros.hpp>
#include "../src/bundle_reader.hpp"
#include "../src/machine.hpp"

using namespace nutmeg;

TEST_CASE("Machine can parse function object JSON", "[bundle_reader]") {
    Machine machine;  // Machine now has parse_function_object.
    
    std::string json = R"({
        "nlocals": 2,
        "nparams": 1,
        "instructions": [
            {
                "type": "PushInt",
                "index": 42
            },
            {
                "type": "PushString",
                "value": "hello"
            },
            {
                "type": "SyscallCounted",
                "name": "print",
                "nargs": 1
            }
        ]
    })";
    
    FunctionObject func = machine.parse_function_object("test", {}, json);
    
    REQUIRE(func.nlocals == 2);
    REQUIRE(func.nparams == 1);
    // Compiled code should be: PUSH_INT 42 PUSH_STRING cell SYSCALL_COUNTED name nargs HALT.
    // That's 1+1 + 1+1 + 1+2 + 1 = 8 instruction words.
    REQUIRE(func.code.size() == 8);
}
