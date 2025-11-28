#ifndef INSTRUCTION_HPP
#define INSTRUCTION_HPP

#include <string>
#include <optional>

namespace nutmeg {

// Instruction represents a single instruction in the function body.
// This uses an adjacently tagged union format with a Type field and type-specific fields.
struct Instruction {
    std::string type;

    // Fields for different instruction types.
    // Only the relevant fields for each type will be populated.

    // PushInt, PopLocal, PushLocal.
    std::optional<int> index;

    // PushString, PushGlobal.
    std::optional<std::string> value;

    // SyscallCounted, CallGlobalCounted.
    std::optional<std::string> name;
    std::optional<int> nargs;
};

} // namespace nutmeg

#endif // INSTRUCTION_HPP
