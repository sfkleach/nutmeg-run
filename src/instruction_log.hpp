#ifndef INSTRUCTION_LOG_HPP
#define INSTRUCTION_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <fstream>
#include <optional>
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
    Global,   // An Ident* (a global binding): "fn <name>" or "global <name>".
    Function, // An untagged pointer to a function object: "fn <name>".
    Syscall,  // A pointer to a built-in function: "sys <name>".
};
inline constexpr OpArgKind OP_RAW = OpArgKind::Raw;
inline constexpr OpArgKind OP_LOCAL = OpArgKind::Local;
inline constexpr OpArgKind OP_TAGGED = OpArgKind::Tagged;
inline constexpr OpArgKind OP_PTR = OpArgKind::Pointer;
inline constexpr OpArgKind OP_GLOBAL = OpArgKind::Global;
inline constexpr OpArgKind OP_FUNCTION = OpArgKind::Function;
inline constexpr OpArgKind OP_SYSCALL = OpArgKind::Syscall;

// What is known about a global binding: its name, and whether it is a function (as opposed
// to a lazily evaluated constant, or a value that is not a function).
struct GlobalInfo {
    std::string name;
    bool is_function;
};

// Looks up the names of things, which the log would otherwise have to show as addresses.
// Implemented by the Machine; tests supply a fake. Each lookup returns nothing if the
// address is not known, and must never dereference an address it has not recognised.
class NameResolver {
public:
    virtual ~NameResolver() = default;

    // `ident` is an operand that should be an Ident*.
    virtual std::optional<GlobalInfo> global_at(const void* ident) const = 0;

    // The name a (non-lazy) global is bound to whose value is this function object.
    virtual std::optional<std::string> function_name(const Cell* function_object) const = 0;

    // The name of a built-in function.
    virtual std::optional<std::string> sys_function_name(const void* function) const = 0;
};

// Everything the operand formatters need to interpret a cell.
struct OpArgContext {
    Heap& heap;                  // To recognise and read heap objects.
    int nlocals;                 // Locals of the function being executed, or -1 if unknown.
    const NameResolver& names;   // To name globals, functions and built-ins.
};

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

// Operands that refer to something with a name. Each falls back to the plain address when
// the name is not available.
//   format_global: "fn NAME" if `ident` is a global holding a function, "global NAME" for any
//                  other global (e.g. a lazy constant).
//   format_function: "fn NAME" for a function object that is the value of a global.
//   format_syscall: "sys NAME" for a built-in.
std::string format_global(const void* ident, const NameResolver& names);
std::string format_function(const Cell* function_object, const NameResolver& names);
std::string format_syscall(const void* function, const NameResolver& names);

// Tagged values: ints, floats and special literals are described; a pointer to a heap
// object is {key: type name, value: contents}, where the contents of a string are the
// (shortened) text and the contents of any other object are its address. A pointer to a
// function that is the value of a global is described as "fn NAME" instead. A pointer that
// does not point into the heap is never followed and has key "unknown".
OpArg format_tagged(Cell cell, const OpArgContext& context);

// Renders the operands that start at `operands`, one per kind, as a JSON array.
std::string format_opargs(const Cell* operands, std::initializer_list<OpArgKind> kinds,
                          const OpArgContext& context);

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
    // operands, which are described by `kinds` and interpreted using `context`. Writes the
    // first half of the line and flushes it.
    void log_entry(const char* name, const Cell* operands, std::initializer_list<OpArgKind> kinds,
                   const OpArgContext& context, size_t stacklength);

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
