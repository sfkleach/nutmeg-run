#include "value.hpp"
#include <fmt/core.h>

namespace nutmeg {

std::string cell_to_string(Cell cell) {
    if (is_tagged_int(cell)) {
        return fmt::format("{}", as_detagged_int(cell));
    } else if (is_tagged_float(cell)) {
        return fmt::format("{}", as_detagged_float(cell));
    } else if (is_tagged_ptr(cell)) {
        // This is a pointer to a heap-allocated object.
        // For now, just show the address.
        return fmt::format("<ptr@{:x}>", reinterpret_cast<uintptr_t>(as_detagged_ptr(cell)));
    } else if (is_bool(cell)) {
        return as_bool(cell) ? "true" : "false";
    } else if (is_nil(cell)) {
        return "nil";
    } else {
        return fmt::format("<unknown cell {:x}>", cell.u64);
    }
}

} // namespace nutmeg
