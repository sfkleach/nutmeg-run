#ifndef INSTRUCTION_LOG_HPP
#define INSTRUCTION_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <fstream>
#include <string>
#include "heap.hpp"
#include "value.hpp"

namespace nutmeg {

// How an inlined operand cell in the code stream is to be interpreted and rendered.
enum class OpArgKind {
    Raw,      // An uninterpreted 64-bit pattern: "0d<signed decimal>,0x<hex>".
    Local,    // A frame offset naming a local variable: "local <index>".
    Tagged,   // A tagged value (see docs/specs/tagging-scheme.md).
    Pointer,  // An untagged C++ pointer: "&0x<hex>".
};
inline constexpr OpArgKind OP_RAW = OpArgKind::Raw;
inline constexpr OpArgKind OP_LOCAL = OpArgKind::Local;
inline constexpr OpArgKind OP_TAGGED = OpArgKind::Tagged;
inline constexpr OpArgKind OP_PTR = OpArgKind::Pointer;

// A rendered operand. If `key` is empty it is a description, written as a JSON string
// containing `text`. Otherwise it is a reference to a heap object, written as the object
// {"key": KEY, "value": TEXT} where KEY is the name of the object's type (its datakey).
struct OpArg {
    std::string key;
    std::string text;
};

// Renderings of a single operand. The text is unquoted and unescaped.
std::string format_raw(uint64_t bits);
std::string format_pointer(const void* ptr);

// `offset` is the frame offset planted in the code; `nlocals` is the number of locals of
// the function being executed, or -1 if unknown. The result is "local <index>", where
// index is the local's number as the compiler knows it (offset = nlocals - index + 2).
// If the index cannot be recovered the result is "local offset <offset>".
std::string format_local(uint64_t offset, int nlocals);

// Strings are shortened if they have more than MAX_STRING_CHARS characters (UTF-8 code
// points): the first KEPT_STRING_CHARS are kept and "..." is appended.
inline constexpr size_t MAX_STRING_CHARS = 16;
inline constexpr size_t KEPT_STRING_CHARS = 13;
std::string shorten_string(const std::string& text);

// Tagged values: ints, floats and special literals are described; a pointer to a heap
// object is {key: type name, value: contents}, where the contents of a string are the
// (shortened) text and the contents of any other object are its address. A pointer that
// does not point into the heap is never followed and has key "unknown".
OpArg format_tagged(Cell cell, Heap& heap);

// Renders the operands that start at `operands`, one per kind, as a JSON array.
std::string format_opargs(const Cell* operands, std::initializer_list<OpArgKind> kinds,
                          Heap& heap, int nlocals);

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
    // operands, which are described by `kinds`; `heap` is needed to interpret pointers and
    // `nlocals` (or -1 if unknown) to interpret local variables. Writes the first half of
    // the line and flushes it.
    void log_entry(const char* name, const Cell* operands, std::initializer_list<OpArgKind> kinds,
                   Heap& heap, int nlocals, size_t stacklength);

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
