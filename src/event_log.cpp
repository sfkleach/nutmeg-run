#include "event_log.hpp"
#include "log_file.hpp"
#include <stdexcept>
#include <fmt/core.h>

namespace nutmeg {

EventField ev_int(const char* key, int64_t value) {
    return {key, std::to_string(value)};
}

EventField ev_bool(const char* key, bool value) {
    return {key, value ? "true" : "false"};
}

EventField ev_str(const char* key, const std::string& value) {
    return {key, json_quote(value)};
}

EventField ev_ptr(const char* key, const void* value) {
    return {key, fmt::format("\"&0x{:x}\"", reinterpret_cast<uintptr_t>(value))};
}

std::string format_event(uint64_t number, const char* name, uint64_t instruction,
                         std::initializer_list<EventField> fields) {
    std::string line = fmt::format("{{\"n\": {}, \"event\": \"{}\", \"instruction\": {}",
                                   number, name, instruction);
    for (const EventField& field : fields) {
        line += fmt::format(", \"{}\": {}", field.key, field.json);
    }
    return line + "}\n";
}

EventLog::EventLog() {
    filename_ = make_log_filename("events");
    out_.open(filename_);
    if (!out_) {
        throw std::runtime_error(fmt::format("Cannot create events log: {}", filename_));
    }
}

// Flushed after every event, for the same reason as the instruction log.
void EventLog::log(const char* name, std::initializer_list<EventField> fields) {
    out_ << format_event(++count_, name, last_instruction_number, fields) << std::flush;
}

EventLog& event_log() {
    static EventLog log;
    return log;
}

} // namespace nutmeg
