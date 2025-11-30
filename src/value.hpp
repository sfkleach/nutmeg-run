#ifndef VALUE_HPP
#define VALUE_HPP

#include <cstdint>
#include <cstring>
#include <string>

namespace nutmeg {

// Tagged 64-bit value representation.
// Bottom 3 bits encode the type tag as per tagging-scheme.md:
// - x00: 62-bit integer
// - x10: 62-bit floating point
// - 001: Pointer (offset by 1, aligned to 8-byte boundaries)
// - 111: Special literals (bool, nil, etc.)
using Cell = uint64_t;

// Type tags.
constexpr uint64_t TAG_INT    = 0x0;  // x00 pattern.
constexpr uint64_t TAG_FLOAT  = 0x2;  // x10 pattern.
constexpr uint64_t TAG_PTR    = 0x1;  // 001 pattern.
constexpr uint64_t TAG_SPECIAL = 0x7;  // 111 pattern.

constexpr uint64_t TAG_MASK_2BIT = 0x3;  // For x00 and x10.
constexpr uint64_t TAG_MASK_3BIT = 0x7;  // For 001 and 111.

// Integer operations (x00 tag - 62-bit integers).
// Bit 2 is the low-order bit of the integer, bits 0-1 are always 00.
// Even integers: 000, odd integers: 100.
inline Cell make_int(int64_t value) {
    // Shift left by 2 bits. The low bit goes into bit 2, bits 0-1 become 00.
    return static_cast<uint64_t>(value) << 2;
}

inline int64_t as_int(Cell cell) {
    // Arithmetic right shift by 2 to preserve sign and recover all 62 bits.
    return static_cast<int64_t>(cell) >> 2;
}

inline bool is_int(Cell cell) {
    // Check that bits 0-1 are 00.
    return (cell & 0x3) == 0;
}

// Floating point operations (x10 tag - 62-bit floats).
// Bit 2 is the low-order bit of the float, bits 0-1 are always 10.
inline Cell make_float(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    // Shift left by 2, then OR with 10 to set bits 0-1.
    return (bits << 2) | TAG_FLOAT;
}

inline double as_float(Cell cell) {
    // Right shift by 2 to extract all 62 bits.
    uint64_t bits = cell >> 2;
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

inline bool is_float(Cell cell) {
    // Check that bits 0-1 are 10.
    return (cell & 0x3) == 0x2;
}

// Pointer operations (001 tag).
// Bottom 3 bits are 001. Assumes pointers are 8-byte aligned (bottom 3 bits are 000).
inline Cell make_ptr(void* ptr) {
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    // OR with 001 to set the tag bits.
    return addr | TAG_PTR;
}

inline void* as_ptr(Cell cell) {
    // Clear the bottom 3 bits to recover the original pointer.
    return reinterpret_cast<void*>(cell & ~TAG_MASK_3BIT);
}

inline bool is_ptr(Cell cell) {
    return (cell & TAG_MASK_3BIT) == TAG_PTR;
}

// Special literals (111 tag).
// Use upper bits to distinguish between bool true, bool false, nil, etc.
constexpr uint64_t SPECIAL_FALSE = TAG_SPECIAL;           // 0x7
constexpr uint64_t SPECIAL_TRUE  = (1ULL << 3) | TAG_SPECIAL;  // 0xF
constexpr uint64_t SPECIAL_NIL   = (2ULL << 3) | TAG_SPECIAL;  // 0x17

inline Cell make_bool(bool value) {
    return value ? SPECIAL_TRUE : SPECIAL_FALSE;
}

inline bool as_bool(Cell cell) {
    return cell == SPECIAL_TRUE;
}

inline bool is_bool(Cell cell) {
    return cell == SPECIAL_FALSE || cell == SPECIAL_TRUE;
}

inline Cell make_nil() {
    return SPECIAL_NIL;
}

inline bool is_nil(Cell cell) {
    return cell == SPECIAL_NIL;
}

// Helper for debugging: convert cell to string representation.
std::string cell_to_string(Cell cell);

} // namespace nutmeg

#endif // VALUE_HPP
