#include "machine.hpp"
#include "instruction.hpp"
#include <nlohmann/json.hpp>
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
    // Allocate string in heap (includes null terminator in char_count).
    size_t char_count = value.size() + 1;
    HeapCell* obj_ptr = heap_.allocate_string(value.c_str(), char_count);
    return make_ptr(obj_ptr);
}

const char* Machine::get_string(Cell cell) {
    if (!is_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    HeapCell* obj_ptr = static_cast<HeapCell*>(as_ptr(cell));
    return heap_.get_string_data(obj_ptr);
}

Cell Machine::allocate_function(const std::vector<InstructionWord>& code, int nlocals, int nparams) {
    // Allocate function in heap.
    HeapCell* obj_ptr = heap_.allocate_function(code.size(), nlocals, nparams);
    
    // Copy instruction words into the heap.
    HeapCell* code_ptr = heap_.get_function_code(obj_ptr);
    for (size_t i = 0; i < code.size(); i++) {
        code_ptr[i].u64 = code[i].u64;
    }
    
    return make_ptr(obj_ptr);
}

HeapCell* Machine::get_function_ptr(Cell cell) {
    if (!is_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    return static_cast<HeapCell*>(as_ptr(cell));
}

// Execution using threaded interpreter.
void Machine::execute(HeapCell* func_ptr) {
    current_function_ = func_ptr;
    
    int nlocals = heap_.get_function_nlocals(func_ptr);
    
    // Reserve space for local variables on the return stack.
    for (int i = 0; i < nlocals; i++) {
        push_return(make_nil());
    }
    
    // Get the instruction code and create a temporary vector for threaded_impl.
    HeapCell* code_ptr = heap_.get_function_code(func_ptr);
    // We need to determine the code length - for now, scan until we hit HALT.
    // This is a bit hacky but works since we always append HALT.
    size_t code_length = 0;
    while (true) {
        void* label = code_ptr[code_length].ptr;
        code_length++;
        if (label == opcode_map_[Opcode::HALT]) {
            break;
        }
        // Skip operands based on instruction type (simplified for now).
        // This is imperfect but will work for our current instructions.
    }
    
    // Create a temporary vector view of the code.
    std::vector<InstructionWord> code_view(code_length);
    for (size_t i = 0; i < code_length; i++) {
        code_view[i].u64 = code_ptr[i].u64;
    }
    
    // Execute the pre-compiled threaded code.
    threaded_impl(&code_view, false);
    
    // Clean up local variables.
    for (int i = 0; i < nlocals; i++) {
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
            // Get string data from heap.
            HeapCell* obj_ptr = static_cast<HeapCell*>(as_ptr(value));
            const char* str = heap_.get_string_data(obj_ptr);
            fmt::print("{}\n", str);
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

FunctionObject Machine::parse_function_object(const std::string& json_str) {
    try {
        nlohmann::json j = nlohmann::json::parse(json_str);

        FunctionObject func;
        func.nlocals = j.at("nlocals").get<int>();
        func.nparams = j.at("nparams").get<int>();

        // Compile instructions to threaded code.
        for (const auto& inst_json : j.at("instructions")) {
            Instruction inst;
            inst.type = inst_json.at("type").get<std::string>();
            inst.opcode = string_to_opcode(inst.type);

            // Optional fields.
            if (inst_json.contains("index")) {
                inst.index = inst_json.at("index").get<int>();
            }
            if (inst_json.contains("value")) {
                inst.value = inst_json.at("value").get<std::string>();
            }
            if (inst_json.contains("name")) {
                inst.name = inst_json.at("name").get<std::string>();
            }
            if (inst_json.contains("nargs")) {
                inst.nargs = inst_json.at("nargs").get<int>();
            }

            // Compile to threaded code: emit label address followed by operands.
            InstructionWord label_word;
            label_word.label_addr = opcode_map_.at(inst.opcode);
            func.code.push_back(label_word);
            
            // Add immediate operands based on instruction type.
            switch (inst.opcode) {
            case Opcode::PUSH_INT:
            case Opcode::POP_LOCAL:
            case Opcode::PUSH_LOCAL: {
                InstructionWord operand;
                operand.i64 = inst.index.value_or(0);
                func.code.push_back(operand);
                break;
            }
            
            case Opcode::PUSH_STRING: {
                // Allocate string in heap and store the Cell pointer.
                std::string str_value = inst.value.value();
                Cell str_cell = allocate_string(str_value);
                InstructionWord operand;
                operand.u64 = str_cell;
                func.code.push_back(operand);
                break;
            }
            
            case Opcode::PUSH_GLOBAL: {
                // Store pointer to global name string (kept in static storage).
                InstructionWord operand;
                static thread_local std::vector<std::string> string_storage;
                string_storage.push_back(inst.value.value());
                operand.str_ptr = &string_storage.back();
                func.code.push_back(operand);
                break;
            }
            
            case Opcode::SYSCALL_COUNTED:
            case Opcode::CALL_GLOBAL_COUNTED: {
                InstructionWord name_word, nargs_word;
                static thread_local std::vector<std::string> string_storage;
                string_storage.push_back(inst.name.value());
                name_word.str_ptr = &string_storage.back();
                nargs_word.i64 = inst.nargs.value_or(0);
                func.code.push_back(name_word);
                func.code.push_back(nargs_word);
                break;
            }
            
            case Opcode::STACK_LENGTH:
            case Opcode::RETURN:
            case Opcode::HALT:
                // No operands.
                break;
            }
        }
        
        // Add HALT at the end.
        InstructionWord halt_word;
        halt_word.label_addr = opcode_map_.at(Opcode::HALT);
        func.code.push_back(halt_word);

        return func;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(fmt::format("JSON parsing error: {}", e.what()));
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
        Cell str_cell = (pc++)->u64;
        push(str_cell);
        goto *(pc++)->label_addr;
    }
    
    L_POP_LOCAL: {
        int64_t idx = (pc++)->i64;
        Cell value = pop();
        int nlocals = heap_.get_function_nlocals(current_function_);
        size_t offset = return_stack_.size() - nlocals + idx;
        return_stack_[offset] = value;
        goto *(pc++)->label_addr;
    }
    
    L_PUSH_LOCAL: {
        int64_t idx = (pc++)->i64;
        int nlocals = heap_.get_function_nlocals(current_function_);
        size_t offset = return_stack_.size() - nlocals + idx;
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
        HeapCell* callee = get_function_ptr(func_cell);
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
