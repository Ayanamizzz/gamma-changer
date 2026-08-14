#include "logger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace gamma_changer {
namespace {

std::filesystem::path log_path() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    const std::filesystem::path root = length > 0 && length < MAX_PATH
                                           ? std::filesystem::path(buffer) / L"GammaChangerCpp"
                                           : std::filesystem::current_path();
    std::error_code ignored;
    std::filesystem::create_directories(root, ignored);
    return root / L"gamma-changer.log";
}

const wchar_t* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::info: return L"INFO";
    case LogLevel::warning: return L"WARN";
    case LogLevel::error: return L"ERROR";
    }
    return L"INFO";
}

}  // namespace

void log_message(LogLevel level, const std::wstring& message) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto path = log_path();
    std::error_code ignored;
    const auto size = std::filesystem::file_size(path, ignored);
    if (!ignored && size > 1024 * 1024) {
        const auto previous = path.wstring() + L".1";
        std::filesystem::remove(previous, ignored);
        MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    std::wofstream output(path, std::ios::app);
    if (!output) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    output << std::setfill(L'0') << std::setw(4) << time.wYear << L'-'
           << std::setw(2) << time.wMonth << L'-' << std::setw(2) << time.wDay << L' '
           << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute << L':'
           << std::setw(2) << time.wSecond << L" [" << level_name(level) << L"] "
           << message << L'\n';
}

}  // namespace gamma_changer
