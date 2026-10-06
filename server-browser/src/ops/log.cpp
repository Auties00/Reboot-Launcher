#include "ops/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace sb::log {

namespace {
std::atomic<int> g_min{static_cast<int>(Level::info)};
std::atomic<bool> g_json{false};
std::mutex g_mu;

const char* name(Level l) {
    switch (l) {
        case Level::debug: return "debug";
        case Level::info: return "info";
        case Level::warn: return "warn";
        case Level::error: return "error";
    }
    return "info";
}

void json_escape(std::string& out, std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\': out += "\\\\"; break;
            case '\n': out += "\n"; break;
            case '\r': out += "\r"; break;
            case '\t': out += "\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += std::format("\u{:04x}", static_cast<unsigned>(c));
                else out.push_back(c);
        }
    }
}
}  // namespace

void init(Level min, bool json) {
    g_min.store(static_cast<int>(min));
    g_json.store(json);
}

Level parse_level(std::string_view s) noexcept {
    if (s == "debug") return Level::debug;
    if (s == "warn" || s == "warning") return Level::warn;
    if (s == "error") return Level::error;
    return Level::info;
}

bool enabled(Level l) noexcept { return static_cast<int>(l) >= g_min.load(std::memory_order_relaxed); }

void write(Level l, std::string_view msg) noexcept {
    try {
        const auto now = std::chrono::system_clock::now();
        std::string line;
        if (g_json.load(std::memory_order_relaxed)) {
            line = std::format(R"({{"ts":"{:%FT%TZ}","level":"{}","msg":")", std::chrono::floor<std::chrono::milliseconds>(now), name(l));
            json_escape(line, msg);
            line += "\"}\n";
        } else {
            line = std::format("{:%FT%TZ} {:5} {}\n", std::chrono::floor<std::chrono::milliseconds>(now), name(l), msg);
        }
        std::lock_guard lk(g_mu);
        std::fwrite(line.data(), 1, line.size(), stderr);
    } catch (...) {
    }
}

}  // namespace sb::log
