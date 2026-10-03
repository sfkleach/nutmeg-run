#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include "../src/instruction_log.hpp"

using namespace nutmeg;

namespace {

// A tagged pointer to a new string object on the heap.
Cell make_string(Heap& heap, const std::string& text) {
    return make_tagged_ptr(heap.allocate_string(text.c_str(), text.size() + 1));
}

// Renders one tagged operand as the JSON element format_opargs would produce.
nlohmann::json tagged_element(Cell cell, Heap& heap) {
    Cell operands[1] = {cell};
    return nlohmann::json::parse(format_opargs(operands, {OP_TAGGED}, heap, -1)).at(0);
}

} // namespace

TEST_CASE("Instruction log entry and exit halves join into a valid JSON line", "[instruction_log]") {
    std::string line = format_entry("PUSH_LOCAL", "[\"local 3\"]", 3) + format_exit(4);

    REQUIRE(line.back() == '\n');
    auto j = nlohmann::json::parse(line);
    REQUIRE(j.size() == 4);
    REQUIRE(j.at("opcode") == "PUSH_LOCAL");
    REQUIRE(j.at("opargs") == nlohmann::json::array({"local 3"}));
    REQUIRE(j.at("onEntry").at("stacklength") == 3);
    REQUIRE(j.at("onExit").at("stacklength") == 4);
}

TEST_CASE("Instruction log line has the documented layout", "[instruction_log]") {
    REQUIRE(format_entry("HALT", "[]", 0) + format_exit(0) ==
        "{\"opcode\": \"HALT\", \"opargs\": [], \"onEntry\": {\"stacklength\": 0}, "
        "\"onExit\": {\"stacklength\": 0}}\n");
}

TEST_CASE("Instruction log entry half is a truncated line without a newline", "[instruction_log]") {
    std::string half = format_entry("CHECK_BOOL", "[\"local 1\"]", 7);
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

TEST_CASE("Locals are shown by index, recovered from the frame offset", "[instruction_log]") {
    // offset = nlocals - index + 2, so for 4 locals: index 3 -> 3, index 0 -> 6.
    REQUIRE(format_local(3, 4) == "local 3");
    REQUIRE(format_local(6, 4) == "local 0");
    REQUIRE(format_local(5, 4) == "local 1");
    // A function with one local.
    REQUIRE(format_local(3, 1) == "local 0");
}

TEST_CASE("Locals whose index cannot be recovered show the raw offset", "[instruction_log]") {
    REQUIRE(format_local(7, 4) == "local offset 7");    // Index would be -1.
    REQUIRE(format_local(2, 4) == "local offset 2");    // Index would be nlocals.
    REQUIRE(format_local(3, 0) == "local offset 3");    // No locals at all.
    REQUIRE(format_local(3, -1) == "local offset 3");   // nlocals unknown.
    REQUIRE(format_local(static_cast<uint64_t>(-3), 4) == "local offset -3");
}

TEST_CASE("Tagged integers are described, including odd ones and the 62-bit extremes", "[instruction_log]") {
    Heap heap;
    REQUIRE(format_tagged(make_tagged_int(0), heap).text == "int 0");
    REQUIRE(format_tagged(make_tagged_int(42), heap).text == "int 42");   // Even.
    REQUIRE(format_tagged(make_tagged_int(43), heap).text == "int 43");   // Odd: uses bit 2.
    REQUIRE(format_tagged(make_tagged_int(-1), heap).text == "int -1");
    const int64_t max62 = (int64_t(1) << 61) - 1;
    const int64_t min62 = -(int64_t(1) << 61);
    REQUIRE(format_tagged(make_tagged_int(max62), heap).text == "int 2305843009213693951");
    REQUIRE(format_tagged(make_tagged_int(min62), heap).text == "int -2305843009213693952");
    REQUIRE(format_tagged(make_tagged_int(42), heap).key.empty());
}

TEST_CASE("Tagged floats are described", "[instruction_log]") {
    Heap heap;
    REQUIRE(format_tagged(make_tagged_float(1.5), heap).text == "float 1.5");
    REQUIRE(format_tagged(make_tagged_float(0.25), heap).text == "float 0.25");
    // Only doubles whose top two bits are clear survive make_tagged_float, so no
    // negative or >= 2.0 values here.
}

TEST_CASE("Special literals are described", "[instruction_log]") {
    Heap heap;
    REQUIRE(format_tagged(SPECIAL_FALSE, heap).text == "false");
    REQUIRE(format_tagged(SPECIAL_TRUE, heap).text == "true");
    REQUIRE(format_tagged(SPECIAL_NIL, heap).text == "nil");
    REQUIRE(format_tagged(SPECIAL_UNDEF, heap).text == "undef");
    REQUIRE(format_tagged(make_raw_i64((9ULL << 3) | TAG_SPECIAL), heap).text == "special 0x4f");
}

TEST_CASE("Reserved tags are flagged rather than misread", "[instruction_log]") {
    Heap heap;
    REQUIRE(format_tagged(make_raw_i64(0x13), heap).text == "reserved 0x13");   // ...011
    REQUIRE(format_tagged(make_raw_i64(0x15), heap).text == "reserved 0x15");   // ...101
}

TEST_CASE("Strings are shown whole up to 16 characters", "[instruction_log]") {
    Heap heap;
    OpArg empty = format_tagged(make_string(heap, ""), heap);
    REQUIRE(empty.key == "string");
    REQUIRE(empty.text == "");
    REQUIRE(format_tagged(make_string(heap, "Hello, world!"), heap).text == "Hello, world!");
    REQUIRE(format_tagged(make_string(heap, "0123456789abcdef"), heap).text == "0123456789abcdef");   // 16.
}

TEST_CASE("Strings over 16 characters are cut to 13 followed by ...", "[instruction_log]") {
    Heap heap;
    OpArg arg = format_tagged(make_string(heap, "0123456789abcdefg"), heap);   // 17.
    REQUIRE(arg.key == "string");
    REQUIRE(arg.text == "0123456789abc...");
    REQUIRE(format_tagged(make_string(heap, std::string(1000, 'x')), heap).text == "xxxxxxxxxxxxx...");
}

TEST_CASE("Strings are cut on characters, not bytes", "[instruction_log]") {
    // 20 copies of e-acute (two bytes each in UTF-8): cut after 13 characters = 26 bytes.
    std::string many;
    for (int i = 0; i < 20; i++) many += "\xC3\xA9";
    std::string expected;
    for (int i = 0; i < 13; i++) expected += "\xC3\xA9";
    REQUIRE(shorten_string(many) == expected + "...");

    // 16 two-byte characters (32 bytes) are within the limit and kept whole.
    std::string sixteen;
    for (int i = 0; i < 16; i++) sixteen += "\xC3\xA9";
    REQUIRE(shorten_string(sixteen) == sixteen);

    // The result is always valid UTF-8, wherever the cut falls.
    std::string mixed = "ab\xE2\x82\xAC" "cd\xF0\x9F\x98\x80" "efghijklmnopqrstuvwxyz";
    auto parsed = nlohmann::json::parse(nlohmann::json(shorten_string(mixed)).dump());   // Throws if invalid.
    REQUIRE(parsed.is_string());
}

TEST_CASE("String operands are objects with the text escaped as JSON", "[instruction_log]") {
    Heap heap;
    Cell cell = make_string(heap, "say \"hi\"\\\n\t\x01");
    auto element = tagged_element(cell, heap);
    REQUIRE(element.is_object());
    REQUIRE(element.size() == 2);
    REQUIRE(element.at("key") == "string");
    REQUIRE(element.at("value") == "say \"hi\"\\\n\t\x01");
}

TEST_CASE("Invalid UTF-8 in a string cannot break the JSON", "[instruction_log]") {
    Heap heap;
    Cell cell = make_string(heap, "bad \xFF\xFE bytes");
    Cell operands[1] = {cell};
    auto element = nlohmann::json::parse(format_opargs(operands, {OP_TAGGED}, heap, -1)).at(0);
    REQUIRE(element.at("key") == "string");
    REQUIRE(element.at("value").is_string());
}

TEST_CASE("Other heap objects are shown by type and address", "[instruction_log]") {
    Heap heap;

    Cell* function = heap.allocate_function(4, 2, 1);
    OpArg f = format_tagged(make_tagged_ptr(function), heap);
    REQUIRE(f.key == "function");
    REQUIRE(f.text == format_pointer(function));

    OpArg d = format_tagged(make_tagged_ptr(heap.get_function_datakey()), heap);
    REQUIRE(d.key == "datakey");
    REQUIRE(d.text == format_pointer(heap.get_function_datakey()));

    // The very first object in the heap: reading before it must not be attempted.
    OpArg dd = format_tagged(make_tagged_ptr(heap.get_datakey_datakey()), heap);
    REQUIRE(dd.key == "datakey");

    // An object whose datakey is not one of the known ones.
    Cell* other_key = heap.get_pool()->allocate(5);
    Cell* odd = heap.get_pool()->allocate(2);
    odd[0].ptr = other_key;
    OpArg o = format_tagged(make_tagged_ptr(odd), heap);
    REQUIRE(o.key == "unknown");
    REQUIRE(o.text == format_pointer(odd));
}

TEST_CASE("A pointer outside the heap is shown but never followed", "[instruction_log]") {
    Heap heap;
    alignas(8) static Cell outside[4] = {};
    OpArg arg = format_tagged(make_tagged_ptr(&outside[1]), heap);
    REQUIRE(arg.key == "unknown");
    REQUIRE(arg.text == format_pointer(&outside[1]));
    // A null tagged pointer is also outside the heap.
    REQUIRE(format_tagged(make_tagged_ptr(nullptr), heap).key == "unknown");
}

TEST_CASE("A string object with a damaged length does not read outside the heap", "[instruction_log]") {
    Heap heap;
    Cell* obj = heap.allocate_string("abc", 4);
    obj[-1].u64 = 0x7fffffffffffffffULL;     // Absurd length.
    OpArg arg = format_tagged(make_tagged_ptr(obj), heap);
    REQUIRE(arg.key == "string");
    REQUIRE(arg.text == "");
    obj[-1].u64 = 0;                          // Zero length.
    REQUIRE(format_tagged(make_tagged_ptr(obj), heap).text == "");
}

TEST_CASE("Opargs are rendered in order, one per kind", "[instruction_log]") {
    Heap heap;
    Cell operands[4];
    operands[0] = make_raw_i64(-3);
    operands[1] = make_raw_ptr(reinterpret_cast<void*>(0xabc0));
    operands[2] = make_tagged_int(7);
    operands[3] = make_raw_i64(6);

    REQUIRE(format_opargs(operands, {}, heap, -1) == "[]");
    REQUIRE(format_opargs(operands, {OP_RAW}, heap, -1) == "[\"0d-3,0xfffffffffffffffd\"]");
    REQUIRE(format_opargs(operands, {OP_RAW, OP_PTR, OP_TAGGED, OP_LOCAL}, heap, 4) ==
            "[\"0d-3,0xfffffffffffffffd\", \"&0xabc0\", \"int 7\", \"local 0\"]");

    auto j = nlohmann::json::parse(format_opargs(operands, {OP_RAW, OP_PTR, OP_TAGGED, OP_LOCAL}, heap, 4));
    REQUIRE(j.size() == 4);
}

TEST_CASE("Mixed descriptions and objects share one array", "[instruction_log]") {
    Heap heap;
    Cell operands[2] = {make_raw_i64(3), make_string(heap, "hi")};
    REQUIRE(format_opargs(operands, {OP_LOCAL, OP_TAGGED}, heap, 1) ==
            "[\"local 0\", {\"key\": \"string\", \"value\": \"hi\"}]");
}
