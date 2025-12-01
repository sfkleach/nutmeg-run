#ifndef VALUE_HPP
#define VALUE_HPP

#include <cstdint>
#include <cstring>
#include <string>

namespace nutmeg {

// Cell is a 64-bit unit of storage.
// Can hold tagged values, raw storage in the heap, or instruction words in threaded code.
union Cell {
    int64_t i64;
    double f64;
    void* ptr;
    uint64_t u64;
    void* label_addr;          // Instruction handler label address (for threaded interpreter).
    std::string* str_ptr;      // Pointer to string (for instruction operands).
};

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
inline Cell make_tagged_int(int64_t value) {
    Cell c;
    c.u64 = static_cast<uint64_t>(value) << 2;
    return c;
}

inline int64_t as_detagged_int(Cell cell) {
    // Arithmetic right shift by 2 to preserve sign and recover all 62 bits.
    return static_cast<int64_t>(cell.u64) >> 2;
}

inline bool is_tagged_int(Cell cell) {
    // Check that bits 0-1 are 00.
    return (cell.u64 & 0x3) == 0;
}

// Floating point operations (x10 tag - 62-bit floats).
// Bit 2 is the low-order bit of the float, bits 0-1 are always 10.
inline Cell make_tagged_float(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    Cell c;
    c.u64 = (bits << 2) | TAG_FLOAT;
    return c;
}

inline double as_detagged_float(Cell cell) {
    // Right shift by 2 to extract all 62 bits.
    uint64_t bits = cell.u64 >> 2;
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

inline bool is_tagged_float(Cell cell) {
    // Check that bits 0-1 are 10.
    return (cell.u64 & 0x3) == 0x2;
}

// Pointer operations (001 tag).
// Bottom 3 bits are 001. Assumes pointers are 8-byte aligned (bottom 3 bits are 000).
inline Cell make_tagged_ptr(void* ptr) {
    Cell c;
    c.u64 = reinterpret_cast<uint64_t>(ptr) | TAG_PTR;
    return c;
}

inline void* as_detagged_ptr(Cell cell) {
    // Clear the bottom 3 bits to recover the original pointer.
    return reinterpret_cast<void*>(cell.u64 & ~TAG_MASK_3BIT);
}

inline bool is_tagged_ptr(Cell cell) {
    return (cell.u64 & TAG_MASK_3BIT) == TAG_PTR;
}

// Special literals (111 tag).
// Use upper bits to distinguish between bool true, bool false, nil, etc.
constexpr uint64_t SPECIAL_FALSE = TAG_SPECIAL;           // 0x7
constexpr uint64_t SPECIAL_TRUE  = (1ULL << 3) | TAG_SPECIAL;  // 0xF
constexpr uint64_t SPECIAL_NIL   = (2ULL << 3) | TAG_SPECIAL;  // 0x17

inline Cell make_bool(bool value) {
    Cell c;
    c.u64 = value ? SPECIAL_TRUE : SPECIAL_FALSE;
    return c;
}

inline bool as_bool(Cell cell) {
    return cell.u64 == SPECIAL_TRUE;
}

inline bool is_bool(Cell cell) {
    return cell.u64 == SPECIAL_FALSE || cell.u64 == SPECIAL_TRUE;
}

inline Cell make_nil() {
    Cell c;
    c.u64 = SPECIAL_NIL;
    return c;
}

inline bool is_nil(Cell cell) {
    return cell.u64 == SPECIAL_NIL;
}

// Indirection provides stable pointer to a value.
// Used for globals dictionary to avoid pointer invalidation during map resizing.
template<typename T>
class Indirection {
private:
    T* ptr_;
    
public:
    Indirection() : ptr_(new T()) {}
    explicit Indirection(const T& value) : ptr_(new T(value)) {}
    
    ~Indirection() {
        delete ptr_;
    }
    
    // Disable copy to ensure single ownership.
    Indirection(const Indirection&) = delete;
    Indirection& operator=(const Indirection&) = delete;
    
    // Enable move.
    Indirection(Indirection&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    Indirection& operator=(Indirection&& other) noexcept {
        if (this != &other) {
            delete ptr_;
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    // Get stable pointer to the value.
    T* get_ptr() { return ptr_; }
    const T* get_ptr() const { return ptr_; }
    
    // Dereference operators.
    T& operator*() { return *ptr_; }
    const T& operator*() const { return *ptr_; }
    
    T* operator->() { return ptr_; }
    const T* operator->() const { return ptr_; }
};

// Helper for debugging: convert cell to string representation.
std::string cell_to_string(Cell cell);

} // namespace nutmeg

#endif // VALUE_HPP
