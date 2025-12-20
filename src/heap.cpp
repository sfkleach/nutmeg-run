#include "heap.hpp"
#include "value.hpp"
#include <cstring>
#include <fmt/core.h>

namespace nutmeg {

// 1MB = 1048576 bytes = 131072 cells (8 bytes each).
static constexpr size_t POOL_SIZE_BYTES = 1024 * 1024;
static constexpr size_t POOL_SIZE_CELLS = POOL_SIZE_BYTES / sizeof(Cell);

Pool::Pool(size_t num_cells)
    : cells_(num_cells), next_free_(0) {
}

Cell* Pool::allocate(size_t n) {
    if (next_free_ + n > cells_.size()) {
        throw std::bad_alloc();
    }
    Cell* result = &cells_[next_free_];
    next_free_ += n;
    return result;
}

Cell* Pool::at(size_t index) {
    return &cells_[index];
}

const Cell* Pool::at(size_t index) const {
    return &cells_[index];
}

bool Pool::contains(const void* ptr) const {
    const Cell* cell_ptr = static_cast<const Cell*>(ptr);
    return cell_ptr >= cells_.data() && cell_ptr < cells_.data() + cells_.size();
}

Heap::Heap()
    : pool_(POOL_SIZE_CELLS) {
    init_datakeys();
}

void Heap::init_datakeys() {
    // DatakeyDatakey is the first object in the pool.
    // Layout: [Flavour=Datakey][unused][unused][unused][Datakey=self]
    datakey_datakey_ = pool_.allocate(5);
    
    // Position 0: Flavour byte (Datakey).
    datakey_datakey_[0].u64 = static_cast<uint64_t>(Flavour::Datakey);
    
    // Position 1-3: Unused for datakey objects.
    datakey_datakey_[1].u64 = 0;
    datakey_datakey_[2].u64 = 0;
    datakey_datakey_[3].u64 = 0;
    
    // Position 4: Datakey field points to self.
    datakey_datakey_[4].ptr = datakey_datakey_;
    
    // StringDatakey: a datakey for binarray with BitWidth=8.
    // Layout: [Flavour=Datakey][BitWidth=8][unused][unused][Datakey=DatakeyDatakey]
    string_datakey_ = pool_.allocate(5);
    string_datakey_[0].u64 = static_cast<uint64_t>(Flavour::Datakey);
    string_datakey_[1].u64 = 8;  // BitWidth for UTF-8.
    string_datakey_[2].u64 = 0;
    string_datakey_[3].u64 = 0;
    string_datakey_[4].ptr = datakey_datakey_;
    
    // FunctionDatakey: a datakey for function objects.
    // Layout: [Flavour=Datakey][unused][unused][unused][Datakey=DatakeyDatakey]
    function_datakey_ = pool_.allocate(5);
    function_datakey_[0].u64 = static_cast<uint64_t>(Flavour::Datakey);
    function_datakey_[1].u64 = 0;
    function_datakey_[2].u64 = 0;
    function_datakey_[3].u64 = 0;
    function_datakey_[4].ptr = datakey_datakey_;
}

Cell* Heap::allocate_string(const char* str, size_t char_count) {
    // String layout:
    // [-1: Length (including null terminator)]
    // [0: Datakey pointer (this is the object identity)]
    // [1..N: character data as cells]
    
    // Calculate cells needed for character data (including null terminator).
    size_t bytes_needed = char_count;  // char_count includes null terminator.
    size_t data_cells = (bytes_needed + sizeof(Cell) - 1) / sizeof(Cell);
    
    // Total: 1 (length) + 1 (datakey) + data_cells.
    size_t total_cells = 2 + data_cells;
    
    Cell* base = pool_.allocate(total_cells);
    
    // Write length at position -1 (relative to datakey).
    base[0].u64 = char_count;
    
    // Write datakey at position 0 (this is the object pointer we return).
    Cell* obj_ptr = &base[1];
    obj_ptr[0].ptr = string_datakey_;
    
    // Copy string data starting at position 1.
    char* data = reinterpret_cast<char*>(&obj_ptr[1]);
    std::memcpy(data, str, char_count);
    
    return obj_ptr;
}

Cell* Heap::allocate_function(size_t num_instructions, int nlocals, int nparams) {
    // Function layout:
    // [-2: N (instruction count)]
    // [-1: L (T-block length, 0 for now)]
    // [0: Datakey pointer (object identity)]
    // [1: nlocals (32-bit) | nparams (32-bit) packed]
    // [2..N+1: instruction words]
    
    // Total: 2 (N,L) + 1 (datakey) + 1 (nlocals|nparams) + num_instructions.
    size_t total_cells = 4 + num_instructions;
    
    Cell* base = pool_.allocate(total_cells);
    
    // Write N at position -2 (as tagged int).
    base[0] = make_tagged_int(static_cast<int64_t>(num_instructions));
    
    // Write L at position -1 (T-block length = 0 for now, as tagged int).
    base[1] = make_tagged_int(0);
    
    // Write datakey at position 0 (this is the object pointer we return).
    Cell* obj_ptr = &base[2];
    obj_ptr[0].ptr = function_datakey_;
    
    // Pack nlocals, nextras and nparams into a single 64-bit field at position 1.
    {
        uint64_t sofar = nparams & 0xFFFF;
        sofar |= (static_cast<uint64_t>(nlocals - nparams) & 0xFFFF) << 16;
        sofar |= (static_cast<uint64_t>(nlocals) & 0xFFFF) << 32;
        obj_ptr[1].u64 = sofar;
    }

    return obj_ptr;
}

const char* Heap::get_string_data(Cell* obj_ptr) const {
    // String data starts at position 1 (after datakey).
    return reinterpret_cast<const char*>(&obj_ptr[1]);
}

Cell* Heap::get_function_code(Cell* obj_ptr) const {
    // Instruction words start at position 2 (after datakey and packed nlocals|nparams).
    return &obj_ptr[2];
}

int Heap::get_function_nlocals(Cell* obj_ptr) const {
    // nlocals is in bits 32-47 of position 1.
    uint64_t packed = obj_ptr[1].u64;
    return static_cast<int>((packed >> 32) & 0xFFFF);
}

int Heap::get_function_nparams(Cell* obj_ptr) const {
    // nparams is in bits 0-15 of position 1.
    uint64_t packed = obj_ptr[1].u64;
    return static_cast<int>(packed & 0xFFFF);
}

int Heap::get_function_nextras(Cell* obj_ptr) const {
    // nextras is in bits 31-16 of position 1.
    uint64_t packed = obj_ptr[1].u64;
    return static_cast<int>((packed >> 16) & 0xFFFF);
}

std::pair<int, int> Heap::get_function_extras_and_params(Cell* obj_ptr) const {
    // Both values are packed in position 1: nlocals in lower 32 bits, nparams in upper 32 bits.
    uint64_t packed = obj_ptr[1].u64;
    int nparams = static_cast<int>(packed & 0xFFFF);
    int nextras = static_cast<int>((packed >> 16) & 0xFFFF);
    return {nextras, nparams};
}

ObjectBuilder::ObjectBuilder(Pool* pool)
    : pool_(pool) {
}

void ObjectBuilder::add_cell(Cell cell) {
    cells_.push_back(cell);
}

void ObjectBuilder::add_u64(uint64_t value) {
    Cell cell;
    cell.u64 = value;
    cells_.push_back(cell);
}

void ObjectBuilder::add_i64(int64_t value) {
    Cell cell;
    cell.i64 = value;
    cells_.push_back(cell);
}

void ObjectBuilder::add_ptr(void* ptr) {
    Cell cell;
    cell.ptr = ptr;
    cells_.push_back(cell);
}

void ObjectBuilder::add_f64(double value) {
    Cell cell;
    cell.f64 = value;
    cells_.push_back(cell);
}

Cell* ObjectBuilder::commit() {
    if (cells_.empty()) {
        throw std::runtime_error("Cannot commit empty ObjectBuilder");
    }
    
    // Allocate space in the pool.
    Cell* base = pool_->allocate(cells_.size());
    
    // Copy accumulated cells to the pool.
    for (size_t i = 0; i < cells_.size(); i++) {
        base[i] = cells_[i];
    }
    
    // Reset the builder for reuse.
    cells_.clear();
    
    return base;
}

void ObjectBuilder::reset() {
    cells_.clear();
}

} // namespace nutmeg
