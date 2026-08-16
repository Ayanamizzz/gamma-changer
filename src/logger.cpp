#include "logger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace gamma_changer {
namespace {

HANDLE log_write_mutex() {
    static HANDLE mutex = CreateMutexW(nullptr, FALSE,
                                       L"Local\\GammaChangerCpp.Log.v1");
    return mutex;
}

class LogWriteLock {
public:
    LogWriteLock() : mutex_(log_write_mutex()) {
        if (mutex_ != nullptr) {
            const DWORD wait = WaitForSingleObject(mutex_, INFINITE);
            locked_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~LogWriteLock() {
        if (mutex_ != nullptr && locked_) ReleaseMutex(mutex_);
    }
    LogWriteLock(const LogWriteLock&) = delete;
    LogWriteLock& operator=(const LogWriteLock&) = delete;

private:
    HANDLE mutex_ = nullptr;
    bool locked_ = false;
};

std::filesystem::path log_path() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const auto root = std::filesystem::path(buffer) / L"GammaChangerCpp";
        std::error_code ignored;
        std::filesystem::create_directories(root, ignored);
        return root / L"gamma-changer.log";
    }
    const DWORD temp_length = GetTempPathW(MAX_PATH, buffer);
    if (temp_length > 0 && temp_length < MAX_PATH) {
        const auto root = std::filesystem::path(buffer) / L"GammaChangerCpp";
        std::error_code ignored;
        std::filesystem::create_directories(root, ignored);
        return root / L"gamma-changer.log";
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        const auto root = cwd / L"GammaChangerCpp";
        std::error_code ignored;
        std::filesystem::create_directories(root, ignored);
        return root / L"gamma-changer.log";
    }
    return L"gamma-changer.log";
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
    LogWriteLock lock;
    const auto path = log_path();
    std::error_code ignored;
    const auto size = std::filesystem::file_size(path, ignored);
    if (!ignored && size > 1024 * 1024) {
        const auto previous = path.wstring() + L".1";
        std::filesystem::remove(previous, ignored);
        if (!MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            // Rotation failed (for example another tool has the log open). Truncate
            // in place so the log cannot grow without bound.
            std::filesystem::resize_file(path, 0, ignored);
        }
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
