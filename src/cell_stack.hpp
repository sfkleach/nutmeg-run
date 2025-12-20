#ifndef CELL_STACK_HPP
#define CELL_STACK_HPP

#include "value.hpp"
#include <stdexcept>
#include <cstddef>

namespace nutmeg {

// Lightweight stack implementation for VM stacks.
// Uses a fixed-size backing array with pointer-based operations.
// Much more efficient than std::vector for push/pop at the end.
class CellStack {
private:
    static constexpr size_t DEFAULT_CAPACITY = 65536;  // 64K cells.
    Cell* data_;           // Backing array.
    Cell* top_;            // Points to next free slot.
    Cell* base_;           // Points to start of array.
    Cell* limit_;          // Points one past end of array.
    size_t capacity_;

public:
    // Constructor with optional capacity.
    explicit CellStack(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity) {
        data_ = new Cell[capacity_];
        base_ = data_;
        top_ = data_;
        limit_ = data_ + capacity_;
    }

    // Destructor.
    ~CellStack() {
        delete[] data_;
    }

    // Delete copy constructor and assignment (stacks shouldn't be copied).
    CellStack(const CellStack&) = delete;
    CellStack& operator=(const CellStack&) = delete;

    // Push a value onto the stack.
    inline void push(Cell value) {
        if (top_ >= limit_) {
            throw std::runtime_error("Stack overflow");
        }
        *top_++ = value;
    }

    // Pop a value from the stack.
    inline Cell pop() {
        if (top_ <= base_) {
            throw std::runtime_error("Stack underflow");
        }
        return *--top_;
    }

    // Peek at the top value without removing it.
    inline Cell& peek() {
        if (top_ <= base_) {
            throw std::runtime_error("Stack is empty");
        }
        return *(top_ - 1);
    }

    // Peek at an arbitrary position (0 = bottom, size()-1 = top).
    inline Cell& peek_at(size_t index) {
        if (index >= size()) {
            throw std::runtime_error("Stack index out of bounds");
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
        if (top_ - count < base_) {
            throw std::runtime_error("Stack underflow");
        }
        top_ -= count;
    }

    // Resize the stack (for return stack frame management).
    inline void resize(size_t new_size) {
        if (new_size > capacity_) {
            throw std::runtime_error("Stack resize exceeds capacity");
        }
        top_ = base_ + new_size;
    }

    // Get reference to element at offset from top.
    // offset_from_top(0) = top element, offset_from_top(1) = second from top, etc.
    inline Cell& offset_from_top(size_t offset) {
        if (top_ - offset <= base_) {
            throw std::runtime_error("Stack offset out of bounds");
        }
        return *(top_ - offset - 1);
    }
};

} // namespace nutmeg

#endif // CELL_STACK_HPP
