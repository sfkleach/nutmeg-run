#include "instruction.hpp"
#include <stdexcept>
#include <unordered_map>

namespace nutmeg {

// Mapping from JSON instruction names to opcodes.
static const std::unordered_map<std::string, std::pair<Opcode, Opcode>> string_to_opcode_map = {
    {"push.int", {Opcode::PUSH_INT, Opcode::PUSH_INT}},
    {"push.bool", {Opcode::PUSH_BOOL, Opcode::PUSH_BOOL}},
    {"push.string", {Opcode::PUSH_STRING, Opcode::PUSH_STRING}},
    {"pop.local", {Opcode::POP_LOCAL, Opcode::POP_LOCAL}},
    {"push.local", {Opcode::PUSH_LOCAL, Opcode::PUSH_LOCAL}},
    {"push.global", {Opcode::PUSH_GLOBAL, Opcode::PUSH_GLOBAL_LAZY}},
    {"call.global.counted", {Opcode::CALL_GLOBAL_COUNTED, Opcode::CALL_GLOBAL_COUNTED_LAZY}},
    {"syscall.counted", {Opcode::SYSCALL_COUNTED, Opcode::SYSCALL_COUNTED}},
    {"stack.length", {Opcode::STACK_LENGTH, Opcode::STACK_LENGTH}},
    {"check.bool", {Opcode::CHECK_BOOL, Opcode::CHECK_BOOL}},
    {"check.count.is.1", {Opcode::CHECK_COUNT_IS_1, Opcode::CHECK_COUNT_IS_1}},
    {"label", {Opcode::LABEL, Opcode::LABEL}},
    {"goto", {Opcode::GOTO, Opcode::GOTO}},
    {"if.not", {Opcode::IF_NOT, Opcode::IF_NOT}},
    {"erase", {Opcode::ERASE, Opcode::ERASE}},
    {"if.so", {Opcode::IF_SO, Opcode::IF_SO}},
    {"if.not.return", {Opcode::IF_NOT_RETURN, Opcode::IF_NOT_RETURN}},
    {"if.so.return", {Opcode::IF_SO_RETURN, Opcode::IF_SO_RETURN}},
    // "if.then.else"'s `name` field is a label, not a global reference, unlike every other
    // instruction that has a `name`. ParseFunctionObject::parse() uses `inst.name` to decide
    // laziness (via `deps_.find(inst.name)`), which would misfire if a label happened to share
    // a name with a lazy global - but both opcodes in this pair are IF_THEN_ELSE, so an
    // incorrect lazy/non-lazy choice here is harmless.
    {"if.then.else", {Opcode::IF_THEN_ELSE, Opcode::IF_THEN_ELSE}},
    {"return", {Opcode::RETURN, Opcode::RETURN}},
    {"halt", {Opcode::HALT, Opcode::HALT}},
    {"done", {Opcode::DONE, Opcode::DONE}},
};

std::pair<Opcode, Opcode> string_to_opcode(const std::string& type) {
    auto it = string_to_opcode_map.find(type);
    if (it == string_to_opcode_map.end()) {
        throw std::runtime_error("Unknown instruction type: " + type);
    }
    return it->second;
}

const char* opcode_to_string(Opcode opcode) {
    switch (opcode) {
        case Opcode::PUSH_INT: return "PUSH_INT";
        case Opcode::PUSH_BOOL: return "PUSH_BOOL";
        case Opcode::PUSH_STRING: return "PUSH_STRING";
        case Opcode::POP_LOCAL: return "POP_LOCAL";
        case Opcode::PUSH_LOCAL: return "PUSH_LOCAL";
        case Opcode::PUSH_GLOBAL: return "PUSH_GLOBAL";
        case Opcode::PUSH_GLOBAL_LAZY: return "PUSH_GLOBAL_LAZY";
        case Opcode::CALL_GLOBAL_COUNTED: return "CALL_GLOBAL_COUNTED";
        case Opcode::CALL_GLOBAL_COUNTED_LAZY: return "CALL_GLOBAL_COUNTED_LAZY";
        case Opcode::SYSCALL_COUNTED: return "SYSCALL_COUNTED";
        case Opcode::STACK_LENGTH: return "STACK_LENGTH";
        case Opcode::CHECK_BOOL: return "CHECK_BOOL";
        case Opcode::CHECK_COUNT_IS_1: return "CHECK_COUNT_IS_1";
        case Opcode::LABEL: return "LABEL";
        case Opcode::GOTO: return "GOTO";
        case Opcode::IF_NOT: return "IF_NOT";
        case Opcode::ERASE: return "ERASE";
        case Opcode::IF_SO: return "IF_SO";
        case Opcode::IF_NOT_RETURN: return "IF_NOT_RETURN";
        case Opcode::IF_SO_RETURN: return "IF_SO_RETURN";
        case Opcode::IF_THEN_ELSE: return "IF_THEN_ELSE";
        case Opcode::RETURN: return "RETURN";
        case Opcode::HALT: return "HALT";
        case Opcode::DONE: return "DONE";
    }
    return "UNKNOWN";
}

} // namespace nutmeg
