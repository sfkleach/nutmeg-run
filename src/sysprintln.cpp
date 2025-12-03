#include "sysprintln.hpp"
#include "machine.hpp"
#include "value.hpp"
#include <fmt/core.h>
#include <stdexcept>

namespace nutmeg {

// Sys-function implementation for "println".
// Takes a reference to a Machine and the number of arguments, and returns no results.
// Uses nargs to determine how many values to print from the stack,
// prints them to stdout, followed by a newline, and removes the N values.
void sys_println(Machine& machine, uint64_t nargs) {
    // Defensive check: Ensure nargs is non-negative (since negative counts don't make sense).
    if (nargs < 0) {
        throw std::runtime_error("println: nargs cannot be negative.");
    }
    
    // Defensive check: Ensure we have enough values on the stack (to avoid confusion from partial pops).
    if (machine.stack_size() < static_cast<size_t>(nargs)) {
        fmt::print("Stack size: {}, nargs: {}\n", machine.stack_size(), nargs);
        throw std::runtime_error("println: Stack underflow, insufficient values for count.");
    }
    
    // Print values directly from the stack by indexing from the appropriate position.
    // The values are at operand_stack_[operand_stack_.size() - nargs + i] for i in [0, nargs).
    size_t base_index = machine.stack_size() - nargs;
    for (int i = 0; i < nargs; ++i) {
        Cell value = machine.peek_at(base_index + i);
        
        if (is_tagged_int(value)) {
            fmt::print("{}", as_detagged_int(value));
        } else if (is_tagged_ptr(value)) {
            // Get string data from heap.
            const char* str = machine.get_string(value);
            fmt::print("{}", str);
        } else if (is_bool(value)) {
            fmt::print("{}", as_bool(value) ? "true" : "false");
        } else if (is_nil(value)) {
            fmt::print("nil");
        } else {
            fmt::print("{}", cell_to_string(value));
        }
        
        // Add space between values (but not after the last one).
        if (i + 1 < nargs) {
            fmt::print(" ");
        }
    }
    
    // Print newline at the end.
    fmt::print("\n");
    
    // Remove the N values from the stack in one step.
    machine.pop_multiple(nargs);
}

} // namespace nutmeg
