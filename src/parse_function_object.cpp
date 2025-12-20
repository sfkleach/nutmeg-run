#include "parse_function_object.hpp"
#include "machine.hpp"
#include "instruction.hpp"
#include "sysfunctions.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <fmt/core.h>

#define TRACE_PLANT_INSTRUCTIONS

namespace nutmeg {

ParseFunctionObject::ParseFunctionObject(Machine& machine, const std::string& idname,
                                       const std::unordered_map<std::string, bool>& deps)
    : machine_(machine), idname_(idname), deps_(deps) {
}

FunctionObject ParseFunctionObject::parse(const std::string& json_str) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Planting instructions for function: {}\n", idname_);
    #endif
    
    try {
        nlohmann::json j = nlohmann::json::parse(json_str);
        
        FunctionObject func;
        func.nlocals = j.at("nlocals").get<int>();
        func.nparams = j.at("nparams").get<int>();
        
        // Compile instructions to threaded code.
        for (const auto& inst_json : j.at("instructions")) {
            fmt::print("Processing instruction JSON: {}\n", inst_json.dump());
            
            Instruction inst;
            inst.type = inst_json.at("type").get<std::string>();
            if (inst_json.contains("name")) {
                inst.name = inst_json.at("name").get<std::string>();
            }
            auto opcodes = string_to_opcode(inst.type);
            
            bool is_lazy = false;
            if (inst.name.has_value()) {
                auto dep_it = deps_.find(inst.name.value());
                if (dep_it != deps_.end()) {
                    is_lazy = dep_it->second;
                }
                fmt::print("Instruction '{}' refers to global '{}', lazy={}\n",
                          inst.type, inst.name.value(), is_lazy);
            } else {
                fmt::print("Instruction '{}' has no global name\n", inst.type);
            }
            inst.opcode = is_lazy ? opcodes.second : opcodes.first;
            
            // Optional fields.
            if (inst_json.contains("index")) {
                inst.index = inst_json.at("index").get<int>();
            }
            if (inst_json.contains("value")) {
                inst.value = inst_json.at("value").get<std::string>();
            }
            if (inst_json.contains("ivalue")) {
                inst.ivalue = inst_json.at("ivalue").get<int64_t>();
            }
            
            plant_instruction(func, inst);
        }
        
        validate_forward_references();
        
        // Add HALT at the end.
        Cell halt_word;
        halt_word.label_addr = machine_.get_opcode_map().at(Opcode::HALT);
        func.code.push_back(halt_word);
        
        #ifdef TRACE_PLANT_INSTRUCTIONS
        fmt::print("End of instructions for function: {}\n", idname_);
        #endif
        
        return func;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(fmt::format("JSON parsing error: {}", e.what()));
    }
}

void ParseFunctionObject::plant_instruction(FunctionObject& func, const Instruction& inst) {
    // LABEL is special - doesn't emit code, just tracks position.
    if (inst.opcode == Opcode::LABEL) {
        plant_label(func, inst);
        return;
    }
    
    // Emit label address for the instruction handler.
    auto& opcode_map = machine_.get_opcode_map();
    auto opcode_it = opcode_map.find(inst.opcode);
    if (opcode_it == opcode_map.end()) {
        throw std::runtime_error(fmt::format("Opcode not found in map: {}", static_cast<int>(inst.opcode)));
    }
    Cell label_word;
    label_word.label_addr = opcode_it->second;
    func.code.push_back(label_word);
    
    // Emit instruction-specific operands.
    fmt::print("Processing operands for instruction: {}\n", opcode_to_string(inst.opcode));
    
    switch (inst.opcode) {
        case Opcode::PUSH_INT:
            plant_push_int(func, inst);
            break;
        case Opcode::PUSH_BOOL:
            plant_push_bool(func, inst);
            break;
        case Opcode::PUSH_STRING:
            plant_push_string(func, inst);
            break;
        case Opcode::POP_LOCAL:
            plant_pop_local(func, inst);
            break;
        case Opcode::PUSH_LOCAL:
            plant_push_local(func, inst);
            break;
        case Opcode::PUSH_GLOBAL_LAZY:
        case Opcode::PUSH_GLOBAL:
            plant_push_global(func, inst);
            break;
        case Opcode::CALL_GLOBAL_COUNTED_LAZY:
        case Opcode::CALL_GLOBAL_COUNTED:
            plant_call_global_counted(func, inst);
            break;
        case Opcode::SYSCALL_COUNTED:
            plant_syscall_counted(func, inst);
            break;
        case Opcode::STACK_LENGTH:
            plant_stack_length(func, inst);
            break;
        case Opcode::CHECK_BOOL:
            plant_check_bool(func, inst);
            break;
        case Opcode::GOTO:
            plant_goto(func, inst);
            break;
        case Opcode::IF_NOT:
            plant_if_not(func, inst);
            break;
        case Opcode::RETURN:
        case Opcode::HALT:
            plant_return_halt(func, inst);
            break;
        case Opcode::DONE:
            plant_done(func, inst);
            break;
        default:
            throw std::runtime_error(fmt::format("Unhandled opcode during compilation: {}", 
                                                static_cast<int>(inst.opcode)));
    }
}

void ParseFunctionObject::plant_label(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: LABEL\n");
    #endif
    if (!inst.value.has_value()) {
        throw std::runtime_error("LABEL requires a value field");
    }
    std::string label_name = inst.value.value();
    
    // Record the current position as the target for this label.
    size_t label_position = func.code.size();
    label_map_[label_name] = label_position;
    
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("  Label '{}' defined at position {}\n", label_name, label_position);
    #endif
    
    // Resolve any forward references to this label.
    auto fwd_it = forward_refs_.find(label_name);
    if (fwd_it != forward_refs_.end()) {
        for (size_t ref_pos : fwd_it->second) {
            int64_t offset = static_cast<int64_t>(label_position) - static_cast<int64_t>(ref_pos + 1);
            func.code[ref_pos].i64 = offset;
            #ifdef TRACE_PLANT_INSTRUCTIONS
            fmt::print("  Patched forward reference at position {} with offset {}\n", ref_pos, offset);
            #endif
        }
        forward_refs_.erase(fwd_it);
    }
}

void ParseFunctionObject::plant_push_int(FunctionObject& func, const Instruction& inst) {
    if (!inst.ivalue.has_value()) {
        throw std::runtime_error("PUSH_INT requires an ivalue field");
    }
    int64_t int_value = inst.ivalue.value();
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: PUSH_INT {}\n", int_value);
    #endif
    
    Cell operand = make_tagged_int(int_value);
    func.code.push_back(operand);
}

void ParseFunctionObject::plant_push_bool(FunctionObject& func, const Instruction& inst) {
    if (!inst.value.has_value()) {
        throw std::runtime_error("PUSH_BOOL requires a value field");
    }
    std::string bool_str = inst.value.value();
    bool bool_value;
    if (bool_str == "true") {
        bool_value = true;
    } else if (bool_str == "false") {
        bool_value = false;
    } else {
        throw std::runtime_error("PUSH_BOOL value must be 'true' or 'false'");
    }
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: PUSH_BOOL {}\n", bool_value);
    #endif
    
    Cell operand = make_bool(bool_value);
    func.code.push_back(operand);
}

void ParseFunctionObject::plant_push_string(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: PUSH_STRING\n");
    #endif
    std::string str_value = inst.value.value();
    Cell str_cell = machine_.allocate_string(str_value);
    func.code.push_back(str_cell);
}

void ParseFunctionObject::plant_pop_local(FunctionObject& func, const Instruction& inst) {
    throw std::runtime_error("POP_LOCAL not yet implemented");
}

void ParseFunctionObject::plant_push_local(FunctionObject& func, const Instruction& inst) {
    Cell operand;
    operand.i64 = inst.calc_offset();
    func.code.push_back(operand);
}

void ParseFunctionObject::plant_push_global(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: PUSH_GLOBAL\n");
    #endif
    
    if (!inst.name.has_value()) {
        throw std::runtime_error("PUSH_GLOBAL requires a name field");
    }
    
    Ident* ident_ptr = machine_.lookup_ident(inst.name.value());
    if (ident_ptr == nullptr) {
        throw std::runtime_error(
            fmt::format("PUSH_GLOBAL: undefined global variable: {}", inst.name.value()));
    }
    
    Cell ident_operand;
    ident_operand.ptr = static_cast<void*>(ident_ptr);
    func.code.push_back(ident_operand);
}

void ParseFunctionObject::plant_call_global_counted(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: (L_)CALL_GLOBAL_COUNTED\n");
    #endif
    
    if (!inst.index.has_value()) {
        throw std::runtime_error("CALL_GLOBAL_COUNTED requires an index field");
    }
    if (!inst.name.has_value()) {
        throw std::runtime_error("CALL_GLOBAL_COUNTED requires a name field");
    }
    
    Ident* ident_ptr = machine_.lookup_ident(inst.name.value());
    if (ident_ptr == nullptr) {
        throw std::runtime_error(
            fmt::format("CALL_GLOBAL_COUNTED: undefined global function: {}", inst.name.value()));
    }
    
    Cell index_operand = make_raw_i64(inst.calc_offset());
    func.code.push_back(index_operand);
    
    Cell func_operand;
    func_operand.ptr = static_cast<void*>(ident_ptr);
    func.code.push_back(func_operand);
}

void ParseFunctionObject::plant_syscall_counted(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: SYSCALL_COUNTED\n");
    #endif
    
    if (!inst.index.has_value()) {
        throw std::runtime_error("SYSCALL_COUNTED requires an index field");
    }
    if (!inst.name.has_value()) {
        throw std::runtime_error("SYSCALL_COUNTED requires a name field");
    }
    
    Cell index_operand = make_raw_i64(inst.calc_offset());
    func.code.push_back(index_operand);
    
    auto it = sysfunctions_table.find(inst.name.value());
    if (it == sysfunctions_table.end()) {
        throw std::runtime_error(fmt::format("Unknown sys-function: {}", inst.name.value()));
    }
    SysFunction sys_function = it->second;
    Cell func_operand;
    func_operand.ptr = reinterpret_cast<void*>(sys_function);
    func.code.push_back(func_operand);
}

void ParseFunctionObject::plant_stack_length(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: STACK_LENGTH\n");
    #endif
    
    if (!inst.index.has_value()) {
        throw std::runtime_error("STACK_LENGTH requires an index field");
    }
    int offset = inst.calc_offset();
    Cell c = make_raw_i64(offset);
    func.code.push_back(c);
}

void ParseFunctionObject::plant_check_bool(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: CHECK_BOOL\n");
    #endif
    
    if (!inst.index.has_value()) {
        throw std::runtime_error("CHECK_BOOL requires an index field");
    }
    int offset = inst.calc_offset();
    Cell c = make_raw_i64(offset);
    func.code.push_back(c);
}

void ParseFunctionObject::plant_goto(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: GOTO\n");
    #endif
    
    if (!inst.value.has_value()) {
        throw std::runtime_error("GOTO requires a value field");
    }
    plant_jump_instruction(func, inst.value.value());
}

void ParseFunctionObject::plant_if_not(FunctionObject& func, const Instruction& inst) {
    #ifdef TRACE_PLANT_INSTRUCTIONS
    fmt::print("Plant: IF_NOT\n");
    #endif
    
    if (!inst.value.has_value()) {
        throw std::runtime_error("IF_NOT requires a value field");
    }
    plant_jump_instruction(func, inst.value.value());
}

void ParseFunctionObject::plant_jump_instruction(FunctionObject& func, const std::string& label_name) {
    // Reserve space for the offset operand.
    size_t operand_pos = func.code.size();
    Cell offset_cell;
    offset_cell.i64 = 0;  // Placeholder.
    func.code.push_back(offset_cell);
    
    // Check if the label has already been defined (backward jump).
    auto label_it = label_map_.find(label_name);
    if (label_it != label_map_.end()) {
        // Backward jump - calculate offset immediately.
        size_t target_pos = label_it->second;
        int64_t offset = static_cast<int64_t>(target_pos) - static_cast<int64_t>(operand_pos + 1);
        func.code[operand_pos].i64 = offset;
        #ifdef TRACE_PLANT_INSTRUCTIONS
        fmt::print("  '{}' (backward) at position {}, target {}, offset {}\n",
                   label_name, operand_pos, target_pos, offset);
        #endif
    } else {
        // Forward jump - add to forward references.
        forward_refs_[label_name].push_back(operand_pos);
        #ifdef TRACE_PLANT_INSTRUCTIONS
        fmt::print("  '{}' (forward) at position {}, deferred\n", label_name, operand_pos);
        #endif
    }
}

void ParseFunctionObject::plant_return_halt(FunctionObject& func, const Instruction& inst) {
    // No operands.
}

void ParseFunctionObject::plant_done(FunctionObject& func, const Instruction& inst) {
    if (!inst.index.has_value()) {
        throw std::runtime_error("DONE requires an index field");
    }
    if (!inst.name.has_value()) {
        throw std::runtime_error("DONE requires a name field");
    }
    
    Ident* ident_ptr = machine_.lookup_ident(inst.name.value());
    if (ident_ptr == nullptr) {
        throw std::runtime_error(
            fmt::format("DONE: undefined global function: {}", inst.name.value()));
    }
    
    Cell index_operand = make_raw_i64(inst.calc_offset());
    func.code.push_back(index_operand);
    
    Cell func_operand;
    func_operand.ptr = static_cast<void*>(ident_ptr);
    func.code.push_back(func_operand);
}

void ParseFunctionObject::validate_forward_references() {
    if (!forward_refs_.empty()) {
        std::string unresolved;
        for (const auto& pair : forward_refs_) {
            if (!unresolved.empty()) unresolved += ", ";
            unresolved += pair.first;
        }
        throw std::runtime_error(fmt::format("Unresolved label references: {}", unresolved));
    }
}

} // namespace nutmeg
