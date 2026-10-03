#include "instruction_log.hpp"
#include "trace.hpp"
#include <chrono>
#include <ctime>
#include <stdexcept>
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

std::string format_tagged(Cell cell) {
    if (is_tagged_int(cell)) {
        return fmt::format("int {}", as_detagged_int(cell));
    }
    if (is_tagged_float(cell)) {
        return fmt::format("float {}", as_detagged_float(cell));
    }
    if (is_tagged_ptr(cell)) {
        return format_pointer(as_detagged_ptr(cell));
    }
    if ((cell.u64 & TAG_MASK_3BIT) == TAG_SPECIAL) {
        if (cell.u64 == SPECIAL_FALSE.u64) return "false";
        if (cell.u64 == SPECIAL_TRUE.u64) return "true";
        if (cell.u64 == SPECIAL_NIL.u64) return "nil";
        if (cell.u64 == SPECIAL_UNDEF.u64) return "undef";
        return fmt::format("special 0x{:x}", cell.u64);
    }
    // Tags 011 and 101 are reserved.
    return fmt::format("reserved 0x{:x}", cell.u64);
}

std::string format_opargs(const Cell* operands, std::initializer_list<OpArgKind> kinds) {
    std::string result = "[";
    size_t i = 0;
    for (OpArgKind kind : kinds) {
        if (i > 0) {
            result += ", ";
        }
        std::string text;
        switch (kind) {
            case OpArgKind::Raw: text = format_raw(operands[i].u64); break;
            case OpArgKind::Tagged: text = format_tagged(operands[i]); break;
            case OpArgKind::Pointer: text = format_pointer(operands[i].ptr); break;
        }
        result += '"' + text + '"';
        i++;
    }
    return result + "]";
}

std::string format_entry(const char* name, const std::string& opargs, size_t stacklength) {
    return fmt::format("{{\"opcode\": \"{}\", \"opargs\": {}, \"onEntry\": {{\"stacklength\": {}}}",
                       name, opargs, stacklength);
}

std::string format_exit(size_t stacklength) {
    return fmt::format(", \"onExit\": {{\"stacklength\": {}}}}}\n", stacklength);
}

// Flush after every half-line: the log is most valuable when the machine crashes,
// and buffered output would lose the final (most interesting) instructions.
void InstructionLog::log_entry(const char* name, const Cell* operands,
                               std::initializer_list<OpArgKind> kinds, size_t stacklength) {
    out_ << format_entry(name, format_opargs(operands, kinds), stacklength) << std::flush;
}

void InstructionLog::log_exit(size_t stacklength) {
    out_ << format_exit(stacklength) << std::flush;
}

} // namespace nutmeg
