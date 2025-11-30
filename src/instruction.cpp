#include "instruction.hpp"
#include <stdexcept>
#include <unordered_map>

namespace nutmeg {

// Mapping from JSON instruction names to opcodes.
static const std::unordered_map<std::string, Opcode> string_to_opcode_map = {
    {"push.int", Opcode::PUSH_INT},
    {"PushInt", Opcode::PUSH_INT},
    {"push.string", Opcode::PUSH_STRING},
    {"PushString", Opcode::PUSH_STRING},
    {"pop.local", Opcode::POP_LOCAL},
    {"PopLocal", Opcode::POP_LOCAL},
    {"push.local", Opcode::PUSH_LOCAL},
    {"PushLocal", Opcode::PUSH_LOCAL},
    {"push.global", Opcode::PUSH_GLOBAL},
    {"PushGlobal", Opcode::PUSH_GLOBAL},
    {"call.global.counted", Opcode::CALL_GLOBAL_COUNTED},
    {"CallGlobalCounted", Opcode::CALL_GLOBAL_COUNTED},
    {"syscall.counted", Opcode::SYSCALL_COUNTED},
    {"SyscallCounted", Opcode::SYSCALL_COUNTED},
    {"stack.length", Opcode::STACK_LENGTH},
    {"return", Opcode::RETURN},
    {"halt", Opcode::HALT},
};

Opcode string_to_opcode(const std::string& type) {
    auto it = string_to_opcode_map.find(type);
    if (it == string_to_opcode_map.end()) {
        throw std::runtime_error("Unknown instruction type: " + type);
    }
    return it->second;
}

const char* opcode_to_string(Opcode opcode) {
    switch (opcode) {
        case Opcode::PUSH_INT: return "PUSH_INT";
        case Opcode::PUSH_STRING: return "PUSH_STRING";
        case Opcode::POP_LOCAL: return "POP_LOCAL";
        case Opcode::PUSH_LOCAL: return "PUSH_LOCAL";
        case Opcode::PUSH_GLOBAL: return "PUSH_GLOBAL";
        case Opcode::CALL_GLOBAL_COUNTED: return "CALL_GLOBAL_COUNTED";
        case Opcode::SYSCALL_COUNTED: return "SYSCALL_COUNTED";
        case Opcode::STACK_LENGTH: return "STACK_LENGTH";
        case Opcode::RETURN: return "RETURN";
        case Opcode::HALT: return "HALT";
    }
    return "UNKNOWN";
}

} // namespace nutmeg
