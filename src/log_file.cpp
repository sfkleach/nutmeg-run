#include "log_file.hpp"
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <fmt/core.h>

namespace nutmeg {

std::string make_log_filename(const std::string& log_type) {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local;
    localtime_r(&t, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H.%M.%S", &local);
    return fmt::format("{}.{}.jsonl", log_type, stamp);
}

std::string json_quote(const std::string& text) {
    return nlohmann::json(text).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace nutmeg
