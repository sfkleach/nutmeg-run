#include "machine.hpp"
#include <stdexcept>
#include <fmt/core.h>

namespace nutmeg {

Machine::Machine()
    : current_function_(nullptr), pc_(0) {
}

Machine::~Machine() {
}

// Stack operations.
void Machine::push(Cell value) {
    operand_stack_.push_back(value);
}

Cell Machine::pop() {
    if (operand_stack_.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    Cell value = operand_stack_.back();
    operand_stack_.pop_back();
    return value;
}

Cell Machine::peek() const {
    if (operand_stack_.empty()) {
        throw std::runtime_error("Stack is empty");
    }
    return operand_stack_.back();
}

bool Machine::empty() const {
    return operand_stack_.empty();
}

size_t Machine::stack_size() const {
    return operand_stack_.size();
}

// Return stack operations.
void Machine::push_return(Cell value) {
    return_stack_.push_back(value);
}

Cell Machine::pop_return() {
    if (return_stack_.empty()) {
        throw std::runtime_error("Return stack underflow");
    }
    Cell value = return_stack_.back();
    return_stack_.pop_back();
    return value;
}

// Global dictionary operations.
void Machine::define_global(const std::string& name, Cell value) {
    globals_[name] = value;
}

Cell Machine::lookup_global(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) {
        throw std::runtime_error(fmt::format("Undefined global: {}", name));
    }
    return it->second;
}

bool Machine::has_global(const std::string& name) const {
    return globals_.find(name) != globals_.end();
}

// Heap allocation.
Cell Machine::allocate_string(const std::string& value) {
    auto str = std::make_unique<HeapString>();
    str->value = value;
    HeapString* ptr = str.get();
    string_heap_.push_back(std::move(str));
    return make_string(ptr);
}

std::string* Machine::get_string(Cell cell) {
    if (!is_string(cell)) {
        throw std::runtime_error("Cell is not a string");
    }
    HeapString* heap_str = static_cast<HeapString*>(get_string_ptr(cell));
    return &heap_str->value;
}

Cell Machine::allocate_function(std::unique_ptr<FunctionObject> func) {
    FunctionObject* ptr = func.get();
    function_heap_.push_back(std::move(func));
    return make_function(ptr);
}

FunctionObject* Machine::get_function(Cell cell) {
    if (!is_function(cell)) {
        throw std::runtime_error("Cell is not a function");
    }
    return static_cast<FunctionObject*>(get_function_ptr(cell));
}

// Execution.
void Machine::execute(FunctionObject* func) {
    current_function_ = func;
    pc_ = 0;
    
    // Reserve space for local variables on the return stack.
    for (int i = 0; i < func->nlocals; i++) {
        push_return(make_nil());
    }
    
    // Execute instructions.
    while (pc_ < static_cast<int>(func->instructions.size())) {
        const Instruction& inst = func->instructions[pc_];
        execute_instruction(inst);
        pc_++;
    }
    
    // Clean up local variables (defensive check).
    for (int i = 0; i < func->nlocals; i++) {
        pop_return();
    }
}

void Machine::execute_instruction(const Instruction& inst) {
    if (inst.type == "PushInt") {
        if (!inst.index) {
            throw std::runtime_error("PushInt missing index field");
        }
        push(make_small_int(*inst.index));
    }
    else if (inst.type == "PushString") {
        if (!inst.value) {
            throw std::runtime_error("PushString missing value field");
        }
        Cell str_cell = allocate_string(*inst.value);
        push(str_cell);
    }
    else if (inst.type == "PopLocal") {
        if (!inst.index) {
            throw std::runtime_error("PopLocal missing index field");
        }
        Cell value = pop();
        // Local variables are stored on the return stack.
        // Index is relative to the current frame.
        int idx = *inst.index;
        if (idx < 0 || idx >= current_function_->nlocals) {
            throw std::runtime_error(fmt::format("Invalid local index: {}", idx));
        }
        // Store in the return stack at the appropriate offset.
        // The return stack has locals at the bottom (defensive check).
        size_t offset = return_stack_.size() - current_function_->nlocals + idx;
        return_stack_[offset] = value;
    }
    else if (inst.type == "PushLocal") {
        if (!inst.index) {
            throw std::runtime_error("PushLocal missing index field");
        }
        int idx = *inst.index;
        if (idx < 0 || idx >= current_function_->nlocals) {
            throw std::runtime_error(fmt::format("Invalid local index: {}", idx));
        }
        size_t offset = return_stack_.size() - current_function_->nlocals + idx;
        push(return_stack_[offset]);
    }
    else if (inst.type == "PushGlobal") {
        if (!inst.value) {
            throw std::runtime_error("PushGlobal missing value field");
        }
        Cell global_value = lookup_global(*inst.value);
        push(global_value);
    }
    else if (inst.type == "CallGlobalCounted") {
        if (!inst.name || !inst.nargs) {
            throw std::runtime_error("CallGlobalCounted missing name or nargs field");
        }
        // Look up the global function.
        Cell func_cell = lookup_global(*inst.name);
        FunctionObject* func = get_function(func_cell);
        
        // For now, just execute the function inline.
        // In a real implementation, this would need proper call stack management (defensive check).
        execute(func);
    }
    else if (inst.type == "SyscallCounted") {
        if (!inst.name || !inst.nargs) {
            throw std::runtime_error("SyscallCounted missing name or nargs field");
        }
        // Placeholder for syscall implementation.
        throw std::runtime_error(fmt::format("Syscall not implemented: {}", *inst.name));
    }
    else {
        throw std::runtime_error(fmt::format("Unknown instruction: {}", inst.type));
    }
}

} // namespace nutmeg
