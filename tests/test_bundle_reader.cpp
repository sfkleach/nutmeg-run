#include <catch2/catch_test_macros.hpp>
#include "../src/bundle_reader.hpp"

using namespace nutmeg;

TEST_CASE("BundleReader can parse function object JSON", "[bundle_reader]") {
    BundleReader reader(":memory:");  // This will fail to open, but we just want to test parsing.
    
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
    
    FunctionObject func = reader.parse_function_object(json);
    
    REQUIRE(func.nlocals == 2);
    REQUIRE(func.nparams == 1);
    REQUIRE(func.instructions.size() == 3);
    
    REQUIRE(func.instructions[0].type == "PushInt");
    REQUIRE(func.instructions[0].index == 42);
    
    REQUIRE(func.instructions[1].type == "PushString");
    REQUIRE(func.instructions[1].value == "hello");
    
    REQUIRE(func.instructions[2].type == "SyscallCounted");
    REQUIRE(func.instructions[2].name == "print");
    REQUIRE(func.instructions[2].nargs == 1);
}
