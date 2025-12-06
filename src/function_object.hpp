#ifndef FUNCTION_OBJECT_HPP
#define FUNCTION_OBJECT_HPP

#include "instruction.hpp"
#include "value.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace nutmeg {

// FunctionObject represents a compiled function with its metadata and threaded code.
struct FunctionObject {
    int nlocals;
    int nparams;
    std::vector<Cell> code;  // Compiled threaded instruction stream.
};

} // namespace nutmeg

#endif // FUNCTION_OBJECT_HPP
