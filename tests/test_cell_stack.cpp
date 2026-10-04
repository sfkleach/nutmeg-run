#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <functional>
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
        CellStack stack(one_page_of_cells(), Guard::Pages);
        REQUIRE_THROWS_WITH(stack.pop(), "Stack underflow");
        fill(stack, one_page_of_cells());
        REQUIRE_THROWS_WITH(stack.push(int_cell(0)), "Stack overflow");
        REQUIRE_THROWS_WITH(stack.push_multiple(int_cell(0), 1), "Stack overflow");
    }
}

TEST_CASE("Guarded stack capacity is rounded up to whole pages", "[cell_stack]") {
    // One cell more than a page of data rounds up to two pages, so a full stack holds two pages' worth.
    CellStack stack(one_page_of_cells() + 1, Guard::Pages);
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
