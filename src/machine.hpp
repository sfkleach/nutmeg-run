#ifndef MACHINE_HPP
#define MACHINE_HPP

#include "value.hpp"
#include "function_object.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace nutmeg {

// Instruction word - a union that can hold different types for threaded interpreter.
// In threaded code, instructions are sequences of these words.
union InstructionWord {
    void* label_addr;           // Address of instruction handler label.
    int64_t i64;               // Integer immediate value.
    uint64_t u64;              // Unsigned integer.
    Cell cell;                 // Tagged cell value.
    std::string* str_ptr;      // Pointer to string.
    void* ptr;                 // Generic pointer.
};

// Heap-allocated string object.
struct HeapString {
    std::string value;
};

// The virtual machine with dual-stack architecture.
class Machine {
private:
    // Operand stack (main data stack).
    std::vector<Cell> operand_stack_;
    
    // Return stack (for function calls and local variables).
    std::vector<Cell> return_stack_;
    
    // Global dictionary mapping names to values.
    std::unordered_map<std::string, Cell> globals_;
    
    // Heap for objects (strings, function objects, etc.).
    // For now, using simple vector with manual memory management.
    // In a real implementation, this would be a garbage collector (defensive check).
    std::vector<std::unique_ptr<HeapString>> string_heap_;
    std::vector<std::unique_ptr<FunctionObject>> function_heap_;
    
    // Current function being executed (for local variable access).
    FunctionObject* current_function_;
    int pc_;  // Program counter.
    
    // Threaded interpreter support.
    std::unordered_map<Opcode, void*> opcode_map_;  // Maps opcodes to label addresses.
    bool threaded_mode_;  // Whether to use threaded interpreter.
    
public:
    Machine();
    ~Machine();
    
    // Initialize the threaded interpreter (populates opcode_map_).
    void init_threaded();
    
    // Stack operations.
    void push(Cell value);
    Cell pop();
    Cell peek() const;
    bool empty() const;
    size_t stack_size() const;
    
    // Return stack operations.
    void push_return(Cell value);
    Cell pop_return();
    
    // Global dictionary operations.
    void define_global(const std::string& name, Cell value);
    Cell lookup_global(const std::string& name) const;
    bool has_global(const std::string& name) const;
    
    // Heap allocation.
    Cell allocate_string(const std::string& value);
    std::string* get_string(Cell cell);
    
    Cell allocate_function(std::unique_ptr<FunctionObject> func);
    FunctionObject* get_function(Cell cell);
    
    // Execution.
    void execute(FunctionObject* func);
    
    // Threaded execution (experimental).
    void execute_threaded(FunctionObject* func);
    
private:
    void execute_instruction(const Instruction& inst);
    void execute_syscall(const std::string& name, int nargs);
    
    // Compile function to threaded code.
    std::vector<InstructionWord> compile_to_threaded(const FunctionObject* func);
    
    // Combined init/run function for threaded interpreter (like Poppy).
    void threaded_impl(std::vector<InstructionWord>* code, bool init_mode);
};

} // namespace nutmeg

#endif // MACHINE_HPP
