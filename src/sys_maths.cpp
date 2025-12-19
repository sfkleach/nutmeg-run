#include "sys_println.hpp"
#include "machine.hpp"
#include "value.hpp"
#include <fmt/core.h>
#include <stdexcept>

#define DEBUG

namespace nutmeg {

// Helper template for binary integer operations.
// Takes an operation function, operation name, and operation symbol.
// Using templates allows the compiler to inline the operation for optimal performance.
template<typename Op>
static void binary_int_operation(
    Machine& machine,
    uint64_t nargs,
    Op operation,
    const char* op_name,
    const char* op_symbol) {
    
    // Defensive check: Ensure nargs is 2.
    if (nargs != 2) {
        throw std::runtime_error(fmt::format("{} ({}): nargs must be 2.", op_name, op_symbol));
    }

    Cell n = machine.pop();
    Cell m = machine.peek();

    #ifdef DEBUG
    fmt::print("{}: operating on {} and {}\n", op_name, cell_to_string(m), cell_to_string(n));
    #endif

    // Both must be tagged integers.
    if (!is_tagged_int(n) || !is_tagged_int(m)) {
        throw std::runtime_error(fmt::format("{} ({}): both arguments must be integers.", op_name, op_symbol));
    }

    int64_t i = as_detagged_int(m);
    int64_t j = as_detagged_int(n);
    Cell result = operation(i, j);

    #ifdef DEBUG
    fmt::print("{}: {} {} {} = {}\n", op_name, i, op_symbol, j, cell_to_string(result));
    #endif

    machine.peek() = result;

    #ifdef DEBUG
    fmt::print("stack after {}: size = {}\n", op_name, machine.stack_size());
    for (size_t idx = 0; idx < machine.stack_size(); idx++) {
        fmt::print("  [{}]: {}\n", idx, cell_to_string(machine.peek_at(idx)));
    }
    #endif
}

// Sys-function implementations for arithmetic operations.
void sys_add(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) { return make_tagged_int(a + b); }, "add", "+");
}

void sys_subtract(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) { return make_tagged_int(a - b); }, "subtract", "-");
}

void sys_multiply(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) { return make_tagged_int(a * b); }, "multiply", "*");
}

void sys_divide(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        if (b == 0) {
            throw std::runtime_error("divide (/): division by zero");
        }
        return make_tagged_int(a / b);
    }, "divide", "/");
}

void sys_less_than(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a < b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "less_than", "<");
}

void sys_greater_than(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a > b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "greater_than", ">");
}

void sys_equal(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a == b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "equal", "==");
}

void sys_not_equal(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a != b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "not_equal", "!=");
}

void sys_less_than_or_equal_to(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a <= b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "less_equal", "<=");
}

void sys_greater_than_or_equal_to(Machine& machine, uint64_t nargs) {
    binary_int_operation(machine, nargs, [](int64_t a, int64_t b) {
        return (a >= b) ? SPECIAL_TRUE : SPECIAL_FALSE;
    }, "greater_equal", ">=");
}


// Helper template for unary integer operations.
// Takes an operation function, operation name, and operation symbol.
// Using templates allows the compiler to inline the operation for optimal performance.
template<typename Op>
static void unary_int_operation(
    Machine& machine,
    uint64_t nargs,
    Op operation,
    const char* op_name,
    const char* op_symbol) {
    
    // Defensive check: Ensure nargs is 2.
    if (nargs != 2) {
        throw std::runtime_error(fmt::format("{} ({}): nargs must be 2.", op_name, op_symbol));
    }

    Cell x = machine.peek();

    #ifdef DEBUG
    fmt::print("{}: operating on {}\n", op_name, cell_to_string(x));
    #endif

    // Both must be tagged integers.
    if (!is_tagged_int(x)) {
        throw std::runtime_error(fmt::format("{} ({}): argument must be an integer.", op_name, op_symbol));
    }

    int64_t i = as_detagged_int(x);
    int64_t result = operation(i);

    #ifdef DEBUG
    fmt::print("{}: {} {} = {}\n", op_name, op_symbol, i, result);
    #endif

    machine.peek() = make_tagged_int(result);

    #ifdef DEBUG
    fmt::print("stack after {}: size = {}\n", op_name, machine.stack_size());
    for (size_t idx = 0; idx < machine.stack_size(); idx++) {
        fmt::print("  [{}]: {}\n", idx, cell_to_string(machine.peek_at(idx)));
    }
    #endif
}

void sys_negate(Machine& machine, uint64_t nargs) {
    unary_int_operation(machine, nargs, [](int64_t x) {
        return -x;
    }, "negate", "-");
}

} // namespace nutmeg
