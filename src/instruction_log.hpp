#ifndef INSTRUCTION_LOG_HPP
#define INSTRUCTION_LOG_HPP

#include <fstream>
#include <string>

namespace nutmeg {

// Streams one JSON object per line (JSONL) to a file, one line for every
// instruction executed, for post-hoc debugging. The log is only created when
// ENABLE_INSTRUCTION_LOG is set in trace.hpp; otherwise this class does nothing.
class InstructionLog {
public:
    InstructionLog();

    InstructionLog(const InstructionLog&) = delete;
    InstructionLog& operator=(const InstructionLog&) = delete;

    // Append {"opcode": "NAME"}. NAME must be a plain identifier (no escaping is done).
    void log_opcode(const char* name);

    // The name of the log file, or empty if no log is open.
    const std::string& filename() const { return filename_; }

private:
    std::string filename_;
    std::ofstream out_;
};

} // namespace nutmeg

#endif // INSTRUCTION_LOG_HPP
