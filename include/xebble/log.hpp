/// @file log.hpp
/// @brief Simple logging interface with a replaceable output callback.
///
/// By default all messages are written to `stderr` with a severity prefix.
/// Call `set_log_callback()` to redirect output — for example to a file, an
/// in-game console, or a GUI overlay. Pass `nullptr` to suppress all output.
///
/// ## Usage
///
/// @code
/// #include <xebble/log.hpp>
/// using namespace xebble;
///
/// // Emit messages at different severity levels.
/// log(LogLevel::Debug, "entering dungeon generation");
/// log(LogLevel::Info,  "loaded tileset: 256 tiles");
/// log(LogLevel::Warn,  "save file version mismatch — attempting upgrade");
/// log(LogLevel::Error, "failed to open archive: assets.zip");
/// @endcode
///
/// ## Redirecting output
///
/// @code
/// // Write everything to a file instead of stderr.
/// std::ofstream logfile("game.log");
/// set_log_callback([&logfile](LogLevel level, std::string_view msg) {
///     const char* tag = level == LogLevel::Error ? "ERROR"
///                     : level == LogLevel::Warn  ? "WARN "
///                     : level == LogLevel::Info  ? "INFO "
///                     :                            "DEBUG";
///     logfile << '[' << tag << "] " << msg << '\n';
/// });
///
/// // Silence all output (e.g. in unit tests).
/// set_log_callback(nullptr);
///
/// // Feed messages into an in-game console widget.
/// set_log_callback([&console](LogLevel level, std::string_view msg) {
///     console.append(level, msg);
/// });
/// @endcode
#pragma once

#include <xebble/types.hpp>
#include <functional>
#include <string_view>

namespace xebble {

/// @brief Signature for a log output callback.
///
/// Called once per `log()` invocation with the severity level and the message
/// text. The callback must not call `log()` recursively.
using LogCallback = std::function<void(LogLevel, std::string_view)>;

/// @brief Replace the active log handler with a custom callback.
///
/// Thread-safety: the callback pointer is stored globally. Avoid changing it
/// from multiple threads simultaneously.
///
/// @param callback  New handler, or `nullptr` to silence all output.
///
/// @code
/// // Restore default (stderr) output after redirecting during a test.
/// set_log_callback([](LogLevel level, std::string_view msg) {
///     // ... your handler ...
/// });
/// @endcode
void set_log_callback(LogCallback callback);

/// @brief Emit a log message at the given severity level.
///
/// The message is passed verbatim to the active log callback. No timestamp or
/// file/line information is added by Xebble — add those in your callback if
/// needed.
///
/// @param level    Severity classification.
/// @param message  Human-readable message text.
///
/// @code
/// log(LogLevel::Info,  "player entered room " + std::to_string(room_id));
/// log(LogLevel::Warn,  "entity " + std::to_string(e.id) + " has no sprite");
/// log(LogLevel::Error, result.error().message);
/// @endcode
void log(LogLevel level, std::string_view message);

} // namespace xebble
