#ifndef HEAP_HPP
#define HEAP_HPP

#include <cstdint>
#include <vector>
#include <stdexcept>

namespace nutmeg {

// HeapCell is a 64-bit unit of storage in the heap.
union HeapCell {
    int64_t i64;
    double f64;
    void* ptr;
    uint64_t u64;
};

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
    std::vector<HeapCell> cells_;
    size_t next_free_;  // Index of next free cell.
    
public:
    explicit Pool(size_t num_cells);
    
    // Allocate n cells, returns pointer to first cell.
    // Throws std::bad_alloc if insufficient space.
    HeapCell* allocate(size_t n);
    
    // Get pointer to cell at index.
    HeapCell* at(size_t index);
    const HeapCell* at(size_t index) const;
    
    // Get the start of the pool.
    HeapCell* start() { return cells_.data(); }
    const HeapCell* start() const { return cells_.data(); }
    
    // Get current allocation position.
    size_t next_free() const { return next_free_; }
    
    // Check if pointer is in this pool.
    bool contains(const void* ptr) const;
};

// ObjectBuilder allows incremental construction of heap objects.
// Values are accumulated in a temporary buffer, then committed to the pool atomically.
class ObjectBuilder {
private:
    std::vector<HeapCell> cells_;
    Pool* pool_;  // Target pool for commit.
    
public:
    explicit ObjectBuilder(Pool* pool);
    
    // Add a cell to the builder.
    void add_cell(HeapCell cell);
    
    // Add typed values.
    void add_u64(uint64_t value);
    void add_i64(int64_t value);
    void add_ptr(void* ptr);
    void add_f64(double value);
    
    // Get current size.
    size_t size() const { return cells_.size(); }
    
    // Get a cell by index (for reading/modifying before commit).
    HeapCell& operator[](size_t index) { return cells_[index]; }
    const HeapCell& operator[](size_t index) const { return cells_[index]; }
    
    // Commit the accumulated cells to the pool and return pointer to first cell.
    // After commit, the builder is reset and can be reused.
    HeapCell* commit();
    
    // Reset the builder without committing.
    void reset();
};

// Heap manages the pool and provides typed allocation.
class Heap {
private:
    Pool pool_;
    
    // Pointers to the fundamental datakeys (at start of pool).
    HeapCell* datakey_datakey_;
    HeapCell* string_datakey_;
    HeapCell* function_datakey_;
    
    // Initialize the fundamental datakeys();
    void init_datakeys();
    
public:
    Heap();
    
    // Get the fundamental datakeys.
    HeapCell* get_datakey_datakey() const { return datakey_datakey_; }
    HeapCell* get_string_datakey() const { return string_datakey_; }
    HeapCell* get_function_datakey() const { return function_datakey_; }
    
    // Allocate a string object.
    // Returns pointer to the datakey field (the object's identity).
    HeapCell* allocate_string(const char* str, size_t char_count);
    
    // Allocate a function object.
    // Returns pointer to the datakey field.
    HeapCell* allocate_function(size_t num_instructions, int nlocals, int nparams);
    
    // Get string data from a string object pointer.
    const char* get_string_data(HeapCell* obj_ptr) const;
    
    // Get function instruction array from a function object pointer.
    HeapCell* get_function_code(HeapCell* obj_ptr) const;
    
    // Get function metadata.
    int get_function_nlocals(HeapCell* obj_ptr) const;
    int get_function_nparams(HeapCell* obj_ptr) const;
    
    // Get access to the pool for ObjectBuilder.
    Pool* get_pool() { return &pool_; }
};

} // namespace nutmeg

#endif // HEAP_HPP
