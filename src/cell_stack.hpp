#ifndef CELL_STACK_HPP
#define CELL_STACK_HPP

#include "value.hpp"
#include <stdexcept>
#include <cstddef>
#include <algorithm>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace nutmeg {

// Enable bounds checking for stack operations.
// Set to false for maximum performance in production builds.
constexpr bool ENABLE_STACK_CHECKS = true;

// Whether a stack is surrounded by guard pages.
enum class Guard {
    None,   // Plain heap array.
    Pages,  // A protected page below the first cell and another above the last (see CellStack).
};

// Lightweight stack implementation for VM stacks.
// Uses a fixed-size backing array with pointer-based operations.
// Much more efficient than std::vector for push/pop at the end.
//
// With Guard::Pages the storage is one anonymous mapping laid out in whole pages:
//
//   | guard page (PROT_NONE) | data pages (read/write) | guard page (PROT_NONE) |
//                            ^ base_                   ^ limit_
//
// so a push past the top or a pop/peek below the bottom faults (SIGSEGV) instead of touching
// neighbouring memory. The guard pages are a backstop behind the software checks, which still
// give the better error message, and which are still needed for the multi-cell operations
// since a large count can jump over a one-page guard. The capacity is rounded up to a whole
// number of pages so that the top guard begins exactly at limit_.
class CellStack {
public:
    static constexpr size_t DEFAULT_CAPACITY = 65536;  // 64K cells.

private:
    Cell* data_;           // Start of the allocation: the array, or the lower guard page.
    Cell* top_;            // Points to next free slot.
    Cell* base_;           // Points to start of array.
    Cell* limit_;          // Points one past end of array.
    size_t capacity_;
    size_t mapping_bytes_; // Size of the mapping, or 0 if the stack is a plain array.

    // Maps guard page + data pages + guard page and makes only the data pages accessible.
    // Sets data_, base_, limit_ and mapping_bytes_. Throws std::bad_alloc on failure.
    void map_with_guards() {
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        const size_t data_bytes = (capacity_ * sizeof(Cell) + page - 1) / page * page;
        capacity_ = data_bytes / sizeof(Cell);
        mapping_bytes_ = page + data_bytes + page;
        void* mapping = mmap(nullptr, mapping_bytes_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            throw std::bad_alloc();
        }
        char* data = static_cast<char*>(mapping) + page;
        if (mprotect(data, data_bytes, PROT_READ | PROT_WRITE) != 0) {
            munmap(mapping, mapping_bytes_);
            throw std::bad_alloc();
        }
        data_ = static_cast<Cell*>(mapping);
        base_ = reinterpret_cast<Cell*>(data);
        limit_ = base_ + capacity_;
    }

public:
    // Constructor with optional capacity (in cells) and guard mode.
    explicit CellStack(size_t capacity = DEFAULT_CAPACITY, Guard guard = Guard::None)
        : capacity_(capacity), mapping_bytes_(0) {
        if (guard == Guard::Pages) {
            map_with_guards();
        } else {
            data_ = new Cell[capacity_];
            base_ = data_;
            limit_ = data_ + capacity_;
        }
        top_ = base_;
    }

    // Destructor.
    ~CellStack() {
        if (mapping_bytes_ != 0) {
            munmap(data_, mapping_bytes_);
        } else {
            delete[] data_;
        }
    }

    // Delete copy constructor and assignment (stacks shouldn't be copied).
    CellStack(const CellStack&) = delete;
    CellStack& operator=(const CellStack&) = delete;

    // Push a value onto the stack.
    inline void push(Cell value) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ >= limit_) {
                throw std::runtime_error("Stack overflow");
            }
        }
        *top_++ = value;
    }

    // Pop a value from the stack.
    inline Cell pop() {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ <= base_) {
                throw std::runtime_error("Stack underflow");
            }
        }
        return *--top_;
    }

    // Peek at the top value without removing it.
    inline Cell& peek() {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ <= base_) {
                throw std::runtime_error("Stack is empty");
            }
        }
        return *(top_ - 1);
    }

    // Peek at an arbitrary position (0 = bottom, size()-1 = top).
    inline Cell& peek_at(size_t index) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (index >= size()) {
                throw std::runtime_error("Stack index out of bounds");
            }
        }
        return base_[index];
    }

    // Get current stack size.
    inline size_t size() const {
        return top_ - base_;
    }

    // Check if stack is empty.
    inline bool empty() const {
        return top_ == base_;
    }

    // Pop multiple values at once.
    inline void pop_multiple(size_t count) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ - count < base_) {
                throw std::runtime_error("Stack underflow");
            }
        }
        top_ -= count;
    }

    // Push the same value multiple times.
    inline void push_multiple(Cell value, size_t count) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ + count > limit_) {
                throw std::runtime_error("Stack overflow");
            }
        }
        std::fill_n(top_, count, value);
        top_ += count;
    }

    inline void move_multiple(size_t count, CellStack& target_stack) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ - count < base_) {
                throw std::runtime_error("Stack underflow during move");
            }
            if (target_stack.top_ + count > target_stack.limit_) {
                throw std::runtime_error("Target stack overflow during move");
            }
        }
        // Copy count elements from source to target, preserving order.
        std::copy_n(top_ - count, count, target_stack.top_);
        top_ -= count;
        target_stack.top_ += count;
    }

    inline void discard_multiple(size_t count) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ - count < base_) {
                throw std::runtime_error("Stack underflow during discard");
            }
        }
        top_ -= count;
    }

    // Resize the stack (for return stack frame management).
    inline void resize(size_t new_size) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (new_size > capacity_) {
                throw std::runtime_error("Stack resize exceeds capacity");
            }
        }
        top_ = base_ + new_size;
    }

    // Get reference to element at offset from top.
    // offset_from_top(0) = top element, offset_from_top(1) = second from top, etc.
    inline Cell& offset_from_top(size_t offset) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ - offset <= base_) {
                throw std::runtime_error("Stack offset out of bounds");
            }
        }
        return *(top_ - offset - 1);
    }
};

} // namespace nutmeg

#endif // CELL_STACK_HPP
