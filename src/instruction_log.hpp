#ifndef INSTRUCTION_LOG_HPP
#define INSTRUCTION_LOG_HPP

#include <cstddef>
#include <fstream>
#include <string>

namespace nutmeg {

// The two halves of a log line. A line is {"opcode": NAME, "onEntry": {...}, "onExit": {...}}
// and is written in two parts (see InstructionLog). NAME must be a plain identifier
// as no escaping is done.
std::string format_entry(const char* name, size_t stacklength);
std::string format_exit(size_t stacklength);

// Streams one JSON object per line (JSONL) to a file, one line for every
// instruction executed, for post-hoc debugging. The log is only created when
// ENABLE_INSTRUCTION_LOG is set in trace.hpp; otherwise this class does nothing.
class InstructionLog {
public:
    InstructionLog();

    InstructionLog(const InstructionLog&) = delete;
    InstructionLog& operator=(const InstructionLog&) = delete;

    // Called on entry to an instruction. Writes the first half of the line and flushes it.
    void log_entry(const char* name, size_t stacklength);

    // Called on exit from the instruction. Writes the second half of the line and flushes it.
    //
    // The two halves are written separately so that the instruction that was executing
    // when the machine crashed or threw is in the log. The cost is that, after a failure,
    // the final line has "onEntry" but no "onExit" and so is not valid JSON.
    void log_exit(size_t stacklength);

    // The name of the log file, or empty if no log is open.
    const std::string& filename() const { return filename_; }

private:
    std::string filename_;
    std::ofstream out_;
};

} // namespace nutmeg

#endif // INSTRUCTION_LOG_HPP
