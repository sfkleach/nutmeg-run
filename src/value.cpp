#include "value.hpp"
#include <fmt/core.h>

namespace nutmeg {

std::string cell_to_string(Cell cell) {
    if (is_int(cell)) {
        return fmt::format("{}", as_int(cell));
    } else if (is_float(cell)) {
        return fmt::format("{}", as_float(cell));
    } else if (is_ptr(cell)) {
        // This is a pointer to a heap-allocated object.
        // For now, just show the address.
        return fmt::format("<ptr@{:x}>", reinterpret_cast<uintptr_t>(as_ptr(cell)));
    } else if (is_bool(cell)) {
        return as_bool(cell) ? "true" : "false";
    } else if (is_nil(cell)) {
        return "nil";
    } else {
        return fmt::format("<unknown cell {:x}>", cell);
    }
}

} // namespace nutmeg
