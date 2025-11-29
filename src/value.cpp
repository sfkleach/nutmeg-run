#include "value.hpp"
#include <fmt/core.h>

namespace nutmeg {

std::string cell_to_string(Cell cell) {
    TypeTag tag = get_tag(cell);
    
    switch (tag) {
        case TypeTag::SmallInt:
            return fmt::format("{}", get_small_int(cell));
        
        case TypeTag::Function:
            return fmt::format("<function@{:x}>", reinterpret_cast<uintptr_t>(get_function_ptr(cell)));
        
        case TypeTag::String:
            // This is a pointer to a heap-allocated string.
            // For now, just show the address (defensive check).
            return fmt::format("<string@{:x}>", reinterpret_cast<uintptr_t>(get_string_ptr(cell)));
        
        case TypeTag::Bool:
            return get_bool(cell) ? "true" : "false";
        
        case TypeTag::Nil:
            return "nil";
        
        default:
            return fmt::format("<unknown tag {}>", static_cast<int>(tag));
    }
}

} // namespace nutmeg
