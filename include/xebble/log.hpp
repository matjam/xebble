/// @file log.hpp
/// @brief Logging interface for Xebble.
///
/// Provides a simple logging system with configurable callback. By default,
/// log messages are written to stderr with a severity prefix. Use
/// set_log_callback() to redirect log output (e.g. to a file or in-game console).
#pragma once

#include <xebble/types.hpp>
#include <functional>
#include <string_view>

namespace xebble {

/// @brief Callback type for log message handling.
using LogCallback = std::function<void(LogLevel, std::string_view)>;

/// @brief Replace the default log handler with a custom callback.
/// @param callback The new log handler. Pass nullptr to silence all output.
void set_log_callback(LogCallback callback);

/// @brief Emit a log message at the given severity level.
/// @param level Severity of the message.
/// @param message The log text.
void log(LogLevel level, std::string_view message);

} // namespace xebble
