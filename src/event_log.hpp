#ifndef EVENT_LOG_HPP
#define EVENT_LOG_HPP

#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <string>
#include "trace.hpp"

namespace nutmeg {

// One field of an event: a key and its value, already rendered as JSON.
struct EventField {
    const char* key;
    std::string json;
};

// Constructors for the field values.
EventField ev_int(const char* key, int64_t value);
EventField ev_bool(const char* key, bool value);
EventField ev_str(const char* key, const std::string& value);
EventField ev_ptr(const char* key, const void* value);  // "&0x<hex>", as in the instruction log.

// A line of the events log is
//   {"n": N, "event": NAME, "instruction": I, <fields>...}
// where N is the 1-based event number (the line number), and I is the number of the last
// instruction written to the instruction log (0 if none, or if that log is disabled), which
// places the event in the instruction stream. NAME must be a plain identifier.
std::string format_event(uint64_t number, const char* name, uint64_t instruction,
                         std::initializer_list<EventField> fields);

// Streams one JSON object per line (JSONL) to events.{timestamp}.jsonl, recording the
// landmarks of a run: loading, the entry point, launch, halt and memory allocations.
// Only created when ENABLE_EVENTS_LOG is set in trace.hpp.
class EventLog {
public:
    EventLog();

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;

    // Writes one complete line and flushes it.
    void log(const char* name, std::initializer_list<EventField> fields);

    const std::string& filename() const { return filename_; }

private:
    std::string filename_;
    std::ofstream out_;
    uint64_t count_ = 0;
};

// The process-wide events log, created on first use. It is process-wide because memory is
// allocated (the datakeys) before there is anything to pass it to. Only call it under
// `if constexpr (ENABLE_EVENTS_LOG)`, which LOG_EVENT does.
EventLog& event_log();

// Records an event. With ENABLE_EVENTS_LOG off this expands to nothing at all: the fields
// are not evaluated and no code is emitted, even at -O0.
//   LOG_EVENT("allocate", ev_ptr("address", p), ev_int("cells", n));
#define LOG_EVENT(NAME, ...) \
    do { \
        if constexpr (::nutmeg::ENABLE_EVENTS_LOG) { \
            ::nutmeg::event_log().log(NAME, {__VA_ARGS__}); \
        } \
    } while (0)

} // namespace nutmeg

#endif // EVENT_LOG_HPP
