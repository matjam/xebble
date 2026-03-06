#pragma once

#include <xebble/types.hpp>
#include <functional>
#include <string_view>

namespace xebble {

using LogCallback = std::function<void(LogLevel, std::string_view)>;

void set_log_callback(LogCallback callback);
void log(LogLevel level, std::string_view message);

} // namespace xebble
