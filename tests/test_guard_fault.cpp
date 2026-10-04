#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <csignal>
#include <unistd.h>
#include "../src/cell_stack.hpp"
#include "../src/machine.hpp"
#include "fork_helpers.hpp"

using namespace nutmeg;
using nutmeg_test::Handlers;
using nutmeg_test::run_in_child;

namespace nutmeg {
// Reaches the private slow path of the software checks.
struct CellStackTestAccess {
    static void make_room(CellStack& stack, size_t cells) { stack.make_room(cells, "no room"); }
};
}

namespace {

size_t page_cells() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)) / sizeof(Cell); }

Cell int_cell(int64_t value) { return make_tagged_int(value); }

void fill(CellStack& stack, size_t count) {
    for (size_t i = 0; i < count; i++) {
        stack.push_unchecked(int_cell(static_cast<int64_t>(i)));
    }
}

} // namespace

TEST_CASE("A store into the top guard is repaired and retried", "[guard_fault]") {
    CellStack stack(page_cells(), Guard::Pages, 8 * page_cells());
    fill(stack, page_cells());
    Cell* first = &stack.peek_at(0);
    volatile int64_t* guard_cell = &(&stack.peek() + 1)->i64;

    *guard_cell = 77;  // Faults; the handler grows the stack; the store is retried.

    REQUIRE(*guard_cell == 77);
    REQUIRE(stack.capacity() > page_cells());
    REQUIRE(&stack.peek_at(0) == first);
    for (size_t i = 0; i < page_cells(); i++) {
        REQUIRE(as_detagged_int(stack.peek_at(i)) == static_cast<int64_t>(i));
    }
}

TEST_CASE("Unchecked pushes grow the stack through several faults", "[guard_fault]") {
    CellStack stack(page_cells(), Guard::Pages, 64 * page_cells());
    Cell* first = nullptr;
    const size_t count = 20 * page_cells();
    for (size_t i = 0; i < count; i++) {
        stack.push_unchecked(int_cell(static_cast<int64_t>(i)));
        if (i == 0) first = &stack.peek_at(0);
    }
    REQUIRE(stack.size() == count);
    REQUIRE(stack.capacity() >= count);
    REQUIRE(&stack.peek_at(0) == first);
    for (size_t i = 0; i < count; i++) {
        REQUIRE(as_detagged_int(stack.peek_at(i)) == static_cast<int64_t>(i));
    }
}

TEST_CASE("Only the stack whose guard was hit is grown", "[guard_fault]") {
    CellStack a(page_cells(), Guard::Pages, 8 * page_cells());
    CellStack b(page_cells(), Guard::Pages, 8 * page_cells());
    fill(a, page_cells());
    fill(b, page_cells());
    volatile int64_t* b_guard = &(&b.peek() + 1)->i64;
    *b_guard = 1;
    REQUIRE(b.capacity() > page_cells());
    REQUIRE(a.capacity() == page_cells());
}

TEST_CASE("A stale limit cannot cause a spurious growth or error", "[guard_fault]") {
    // At its full reserve with room to spare, asking for room that is already there succeeds,
    // although growing is impossible.
    CellStack stack(page_cells(), Guard::Pages, page_cells());
    REQUIRE_FALSE(stack.try_grow());
    REQUIRE_NOTHROW(CellStackTestAccess::make_room(stack, page_cells() / 2));
    REQUIRE_NOTHROW(CellStackTestAccess::make_room(stack, page_cells()));
    REQUIRE_THROWS_WITH(CellStackTestAccess::make_room(stack, page_cells() + 1), "no room");
}

TEST_CASE("A stack that has used up its reserve is fatal, with a message", "[guard_fault][fatal]") {
    auto result = run_in_child([] {
        CellStack stack(page_cells(), Guard::Pages, 4 * page_cells());
        for (size_t i = 0; i <= 4 * page_cells(); i++) {
            stack.push_unchecked(int_cell(0));
        }
    }, Handlers::Keep);
    REQUIRE(result.signal == SIGSEGV);
    REQUIRE(result.error_text.find("value stack overflow") != std::string::npos);
}

TEST_CASE("A runaway program dies at the stack limit, having grown first", "[guard_fault][fatal]") {
    auto result = run_in_child([] {
        Machine machine;
        std::string json = R"({"nlocals": 0, "nparams": 0, "instructions": [
            {"type": "label", "value": "top"},
            {"type": "push.int", "ivalue": 1},
            {"type": "goto", "value": "top"}]})";
        FunctionObject func = machine.parse_function_object("runaway", {}, json);
        Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
        machine.execute(func_obj);
    }, Handlers::Keep);
    REQUIRE(result.signal == SIGSEGV);
    REQUIRE(result.error_text.find("value stack overflow") != std::string::npos);
}

TEST_CASE("Faults outside the top guard are not repaired", "[guard_fault][fatal]") {
    CellStack stack(page_cells(), Guard::Pages, 8 * page_cells());
    stack.push_unchecked(int_cell(1));
    volatile int64_t* below_base = &(&stack.peek_at(0) - 1)->i64;
    // Two pages above the end of the committed data: past the guard page, not in it.
    volatile int64_t* skipped_guard = &stack.peek_at(0).i64 + 2 * page_cells();

    auto below = run_in_child([&] { *below_base = 1; }, Handlers::Keep);
    REQUIRE(below.signal == SIGSEGV);
    REQUIRE(below.error_text.empty());

    auto skipped = run_in_child([&] { *skipped_guard = 1; }, Handlers::Keep);
    REQUIRE(skipped.signal == SIGSEGV);
    REQUIRE(skipped.error_text.empty());
}

TEST_CASE("A destroyed stack is no longer repaired", "[guard_fault][fatal]") {
    volatile int64_t* guard_cell = nullptr;
    {
        CellStack stack(page_cells(), Guard::Pages, 8 * page_cells());
        fill(stack, page_cells());
        guard_cell = &(&stack.peek() + 1)->i64;
    }
    auto result = run_in_child([&] { *guard_cell = 1; }, Handlers::Keep);
    REQUIRE(result.signal == SIGSEGV);
    REQUIRE(result.error_text.empty());
}
