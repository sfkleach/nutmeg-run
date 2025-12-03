#ifndef MACHINE_HPP
#define MACHINE_HPP

#include "value.hpp"
#include "function_object.hpp"
#include "heap.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace nutmeg {

// The virtual machine with dual-stack architecture.
class Machine {
private:
    // Operand stack (main data stack).
    std::vector<Cell> operand_stack_;
    
    // Return stack (for function calls and local variables).
    std::vector<Cell> return_stack_;
    
    // Global dictionary mapping names to values via indirection.
    // Indirection ensures stable pointers that won't be invalidated by map resizing.
    std::unordered_map<std::string, Ident* > globals_;
    
    // Heap for objects (strings, function objects, etc.).
    Heap heap_;
    
    // Current function being executed (for local variable access).
    Cell* current_function_;  // Now points to heap object.
    int pc_;  // Program counter.
    
    // Threaded interpreter support.
    std::unordered_map<Opcode, void*> opcode_map_;  // Maps opcodes to label addresses.
    
public:
    Machine();
    ~Machine();
    
    // Get the opcode map for compiling functions.
    const std::unordered_map<Opcode, void*>& get_opcode_map() const { return opcode_map_; }
    
    // Stack operations.
    void push(Cell value);
    Cell pop();
    void pop_multiple(size_t count);
    Cell peek() const;
    Cell peek_at(size_t index) const;
    bool empty() const;
    size_t stack_size() const;
    
    // Return stack operations.
    void push_return(Cell value);
    Cell pop_return();
    
    Cell& get_return_address();
    Cell& get_frame_function_object();
    Cell& get_local_variable(int offset);
    
    // Pop nlocals slots from the return stack.
    void pop_return_frame(int nlocals) {
        return_stack_.resize(return_stack_.size() - nlocals);
    }
    
    // Global dictionary operations.
    void define_global(const std::string& name, Cell value);
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
    FunctionObject parse_function_object(const std::string& json_str);
    
    // Get the heap for external use (e.g., initializing globals).
    Heap& get_heap() { return heap_; }
    
    // Execution.
    void execute(Cell* func_ptr);
    
private:
    void execute_syscall(const std::string& name, int nargs);
    
    // Combined init/run function for threaded interpreter (like Poppy).
    void threaded_impl(std::vector<Cell> *code, bool init_mode);
    Cell * LaunchInstruction(Cell *pc);
}; // class Machine

} // namespace nutmeg

#endif // MACHINE_HPP
