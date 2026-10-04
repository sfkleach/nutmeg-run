#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <algorithm>
#include <functional>
#include <limits>
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/cell_stack.hpp"
#include "../src/machine.hpp"

using namespace nutmeg;

namespace {

// One page of data, so the stack is quick to fill.
size_t one_page_of_cells() {
    return static_cast<size_t>(sysconf(_SC_PAGESIZE)) / sizeof(Cell);
}

Cell int_cell(int64_t value) { return make_tagged_int(value); }

// Runs `action` in a child process and returns the signal that killed it, or 0 if it
// survived. A test cannot survive its own SIGSEGV, so faults are provoked in a child. The
// child must not run the test framework or atexit handlers, so it ends with _exit.
int signal_from(const std::function<void()>& action) {
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        // Catch2 installs fatal-signal handlers that would print a bogus failure report from the
        // child; restore the default action so the child just dies with the signal.
        for (int sig : {SIGSEGV, SIGBUS, SIGABRT}) {
            signal(sig, SIG_DFL);
        }
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);  // No core file or crash reporter for a deliberate crash.
        action();
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
}

void fill(CellStack& stack, size_t count) {
    for (size_t i = 0; i < count; i++) {
        stack.push(int_cell(static_cast<int64_t>(i)));
    }
}

} // namespace

TEST_CASE("CellStack pushes and pops in order in both modes", "[cell_stack]") {
    for (Guard guard : {Guard::None, Guard::Pages}) {
        CellStack stack(one_page_of_cells(), guard);
        REQUIRE(stack.empty());
        stack.push(int_cell(1));
        stack.push(int_cell(2));
        REQUIRE(stack.size() == 2);
        REQUIRE(as_detagged_int(stack.peek()) == 2);
        REQUIRE(as_detagged_int(stack.peek_at(0)) == 1);
        REQUIRE(as_detagged_int(stack.pop()) == 2);
        REQUIRE(as_detagged_int(stack.pop()) == 1);
        REQUIRE(stack.empty());
    }
}

TEST_CASE("CellStack multi-cell operations work in both modes", "[cell_stack]") {
    for (Guard guard : {Guard::None, Guard::Pages}) {
        CellStack stack(one_page_of_cells(), guard);
        stack.push_multiple(int_cell(7), 5);
        REQUIRE(stack.size() == 5);
        stack.pop_multiple(2);
        REQUIRE(stack.size() == 3);
        stack.discard_multiple(1);
        REQUIRE(stack.size() == 2);
    }
}

TEST_CASE("CellStack moves cells between guarded and unguarded stacks", "[cell_stack]") {
    for (Guard source_guard : {Guard::None, Guard::Pages}) {
        for (Guard target_guard : {Guard::None, Guard::Pages}) {
            CellStack source(one_page_of_cells(), source_guard);
            CellStack target(one_page_of_cells(), target_guard);
            fill(source, 4);
            source.move_multiple(3, target);
            REQUIRE(source.size() == 1);
            REQUIRE(target.size() == 3);
            REQUIRE(as_detagged_int(target.peek_at(0)) == 1);
            REQUIRE(as_detagged_int(target.peek_at(2)) == 3);
        }
    }
}

TEST_CASE("Soft checks still fire before the guard pages are reached", "[cell_stack]") {
    if constexpr (ENABLE_STACK_CHECKS) {
        CellStack stack(one_page_of_cells(), Guard::Pages, one_page_of_cells());  // Cannot grow.
        REQUIRE_THROWS_WITH(stack.pop(), "Stack underflow");
        fill(stack, one_page_of_cells());
        REQUIRE_THROWS_WITH(stack.push(int_cell(0)), "Stack overflow");
        REQUIRE_THROWS_WITH(stack.push_multiple(int_cell(0), 1), "Stack overflow");
    }
}

TEST_CASE("Guarded stack capacity is rounded up to whole pages", "[cell_stack]") {
    // One cell more than a page of data rounds up to two pages, so a full stack holds two pages' worth.
    CellStack stack(one_page_of_cells() + 1, Guard::Pages, one_page_of_cells() + 1);  // Cannot grow.
    REQUIRE(stack.capacity() == 2 * one_page_of_cells());
    if constexpr (ENABLE_STACK_CHECKS) {
        fill(stack, 2 * one_page_of_cells());
        REQUIRE_THROWS_WITH(stack.push(int_cell(0)), "Stack overflow");
    }
}

TEST_CASE("The first and last data cells of a guarded stack are accessible", "[cell_stack]") {
    CellStack stack(one_page_of_cells(), Guard::Pages);
    fill(stack, one_page_of_cells());
    volatile int64_t* last = &stack.peek().i64;
    volatile int64_t* first = &stack.peek_at(0).i64;
    *last = 11;
    *first = 22;
    REQUIRE(*last == 11);
    REQUIRE(*first == 22);
}

TEST_CASE("Writing one cell past the top of a guarded stack faults", "[cell_stack][guard]") {
    CellStack stack(one_page_of_cells(), Guard::Pages);
    fill(stack, one_page_of_cells());
    volatile int64_t* past_top = &(&stack.peek() + 1)->i64;
    REQUIRE(signal_from([&] { *past_top = 1; }) == SIGSEGV);
}

TEST_CASE("Reading one cell past the top of a guarded stack faults", "[cell_stack][guard]") {
    CellStack stack(one_page_of_cells(), Guard::Pages);
    fill(stack, one_page_of_cells());
    volatile int64_t* past_top = &(&stack.peek() + 1)->i64;
    REQUIRE(signal_from([&] { volatile int64_t value = *past_top; (void)value; }) == SIGSEGV);
}

TEST_CASE("Reading the cell below the base of a guarded stack faults", "[cell_stack][guard]") {
    CellStack stack(one_page_of_cells(), Guard::Pages);
    stack.push(int_cell(1));
    volatile int64_t* below_base = &(&stack.peek_at(0) - 1)->i64;
    REQUIRE(signal_from([&] { volatile int64_t value = *below_base; (void)value; }) == SIGSEGV);
}

TEST_CASE("Writing the cell below the base of a guarded stack faults", "[cell_stack][guard]") {
    CellStack stack(one_page_of_cells(), Guard::Pages);
    stack.push(int_cell(1));
    volatile int64_t* below_base = &(&stack.peek_at(0) - 1)->i64;
    REQUIRE(signal_from([&] { *below_base = 1; }) == SIGSEGV);
}

TEST_CASE("The fault harness reports a surviving child as 0", "[cell_stack][guard]") {
    REQUIRE(signal_from([] {}) == 0);
}

TEST_CASE("The Machine's value stack is guarded", "[cell_stack][guard]") {
    Machine machine;
    if constexpr (ENABLE_STACK_CHECKS) {
        // Fill it through the machine (the soft check allows exactly the capacity).
        for (size_t i = 0; i < CellStack::DEFAULT_CAPACITY; i++) {
            machine.push(int_cell(0));
        }
        volatile int64_t* past_top = &(&machine.peek() + 1)->i64;
        REQUIRE(signal_from([&] { *past_top = 1; }) == SIGSEGV);
    }
}

// ---- Growing the stack ----

namespace {

constexpr size_t PAGE = 4096;  // For the pure policy tests only.

// The expected next size under the configured policy, computed independently of the stack.
size_t expected_next_bytes(size_t committed, size_t needed, size_t reserve) {
    size_t grown = committed + VALUE_STACK_GROWTH_ADD_BYTES;
    for (size_t n = 0; n < VALUE_STACK_GROWTH_COEFFICIENTS.size(); n++) {
        grown += VALUE_STACK_GROWTH_COEFFICIENTS[n] * (committed >> n);
    }
    grown = std::max({grown, needed, committed + static_cast<size_t>(sysconf(_SC_PAGESIZE))});
    grown = std::min(grown, reserve);
    size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return (grown + page - 1) / page * page;
}

} // namespace

TEST_CASE("The growth policy is a pure function of its inputs", "[cell_stack][grow]") {
    constexpr size_t NONE[] = {0};
    constexpr size_t DOUBLE[] = {1, 0};
    constexpr size_t HALF_AGAIN[] = {0, 1};
    constexpr size_t QUARTER_AGAIN[] = {0, 0, 1};
    constexpr size_t BIG = 64 * PAGE;

    // Doubling, 1.5x and 1.25x.
    static_assert(next_committed_bytes(8 * PAGE, 0, BIG, PAGE, DOUBLE, 0) == 16 * PAGE);
    static_assert(next_committed_bytes(8 * PAGE, 0, BIG, PAGE, HALF_AGAIN, 0) == 12 * PAGE);
    static_assert(next_committed_bytes(16 * PAGE, 0, BIG, PAGE, QUARTER_AGAIN, 0) == 20 * PAGE);
    // A constant added on top.
    static_assert(next_committed_bytes(8 * PAGE, 0, BIG, PAGE, NONE, 2 * PAGE) == 10 * PAGE);
    // Results are rounded up to whole pages: 1.5x of 3 pages is 4.5 pages -> 5.
    static_assert(next_committed_bytes(3 * PAGE, 0, BIG, PAGE, HALF_AGAIN, 0) == 5 * PAGE);
    // A policy that would not grow still grows by a page.
    static_assert(next_committed_bytes(8 * PAGE, 0, BIG, PAGE, NONE, 0) == 9 * PAGE);
    // A larger request wins, rounded up to a page.
    static_assert(next_committed_bytes(8 * PAGE, 20 * PAGE + 1, BIG, PAGE, DOUBLE, 0) == 21 * PAGE);
    // The reserve is a ceiling.
    static_assert(next_committed_bytes(48 * PAGE, 0, BIG, PAGE, DOUBLE, 0) == BIG);
    static_assert(next_committed_bytes(BIG, 0, BIG, PAGE, DOUBLE, 0) == BIG);
    // Nothing overflows, whatever the inputs.
    constexpr size_t HUGE_COEFFICIENT[] = {std::numeric_limits<size_t>::max(), 1};
    static_assert(next_committed_bytes(8 * PAGE, 0, BIG, PAGE, HUGE_COEFFICIENT, 0) == BIG);
    static_assert(next_committed_bytes(8 * PAGE, std::numeric_limits<size_t>::max(), BIG, PAGE,
                                       DOUBLE, std::numeric_limits<size_t>::max()) == BIG);
    SUCCEED();
}

TEST_CASE("A guarded stack reports its committed size and reserve", "[cell_stack][grow]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 8 * page_cells);
    REQUIRE(stack.capacity() == page_cells);
    REQUIRE(stack.reserve() == 8 * page_cells);

    // Reserve is raised to the capacity if it is smaller, and both are page rounded.
    CellStack odd(2 * page_cells + 1, Guard::Pages, 1);
    REQUIRE(odd.capacity() == 3 * page_cells);
    REQUIRE(odd.reserve() == 3 * page_cells);
}

TEST_CASE("Growing keeps the contents and the addresses and follows the policy", "[cell_stack][grow]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 64 * page_cells);
    fill(stack, page_cells);
    Cell* first = &stack.peek_at(0);
    Cell* last = &stack.peek();

    for (int round = 0; round < 3; round++) {
        size_t before = stack.capacity();
        stack.grow();
        REQUIRE(stack.capacity() * sizeof(Cell) ==
                expected_next_bytes(before * sizeof(Cell), 0, stack.reserve() * sizeof(Cell)));
        REQUIRE(stack.capacity() > before);
        REQUIRE(&stack.peek_at(0) == first);
        REQUIRE(&stack.peek() == last);
    }
    for (size_t i = 0; i < page_cells; i++) {
        REQUIRE(as_detagged_int(stack.peek_at(i)) == static_cast<int64_t>(i));
    }
    // The new room is usable and zero.
    stack.push_multiple(int_cell(5), stack.capacity() - page_cells);
    REQUIRE(stack.size() == stack.capacity());
}

TEST_CASE("Growing honours a larger request and refuses one beyond the reserve", "[cell_stack][grow]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 16 * page_cells);
    REQUIRE(stack.try_grow(10 * page_cells));
    REQUIRE(stack.capacity() >= 10 * page_cells);
    size_t capacity = stack.capacity();
    REQUIRE_FALSE(stack.try_grow(17 * page_cells));  // More than the reserve: nothing changes.
    REQUIRE(stack.capacity() == capacity);
}

TEST_CASE("The software checks grow the stack", "[cell_stack][grow]") {
    if constexpr (ENABLE_STACK_CHECKS) {
        const size_t page_cells = one_page_of_cells();
        CellStack stack(page_cells, Guard::Pages, 64 * page_cells);
        Cell* first = nullptr;

        for (size_t i = 0; i < 5 * page_cells; i++) {
            stack.push(int_cell(static_cast<int64_t>(i)));
            if (i == 0) first = &stack.peek_at(0);
        }
        REQUIRE(stack.capacity() >= 5 * page_cells);
        REQUIRE(&stack.peek_at(0) == first);
        for (size_t i = 0; i < 5 * page_cells; i++) {
            REQUIRE(as_detagged_int(stack.peek_at(i)) == static_cast<int64_t>(i));
        }

        CellStack more(page_cells, Guard::Pages, 64 * page_cells);
        more.push_multiple(int_cell(3), 7 * page_cells);  // Needs more than one doubling.
        REQUIRE(more.size() == 7 * page_cells);
        REQUIRE(more.capacity() >= 7 * page_cells);

        CellStack source(page_cells, Guard::None);
        CellStack target(page_cells, Guard::Pages, 64 * page_cells);
        fill(target, page_cells);
        fill(source, 3);
        source.move_multiple(3, target);  // The target is full, so it must grow.
        REQUIRE(target.size() == page_cells + 3);
        REQUIRE(as_detagged_int(target.peek()) == 2);
        REQUIRE(as_detagged_int(target.peek_at(page_cells)) == 0);

        CellStack resized(page_cells, Guard::Pages, 64 * page_cells);
        resized.resize(9 * page_cells);
        REQUIRE(resized.size() == 9 * page_cells);
        REQUIRE(resized.capacity() >= 9 * page_cells);
    }
}

TEST_CASE("A stack whose reserve is used up reports overflow and is left intact", "[cell_stack][grow]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 4 * page_cells);
    while (stack.try_grow()) {}
    REQUIRE(stack.capacity() == stack.reserve());
    REQUIRE(stack.capacity() == 4 * page_cells);
    REQUIRE_FALSE(stack.try_grow());
    REQUIRE_THROWS_WITH(stack.grow(), "Stack overflow: reserve exhausted");

    if constexpr (ENABLE_STACK_CHECKS) {
        fill(stack, 4 * page_cells);
        REQUIRE_THROWS_WITH(stack.push(int_cell(0)), "Stack overflow");
        REQUIRE_THROWS_WITH(stack.push_multiple(int_cell(0), 2), "Stack overflow");
        REQUIRE_THROWS_WITH(stack.resize(4 * page_cells + 1), "Stack resize exceeds capacity");
        REQUIRE(stack.size() == 4 * page_cells);
        REQUIRE(as_detagged_int(stack.peek_at(4 * page_cells - 1)) == static_cast<int64_t>(4 * page_cells - 1));
    }
}

TEST_CASE("Unguarded stacks cannot grow and still throw on overflow", "[cell_stack][grow]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::None);
    REQUIRE(stack.capacity() == page_cells);
    REQUIRE(stack.reserve() == page_cells);
    REQUIRE_FALSE(stack.try_grow());
    REQUIRE_FALSE(stack.try_grow(page_cells + 1));
    if constexpr (ENABLE_STACK_CHECKS) {
        fill(stack, page_cells);
        REQUIRE_THROWS_WITH(stack.push(int_cell(0)), "Stack overflow");

        CellStack source(page_cells, Guard::Pages, page_cells);
        source.push(int_cell(1));
        REQUIRE_THROWS_WITH(source.move_multiple(1, stack), "Target stack overflow during move");
        REQUIRE(source.size() == 1);
    }
}

TEST_CASE("After growing, the guard moves up with the new limit", "[cell_stack][grow][guard]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 8 * page_cells);
    fill(stack, page_cells);
    volatile int64_t* old_guard_cell = &(&stack.peek() + 1)->i64;

    // Before growing the cell is in the guard; after growing it is ordinary data.
    REQUIRE(signal_from([&] { *old_guard_cell = 1; }) == SIGSEGV);
    stack.grow();
    *old_guard_cell = 1;
    REQUIRE(*old_guard_cell == 1);

    // The guard is now above the enlarged data.
    fill(stack, stack.capacity() - stack.size());
    volatile int64_t* new_guard_cell = &(&stack.peek() + 1)->i64;
    REQUIRE(signal_from([&] { *new_guard_cell = 1; }) == SIGSEGV);

    // The guard below the base is unchanged.
    volatile int64_t* below_base = &(&stack.peek_at(0) - 1)->i64;
    REQUIRE(signal_from([&] { volatile int64_t value = *below_base; (void)value; }) == SIGSEGV);
}

TEST_CASE("A fully grown stack is guarded just past the end of its reserve", "[cell_stack][grow][guard]") {
    const size_t page_cells = one_page_of_cells();
    CellStack stack(page_cells, Guard::Pages, 4 * page_cells);
    while (stack.try_grow()) {}
    fill(stack, stack.capacity());
    volatile int64_t* past_reserve = &(&stack.peek() + 1)->i64;
    REQUIRE(signal_from([&] { *past_reserve = 1; }) == SIGSEGV);
    REQUIRE(signal_from([&] { volatile int64_t value = *past_reserve; (void)value; }) == SIGSEGV);
}

TEST_CASE("The Machine's value stack grows and has the configured limit", "[cell_stack][grow]") {
    Machine machine;
    if constexpr (ENABLE_STACK_CHECKS) {
        const size_t initial = VALUE_STACK_INITIAL_BYTES / sizeof(Cell);
        const size_t reserve = VALUE_STACK_RESERVE_BYTES / sizeof(Cell);
        for (size_t i = 0; i < initial + 1000; i++) {
            machine.push(int_cell(static_cast<int64_t>(i)));
        }
        REQUIRE(machine.stack_size() == initial + 1000);
        REQUIRE(as_detagged_int(machine.peek_at(initial + 999)) == static_cast<int64_t>(initial + 999));
        while (machine.stack_size() < reserve) {
            machine.push(int_cell(0));
        }
        REQUIRE_THROWS_WITH(machine.push(int_cell(0)), "Stack overflow");
    }
}
