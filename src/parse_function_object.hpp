#ifndef PARSE_FUNCTION_OBJECT_HPP
#define PARSE_FUNCTION_OBJECT_HPP

#include "instruction.hpp"
#include "machine.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace nutmeg {

// Forward declarations.
class Machine;
struct FunctionObject;

// Helper class for parsing function objects from JSON into threaded code.
// Handles label resolution and forward reference patching.
class ParseFunctionObject {
public:
    ParseFunctionObject(Machine& machine, const std::string& idname, 
                       const std::unordered_map<std::string, bool>& deps);
    
    // Parse JSON string into a FunctionObject with threaded code.
    FunctionObject parse(const std::string& json_str);

private:
    int calc_offset(const Instruction& inst);

    // Parse and plant a single instruction.
    void plant_instruction(FunctionObject& func, const Instruction& inst);
    
    // Instruction-specific planting methods.
    void plant_label(FunctionObject& func, const Instruction& inst);
    void plant_push_int(FunctionObject& func, const Instruction& inst);
    void plant_push_bool(FunctionObject& func, const Instruction& inst);
    void plant_push_string(FunctionObject& func, const Instruction& inst);
    void plant_pop_local(FunctionObject& func, const Instruction& inst);
    void plant_push_local(FunctionObject& func, const Instruction& inst);
    void plant_push_global(FunctionObject& func, const Instruction& inst);
    void plant_call_global_counted(FunctionObject& func, const Instruction& inst);
    void plant_syscall_counted(FunctionObject& func, const Instruction& inst);
    void plant_stack_length(FunctionObject& func, const Instruction& inst);
    void plant_check_bool(FunctionObject& func, const Instruction& inst);
    void plant_goto(FunctionObject& func, const Instruction& inst);
    void plant_if_not(FunctionObject& func, const Instruction& inst);
    void plant_return_halt(FunctionObject& func, const Instruction& inst);
    void plant_done(FunctionObject& func, const Instruction& inst);
    
    // Helper for jump instructions (goto/if.not).
    void plant_jump_instruction(FunctionObject& func, const std::string& label_name);
    
    // Validate that all forward references have been resolved.
    void validate_forward_references();
    
    Machine& machine_;
    const std::string& idname_;
    const std::unordered_map<std::string, bool>& deps_;
    FunctionObject func_;
    
    // Label tracking for jumps.
    std::unordered_map<std::string, size_t> label_map_;
    std::unordered_map<std::string, std::vector<size_t>> forward_refs_;
};

} // namespace nutmeg

#endif // PARSE_FUNCTION_OBJECT_HPP
