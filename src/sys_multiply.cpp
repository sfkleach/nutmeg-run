#include "sys_println.hpp"
#include "machine.hpp"
#include "value.hpp"
#include <fmt/core.h>
#include <stdexcept>

#define DEBUG

namespace nutmeg {

// Sys-function implementation for "println".
// Takes a reference to a Machine and the number of arguments, and returns no results.
// Uses nargs to determine how many values to print from the stack,
// prints them to stdout, followed by a newline, and removes the N values.
void sys_multiply(Machine& machine, uint64_t nargs) {
    // Defensive check: Ensure nargs is 2.
    if (nargs != 2) {
        throw std::runtime_error("multiply (*): nargs must be 2.");
    }

    Cell n = machine.pop();
    Cell m = machine.peek();

    fmt::print("multiply: multiplying {} and {}\n", cell_to_string(n), cell_to_string(m));

    // Both must be tagged integers.
    if (!is_tagged_int(n) || !is_tagged_int(m)) {
        throw std::runtime_error("multiply (*): both arguments must be integers.");
    }

    int64_t i = (n.i64) >> 2;
    int64_t k = (m.i64);
    Cell c;
    c.i64 = i * k;

    #ifdef DEBUG
    int64_t j = (m.i64) >> 2;
    fmt::print("multiply: {} * {} = {}\n", i, j, c.i64);
    #endif

    machine.peek() = c;

    fmt::print("stack after multiply: size = {}\n", machine.stack_size());
    for (size_t idx = 0; idx < machine.stack_size(); idx++) {
        fmt::print("  [{}]: {}\n", idx, cell_to_string(machine.peek_at(idx)));
    }
}

} // namespace nutmeg
