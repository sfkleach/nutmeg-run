#ifndef LOG_FILE_HPP
#define LOG_FILE_HPP

#include <cstdint>
#include <string>

namespace nutmeg {

// Log files are named {LOG_TYPE}.{YYYY-MM-DD-HH.MM.SS}.jsonl, so that they are
// grouped by function and then sorted by (local) date-time.
std::string make_log_filename(const std::string& log_type);

// A JSON string literal, quoted and escaped. Invalid UTF-8 is replaced rather than thrown on,
// so the log stays valid JSON.
std::string json_quote(const std::string& text);

// The number ("n") of the most recent line written to an instruction log, or 0 if none yet.
// The events log reports it so that events can be placed in the instruction stream.
inline uint64_t last_instruction_number = 0;

} // namespace nutmeg

#endif // LOG_FILE_HPP
