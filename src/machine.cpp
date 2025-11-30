#include "machine.hpp"
#include <stdexcept>
#include <fmt/core.h>

namespace nutmeg {

Machine::Machine()
    : current_function_(nullptr), pc_(0) {
    // Initialize the threaded interpreter by capturing label addresses.
    #ifdef __GNUC__
    threaded_impl(nullptr, true);
    #else
    throw std::runtime_error("Threaded interpreter requires GCC/Clang with computed goto support");
    #endif
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
    return make_ptr(ptr);
}

std::string* Machine::get_string(Cell cell) {
    if (!is_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    HeapString* heap_str = static_cast<HeapString*>(as_ptr(cell));
    return &heap_str->value;
}

Cell Machine::allocate_function(std::unique_ptr<FunctionObject> func) {
    FunctionObject* ptr = func.get();
    function_heap_.push_back(std::move(func));
    return make_ptr(ptr);
}

FunctionObject* Machine::get_function(Cell cell) {
    if (!is_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    return static_cast<FunctionObject*>(as_ptr(cell));
}

// Execution using threaded interpreter.
void Machine::execute(FunctionObject* func) {
    current_function_ = func;
    
    // Reserve space for local variables on the return stack.
    for (int i = 0; i < func->nlocals; i++) {
        push_return(make_nil());
    }
    
    // Execute the pre-compiled threaded code.
    threaded_impl(&func->code, false);
    
    // Clean up local variables.
    for (int i = 0; i < func->nlocals; i++) {
        pop_return();
    }
}

void Machine::execute_syscall(const std::string& name, int nargs) {
    if (name == "println") {
        // Pop the value to print from the stack.
        if (operand_stack_.empty()) {
            throw std::runtime_error("println: stack underflow");
        }
        Cell value = pop();
        
        // Print based on type.
        if (is_int(value)) {
            fmt::print("{}\n", as_int(value));
        } else if (is_ptr(value)) {
            // Assume it's a string pointer (defensive check).
            std::string* str = get_string(value);
            fmt::print("{}\n", *str);
        } else if (is_bool(value)) {
            fmt::print("{}\n", as_bool(value) ? "true" : "false");
        } else if (is_nil(value)) {
            fmt::print("nil\n");
        } else {
            fmt::print("{}\n", cell_to_string(value));
        }
    }
    else {
        throw std::runtime_error(fmt::format("Unknown syscall: {}", name));
    }
}



// Combined init/run function for threaded interpreter (like Poppy's init_or_run).
// 
// Key implementation constraint: This must be a SINGLE function handling both
// initialization and execution phases. In C++, label addresses (&&label) are only
// valid within the function where they are defined. We cannot capture labels in
// one function and use them in another.
//
// The two-phase pattern:
// 1. Init phase (init_mode=true): Captures label addresses into opcode_map_.
//    This must happen before any code compilation, as compile_to_threaded()
//    depends on opcode_map_ being populated.
// 2. Run phase (init_mode=false): Executes the compiled instruction stream
//    using the previously captured label addresses.
//
// Both phases occur within the same function scope, ensuring label addresses
// remain valid throughout the threaded interpreter's lifetime.
void Machine::threaded_impl(std::vector<InstructionWord>* code, bool init_mode) {
    #ifdef __GNUC__
    // In init mode, just capture the labels and return.
    if (init_mode) {
        opcode_map_ = {
            {Opcode::PUSH_INT, &&L_PUSH_INT},
            {Opcode::PUSH_STRING, &&L_PUSH_STRING},
            {Opcode::POP_LOCAL, &&L_POP_LOCAL},
            {Opcode::PUSH_LOCAL, &&L_PUSH_LOCAL},
            {Opcode::PUSH_GLOBAL, &&L_PUSH_GLOBAL},
            {Opcode::CALL_GLOBAL_COUNTED, &&L_CALL_GLOBAL_COUNTED},
            {Opcode::SYSCALL_COUNTED, &&L_SYSCALL_COUNTED},
            {Opcode::STACK_LENGTH, &&L_STACK_LENGTH},
            {Opcode::RETURN, &&L_RETURN},
            {Opcode::HALT, &&L_HALT},
        };
        return;
    }
    
    // Run mode: execute the compiled code.
    InstructionWord* pc = code->data();
    
    // Jump to the first instruction.
    goto *pc++->label_addr;
    
    L_PUSH_INT: {
        int64_t value = (pc++)->i64;
        push(make_int(value));
        goto *(pc++)->label_addr;
    }
    
    L_PUSH_STRING: {
        std::string* str = (pc++)->str_ptr;
        push(allocate_string(*str));
        goto *(pc++)->label_addr;
    }
    
    L_POP_LOCAL: {
        int64_t idx = (pc++)->i64;
        Cell value = pop();
        size_t offset = return_stack_.size() - current_function_->nlocals + idx;
        return_stack_[offset] = value;
        goto *(pc++)->label_addr;
    }
    
    L_PUSH_LOCAL: {
        int64_t idx = (pc++)->i64;
        size_t offset = return_stack_.size() - current_function_->nlocals + idx;
        push(return_stack_[offset]);
        goto *(pc++)->label_addr;
    }
    
    L_PUSH_GLOBAL: {
        std::string* name = (pc++)->str_ptr;
        push(lookup_global(*name));
        goto *(pc++)->label_addr;
    }
    
    L_CALL_GLOBAL_COUNTED: {
        std::string* name = (pc++)->str_ptr;
        int64_t nargs = (pc++)->i64;
        (void)nargs;  // Not used yet.
        Cell func_cell = lookup_global(*name);
        FunctionObject* callee = get_function(func_cell);
        // For now, execute inline (proper call stack management needed).
        execute(callee);
        goto *(pc++)->label_addr;
    }
    
    L_SYSCALL_COUNTED: {
        std::string* name = (pc++)->str_ptr;
        int64_t nargs = (pc++)->i64;
        execute_syscall(*name, static_cast<int>(nargs));
        goto *(pc++)->label_addr;
    }
    
    L_STACK_LENGTH: {
        push(make_int(static_cast<int64_t>(operand_stack_.size())));
        goto *(pc++)->label_addr;
    }
    
    L_RETURN:
    L_HALT:
        return;
    
    #else
    throw std::runtime_error("Threaded interpreter requires GCC/Clang");
    #endif
}



} // namespace nutmeg
