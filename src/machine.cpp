#include "machine.hpp"
#include "instruction.hpp"
#include "sysfunctions.hpp"
#include "parse_function_object.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <fmt/core.h>
#include <iostream>

#define DEBUG_INSTRUCTIONS
// #define DEBUG_INSTRUCTIONS_DETAIL
#define TRACE_PLANT_INSTRUCTIONS
// #define TRACE_CODEGEN
// #define TRACE_CODEGEN_DETAILED
#define EXTRA_CHECKS
#define TRACE_EXECUTION

namespace nutmeg {

Machine::Machine()
    : pc_(0) {
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

Cell& Machine::peek() {
    if (operand_stack_.empty()) {
        throw std::runtime_error("Stack is empty");
    }
    return operand_stack_.back();
}

Cell& Machine::peek_at(size_t index) {
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
void Machine::define_global(const std::string& name, Cell value, bool lazy) {
    #ifdef TRACE_CODEGEN
    fmt::print("DEFINING global: {}\n", name);
    #endif
    auto it = globals_.find(name);
    if (it == globals_.end()) {
        // Create new global.
        globals_[name] = new Ident{value, lazy};
    } else {
        // Update existing global.
        it->second->cell = value;
        it->second->lazy = lazy;
    }
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
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("Available globals:\n");
    for (const auto& pair : globals_) {
        fmt::print("  {}\n", pair.first);
    }
    #endif
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
    #ifdef TRACE_CODEGEN
    fmt::print("execute() called\n");
    #endif

    // Display the structure of the function object for debugging.
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("Length of instructions: {}\n", as_detagged_int(func_obj[-2]));
    fmt::print("T-block length: {}\n", as_detagged_int(func_obj[-1]));
    fmt::print("FunctionDataKey: {}\n", static_cast<void*>(func_obj[0].ptr));
    fmt::print("NLocals: {}\n", heap_.get_function_nlocals(func_obj));
    fmt::print("NParams: {}\n", heap_.get_function_nparams(func_obj));
    for (int i = 0; i < as_detagged_int(func_obj[-2]); i++) {
        Cell instr = heap_.get_function_code(func_obj)[i];
        fmt::print("Instruction[{}]: label_addr={}\n", i, static_cast<void*>(instr.label_addr));
    }
    #endif

    // Create tiny launcher code.
    std::vector<Cell> launcher(3);
    launcher[0].label_addr = opcode_map_[Opcode::LAUNCH];
    launcher[1].ptr = func_obj;
    launcher[2].label_addr = opcode_map_[Opcode::HALT];

    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("About to call threaded_impl\n");
    #endif
    threaded_impl(&launcher, false);
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("Returned from threaded_impl\n");
    #endif
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

FunctionObject Machine::parse_function_object(const std::string& idname, const std::unordered_map<std::string, bool>& deps, const std::string& json_str) {
    ParseFunctionObject parser(*this, idname, deps);
    return parser.parse(json_str);
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
    #ifdef TRACE_CODEGEN
    fmt::print("threaded_impl called, init_mode={}\n", init_mode);
    #endif
    #ifdef __GNUC__
    // In init mode, just capture the labels and return.
    if (init_mode) {
        #ifdef TRACE_CODEGEN
        fmt::print("In init mode, capturing labels\n");
        #endif
        opcode_map_ = {
            {Opcode::PUSH_INT, &&L_PUSH_VALUE},
            {Opcode::PUSH_BOOL, &&L_PUSH_VALUE},
            {Opcode::PUSH_STRING, &&L_PUSH_VALUE},
            {Opcode::POP_LOCAL, &&L_POP_LOCAL},
            {Opcode::PUSH_LOCAL, &&L_PUSH_LOCAL},
            {Opcode::PUSH_GLOBAL, &&L_PUSH_GLOBAL},
            {Opcode::PUSH_GLOBAL_LAZY, &&L_PUSH_GLOBAL_LAZY},
            {Opcode::LAUNCH, &&L_LAUNCH},
            {Opcode::CALL_GLOBAL_COUNTED, &&L_CALL_GLOBAL_COUNTED},
            {Opcode::CALL_GLOBAL_COUNTED_LAZY, &&L_CALL_GLOBAL_COUNTED_LAZY},
            {Opcode::SYSCALL_COUNTED, &&L_SYSCALL_COUNTED},
            {Opcode::STACK_LENGTH, &&L_STACK_LENGTH},
            {Opcode::CHECK_BOOL, &&L_CHECK_BOOL},
            {Opcode::GOTO, &&L_GOTO},
            {Opcode::IF_NOT, &&L_IF_NOT},
            {Opcode::RETURN, &&L_RETURN},
            {Opcode::HALT, &&L_HALT},
            {Opcode::DONE, &&L_DONE},
        };
        return;
    }

    // Run mode: execute the compiled code.
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("Run mode: code->data() = {}\n", static_cast<void*>(code->data()));
    #endif
    Cell* pc = code->data();
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("pc = {}, label = {}\n", static_cast<void*>(pc), static_cast<void*>(pc->label_addr));
    #endif

    // Jump to the first instruction.
    #ifdef TRACE_CODEGEN_DETAILED
    fmt::print("About to jump\n");
    #endif
    goto *pc++->label_addr;

    L_PUSH_VALUE: {
        Cell value = *pc++;
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("PUSH_VALUE {}\n", cell_to_string(value));
        #endif
        push(value);
        goto *(pc++)->label_addr;
    }

    L_POP_LOCAL: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("POP_LOCAL\n");
        #endif
    //     #ifdef DEBUG_INSTRUCTIONS
    //     fmt::print("POP_LOCAL\n");
    //     #endif
    //     int64_t idx = (pc++)->i64;
    //     Cell value = pop();
    //     int nlocals = heap_.get_function_nlocals(current_function_);
    //     size_t offset = return_stack_.size() - nlocals + idx;
    //     return_stack_[offset] = value;
        throw std::runtime_error("POP_LOCAL not implemented yet");
        goto *(pc++)->label_addr;
    }

    L_PUSH_LOCAL: {
        int offset = (pc++)->i64;
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("PUSH_LOCAL #{}\n", offset);
        #endif
        operand_stack_.push_back(get_local_variable(offset));
        goto *(pc++)->label_addr;
    }

    L_IN_PROGRESS: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("IN_PROGRESS\n");
        #endif
        Ident* ident_ptr = static_cast<Ident*>(pc->ptr);
        if (ident_ptr->in_progress) {
            throw std::runtime_error("Recursive evaluation of top-level constants detected");
        }
        ident_ptr->in_progress = true;
        goto *(pc++)->label_addr;
    }

    L_DONE: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("DONE\n");
        #endif
        // Get the count of arguments from the local variable.
        int64_t offset = (pc++)->i64;
        uint64_t count = operand_stack_.size() - as_detagged_int(get_local_variable(offset));

        if (count != 1) {
            throw std::runtime_error("DONE instruction expects 1 argument on the stack");
        }

        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        ident_ptr->cell = peek();
        ident_ptr->in_progress = false;
        ident_ptr->lazy = false;

        // Verify the value is now a function pointer.
        heap_.must_be_function_value(ident_ptr->cell);
        
        goto *(pc++)->label_addr;
    }

    L_PUSH_GLOBAL_LAZY: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("PUSH_GLOBAL_LAZY\n");
        #endif
        Cell * self = pc - 1;
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        if (ident_ptr->lazy) {
            // This sets the PC to the first instruction of the function object.
            pc = call_function_object(pc, get_function_ptr(ident_ptr->cell), 0);
        } else {
            // Second time around it replaces the instruction with non-lazy version
            // and repeats the instruction!
            self->ptr = &&L_PUSH_GLOBAL;
            pc = self;
        }
        goto *(pc++)->label_addr;
    }

    L_PUSH_GLOBAL: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("PUSH_GLOBAL\n");
        #endif
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        push(ident_ptr->cell);
        goto *(pc++)->label_addr;
    }

    L_CALL_GLOBAL_COUNTED_LAZY: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("L_CALL_GLOBAL_COUNTED_LAZY\n");
        #endif
        Cell * self = pc - 1;
        int64_t offset = (pc++)->i64;
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        if (ident_ptr->lazy) {
            // Skip the check that any parameters are being passed. Will be zero.
            // This sets the PC to the first instruction of the function object.
            pc = call_function_object(pc, get_function_ptr(ident_ptr->cell), 0);
        } else {
            // Second time around it replaces the instruction with non-lazy version
            // and repeats the instruction!
            self->ptr = &&L_CALL_GLOBAL_COUNTED;
            pc = self;
        }
        goto *(pc++)->label_addr;
    }

    L_CALL_GLOBAL_COUNTED: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("CALL_GLOBAL_COUNTED\n");
        #endif

        // Get the count of arguments from the local variable.
        int64_t offset = (pc++)->i64;
        uint64_t count = operand_stack_.size() - as_detagged_int(get_local_variable(offset));

        // Get the Ident* pointer to the function to call.
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        Cell* func_ptr = get_function_ptr(ident_ptr->cell);

        #ifdef EXTRA_CHECKS
        if (!heap_.is_function_object(func_ptr)) {
            throw std::runtime_error("Attempt to call a non-function object");
        } else {
            fmt::print("Verified function object\n");
        }
        #endif

        // Get the number of nlocals and nparams from the function object.
        int nlocals = heap_.get_function_nlocals(func_ptr);
        int nparams = heap_.get_function_nparams(func_ptr);

        #ifdef TRACE_EXECUTION
        fmt::print("CALL_GLOBAL_COUNTED: nparams = {}, nlocals = {}, arg_count = {}\n", nparams, nlocals, count);
        #endif

        // Build stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]
        // Initialize remaining locals to nil.
        for (int i = nparams; i < nlocals; i++)
        {
            push_return(SPECIAL_NIL);
        }

        // Pop parameters from operand stack into temporary buffer.
        // Operand stack has params in reverse order (last param on top).
        std::vector<Cell> params(nparams);
        for (int i = nparams - 1; i >= 0; i--)
        {
            params[i] = pop();
            fmt::print("Popping param {} = {}\n", i, cell_to_string(params[i]));
        }
        
        // Push parameters to return stack in correct order (first param at lowest index).
        for (int i = 0; i < nparams; i++)
        {
            push_return(params[i]);
        }

        // Save func_obj pointer so RETURN can read nlocals.
        Cell func_cell;
        func_cell.ptr = func_ptr;
        push_return(func_cell);

        // Save return address on return stack (points to next instruction after operand).
        Cell return_cell;
        return_cell.ptr = pc;
        push_return(return_cell);

        // Now pass control to the called function.
        pc = heap_.get_function_code(func_ptr);

        goto *(pc++)->label_addr;
    }

    L_SYSCALL_COUNTED: {
        int64_t offset = (pc++)->i64;
        auto value = as_detagged_int(get_local_variable(offset));
        uint64_t count = operand_stack_.size() - as_detagged_int(get_local_variable(offset));
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("SYSCALL_COUNTED, offset={}, value={}, stack_size={}, count={}\n", offset, value, operand_stack_.size(), count);
        #endif
        SysFunction sys_function = reinterpret_cast<SysFunction>((pc++)->ptr);
        sys_function(*this, static_cast<int>(count));

        goto *(pc++)->label_addr;
    }

    L_STACK_LENGTH: {
        // Assign the current stack length into the local variable defined by
        // the operand, which is a raw i64.
        int64_t offset = (pc++)->i64;
        get_local_variable(offset) = make_tagged_int(static_cast<int64_t>(operand_stack_.size()));
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("STACK_LENGTH, offset = {}, size = {}\n", offset, operand_stack_.size());
        #endif

        goto *(pc++)->label_addr;
    }

    L_CHECK_BOOL: {
        // Verify that the stack has grown by exactly 1 since the "before"
        // snapshot and that the top of stack is a boolean value.
        int64_t offset = (pc++)->i64;
        int64_t before_size = as_detagged_int(get_local_variable(offset));
        int64_t current_size = static_cast<int64_t>(operand_stack_.size());
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("CHECK_BOOL, offset = {}, before = {}, current = {}\n", offset, before_size, current_size);
        #endif

        // Check that exactly one value was pushed.
        if (current_size != before_size + 1) {
            throw std::runtime_error(
                fmt::format("CHECK_BOOL failed: expected stack size {}, got {}", before_size + 1, current_size)
            );
        }

        // Check that the top of stack is a boolean.
        Cell top = peek();
        if (!is_bool(top)) {
            throw std::runtime_error(
                fmt::format("CHECK_BOOL failed: expected boolean, got {}", cell_to_string(top))
            );
        }

        goto *(pc++)->label_addr;
    }

    L_GOTO: {
        // Unconditional jump. Read the relative offset and adjust pc.
        int64_t offset = (pc++)->i64;
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("GOTO, offset = {}\n", offset);
        #endif
        
        // Apply the offset to pc. The offset is relative to the current pc position.
        pc += offset;
        
        // Jump to the instruction at the target.
        goto *pc++->label_addr;
    }

    L_IF_NOT: {
        // Conditional jump: jump if top of stack is SPECIAL_FALSE.
        int64_t offset = (pc++)->i64;
        
        // Pop the condition from the stack.
        Cell condition = pop();
        
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("IF_NOT, offset = {}, condition = {}\n", offset, cell_to_string(condition));
        #endif
        
        // Check if the condition is false.
        if (condition.u64 == SPECIAL_FALSE.u64) {
            // Condition is false - take the jump.
            pc += offset;
            #ifdef DEBUG_INSTRUCTIONS
            fmt::print("  Taking jump to offset {}\n", offset);
            #endif
        } else {
            // Condition is not false - fall through (no jump).
            #ifdef DEBUG_INSTRUCTIONS
            fmt::print("  Not taking jump, falling through\n");
            #endif
        }
        
        // Continue execution at the (possibly adjusted) pc.
        goto *pc++->label_addr;
    }

    L_RETURN: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("RETURN\n");
        #endif
        // Clean up stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]

        // Restore return address (raw).
        Cell return_cell = pop_return();

        // Pop the func_obj pointer (raw) and restore previous function context.
        Cell * func_obj = static_cast<Cell *>(pop_return().ptr);

        // Pop nlocals slots first.
        // Get nlocals from current_function_.
        int nlocals = heap_.get_function_nlocals(func_obj);
        pop_return_frame(nlocals);

        pc = static_cast<Cell*>(return_cell.ptr);

        // Continue execution at return address.
        goto *pc++->label_addr;
    }

    L_HALT: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("HALT\n");
        #endif
        return;
    }

    L_LAUNCH: {
        #ifdef DEBUG_INSTRUCTIONS
        fmt::print("LAUNCH\n");
        #endif
        pc = LaunchInstruction(pc);
        #ifdef DEBUG_INSTRUCTIONS_DETAIL
        fmt::print("&&L_STACK_LENGTH = {}, new pc = {}\n", static_cast<void*>(&&L_STACK_LENGTH), static_cast<void*>(pc));
        #endif
        goto *pc++->label_addr;
    }

    #else
    throw std::runtime_error("Threaded interpreter requires GCC/Clang");
    #endif
}

inline Cell* Machine::call_function_object(Cell* pc, Cell* func_ptr, int arg_count) {
    // Verify that it is a function object.
    if (!heap_.is_function_object(func_ptr)) {
        throw std::runtime_error("Attempt to lazily evaluate a non-function object");
    }


    // Get the number of nlocals and nparams from the function object.
    int nlocals = heap_.get_function_nlocals(func_ptr);
    int nparams = heap_.get_function_nparams(func_ptr);

    // Check the number of arguments is consistent with nparams.
    if (arg_count != nparams) {
        throw std::runtime_error(
            fmt::format("Function expected {} arguments, but got {}", nparams, arg_count));
    }

    // Build stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]
    // Initialize remaining locals to nil.
    for (int i = nparams; i < nlocals; i++)
    {
        push_return(SPECIAL_NIL);
    }

    // Pop parameters from operand stack into temporary buffer.
    // Operand stack has params in reverse order (last param on top).
    std::vector<Cell> params(nparams);
    for (int i = nparams - 1; i >= 0; i--)
    {
        params[i] = pop();
    }
    
    // Push parameters to return stack in correct order (first param at lowest index).
    for (int i = 0; i < nparams; i++)
    {
        push_return(params[i]);
    }

    // Save func_obj pointer so RETURN can read nlocals.
    Cell func_cell;
    func_cell.ptr = func_ptr;
    push_return(func_cell);

    // Save return address on return stack (points to next instruction after operand).
    Cell return_cell;
    return_cell.ptr = pc;
    push_return(return_cell);

    // Now pass control to the called function.
    pc = heap_.get_function_code(func_ptr);

    return pc;
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
    #ifdef DEBUG_INSTRUCTIONS_DETAIL
    fmt::print("Length of instructions: {}\n", as_detagged_int(func_obj[-2]));
    fmt::print("T-block length: {}\n", as_detagged_int(func_obj[-1]));
    fmt::print("FunctionDataKey: {}\n", static_cast<void*>(func_obj[0].ptr));
    fmt::print("NLocals: {}\n", heap_.get_function_nlocals(func_obj));
    fmt::print("NParams: {}\n", heap_.get_function_nparams(func_obj));
    for (int i = 0; i < as_detagged_int(func_obj[-2]); i++) {
        Cell instr = heap_.get_function_code(func_obj)[i];
        fmt::print("Instruction[{}]: label_addr={}\n", i, static_cast<void*>(instr.label_addr));
    }
    #endif

    // Get function metadata.
    int nlocals = heap_.get_function_nlocals(func_obj);
    int nparams = heap_.get_function_nparams(func_obj);

    // Build stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]

    // Initialize remaining locals to nil.
    for (int i = nparams; i < nlocals; i++)
    {
        push_return(SPECIAL_NIL);
    }

    // Pop parameters from operand stack into temporary buffer.
    // Operand stack has params in reverse order (last param on top).
    std::vector<Cell> params(nparams);
    for (int i = nparams - 1; i >= 0; i--)
    {
        params[i] = pop();
    }
    
    // Push parameters to return stack in correct order (first param at lowest index).
    for (int i = 0; i < nparams; i++)
    {
        push_return(params[i]);
    }

    // Save func_obj pointer so RETURN can read nlocals.
    Cell func_cell;
    func_cell.ptr = func_obj;
    push_return(func_cell);

    // Save return address on return stack (points to next instruction after operand).
    Cell return_cell;
    return_cell.ptr = pc;
    push_return(return_cell);


    // Set pc to function code (caller will do the goto).
    pc = heap_.get_function_code(func_obj);
    #ifdef DEBUG_INSTRUCTIONS_DETAIL
    fmt::print("LaunchInstruction: func_obj={}, returned pc={}\n", static_cast<void*>(func_obj), static_cast<void*>(pc));
    if (pc == func_obj) {
        fmt::print("ERROR: get_function_code returned func_obj itself!\n");
    }
    #endif
    return pc;
}

} // namespace nutmeg
