#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "../src/event_log.hpp"
#include "../src/heap.hpp"

using namespace nutmeg;

TEST_CASE("Event line has the documented layout", "[event_log]") {
    REQUIRE(format_event(3, "halt", 42, {}) ==
            "{\"n\": 3, \"event\": \"halt\", \"instruction\": 42}\n");
    REQUIRE(format_event(1, "allocate", 0, {ev_ptr("address", reinterpret_cast<void*>(0xabc0)), ev_int("cells", 5)}) ==
            "{\"n\": 1, \"event\": \"allocate\", \"instruction\": 0, \"address\": \"&0xabc0\", \"cells\": 5}\n");
}

TEST_CASE("Event fields have the right JSON types", "[event_log]") {
    auto j = nlohmann::json::parse(format_event(
        2, "load.binding", 0,
        {ev_str("name", "a\"b\\c\n"), ev_bool("lazy", false), ev_int("nlocals", -3)}));
    REQUIRE(j.at("name") == "a\"b\\c\n");
    REQUIRE(j.at("lazy") == false);
    REQUIRE(j.at("nlocals") == -3);
    REQUIRE(j.at("n") == 2);
}

TEST_CASE("Event strings with invalid UTF-8 still give valid JSON", "[event_log]") {
    auto j = nlohmann::json::parse(format_event(1, "bundle.open", 0, {ev_str("file", "bad\xff.bundle")}));
    REQUIRE(j.at("file").is_string());
}

TEST_CASE("Allocation and failed allocation are still ordinary Pool behaviour", "[event_log]") {
    Pool pool(4);
    Cell* a = pool.allocate(3);
    REQUIRE(pool.contains(a));
    REQUIRE_THROWS_AS(pool.allocate(2), std::bad_alloc);
    REQUIRE(pool.allocate(1) == a + 3);
}
