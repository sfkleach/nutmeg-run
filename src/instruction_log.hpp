#ifndef INSTRUCTION_LOG_HPP
#define INSTRUCTION_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <fstream>
#include <string>
#include "value.hpp"

namespace nutmeg {

// How an inlined operand cell in the code stream is to be interpreted and rendered.
enum class OpArgKind {
    Raw,      // An uninterpreted 64-bit pattern: "0d<signed decimal>,0x<hex>".
    Tagged,   // A tagged value (see docs/specs/tagging-scheme.md): a description, or &0x<hex> for pointers.
    Pointer,  // An untagged C++ pointer: "&0x<hex>".
};
inline constexpr OpArgKind OP_RAW = OpArgKind::Raw;
inline constexpr OpArgKind OP_TAGGED = OpArgKind::Tagged;
inline constexpr OpArgKind OP_PTR = OpArgKind::Pointer;

// Renderings of a single operand (without the surrounding JSON quotes). None of them
// can contain a quote or backslash, so no JSON escaping is needed.
std::string format_raw(uint64_t bits);
std::string format_pointer(const void* ptr);
std::string format_tagged(Cell cell);

// Renders the operands that start at `operands`, one per kind, as a JSON array of strings.
std::string format_opargs(const Cell* operands, std::initializer_list<OpArgKind> kinds);

// The two halves of a log line. A line is
//   {"opcode": NAME, "opargs": [...], "onEntry": {...}, "onExit": {...}}
// and is written in two parts (see InstructionLog). NAME must be a plain identifier
// as no escaping is done. `opargs` is the text produced by format_opargs.
std::string format_entry(const char* name, const std::string& opargs, size_t stacklength);
std::string format_exit(size_t stacklength);

// Streams one JSON object per line (JSONL) to a file, one line for every
// instruction executed, for post-hoc debugging. The log is only created when
// ENABLE_INSTRUCTION_LOG is set in trace.hpp; otherwise this class does nothing.
class InstructionLog {
public:
    InstructionLog();

    InstructionLog(const InstructionLog&) = delete;
    InstructionLog& operator=(const InstructionLog&) = delete;

    // Called on entry to an instruction. `operands` points at the instruction's inlined
    // operands, which are described by `kinds`. Writes the first half of the line and
    // flushes it.
    void log_entry(const char* name, const Cell* operands, std::initializer_list<OpArgKind> kinds,
                   size_t stacklength);

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
