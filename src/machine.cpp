#include "machine.hpp"
#include "event_log.hpp"
#include "instruction.hpp"
#include "sysfunctions.hpp"
#include "parse_function_object.hpp"
#include "trace.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <fmt/core.h>
#include <iostream>

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
bool Machine::empty() const {
    return operand_stack_.empty();
}

size_t Machine::stack_size() const {
    return operand_stack_.size();
}

Cell& Machine::get_local_variable(int offset) {
    // Note that the -3 additional offset is rolled into the supplied offset
    // by the loader.
    return return_stack_.offset_from_top(offset - 1);
}

// Global dictionary operations.
void Machine::define_global(const std::string& name, Cell value, bool lazy) {
    if constexpr (TRACE_CODEGEN) {
        fmt::print("DEFINING global: {}\n", name);
    }
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
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("Available globals:\n");
        for (const auto& pair : globals_) {
            fmt::print("  {}\n", pair.first);
        }
    }
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
    if constexpr (TRACE_CODEGEN) {
        fmt::print("execute() called\n");
    }

    // Display the structure of the function object for debugging.
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("Length of instructions: {}\n", as_detagged_int(func_obj[-2]));
        fmt::print("T-block length: {}\n", as_detagged_int(func_obj[-1]));
        fmt::print("FunctionDataKey: {}\n", static_cast<void*>(func_obj[0].ptr));
        fmt::print("NLocals: {}\n", heap_.get_function_nlocals(func_obj));
        fmt::print("NParams: {}\n", heap_.get_function_nparams(func_obj));
        for (int i = 0; i < as_detagged_int(func_obj[-2]); i++) {
            Cell instr = heap_.get_function_code(func_obj)[i];
            fmt::print("Instruction[{}]: label_addr={}\n", i, static_cast<void*>(instr.label_addr));
        }
    }

    // Create tiny launcher code.
    std::vector<Cell> launcher(3);
    launcher[0].label_addr = opcode_map_[Opcode::LAUNCH];
    launcher[1].ptr = func_obj;
    launcher[2].label_addr = opcode_map_[Opcode::HALT];

    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("About to call threaded_impl\n");
    }
    if constexpr (ENABLE_EVENTS_LOG) {
        MachineNames names(*this);
        LOG_EVENT("launch", ev_str("name", names.function_name(func_obj).value_or("")),
                  ev_ptr("address", func_obj));
    }
    threaded_impl(&launcher, false);
    // Reached only if the program halts normally; after an exception the log just ends.
    LOG_EVENT("halt");
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("Returned from threaded_impl\n");
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
    nlohmann::json j = nlohmann::json::parse(json_str);
    return parser.parse(json_str);
}



// Combined init/run function for threaded interpreter (like Poppy's init_or_run).
//
int Machine::log_frame_nlocals() {
    // The frame is [return address][function object][locals...], with the return address on top.
    if (return_stack_.size() < 2) {
        return -1;
    }
    Cell* func_obj = static_cast<Cell*>(get_frame_function_object().ptr);
    if (!heap_.get_pool()->contains(func_obj)) {
        return -1;
    }
    return heap_.get_function_nlocals(func_obj);
}

// Reverse lookups for the instruction log. Where several names are bound to the same function
// the smallest is used, so that the output does not depend on the dictionary's iteration order.
static void keep_smaller(std::optional<std::string>& best, const std::string& name) {
    if (!best || name < *best) {
        best = name;
    }
}

std::optional<GlobalInfo> MachineNames::global_at(const void* ident) const {
    // Compare addresses only: an operand is not dereferenced until it is known to be an Ident.
    for (const auto& [name, candidate] : machine_.globals_) {
        if (candidate == ident) {
            bool is_function = false;
            if (!candidate->lazy && is_tagged_ptr(candidate->cell)) {
                Cell* obj = static_cast<Cell*>(as_detagged_ptr(candidate->cell));
                is_function = machine_.heap_.get_pool()->contains(obj) && machine_.heap_.is_function_object(obj);
            }
            return GlobalInfo{name, is_function};
        }
    }
    return std::nullopt;
}

std::optional<std::string> MachineNames::function_name(const Cell* function_object) const {
    std::optional<std::string> best;
    for (const auto& [name, candidate] : machine_.globals_) {
        if (!candidate->lazy && is_tagged_ptr(candidate->cell) &&
            as_detagged_ptr(candidate->cell) == function_object) {
            keep_smaller(best, name);
        }
    }
    return best;
}

std::optional<std::string> MachineNames::sys_function_name(const void* function) const {
    std::optional<std::string> best;
    for (const auto& [name, candidate] : sysfunctions_table) {
        if (reinterpret_cast<const void*>(candidate) == function) {
            keep_smaller(best, name);
        }
    }
    return best;
}

// Record entry to / exit from an instruction in the instruction log, with the
// operand stack length at that point. LOG_INSTRUCTION_ENTRY also takes the kinds of
// the operands inlined after the label word (OP_RAW, OP_LOCAL, OP_TAGGED, OP_GLOBAL, ... in order,
// none for an instruction without operands) and, as it must be the first statement
// of a handler, reads them from the local variable pc. These are macros, not functions, so that a
// build with ENABLE_INSTRUCTION_LOG off contains no trace of them: even an empty
// inline function leaves a call, or at least spilled arguments, in an unoptimised
// build, and this is on the path executed for every instruction.
#define LOG_INSTRUCTION_ENTRY(NAME, ...)                                    \
    do {                                                                    \
        if constexpr (ENABLE_INSTRUCTION_LOG) {                             \
            MachineNames log_names(*this);                                  \
            instruction_log_.log_entry(NAME, pc, {__VA_ARGS__},             \
                OpArgContext{heap_, log_frame_nlocals(), log_names},        \
                operand_stack_.size());                                     \
        }                                                                   \
    } while (0)
#define LOG_INSTRUCTION_EXIT()                                              \
    do {                                                                    \
        if constexpr (ENABLE_INSTRUCTION_LOG) {                             \
            instruction_log_.log_exit(operand_stack_.size());               \
        }                                                                   \
    } while (0)

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
    if constexpr (TRACE_CODEGEN) {
        fmt::print("threaded_impl called, init_mode={}\n", init_mode);
    }
    #ifdef __GNUC__
    // In init mode, just capture the labels and return.
    if (init_mode) {
        if constexpr (TRACE_CODEGEN) {
            fmt::print("In init mode, capturing labels\n");
        }
        this->opcode_map_ = {
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
            {Opcode::CHECK_COUNT_IS_1, &&L_CHECK_COUNT_IS_1},
            {Opcode::GOTO, &&L_GOTO},
            {Opcode::IF_NOT, &&L_IF_NOT},
            {Opcode::RETURN, &&L_RETURN},
            {Opcode::HALT, &&L_HALT},
            {Opcode::DONE, &&L_DONE},
        };
        return;
    }

    // Run mode: execute the compiled code.
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("Run mode: code->data() = {}\n", static_cast<void*>(code->data()));
    }
    Cell* pc = code->data();
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("pc = {}, label = {}\n", static_cast<void*>(pc), static_cast<void*>(pc->label_addr));
    }

    // Jump to the first instruction.
    if constexpr (TRACE_CODEGEN_DETAILED) {
        fmt::print("About to jump\n");
    }
    goto *pc++->label_addr;

    L_PUSH_VALUE: {
        LOG_INSTRUCTION_ENTRY("PUSH_VALUE", OP_TAGGED);
        Cell value = *pc++;
        push(value);
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_POP_LOCAL: {
        LOG_INSTRUCTION_ENTRY("POP_LOCAL", OP_LOCAL);
        int offset = (pc++)->i64;
        get_local_variable(offset) = operand_stack_.pop();
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_PUSH_LOCAL: {
        LOG_INSTRUCTION_ENTRY("PUSH_LOCAL", OP_LOCAL);
        int offset = (pc++)->i64;
        push(get_local_variable(offset));
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_DONE: {
        LOG_INSTRUCTION_ENTRY("DONE", OP_LOCAL, OP_GLOBAL);
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

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_PUSH_GLOBAL_LAZY: {
        LOG_INSTRUCTION_ENTRY("PUSH_GLOBAL_LAZY", OP_GLOBAL);
        Cell * self = pc - 1;
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        if (ident_ptr->lazy) {
            // A thunk that (indirectly) refers to its own constant would never finish.
            if (ident_ptr->in_progress) {
                throw std::runtime_error("Recursive evaluation of top-level constants detected");
            }
            ident_ptr->in_progress = true;
            // This sets the PC to the first instruction of the function object.
            pc = call_function_object(pc, get_function_ptr(ident_ptr->cell), 0);
        } else {
            // Second time around it replaces the instruction with non-lazy version
            // and repeats the instruction!
            self->ptr = &&L_PUSH_GLOBAL;
            pc = self;
        }
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_PUSH_GLOBAL: {
        LOG_INSTRUCTION_ENTRY("PUSH_GLOBAL", OP_GLOBAL);
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        push(ident_ptr->cell);
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_CALL_GLOBAL_COUNTED_LAZY: {
        LOG_INSTRUCTION_ENTRY("CALL_GLOBAL_COUNTED_LAZY", OP_LOCAL, OP_GLOBAL);
        Cell * self = pc - 1;
        int64_t offset = (pc++)->i64;
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        if (ident_ptr->lazy) {
            // A thunk that (indirectly) refers to its own constant would never finish.
            if (ident_ptr->in_progress) {
                throw std::runtime_error("Recursive evaluation of top-level constants detected");
            }
            ident_ptr->in_progress = true;
            // Skip the check that any parameters are being passed. Will be zero.
            // This sets the PC to the first instruction of the function object.
            pc = call_function_object(pc, get_function_ptr(ident_ptr->cell), 0);
        } else {
            // Second time around it replaces the instruction with non-lazy version
            // and repeats the instruction!
            self->ptr = &&L_CALL_GLOBAL_COUNTED;
            pc = self;
        }
        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_CALL_GLOBAL_COUNTED: {
        LOG_INSTRUCTION_ENTRY("CALL_GLOBAL_COUNTED", OP_LOCAL, OP_GLOBAL);

        // Get the count of arguments from the local variable.
        int64_t offset = (pc++)->i64;
        uint64_t count = operand_stack_.size() - as_detagged_int(get_local_variable(offset));

        // Get the Ident* pointer to the function to call.
        Ident* ident_ptr = static_cast<Ident*>((pc++)->ptr);
        Cell* func_ptr = get_function_ptr(ident_ptr->cell);

        if constexpr (EXTRA_CHECKS) {
            if (!heap_.is_function_object(func_ptr)) {
                throw std::runtime_error("Attempt to call a non-function object");
            } else {
                fmt::print("Verified function object\n");
            }
        }

        // Get the number of nlocals and nparams from the function object.
        auto [nextras, nparams] = heap_.get_function_extras_and_params(func_ptr);

        if constexpr (TRACE_EXECUTION) {
            fmt::print("CALL_GLOBAL_COUNTED: nparams = {}, nlocals = {}, arg_count = {}\n", nparams, nextras + nparams, count);
        }

        // Build stack frame: [return_address][func_obj][local_nlocals-1]...[local_0]

        operand_stack_.move_multiple(count, return_stack_);

        // Initialize remaining locals to nil.
        return_stack_.push_multiple(SPECIAL_NIL, nextras);

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

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_SYSCALL_COUNTED: {
        LOG_INSTRUCTION_ENTRY("SYSCALL_COUNTED", OP_LOCAL, OP_SYSCALL);
        int64_t offset = (pc++)->i64;
        auto value = as_detagged_int(get_local_variable(offset));
        uint64_t count = operand_stack_.size() - as_detagged_int(get_local_variable(offset));
        SysFunction sys_function = reinterpret_cast<SysFunction>((pc++)->ptr);
        sys_function(*this, static_cast<int>(count));

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_STACK_LENGTH: {
        LOG_INSTRUCTION_ENTRY("STACK_LENGTH", OP_LOCAL);
        // Assign the current stack length into the local variable defined by
        // the operand, which is a raw i64.
        int64_t offset = (pc++)->i64;
        get_local_variable(offset) = make_tagged_int(static_cast<int64_t>(operand_stack_.size()));

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_CHECK_BOOL: {
        LOG_INSTRUCTION_ENTRY("CHECK_BOOL", OP_LOCAL);
        // Verify that the stack has grown by exactly 1 since the "before"
        // snapshot and that the top of stack is a boolean value.
        int64_t offset = (pc++)->i64;
        int64_t before_size = as_detagged_int(get_local_variable(offset));
        int64_t current_size = static_cast<int64_t>(operand_stack_.size());

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

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_CHECK_COUNT_IS_1: {
        LOG_INSTRUCTION_ENTRY("CHECK_COUNT_IS_1", OP_LOCAL);
        // Verify that the stack has grown by exactly 1 since the "before"
        // snapshot.
        int64_t offset = (pc++)->i64;
        int64_t before_size = as_detagged_int(get_local_variable(offset));
        int64_t current_size = static_cast<int64_t>(operand_stack_.size());

        // Check that exactly one value was pushed.
        if (current_size != before_size + 1) {
            throw std::runtime_error(
                fmt::format("CHECK_COUNT_IS_1 failed: expected stack size {}, got {}", before_size + 1, current_size)
            );
        }

        LOG_INSTRUCTION_EXIT();
        goto *(pc++)->label_addr;
    }

    L_GOTO: {
        LOG_INSTRUCTION_ENTRY("GOTO", OP_RAW);
        // Unconditional jump. Read the relative offset and adjust pc.
        int64_t offset = (pc++)->i64;

        // Apply the offset to pc. The offset is relative to the current pc position.
        pc += offset;

        // Jump to the instruction at the target.
        LOG_INSTRUCTION_EXIT();
        goto *pc++->label_addr;
    }

    L_IF_NOT: {
        LOG_INSTRUCTION_ENTRY("IF_NOT", OP_RAW);
        // Conditional jump: jump if top of stack is SPECIAL_FALSE.
        int64_t offset = (pc++)->i64;

        // Pop the condition from the stack.
        Cell condition = pop();

        // Check if the condition is false.
        if (condition.u64 == SPECIAL_FALSE.u64) {
            // Condition is false - take the jump.
            pc += offset;
        }

        // Continue execution at the (possibly adjusted) pc.
        LOG_INSTRUCTION_EXIT();
        goto *pc++->label_addr;
    }

    L_RETURN: {
        LOG_INSTRUCTION_ENTRY("RETURN");
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
        LOG_INSTRUCTION_EXIT();
        goto *pc++->label_addr;
    }

    L_HALT: {
        LOG_INSTRUCTION_ENTRY("HALT");
        LOG_INSTRUCTION_EXIT();
        return;
    }

    L_LAUNCH: {
        LOG_INSTRUCTION_ENTRY("LAUNCH", OP_FUNCTION);
        pc = LaunchInstruction(pc);
        LOG_INSTRUCTION_EXIT();
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
    auto [nextras, nparams] = heap_.get_function_extras_and_params(func_ptr);

    // Check the number of arguments is consistent with nparams.
    if (arg_count != nparams) {
        throw std::runtime_error(
            fmt::format("Function expected {} arguments, but got {}", nparams, arg_count));
    }

    // Move n parameters from operand stack to return stack. This means that the
    // first parameter (#0) is lowest on the return stack, which means we have to
    // adjust the offset accordingly.
    operand_stack_.move_multiple(nparams, return_stack_);

    // Build stack frame: [return_address][func_obj][local_0]...[local_nlocals-1]
    // Initialize remaining locals to nil.
    return_stack_.push_multiple(SPECIAL_NIL, nextras);

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

    // Get function metadata.
    auto [nextras, nparams] = heap_.get_function_extras_and_params(func_obj);

    // Build stack frame: [return_address][func_obj][local_nlocals-1]...[local_0]

    operand_stack_.move_multiple(nparams, return_stack_);

    // Initialize remaining locals to nil.
    return_stack_.push_multiple(SPECIAL_NIL, nextras);


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
    return pc;
}

#undef LOG_INSTRUCTION_ENTRY
#undef LOG_INSTRUCTION_EXIT

} // namespace nutmeg
