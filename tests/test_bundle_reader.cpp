#include <catch2/catch_test_macros.hpp>
#include "../src/bundle_reader.hpp"
#include "../src/machine.hpp"

using namespace nutmeg;

TEST_CASE("BundleReader can parse function object JSON", "[bundle_reader]") {
    BundleReader reader(":memory:");  // This will fail to open, but we just want to test parsing.
    Machine machine;  // Need machine to get opcode map.
    
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
    
    FunctionObject func = reader.parse_function_object(json, machine.get_opcode_map());
    
    REQUIRE(func.nlocals == 2);
    REQUIRE(func.nparams == 1);
    // Compiled code should be: PUSH_INT 42 PUSH_STRING ptr SYSCALL_COUNTED name nargs HALT.
    // That's 1+1 + 1+1 + 1+2 + 1 = 8 instruction words.
    REQUIRE(func.code.size() == 8);
}
