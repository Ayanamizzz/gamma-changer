#include "logger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

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
            const DWORD wait = WaitForSingleObject(mutex_, 250);
            locked_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~LogWriteLock() {
        if (mutex_ != nullptr && locked_) ReleaseMutex(mutex_);
    }
    LogWriteLock(const LogWriteLock&) = delete;
    LogWriteLock& operator=(const LogWriteLock&) = delete;
    bool acquired() const { return locked_; }

private:
    HANDLE mutex_ = nullptr;
    bool locked_ = false;
};

std::filesystem::path log_path() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 1) {
        std::vector<wchar_t> buffer(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
        const auto root = length > 0 && length < buffer.size()
                              ? std::filesystem::path(std::wstring(buffer.data(), length)) /
                                    L"GammaChangerCpp"
                              : std::filesystem::path{};
        if (!root.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(root, ignored);
            return root / L"gamma-changer.log";
        }
    }
    std::error_code temp_error;
    const auto temp = std::filesystem::temp_directory_path(temp_error);
    if (!temp_error) {
        const auto root = temp / L"GammaChangerCpp";
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

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), bytes, nullptr, nullptr) != bytes) {
        return {};
    }
    return result;
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
    if (!lock.acquired()) return;
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
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wostringstream line;
    line << std::setfill(L'0') << std::setw(4) << time.wYear << L'-'
         << std::setw(2) << time.wMonth << L'-' << std::setw(2) << time.wDay << L' '
         << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute << L':'
         << std::setw(2) << time.wSecond << L" [" << level_name(level) << L"] "
         << message << L'\n';
    const std::string bytes = to_utf8(line.str());
    if (bytes.empty()) return;
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) return;
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace gamma_changer
