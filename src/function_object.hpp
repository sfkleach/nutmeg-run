#ifndef FUNCTION_OBJECT_HPP
#define FUNCTION_OBJECT_HPP

#include "instruction.hpp"
#include <vector>

namespace nutmeg {

// FunctionObject represents a compiled function with its metadata and instructions.
struct FunctionObject {
    int nlocals;
    int nparams;
    std::vector<Instruction> instructions;
};

} // namespace nutmeg

#endif // FUNCTION_OBJECT_HPP
