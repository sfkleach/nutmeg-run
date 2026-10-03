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

std::string format_entry(const char* name, size_t stacklength) {
    return fmt::format("{{\"opcode\": \"{}\", \"onEntry\": {{\"stacklength\": {}}}", name, stacklength);
}

std::string format_exit(size_t stacklength) {
    return fmt::format(", \"onExit\": {{\"stacklength\": {}}}}}\n", stacklength);
}

// Flush after every half-line: the log is most valuable when the machine crashes,
// and buffered output would lose the final (most interesting) instructions.
void InstructionLog::log_entry(const char* name, size_t stacklength) {
    out_ << format_entry(name, stacklength) << std::flush;
}

void InstructionLog::log_exit(size_t stacklength) {
    out_ << format_exit(stacklength) << std::flush;
}

} // namespace nutmeg
