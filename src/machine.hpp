#ifndef MACHINE_HPP
#define MACHINE_HPP

#include "value.hpp"
#include "function_object.hpp"
#include "heap.hpp"
#include "cell_stack.hpp"
#include "instruction_log.hpp"
#include "trace.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace nutmeg {

class Ident {
public:
    Cell cell;
    bool lazy = false;
    bool in_progress = false;
};


class Machine;

// Resolves addresses to names for the instruction log, by reverse lookup in the machine's
// global dictionary (which maps names to Idents) and in the sys-functions table. Neither
// Ident nor function objects store a name, so these are linear scans; that is acceptable
// for a debugging aid, and keeps the cost out of the machine proper.
class MachineNames : public NameResolver {
public:
    explicit MachineNames(Machine& machine) : machine_(machine) {}

    std::optional<GlobalInfo> global_at(const void* ident) const override;
    std::optional<std::string> function_name(const Cell* function_object) const override;
    std::optional<std::string> sys_function_name(const void* function) const override;

private:
    Machine& machine_;
};

// The virtual machine with dual-stack architecture.
class Machine {
    friend class MachineNames;
private:
    // Operand stack (main data stack). Surrounded by guard pages.
    CellStack operand_stack_{VALUE_STACK_INITIAL_BYTES / sizeof(Cell), Guard::Pages,
                             VALUE_STACK_RESERVE_BYTES / sizeof(Cell)};

    // Return stack (for function calls and local variables).
    CellStack return_stack_;

    // Global dictionary mapping names to values via indirection.
    // Indirection ensures stable pointers that won't be invalidated by map resizing.
    std::unordered_map<std::string, Ident* > globals_;

    // Heap for objects (strings, function objects, etc.).
    Heap heap_;

    // Current function being executed (for local variable access).
    int pc_;  // Program counter.

    // Log of executed instructions (only opened if ENABLE_INSTRUCTION_LOG is set).
    InstructionLog instruction_log_;

    // Threaded interpreter support.
    std::unordered_map<Opcode, void*> opcode_map_;  // Maps opcodes to label addresses.

public:
    Machine();
    ~Machine();

    // Get the opcode map for compiling functions.
    const std::unordered_map<Opcode, void*>& get_opcode_map() const { return opcode_map_; }

    // Stack operations.
    inline void push(Cell value) {
        if constexpr (VALUE_STACK_HARDWARE_GROWTH) {
            operand_stack_.push_unchecked(value);
        } else {
            operand_stack_.push(value);
        }
    }
    inline Cell pop() {
        return operand_stack_.pop();
    }
    inline void pop_multiple(size_t count) {
        operand_stack_.pop_multiple(count);
    }
    inline Cell& peek() {
        return operand_stack_.peek();
    }
    inline Cell& peek_at(size_t index) {
        return operand_stack_.peek_at(index);
    }
    bool empty() const;
    size_t stack_size() const;

    // Return stack operations.
    inline void push_return(Cell value) {
        return_stack_.push(value);
    }
    inline Cell pop_return() {
        return return_stack_.pop();
    }

    inline Cell& get_return_address() {
        return return_stack_.offset_from_top(0);
    }
    inline Cell& get_frame_function_object() {
        return return_stack_.offset_from_top(1);
    }
    Cell& get_local_variable(int offset);

    // Pop nlocals slots from the return stack.
    void pop_return_frame(int nlocals) {
        return_stack_.discard_multiple(nlocals);
    }

    // Global dictionary operations.
    void define_global(const std::string& name, Cell value, bool lazy);
    Cell lookup_global(const std::string& name) const;
    bool has_global(const std::string& name) const;
    Cell* get_global_cell_ptr(const std::string& name);
    Ident * lookup_ident(const std::string& name) const;


    // Heap allocation.
    Cell allocate_string(const std::string& value);
    const char* get_string(Cell cell);

    Cell* allocate_function(const std::vector<Cell>& code, int nlocals, int nparams);
    Cell* get_function_ptr(Cell cell);

    // Parse JSON function object and compile to threaded code.
    FunctionObject parse_function_object(const std::string& idname, const std::unordered_map<std::string, bool>& deps, const std::string& json_str);

    // Get the heap for external use (e.g., initializing globals).
    Heap& get_heap() { return heap_; }

    // Execution.
    void execute(Cell* func_ptr);

private:
    void execute_syscall(const std::string& name, int nargs);

    // The number of locals of the function being executed, or -1 if there is no frame yet.
    // Used only by the instruction log.
    int log_frame_nlocals();

    // Combined init/run function for threaded interpreter (like Poppy).
    void threaded_impl(std::vector<Cell> *code, bool init_mode);
    Cell * LaunchInstruction(Cell *pc);
    Cell * call_function_object(Cell * pc, Cell* func_ptr, int arg_count);
}; // class Machine

} // namespace nutmeg

#endif // MACHINE_HPP
