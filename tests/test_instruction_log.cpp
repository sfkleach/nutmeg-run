#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include "../src/instruction_log.hpp"

using namespace nutmeg;

TEST_CASE("Instruction log entry and exit halves join into a valid JSON line", "[instruction_log]") {
    std::string line = format_entry("PUSH_LOCAL", "[\"0d3,0x3\"]", 3) + format_exit(4);

    REQUIRE(line.back() == '\n');
    auto j = nlohmann::json::parse(line);
    REQUIRE(j.size() == 4);
    REQUIRE(j.at("opcode") == "PUSH_LOCAL");
    REQUIRE(j.at("opargs") == nlohmann::json::array({"0d3,0x3"}));
    REQUIRE(j.at("onEntry").at("stacklength") == 3);
    REQUIRE(j.at("onExit").at("stacklength") == 4);
}

TEST_CASE("Instruction log line has the documented layout", "[instruction_log]") {
    REQUIRE(format_entry("HALT", "[]", 0) + format_exit(0) ==
        "{\"opcode\": \"HALT\", \"opargs\": [], \"onEntry\": {\"stacklength\": 0}, "
        "\"onExit\": {\"stacklength\": 0}}\n");
}

TEST_CASE("Instruction log entry half is a truncated line without a newline", "[instruction_log]") {
    std::string half = format_entry("CHECK_BOOL", "[\"0d7,0x7\"]", 7);
    REQUIRE(half.find('\n') == std::string::npos);
    REQUIRE(half.find("onExit") == std::string::npos);
    REQUIRE(half.find("opargs") != std::string::npos);
}

TEST_CASE("Raw operands show a signed decimal and the hex bit pattern", "[instruction_log]") {
    REQUIRE(format_raw(0) == "0d0,0x0");
    REQUIRE(format_raw(12) == "0d12,0xc");
    REQUIRE(format_raw(255) == "0d255,0xff");
    REQUIRE(format_raw(static_cast<uint64_t>(-3)) == "0d-3,0xfffffffffffffffd");
    REQUIRE(format_raw(0x7fffffffffffffffULL) == "0d9223372036854775807,0x7fffffffffffffff");
    REQUIRE(format_raw(0x8000000000000000ULL) == "0d-9223372036854775808,0x8000000000000000");
}

TEST_CASE("Pointers show their hex address without leading zeros", "[instruction_log]") {
    REQUIRE(format_pointer(nullptr) == "&0x0");
    REQUIRE(format_pointer(reinterpret_cast<void*>(0x1a2b8)) == "&0x1a2b8");
    REQUIRE(format_pointer(reinterpret_cast<void*>(0x55d0c8a4e2f0ULL)) == "&0x55d0c8a4e2f0");
}

TEST_CASE("Tagged integers are described, including odd ones and the 62-bit extremes", "[instruction_log]") {
    REQUIRE(format_tagged(make_tagged_int(0)) == "int 0");
    REQUIRE(format_tagged(make_tagged_int(42)) == "int 42");   // Even.
    REQUIRE(format_tagged(make_tagged_int(43)) == "int 43");   // Odd: uses bit 2.
    REQUIRE(format_tagged(make_tagged_int(-1)) == "int -1");
    const int64_t max62 = (int64_t(1) << 61) - 1;
    const int64_t min62 = -(int64_t(1) << 61);
    REQUIRE(format_tagged(make_tagged_int(max62)) == "int 2305843009213693951");
    REQUIRE(format_tagged(make_tagged_int(min62)) == "int -2305843009213693952");
}

TEST_CASE("Tagged floats are described", "[instruction_log]") {
    REQUIRE(format_tagged(make_tagged_float(1.5)) == "float 1.5");
    REQUIRE(format_tagged(make_tagged_float(0.25)) == "float 0.25");
    // Only doubles whose top two bits are clear survive make_tagged_float, so no
    // negative or >= 2.0 values here (see the note on make_tagged_float's limits).
}

TEST_CASE("Tagged pointers show the detagged address", "[instruction_log]") {
    void* p = reinterpret_cast<void*>(0x7f0012345670ULL);
    REQUIRE(format_tagged(make_tagged_ptr(p)) == "&0x7f0012345670");
}

TEST_CASE("Special literals are described", "[instruction_log]") {
    REQUIRE(format_tagged(SPECIAL_FALSE) == "false");
    REQUIRE(format_tagged(SPECIAL_TRUE) == "true");
    REQUIRE(format_tagged(SPECIAL_NIL) == "nil");
    REQUIRE(format_tagged(SPECIAL_UNDEF) == "undef");
    REQUIRE(format_tagged(make_raw_i64((9ULL << 3) | TAG_SPECIAL)) == "special 0x4f");
}

TEST_CASE("Reserved tags are flagged rather than misread", "[instruction_log]") {
    REQUIRE(format_tagged(make_raw_i64(0x13)) == "reserved 0x13");   // ...011
    REQUIRE(format_tagged(make_raw_i64(0x15)) == "reserved 0x15");   // ...101
}

TEST_CASE("Opargs are rendered in order, one per kind", "[instruction_log]") {
    Cell operands[3];
    operands[0] = make_raw_i64(-3);
    operands[1] = make_raw_ptr(reinterpret_cast<void*>(0xabc0));
    operands[2] = make_tagged_int(7);

    REQUIRE(format_opargs(operands, {}) == "[]");
    REQUIRE(format_opargs(operands, {OP_RAW}) == "[\"0d-3,0xfffffffffffffffd\"]");
    REQUIRE(format_opargs(operands, {OP_RAW, OP_PTR, OP_TAGGED}) ==
            "[\"0d-3,0xfffffffffffffffd\", \"&0xabc0\", \"int 7\"]");

    // Whatever the contents, the result must be valid JSON.
    auto j = nlohmann::json::parse(format_opargs(operands, {OP_RAW, OP_PTR, OP_TAGGED}));
    REQUIRE(j.size() == 3);
}
