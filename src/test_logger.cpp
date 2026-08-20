#include "logger.h"

namespace gamma_changer {

// Automated checks intentionally avoid writing to the user's real
// %LOCALAPPDATA% log. Production targets continue to link logger.cpp.
void log_message(LogLevel, const std::wstring&) {}

}  // namespace gamma_changer
