#ifndef VALUE_HPP
#define VALUE_HPP

#include <cstdint>
#include <string>

namespace nutmeg {

// Tagged 64-bit value representation.
// Low 3 bits encode the type tag.
using Cell = uint64_t;

// Type tags (3 bits = 8 possible types).
enum class TypeTag : uint8_t {
    SmallInt = 0,    // Small integer (61 bits, signed).
    Function = 1,    // Pointer to function object on heap.
    String   = 2,    // Pointer to string on heap.
    Bool     = 3,    // Boolean value.
    Nil      = 4,    // Nil/null value.
    // Reserve remaining tags for future use.
};

constexpr int TAG_BITS = 3;
constexpr uint64_t TAG_MASK = 0x7;
constexpr uint64_t PAYLOAD_MASK = ~TAG_MASK;

// Check if a cell has a specific tag.
inline bool has_tag(Cell cell, TypeTag tag) {
    return (cell & TAG_MASK) == static_cast<uint64_t>(tag);
}

// Extract the type tag from a cell.
inline TypeTag get_tag(Cell cell) {
    return static_cast<TypeTag>(cell & TAG_MASK);
}

// Small integer operations.
inline Cell make_small_int(int64_t value) {
    // Shift left by TAG_BITS, then set the tag.
    return (static_cast<uint64_t>(value) << TAG_BITS) | static_cast<uint64_t>(TypeTag::SmallInt);
}

inline int64_t get_small_int(Cell cell) {
    // Arithmetic right shift to preserve sign.
    return static_cast<int64_t>(cell) >> TAG_BITS;
}

inline bool is_small_int(Cell cell) {
    return has_tag(cell, TypeTag::SmallInt);
}

// Function reference operations.
inline Cell make_function(void* ptr) {
    return reinterpret_cast<uint64_t>(ptr) | static_cast<uint64_t>(TypeTag::Function);
}

inline void* get_function_ptr(Cell cell) {
    return reinterpret_cast<void*>(cell & PAYLOAD_MASK);
}

inline bool is_function(Cell cell) {
    return has_tag(cell, TypeTag::Function);
}

// String reference operations.
inline Cell make_string(void* ptr) {
    return reinterpret_cast<uint64_t>(ptr) | static_cast<uint64_t>(TypeTag::String);
}

inline void* get_string_ptr(Cell cell) {
    return reinterpret_cast<void*>(cell & PAYLOAD_MASK);
}

inline bool is_string(Cell cell) {
    return has_tag(cell, TypeTag::String);
}

// Boolean operations.
inline Cell make_bool(bool value) {
    return (static_cast<uint64_t>(value) << TAG_BITS) | static_cast<uint64_t>(TypeTag::Bool);
}

inline bool get_bool(Cell cell) {
    return (cell >> TAG_BITS) != 0;
}

inline bool is_bool(Cell cell) {
    return has_tag(cell, TypeTag::Bool);
}

// Nil operations.
inline Cell make_nil() {
    return static_cast<uint64_t>(TypeTag::Nil);
}

inline bool is_nil(Cell cell) {
    return has_tag(cell, TypeTag::Nil);
}

// Helper for debugging: convert cell to string representation.
std::string cell_to_string(Cell cell);

} // namespace nutmeg

#endif // VALUE_HPP
