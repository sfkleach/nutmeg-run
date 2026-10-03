#include "instruction_log.hpp"
#include "trace.hpp"
#include <chrono>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <fmt/core.h>

namespace nutmeg {

// Log files are named {LOG_TYPE}.{YYYY-MM-DD-HH.MM.SS}.jsonl, so that they are
// grouped by function and then sorted by (local) date-time.
static std::string make_log_filename(const std::string& log_type) {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local;
    localtime_r(&t, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H.%M.%S", &local);
    return fmt::format("{}.{}.jsonl", log_type, stamp);
}

InstructionLog::InstructionLog() {
    if constexpr (ENABLE_INSTRUCTION_LOG) {
        filename_ = make_log_filename("run");
        out_.open(filename_);
        if (!out_) {
            throw std::runtime_error(fmt::format("Cannot create instruction log: {}", filename_));
        }
    }
}

std::string format_raw(uint64_t bits) {
    return fmt::format("0d{},0x{:x}", static_cast<int64_t>(bits), bits);
}

std::string format_pointer(const void* ptr) {
    return fmt::format("&0x{:x}", reinterpret_cast<uintptr_t>(ptr));
}

std::string format_local(uint64_t offset, int nlocals) {
    if (nlocals >= 0) {
        int64_t index = static_cast<int64_t>(nlocals) + 2 - static_cast<int64_t>(offset);
        if (index >= 0 && index < nlocals) {
            return fmt::format("local {}", index);
        }
    }
    return fmt::format("local offset {}", static_cast<int64_t>(offset));
}

std::string shorten_string(const std::string& text) {
    // A character starts at every byte that is not a UTF-8 continuation byte (10xxxxxx), so
    // a cut at a character start never splits a multi-byte character. Invalid UTF-8 is
    // simply counted this way too.
    size_t chars = 0;
    size_t cut = text.size();
    for (size_t i = 0; i < text.size(); i++) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (chars == KEPT_STRING_CHARS) {
                cut = i;
            }
            chars++;
        }
    }
    if (chars <= MAX_STRING_CHARS) {
        return text;
    }
    return text.substr(0, cut) + "...";
}

// The name of the type of the object at `obj`, which must be inside the heap. Datakeys have
// no name of their own, so the three fundamental datakeys are recognised by identity.
static const char* type_name(const Cell* obj, Heap& heap) {
    if (obj == heap.get_datakey_datakey() || obj == heap.get_string_datakey() ||
        obj == heap.get_function_datakey()) {
        return "datakey";
    }
    // Every other object holds its datakey in cell 0.
    const void* datakey = obj[0].ptr;
    if (datakey == heap.get_string_datakey()) return "string";
    if (datakey == heap.get_function_datakey()) return "function";
    return "unknown";
}

// The text of the string object at `obj`, read within the bounds recorded in the object
// and the heap: [-1] is the length including the terminating NUL, [1...] is the data.
static std::string read_string(const Cell* obj, Heap& heap) {
    if (!heap.get_pool()->contains(obj - 1)) {
        return "";
    }
    size_t length = obj[-1].u64;
    const char* data = reinterpret_cast<const char*>(&obj[1]);
    if (length == 0 || !heap.get_pool()->contains(data + length - 1)) {
        return "";
    }
    return std::string(data, strnlen(data, length));
}

std::string format_global(const void* ident, const NameResolver& names) {
    std::optional<GlobalInfo> info = names.global_at(ident);
    if (!info) {
        return format_pointer(ident);
    }
    return (info->is_function ? "fn " : "global ") + info->name;
}

std::string format_function(const Cell* function_object, const NameResolver& names) {
    std::optional<std::string> name = names.function_name(function_object);
    return name ? "fn " + *name : format_pointer(function_object);
}

std::string format_syscall(const void* function, const NameResolver& names) {
    std::optional<std::string> name = names.sys_function_name(function);
    return name ? "sys " + *name : format_pointer(function);
}

OpArg format_tagged(Cell cell, const OpArgContext& context) {
    Heap& heap = context.heap;
    if (is_tagged_int(cell)) {
        return {"", fmt::format("int {}", as_detagged_int(cell))};
    }
    if (is_tagged_float(cell)) {
        return {"", fmt::format("float {}", as_detagged_float(cell))};
    }
    if (is_tagged_ptr(cell)) {
        const Cell* obj = static_cast<const Cell*>(as_detagged_ptr(cell));
        // Never follow a pointer that does not point into the heap.
        if (!heap.get_pool()->contains(obj)) {
            return {"unknown", format_pointer(obj)};
        }
        const char* type = type_name(obj, heap);
        if (obj[0].ptr == heap.get_string_datakey() && obj != heap.get_string_datakey()) {
            return {type, shorten_string(read_string(obj, heap))};
        }
        if (obj[0].ptr == heap.get_function_datakey() && obj != heap.get_function_datakey()) {
            if (std::optional<std::string> name = context.names.function_name(obj)) {
                return {"", "fn " + *name};
            }
        }
        return {type, format_pointer(obj)};
    }
    if ((cell.u64 & TAG_MASK_3BIT) == TAG_SPECIAL) {
        if (cell.u64 == SPECIAL_FALSE.u64) return {"", "false"};
        if (cell.u64 == SPECIAL_TRUE.u64) return {"", "true"};
        if (cell.u64 == SPECIAL_NIL.u64) return {"", "nil"};
        if (cell.u64 == SPECIAL_UNDEF.u64) return {"", "undef"};
        return {"", fmt::format("special 0x{:x}", cell.u64)};
    }
    // Tags 011 and 101 are reserved.
    return {"", fmt::format("reserved 0x{:x}", cell.u64)};
}

// A JSON string literal, quoted and escaped. Invalid UTF-8 is replaced rather than thrown on,
// so the log is always valid JSON.
static std::string json_quote(const std::string& text) {
    return nlohmann::json(text).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string format_opargs(const Cell* operands, std::initializer_list<OpArgKind> kinds,
                          const OpArgContext& context) {
    std::string result = "[";
    size_t i = 0;
    for (OpArgKind kind : kinds) {
        if (i > 0) {
            result += ", ";
        }
        OpArg arg;
        switch (kind) {
            case OpArgKind::Raw: arg.text = format_raw(operands[i].u64); break;
            case OpArgKind::Local: arg.text = format_local(operands[i].u64, context.nlocals); break;
            case OpArgKind::Tagged: arg = format_tagged(operands[i], context); break;
            case OpArgKind::Pointer: arg.text = format_pointer(operands[i].ptr); break;
            case OpArgKind::Global: arg.text = format_global(operands[i].ptr, context.names); break;
            case OpArgKind::Function:
                arg.text = format_function(static_cast<const Cell*>(operands[i].ptr), context.names);
                break;
            case OpArgKind::Syscall: arg.text = format_syscall(operands[i].ptr, context.names); break;
        }
        if (arg.key.empty()) {
            result += json_quote(arg.text);
        } else {
            result += "{\"key\": " + json_quote(arg.key) + ", \"value\": " + json_quote(arg.text) + "}";
        }
        i++;
    }
    return result + "]";
}

std::string format_entry(uint64_t number, const char* name, const std::string& opargs,
                         size_t stacklength) {
    return fmt::format("{{\"n\": {}, \"opcode\": \"{}\", \"opargs\": {}, "
                       "\"onEntry\": {{\"stacklength\": {}}}",
                       number, name, opargs, stacklength);
}

std::string format_exit(size_t stacklength) {
    return fmt::format(", \"onExit\": {{\"stacklength\": {}}}}}\n", stacklength);
}

// Flush after every half-line: the log is most valuable when the machine crashes,
// and buffered output would lose the final (most interesting) instructions.
void InstructionLog::log_entry(const char* name, const Cell* operands,
                               std::initializer_list<OpArgKind> kinds, const OpArgContext& context,
                               size_t stacklength) {
    out_ << format_entry(++count_, name, format_opargs(operands, kinds, context), stacklength) << std::flush;
}

void InstructionLog::log_exit(size_t stacklength) {
    out_ << format_exit(stacklength) << std::flush;
}

} // namespace nutmeg
