#ifndef INSTRUCTION_HPP
#define INSTRUCTION_HPP

#include <string>
#include <optional>
#include <stdexcept>

namespace nutmeg {

// Instruction opcodes for threaded interpreter.
enum class Opcode {
    DONE,
    PUSH_INT,
    PUSH_STRING,
    PUSH_BOOL,
    POP_LOCAL,
    PUSH_LOCAL,
    PUSH_GLOBAL,
    PUSH_GLOBAL_LAZY,
    LAUNCH,
    CALL_GLOBAL_COUNTED,
    CALL_GLOBAL_COUNTED_LAZY,
    SYSCALL_COUNTED,
    STACK_LENGTH,
    RETURN,
    HALT,
};

// Map JSON instruction type strings to opcodes.
std::pair<Opcode, Opcode> string_to_opcode(const std::string& type);

// Get the instruction name for debugging.
const char* opcode_to_string(Opcode opcode);

// Instruction represents a single instruction in the function body.
// This uses an adjacently tagged union format with a Type field and type-specific fields.
struct Instruction {
    std::string type;        // Original JSON type (for now, during transition).
    Opcode opcode;           // Mapped opcode.

    // Fields for different instruction types.
    // Only the relevant fields for each type will be populated.

    // POP_LOCAL, PUSH_LOCAL.
    std::optional<int> index;

    // PUSH_INT.
    std::optional<int64_t> ivalue;

    // PUSH_STRING, PUSH_BOOL
    std::optional<std::string> value;

    // PUSH_GLOBAL, SYSCALL_COUNTED, CALL_GLOBAL_COUNTED.
    std::optional<std::string> name;

    int calc_offset() const {
        if (index.has_value()) {
            return index.value() + 3;  // Adjust for tagged int representation.
        }
        throw std::runtime_error("calc_offset called on instruction without index");
    }
};

} // namespace nutmeg

#endif // INSTRUCTION_HPP
