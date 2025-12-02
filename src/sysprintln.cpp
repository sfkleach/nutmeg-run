#include "sysprintln.hpp"
#include "machine.hpp"
#include "value.hpp"
#include <fmt/core.h>
#include <stdexcept>

namespace nutmeg {

// Sys-function implementation for "println".
// Takes a reference to a Machine and returns no results.
// Pops a count N from the stack (as a tagged integer), then pops N values,
// prints them to stdout, followed by a newline, and removes the N values.
void sys_println(Machine& machine) {
    // Pop the count from the stack.
    if (machine.empty()) {
        throw std::runtime_error("println: Stack underflow when reading count.");
    }
    Cell count_cell = machine.pop();
    
    if (!is_tagged_int(count_cell)) {
        throw std::runtime_error("println: Count must be an integer.");
    }
    
    int64_t count = as_detagged_int(count_cell);
    
    // Defensive check: Ensure count is non-negative (since negative counts don't make sense).
    if (count < 0) {
        throw std::runtime_error("println: Count cannot be negative.");
    }
    
    // Defensive check: Ensure we have enough values on the stack (to avoid confusion from partial pops).
    if (machine.stack_size() < static_cast<size_t>(count)) {
        throw std::runtime_error("println: Stack underflow, insufficient values for count.");
    }
    
    // Print values directly from the stack by indexing from the appropriate position.
    // The values are at operand_stack_[operand_stack_.size() - count + i] for i in [0, count).
    size_t base_index = machine.stack_size() - count;
    for (int64_t i = 0; i < count; ++i) {
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
        if (i + 1 < count) {
            fmt::print(" ");
        }
    }
    
    // Print newline at the end.
    fmt::print("\n");
    
    // Remove the N values from the stack in one step.
    machine.pop_multiple(count);
}

} // namespace nutmeg
