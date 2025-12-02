#include "machine.hpp"
#include "instruction.hpp"
#include "sysfunctions.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <fmt/core.h>
#include <iostream>

namespace nutmeg {

Machine::Machine()
    : current_function_(nullptr), pc_(0) {
    // Initialize the threaded interpreter by capturing label addresses.
    #ifdef __GNUC__
    threaded_impl(static_cast<std::vector<Cell>*>(nullptr), true);
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

void Machine::pop_multiple(size_t count) {
    if (operand_stack_.size() < count) {
        throw std::runtime_error("Stack underflow");
    }
    operand_stack_.resize(operand_stack_.size() - count);
}

Cell Machine::peek() const {
    if (operand_stack_.empty()) {
        throw std::runtime_error("Stack is empty");
    }
    return operand_stack_.back();
}

Cell Machine::peek_at(size_t index) const {
    if (index >= operand_stack_.size()) {
        throw std::runtime_error("Stack index out of bounds");
    }
    return operand_stack_[index];
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

Cell& Machine::get_return_address() {
    return return_stack_[return_stack_.size() -1];
}

Cell& Machine::get_frame_function_object() {
    return return_stack_[return_stack_.size() -2];
}

Cell& Machine::get_local_variable(int offset) {
    // Note that the -3 additional offset is rolled into the supplied offset
    // by the loader.
    return return_stack_[return_stack_.size() - offset];
}

// Global dictionary operations.
void Machine::define_global(const std::string& name, Cell value) {
    globals_[name] = new Ident{value};
}

Cell Machine::lookup_global(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) {
        throw std::runtime_error(fmt::format("Undefined global: {}", name));
    }
    return it->second->cell;
}

bool Machine::has_global(const std::string& name) const {
    return globals_.find(name) != globals_.end();
}

Cell* Machine::get_global_cell_ptr(const std::string& name) {
    auto it = globals_.find(name);
    if (it == globals_.end()) {
        throw std::runtime_error(fmt::format("Undefined global: {}", name));
    }
    // The cell contains a tagged pointer - detag it to get the actual function pointer.
    return static_cast<Cell*>(as_detagged_ptr(it->second->cell));
}

Ident * Machine::lookup_ident(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) {
        return nullptr;
    }
    return it->second;
}

// Heap allocation.
Cell Machine::allocate_string(const std::string& value) {
    // Allocate string in heap (includes null terminator in char_count).
    size_t char_count = value.size() + 1;
    Cell* obj_ptr = heap_.allocate_string(value.c_str(), char_count);
    return make_tagged_ptr(obj_ptr);
}

const char* Machine::get_string(Cell cell) {
    if (!is_tagged_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    Cell* obj_ptr = static_cast<Cell*>(as_detagged_ptr(cell));
    return heap_.get_string_data(obj_ptr);
}

Cell* Machine::allocate_function(const std::vector<Cell>& code, int nlocals, int nparams) {
    // Allocate function in heap.
    Cell* obj_ptr = heap_.allocate_function(code.size(), nlocals, nparams);
    
    // Copy instruction words into the heap.
    Cell* code_ptr = heap_.get_function_code(obj_ptr);
    for (size_t i = 0; i < code.size(); i++) {
        code_ptr[i] = code[i];
        // fmt::print("allocate_function: code[{}] = {}\n", i, static_cast<void*>(code[i].label_addr));
    }
    
    return obj_ptr;
}

Cell* Machine::get_function_ptr(Cell cell) {
    if (!is_tagged_ptr(cell)) {
        throw std::runtime_error("Cell is not a pointer");
    }
    return static_cast<Cell*>(as_detagged_ptr(cell));
}

// Execution entry point - only used for initial launch from main and tests.
// Creates a minimal launcher: LAUNCH HALT.
void Machine::execute(Cell* func_obj) {
    fmt::print("execute() called\n");

    // Display the structure of the function object for debugging.
    fmt::print("Length of instructions: {}\n", as_detagged_int(func_obj[-2]));
    fmt::print("T-block length: {}\n", as_detagged_int(func_obj[-1]));
    fmt::print("FunctionDataKey: {}\n", static_cast<void*>(func_obj[0].ptr));
    fmt::print("NLocals: {}\n", heap_.get_function_nlocals(func_obj));
    fmt::print("NParams: {}\n", heap_.get_function_nparams(func_obj));
    for (int i = 0; i < as_detagged_int(func_obj[-2]); i++) {
        Cell instr = heap_.get_function_code(func_obj)[i];
        fmt::print("Instruction[{}]: label_addr={}\n", i, static_cast<void*>(instr.label_addr));
    }
    
    // Create tiny launcher code.
    std::vector<Cell> launcher(3);
    launcher[0].label_addr = opcode_map_[Opcode::LAUNCH];
    launcher[1].ptr = func_obj;
    launcher[2].label_addr = opcode_map_[Opcode::HALT];
    
    fmt::print("About to call threaded_impl\n");
    threaded_impl(&launcher, false);
    fmt::print("Returned from threaded_impl\n");
}

void Machine::execute_syscall(const std::string& name, int nargs) {
    if (name == "println") {
        // Pop the value to print from the stack.
        if (operand_stack_.empty()) {
            throw std::runtime_error("println: stack underflow");
        }
        Cell value = pop();
        
        // Print based on type.
        if (is_tagged_int(value)) {
            fmt::print("{}\n", as_detagged_int(value));
        } else if (is_tagged_ptr(value)) {
            // Get string data from heap.
            Cell* obj_ptr = static_cast<Cell*>(as_detagged_ptr(value));
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
            fmt::print("Parsing instruction: {} of type {}\n", inst.type, static_cast<int>(inst.opcode));

            // Optional fields.
            if (inst_json.contains("index")) {
                inst.index = inst_json.at("index").get<int>();
                fmt::print("  Found index field: {}\n", inst.index.value());
                fmt::print("  index.hasvalue() = {}\n", inst.index.has_value());
            }
            if (inst_json.contains("value")) {
                inst.value = inst_json.at("value").get<std::string>();
            }
            if (inst_json.contains("name")) {
                inst.name = inst_json.at("name").get<std::string>();
            }

            // Compile to threaded code: emit label address followed by operands.
            Cell label_word;
            label_word.label_addr = opcode_map_.at(inst.opcode);
            func.code.push_back(label_word);
            fmt::print("Compiling instruction: {} at label {}\n", inst.type, static_cast<void*>(label_word.label_addr));
            
            // Add immediate operands based on instruction type.
            switch (inst.opcode) {
            case Opcode::PUSH_INT:
            case Opcode::POP_LOCAL:
            case Opcode::PUSH_LOCAL: {
                Cell operand;
                operand.i64 = inst.index.value_or(0);
                func.code.push_back(operand);
                break;
            }
            
            case Opcode::PUSH_STRING: {
                // Allocate string in heap and store the Cell.
                std::string str_value = inst.value.value();
                Cell str_cell = allocate_string(str_value);
                func.code.push_back(str_cell);
                break;
            }
            
            case Opcode::PUSH_GLOBAL: {
                // Store pointer to global name string (kept in static storage).
                Cell operand;
                static thread_local std::vector<std::string> string_storage;
                string_storage.push_back(inst.value.value());
                operand.str_ptr = &string_storage.back();
                func.code.push_back(operand);
                break;
            }
            
            case Opcode::SYSCALL_COUNTED: {
                // SYSCALL has two arguments: 
                // * index = the local variable index to get the previous stack length from, and 
                // * name = the name of the syscall.
                if (!inst.index.has_value()) {
                    throw std::runtime_error("SYSCALL_COUNTED requires an index field");
                }
                if (!inst.name.has_value()) {
                    throw std::runtime_error("SYSCALL_COUNTED requires a name field");
                }
                fmt::print("  SYSCALL_COUNTED compiling with index={} name={}\n", inst.index.value(), inst.name.value());
                // L_SYSCALL_COUNTED requires two operands: the index and the sys-function pointer.
                Cell index_operand = make_raw_i64(inst.index.value());
                func.code.push_back(index_operand);
                
                // Look up sys-function in the table.
                auto it = sysfunctions_table.find(inst.name.value());
                if (it == sysfunctions_table.end()) {
                    throw std::runtime_error(fmt::format("Unknown sys-function: {}", inst.name.value()));
                }
                SysFunction sys_function = it->second;
                Cell func_operand;
                func_operand.ptr = reinterpret_cast<void*>(sys_function);
                func.code.push_back(func_operand);
                break;
            }

            
            case Opcode::STACK_LENGTH: {
                // We will assign the current stack length into the local
                // variable defined by index.
                if (!inst.index.has_value()) {
                    throw std::runtime_error("STACK_LENGTH requires an index field");
                }
                fmt::print("  STACK_LENGTH compiling with index={}\n", inst.index.value());
                int offset = inst.index.value() + 3; // +3 for return address and func_obj and 0-based.
                Cell c = make_raw_i64(offset);
                func.code.push_back(c);
                break;
            }

            case Opcode::RETURN:
            case Opcode::HALT:
                // No operands.
                break;
            }
        }
        
        // Add HALT at the end.
        Cell halt_word;
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
void Machine::threaded_impl(std::vector<Cell>* code, bool init_mode) {
    fmt::print("threaded_impl called, init_mode={}\n", init_mode);
    #ifdef __GNUC__
    // In init mode, just capture the labels and return.
    if (init_mode) {
        fmt::print("In init mode, capturing labels\n");
        opcode_map_ = {
            {Opcode::PUSH_INT, &&L_PUSH_INT},
            {Opcode::PUSH_STRING, &&L_PUSH_STRING},
            {Opcode::POP_LOCAL, &&L_POP_LOCAL},
            {Opcode::PUSH_LOCAL, &&L_PUSH_LOCAL},
            {Opcode::PUSH_GLOBAL, &&L_PUSH_GLOBAL},
            {Opcode::LAUNCH, &&L_LAUNCH},
            {Opcode::CALL_GLOBAL_COUNTED, &&L_CALL_GLOBAL_COUNTED},
            {Opcode::SYSCALL_COUNTED, &&L_SYSCALL_COUNTED},
            {Opcode::STACK_LENGTH, &&L_STACK_LENGTH},
            {Opcode::RETURN, &&L_RETURN},
            {Opcode::HALT, &&L_HALT},
        };
        return;
    }
    
    // Run mode: execute the compiled code.
    fmt::print("Run mode: code->data() = {}\n", static_cast<void*>(code->data()));
    Cell* pc = code->data();
    fmt::print("pc = {}, label = {}\n", static_cast<void*>(pc), static_cast<void*>(pc->label_addr));
    
    // Jump to the first instruction.
    fmt::print("About to jump\n");
    goto *pc++->label_addr;
    
    L_PUSH_INT: {
        int64_t value = (pc++)->i64;
        push(make_tagged_int(value));
        goto *(pc++)->label_addr;
    }
    
    L_PUSH_STRING: {
        fmt::print("PUSH_STRING\n");
        Cell str_cell = *(pc++);
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

    L_LAUNCH: {
        fmt::print("LAUNCH\n");
        pc = LaunchInstruction(pc);
        fmt::print("&&L_STACK_LENGTH = {}, new pc = {}\n", static_cast<void*>(&&L_STACK_LENGTH), static_cast<void*>(pc));
        goto *pc++->label_addr;
    }

    L_CALL_GLOBAL_COUNTED: {
        fmt::print("CALL_GLOBAL_COUNTED\n");
        // TODO
        goto *(pc++)->label_addr;
    }

    L_SYSCALL_COUNTED: {
        fmt::print("SYSCALL_COUNTED\n");
        int64_t offset = (pc++)->i64;
        uint64_t count = operand_stack_.size() - get_local_variable(offset).u64;
        SysFunction sys_function = reinterpret_cast<SysFunction>((pc++)->ptr);
        sys_function(*this, static_cast<int>(count));
        
        goto *(pc++)->label_addr;
    }
    
    L_STACK_LENGTH: {
        // Assign the current stack length into the local variable defined by 
        // the operand, which is a raw i64.
        fmt::print("STACK_LENGTH\n");
        int64_t offset = (pc++)->i64;
        get_local_variable(offset) = make_tagged_int(static_cast<int64_t>(operand_stack_.size()));
        
        goto *(pc++)->label_addr;
    }
    
    L_RETURN: {
        fmt::print("RETURN\n");
        // Clean up stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]
        // Pop nlocals slots first.
        // Get nlocals from current_function_.
        int nlocals = heap_.get_function_nlocals(current_function_);
        for (int i = 0; i < nlocals; i++) {
            pop_return();
        }
        
        // Pop the func_obj pointer (raw) and restore previous function context.
        Cell func_obj_cell = pop_return();
        current_function_ = static_cast<Cell*>(func_obj_cell.ptr);
        
        // Restore return address (raw).
        Cell return_cell = pop_return();
        pc = static_cast<Cell*>(return_cell.ptr);
        
        // Continue execution at return address.
        goto *pc++->label_addr;
    }
    
    L_HALT:
        fmt::print("HALT\n");
        return;
    
    #else
    throw std::runtime_error("Threaded interpreter requires GCC/Clang");
    #endif
}


/**
 * LaunchInstruction sets up the initial call to the program entry point.
 * This is ONLY called once at program startup from execute(), not for regular function calls.
 * It unconditionally creates a dummy frame at the bottom of the call stack.
 */
Cell * Machine::LaunchInstruction(Cell *pc)
{
    // Read operand: func_obj pointer.
    Cell *func_obj = static_cast<Cell *>((pc++)->ptr);

    // Display the structure of the function object for debugging.
    fmt::print("Length of instructions: {}\n", as_detagged_int(func_obj[-2]));
    fmt::print("T-block length: {}\n", as_detagged_int(func_obj[-1]));
    fmt::print("FunctionDataKey: {}\n", static_cast<void*>(func_obj[0].ptr));  
    fmt::print("NLocals: {}\n", heap_.get_function_nlocals(func_obj));
    fmt::print("NParams: {}\n", heap_.get_function_nparams(func_obj));
    for (int i = 0; i < as_detagged_int(func_obj[-2]); i++) {
        Cell instr = heap_.get_function_code(func_obj)[i];
        fmt::print("Instruction[{}]: label_addr={}\n", i, static_cast<void*>(instr.label_addr));
    }
    
    // Set current function context.
    current_function_ = func_obj;

    // Get function metadata.
    int nlocals = heap_.get_function_nlocals(func_obj);
    int nparams = heap_.get_function_nparams(func_obj);

    // Build stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]
    // This allows RETURN to unwind without knowing the function context.
    
    // For the initial launch, create a dummy frame at the bottom of the call stack.
    // These nullptrs will never be used because HALT stops execution before RETURN.
    Cell dummy_return;
    dummy_return.ptr = nullptr;
    push_return(dummy_return);  // Dummy return address.
    
    Cell dummy_func;
    dummy_func.ptr = nullptr;
    push_return(dummy_func);  // Dummy func_obj.

    // Save return address on return stack (points to next instruction after operand).
    Cell return_cell;
    return_cell.ptr = pc;
    push_return(return_cell);

    // Save func_obj pointer so RETURN can read nlocals.
    Cell func_cell;
    func_cell.ptr = func_obj;
    push_return(func_cell);

    // Initialize remaining locals to nil.
    for (int i = nparams; i < nlocals; i++)
    {
        push_return(make_nil());
    }

    // Pop parameters from operand stack and push to return stack.
    // Operand stack has params in reverse order, so popping gives us the right order.
    for (int i = 0; i < nparams; i++)
    {
        push_return(pop());
    }


    // Set pc to function code (caller will do the goto).
    pc = heap_.get_function_code(func_obj);
    std::cerr << "LaunchInstruction: func_obj=" << func_obj << ", returned pc=" << pc << std::endl;
    if (pc == func_obj) {
        std::cerr << "ERROR: get_function_code returned func_obj itself!" << std::endl;
    }
    return pc;
}

} // namespace nutmeg
