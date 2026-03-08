#include <xebble/log.hpp>

#include <cstdio>

namespace xebble {

namespace {
LogCallback g_log_callback = [](LogLevel level, std::string_view msg) {
    const char* prefix = "???";
    switch (level) {
    case LogLevel::Debug:
        prefix = "DEBUG";
        break;
    case LogLevel::Info:
        prefix = "INFO";
        break;
    case LogLevel::Warn:
        prefix = "WARN";
        break;
    case LogLevel::Error:
        prefix = "ERROR";
        break;
    }
    std::fprintf(stderr, "[xebble:%s] %.*s\n", prefix, static_cast<int>(msg.size()), msg.data());
};
} // namespace

void set_log_callback(LogCallback callback) {
    g_log_callback = std::move(callback);
}

void log(LogLevel level, std::string_view message) {
    if (g_log_callback) {
        g_log_callback(level, message);
    }
}

} // namespace xebble
