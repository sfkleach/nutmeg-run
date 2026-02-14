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
                "type": "push.int",
                "ivalue": 42
            },
            {
                "type": "push.string",
                "value": "hello"
            },
            {
                "type": "syscall.counted",
                "name": "println",
                "index": 0
            }
        ]
    })";
    
    FunctionObject func = machine.parse_function_object("test", {}, json);
    
    REQUIRE(func.nlocals == 2);
    REQUIRE(func.nparams == 1);
    // Compiled code should be: PUSH_INT 42 PUSH_STRING cell SYSCALL_COUNTED index name HALT.
    // That's 1+1 + 1+1 + 1+2 + 1 = 8 instruction words.
    REQUIRE(func.code.size() == 8);
}
