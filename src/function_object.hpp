#ifndef FUNCTION_OBJECT_HPP
#define FUNCTION_OBJECT_HPP

#include "instruction.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace nutmeg {

// Instruction word - a union that can hold different types for threaded interpreter.
// In threaded code, instructions are sequences of these words.
union InstructionWord {
    void* label_addr;           // Address of instruction handler label.
    int64_t i64;               // Integer immediate value.
    uint64_t u64;              // Unsigned integer.
    uint64_t cell;             // Tagged cell value.
    std::string* str_ptr;      // Pointer to string.
    void* ptr;                 // Generic pointer.
};

// FunctionObject represents a compiled function with its metadata and threaded code.
struct FunctionObject {
    int nlocals;
    int nparams;
    std::vector<InstructionWord> code;  // Compiled threaded instruction stream.
};

} // namespace nutmeg

#endif // FUNCTION_OBJECT_HPP
