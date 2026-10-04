#ifndef STACK_CONFIG_HPP
#define STACK_CONFIG_HPP

#include <array>
#include <cstddef>
#include <limits>
#include "value.hpp"

// Compile-time configuration of the value (operand) stack. See cell_stack.hpp.
namespace nutmeg {

// Committed (usable) size of the value stack at start-up.
inline constexpr size_t VALUE_STACK_INITIAL_BYTES = 512 * 1024;

// The hard limit: the value stack can grow in place up to this size and no further. Only
// address space is reserved for it, so memory is not used until the stack grows into it.
inline constexpr size_t VALUE_STACK_RESERVE_BYTES = 64 * 1024 * 1024;

// Growth policy. When the stack is full and more room is needed, the committed size m becomes
//
//   m' = m + COEFFICIENTS[0] * (m >> 0) + COEFFICIENTS[1] * (m >> 1) + ... + ADD_BYTES
//
// so {1, 0} and 0 (the default) doubles, {0, 1} gives 1.5x, and a third entry would add an
// (m >> 2) term. Only shifts, multiplications and additions are used, so that growing can
// be done from a signal handler. The result is raised to at least one page more than m, then
// rounded up to whole pages and clamped to VALUE_STACK_RESERVE_BYTES.
inline constexpr std::array<size_t, 2> VALUE_STACK_GROWTH_COEFFICIENTS = {1, 0};
inline constexpr size_t VALUE_STACK_GROWTH_ADD_BYTES = 0;

static_assert(VALUE_STACK_INITIAL_BYTES > 0, "The value stack must start with some room");
static_assert(VALUE_STACK_RESERVE_BYTES >= VALUE_STACK_INITIAL_BYTES,
              "The value stack reserve must be at least its initial size");
static_assert(VALUE_STACK_INITIAL_BYTES % sizeof(Cell) == 0 &&
              VALUE_STACK_RESERVE_BYTES % sizeof(Cell) == 0,
              "The value stack sizes must be whole numbers of cells");

namespace detail {
constexpr size_t sum_of(const std::array<size_t, 2>& coefficients) {
    size_t sum = 0;
    for (size_t coefficient : coefficients) sum += coefficient;
    return sum;
}
}

static_assert(detail::sum_of(VALUE_STACK_GROWTH_COEFFICIENTS) <
                  (std::numeric_limits<size_t>::max() - VALUE_STACK_GROWTH_ADD_BYTES) /
                  VALUE_STACK_RESERVE_BYTES - 1,
              "The value stack growth policy could overflow");

} // namespace nutmeg

#endif // STACK_CONFIG_HPP
