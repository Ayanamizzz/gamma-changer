#pragma once

#include <string>

namespace gamma_changer {

enum class LogLevel {
    info,
    warning,
    error,
};

void log_message(LogLevel level, const std::wstring& message);

}  // namespace gamma_changer
