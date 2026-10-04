#ifndef CELL_STACK_HPP
#define CELL_STACK_HPP

#include "value.hpp"
#include <stdexcept>
#include <cstddef>
#include <algorithm>
#include <new>
#include <span>
#include <limits>
#include "stack_config.hpp"
#include <sys/mman.h>
#include <unistd.h>

namespace nutmeg {

// Enable bounds checking for stack operations.
// Set to false for maximum performance in production builds.
constexpr bool ENABLE_STACK_CHECKS = true;

namespace detail {
// Saturating arithmetic, so that no policy or request can overflow.
constexpr size_t saturating_add(size_t a, size_t b) {
    size_t result;
    return __builtin_add_overflow(a, b, &result) ? std::numeric_limits<size_t>::max() : result;
}
constexpr size_t saturating_mul(size_t a, size_t b) {
    size_t result;
    return __builtin_mul_overflow(a, b, &result) ? std::numeric_limits<size_t>::max() : result;
}
}

// The size, in bytes, that a stack with `committed` bytes should grow to (see stack_config.hpp
// for the policy): committed + sum(coefficients[N] * (committed >> N)) + add_bytes, raised to
// at least `needed` and to one `page` more than `committed`, clamped to `reserve` and then
// rounded up to whole pages. `reserve` must be a multiple of `page`, so the result is too and
// never exceeds it. Allocation-free and without exceptions, so it is usable from a signal
// handler.
constexpr size_t next_committed_bytes(size_t committed, size_t needed, size_t reserve, size_t page,
                                      std::span<const size_t> coefficients, size_t add_bytes) {
    size_t grown = detail::saturating_add(committed, add_bytes);
    for (size_t n = 0; n < coefficients.size(); n++) {
        size_t shifted = n < std::numeric_limits<size_t>::digits ? committed >> n : 0;
        grown = detail::saturating_add(grown, detail::saturating_mul(coefficients[n], shifted));
    }
    grown = std::max({grown, needed, detail::saturating_add(committed, page)});
    grown = std::min(grown, reserve);
    return (grown + page - 1) / page * page;
}

// Whether a stack is surrounded by guard pages.
enum class Guard {
    None,   // Plain heap array.
    Pages,  // A protected page below the first cell and another above the last (see CellStack).
};

// Lightweight stack implementation for VM stacks.
// Uses a backing array with pointer-based operations.
// Much more efficient than std::vector for push/pop at the end.
//
// With Guard::Pages the storage is one anonymous mapping, in whole pages, that reserves address
// space for the stack to grow into:
//
//   | guard | committed data (read/write) | uncommitted (PROT_NONE) ... | guard |
//           ^ base_                       ^ limit_                      ^ end of reserve
//
// A push past the committed data, or a pop/peek below the bottom, faults (SIGSEGV) instead of
// touching neighbouring memory: the first uncommitted page is the top guard, and a page past
// the reserve guards the very end. The stack grows in place (try_grow commits more of the
// reserve and moves limit_ up), so base_ never moves and pointers into the stack stay valid.
// It never shrinks. The software checks are a front line behind which the guard pages stand:
// they give the better error message, they are what grows the stack, and the multi-cell
// operations still need them as a large count can jump over a one-page guard.
class CellStack {
public:
    static constexpr size_t DEFAULT_CAPACITY = 65536;  // 64K cells.
    static constexpr size_t DEFAULT_RESERVE = VALUE_STACK_RESERVE_BYTES / sizeof(Cell);

private:
    Cell* data_;           // Start of the allocation: the array, or the lower guard page.
    Cell* top_;            // Points to next free slot.
    Cell* base_;           // Points to start of array.
    Cell* limit_;          // Points one past the committed part of the array.
    size_t capacity_;      // Committed cells (base_ .. limit_).
    size_t reserve_cells_; // Cells that can be committed in all (== capacity_ if it cannot grow).
    size_t mapping_bytes_; // Size of the mapping, or 0 if the stack is a plain array.
    size_t page_;          // Page size, for growing.

    // Maps guard + reserve + guard and commits only the first capacity_ cells.
    // Sets data_, base_, limit_, capacity_, reserve_cells_ and mapping_bytes_. Throws
    // std::bad_alloc on failure.
    void map_with_guards(size_t reserve) {
        const size_t page = page_;
        const size_t data_bytes = (capacity_ * sizeof(Cell) + page - 1) / page * page;
        const size_t reserve_bytes =
            (std::max(reserve * sizeof(Cell), data_bytes) + page - 1) / page * page;
        capacity_ = data_bytes / sizeof(Cell);
        reserve_cells_ = reserve_bytes / sizeof(Cell);
        mapping_bytes_ = page + reserve_bytes + page;
        void* mapping = mmap(nullptr, mapping_bytes_, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
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

    // Slow path of the software checks: makes room for `needed_cells` in all, by growing, or
    // throws std::runtime_error(message).
    [[gnu::cold, gnu::noinline]] void make_room(size_t needed_cells, const char* message) {
        if (!try_grow(needed_cells)) {
            throw std::runtime_error(message);
        }
    }

public:
    // Constructor with optional capacity (in cells), guard mode and, for Guard::Pages, the
    // maximum size (in cells) the stack can grow to.
    explicit CellStack(size_t capacity = DEFAULT_CAPACITY, Guard guard = Guard::None,
                       size_t reserve = DEFAULT_RESERVE)
        : capacity_(capacity), reserve_cells_(capacity), mapping_bytes_(0),
          page_(static_cast<size_t>(sysconf(_SC_PAGESIZE))) {
        if (guard == Guard::Pages) {
            map_with_guards(reserve);
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

    // Committed cells, and the most cells the stack can ever hold.
    inline size_t capacity() const { return capacity_; }
    inline size_t reserve() const { return reserve_cells_; }

    // Commits more of the reserved range, following the growth policy in stack_config.hpp and
    // at least `min_cells` cells in all. Always makes the stack strictly bigger. Returns false,
    // changing nothing, if the reserve is exhausted, `min_cells` is more than the reserve, the
    // OS refuses, or the stack is not guarded. Cells already in the stack, and their
    // addresses, are untouched; new cells are zero. Does no allocation and throws nothing, so
    // that it can be called from a signal handler.
    bool try_grow(size_t min_cells = 0) noexcept {
        if (capacity_ >= reserve_cells_ || min_cells > reserve_cells_) {
            return false;
        }
        const size_t committed = capacity_ * sizeof(Cell);
        const size_t new_bytes = next_committed_bytes(
            committed, min_cells * sizeof(Cell), reserve_cells_ * sizeof(Cell), page_,
            VALUE_STACK_GROWTH_COEFFICIENTS, VALUE_STACK_GROWTH_ADD_BYTES);
        if (new_bytes <= committed ||
            mprotect(reinterpret_cast<char*>(base_) + committed, new_bytes - committed,
                     PROT_READ | PROT_WRITE) != 0) {
            return false;
        }
        capacity_ = new_bytes / sizeof(Cell);
        limit_ = base_ + capacity_;
        return true;
    }

    // As try_grow, but throws std::runtime_error("Stack overflow: reserve exhausted").
    void grow(size_t min_cells = 0) {
        if (!try_grow(min_cells)) {
            throw std::runtime_error("Stack overflow: reserve exhausted");
        }
    }

    // Push a value onto the stack.
    inline void push(Cell value) {
        if constexpr (ENABLE_STACK_CHECKS) {
            if (top_ >= limit_) {
                make_room(size() + 1, "Stack overflow");
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
                make_room(size() + count, "Stack overflow");
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
                target_stack.make_room(target_stack.size() + count,
                                       "Target stack overflow during move");
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
                make_room(new_size, "Stack resize exceeds capacity");
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
