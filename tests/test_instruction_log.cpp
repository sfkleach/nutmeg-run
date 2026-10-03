#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "../src/instruction_log.hpp"

using namespace nutmeg;

TEST_CASE("Instruction log entry and exit halves join into a valid JSON line", "[instruction_log]") {
    std::string line = format_entry("PUSH_LOCAL", 3) + format_exit(4);

    REQUIRE(line.back() == '\n');
    auto j = nlohmann::json::parse(line);
    REQUIRE(j.size() == 3);
    REQUIRE(j.at("opcode") == "PUSH_LOCAL");
    REQUIRE(j.at("onEntry").at("stacklength") == 3);
    REQUIRE(j.at("onExit").at("stacklength") == 4);
}

TEST_CASE("Instruction log line has the documented layout", "[instruction_log]") {
    REQUIRE(format_entry("HALT", 0) + format_exit(0) ==
        "{\"opcode\": \"HALT\", \"onEntry\": {\"stacklength\": 0}, \"onExit\": {\"stacklength\": 0}}\n");
}

TEST_CASE("Instruction log entry half is a truncated line without a newline", "[instruction_log]") {
    std::string half = format_entry("CHECK_BOOL", 7);
    REQUIRE(half.find('\n') == std::string::npos);
    REQUIRE(half.find("onExit") == std::string::npos);
}
