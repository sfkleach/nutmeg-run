#ifndef HEAP_HPP
#define HEAP_HPP

#include <cstdint>
#include <vector>
#include <stdexcept>
#include "value.hpp"

namespace nutmeg {

// Object flavours in the heap.
enum class Flavour : uint8_t {
    Datakey = 0,
    Record = 1,
    Vector = 2,
    Binarray = 3,
    Function = 4
};

// Forward declarations.
class Pool;
class Heap;
class ObjectBuilder;

// Pool is a fixed-size linear allocation arena.
class Pool {
private:
    std::vector<Cell> cells_;
    size_t next_free_;  // Index of next free cell.

public:
    explicit Pool(size_t num_cells);

    // Allocate n cells, returns pointer to first cell.
    // Throws std::bad_alloc if insufficient space.
    Cell* allocate(size_t n);

    // Get pointer to cell at index.
    Cell* at(size_t index);
    const Cell* at(size_t index) const;

    // Get the start of the pool.
    Cell* start() { return cells_.data(); }
    const Cell* start() const { return cells_.data(); }

    // Get current allocation position.
    size_t next_free() const { return next_free_; }

    // Check if pointer is in this pool.
    bool contains(const void* ptr) const;
};

// ObjectBuilder allows incremental construction of heap objects.
// Values are accumulated in a temporary buffer, then committed to the pool atomically.
class ObjectBuilder {
private:
    std::vector<Cell> cells_;
    Pool* pool_;  // Target pool for commit.

public:
    explicit ObjectBuilder(Pool* pool);

    // Add a cell to the builder.
    void add_cell(Cell cell);

    // Add typed values.
    void add_u64(uint64_t value);
    void add_i64(int64_t value);
    void add_ptr(void* ptr);
    void add_f64(double value);

    // Get current size.
    size_t size() const { return cells_.size(); }

    // Get a cell by index (for reading/modifying before commit).
    Cell& operator[](size_t index) { return cells_[index]; }
    const Cell& operator[](size_t index) const { return cells_[index]; }

    // Commit the accumulated cells to the pool and return pointer to first cell.
    // After commit, the builder is reset and can be reused.
    Cell* commit();

    // Reset the builder without committing.
    void reset();
};

// Heap manages the pool and provides typed allocation.
class Heap {
private:
    Pool pool_;

    // Pointers to the fundamental datakeys (at start of pool).
    Cell* datakey_datakey_;
    Cell* string_datakey_;
    Cell* function_datakey_;

    // Initialize the fundamental datakeys();
    void init_datakeys();

public:
    Heap();

    // Get the fundamental datakeys.
    Cell* get_datakey_datakey() const { return datakey_datakey_; }
    Cell* get_string_datakey() const { return string_datakey_; }
    Cell* get_function_datakey() const { return function_datakey_; }

    // Allocate a string object.
    // Returns pointer to the datakey field (the object's identity).
    Cell* allocate_string(const char* str, size_t char_count);

    // Allocate a function object.
    // Returns pointer to the datakey field.
    Cell* allocate_function(size_t num_instructions, int nlocals, int nparams);

    // Get string data from a string object pointer.
    const char* get_string_data(Cell* obj_ptr) const;

    // Get function instruction array from a function object pointer.
    Cell* get_function_code(Cell* obj_ptr) const;

    // Get function metadata.
    int get_function_nlocals(Cell* obj_ptr) const;
    int get_function_nparams(Cell* obj_ptr) const;
    int get_function_nextras(Cell* obj_ptr) const;
    
    // Get both nlocals and nparams efficiently (single memory access).
    std::pair<int, int> get_function_extras_and_params(Cell* obj_ptr) const;

    // Get access to the pool for ObjectBuilder.
    Pool* get_pool() { return &pool_; }


    inline bool is_function_object(Cell* cell_ptr) {
        // Function objects are heap objects with FunctionDataKey at offset 0.
        // We can identify them by checking if the datakey at offset 0 matches.
        return cell_ptr[0].ptr == function_datakey_;
    }

    inline bool is_function_value(Cell cell) {
        if (!is_tagged_ptr(cell)) {
            return false;
        }
        Cell* obj_ptr = static_cast<Cell*>(as_detagged_ptr(cell));
        return is_function_object(obj_ptr);
    }

    inline void must_be_function_object(Cell* cell_ptr) {
        if (!is_function_object(cell_ptr)) {
            throw std::runtime_error("Expected function object in heap (datakey mismatch)");
        }
    }

    inline void must_be_function_value(Cell cell) {
        if (!is_function_value(cell)) {
            throw std::runtime_error("Expected function object value (not a tagged pointer or datakey mismatch)");
        }
    }
};

} // namespace nutmeg

#endif // HEAP_HPP
