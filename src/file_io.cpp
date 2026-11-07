#include "file_io.h"

#include <windows.h>

#include <fstream>
#include <iterator>

namespace gamma_changer::file_io {
namespace {

HANDLE store_write_mutex() {
    static HANDLE mutex = CreateMutexW(nullptr, FALSE,
                                       L"Local\\GammaChangerCpp.StoreWrites.v1");
    return mutex;
}

// Serializes writers across processes (a second instance, an old version that
// is still shutting down). Readers do not lock: atomic replacement means they
// always observe a complete file.
class StoreWriteLock {
public:
    StoreWriteLock() : mutex_(store_write_mutex()) {
        if (mutex_ != nullptr) {
            const DWORD wait = WaitForSingleObject(mutex_, INFINITE);
            locked_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~StoreWriteLock() {
        if (mutex_ != nullptr && locked_) ReleaseMutex(mutex_);
    }
    StoreWriteLock(const StoreWriteLock&) = delete;
    StoreWriteLock& operator=(const StoreWriteLock&) = delete;

private:
    HANDLE mutex_ = nullptr;
    bool locked_ = false;
};

void remove_quietly(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

}  // namespace

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return std::string{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::string{};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), bytes, nullptr, nullptr) != bytes) {
        return std::string{};
    }
    return result;
}

std::wstring from_utf8(const std::string& value) {
    if (value.empty()) return std::wstring{};
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (chars <= 0) return std::wstring{};
    std::wstring result(static_cast<std::size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), chars) != chars) {
        return std::wstring{};
    }
    return result;
}

bool file_exceeds(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    return !ec && size > limit;
}

bool read_bytes(const std::filesystem::path& path, std::string& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad();
}

bool read_utf8_text(const std::filesystem::path& path, std::wstring& text) {
    std::string bytes;
    if (!read_bytes(path, bytes)) return false;
    if (bytes.empty()) {
        text.clear();
        return true;
    }
    text = from_utf8(bytes);
    return !text.empty();
}

bool write_atomically(const std::filesystem::path& target, const std::string& bytes,
                      const wchar_t* description, std::wstring& error) {
    StoreWriteLock lock;
    const std::filesystem::path temporary = target.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = L"cannot write " + std::wstring(description) + L" file";
            // Clear a stale or foreign temp entry so the next attempt can succeed.
            remove_quietly(temporary);
            return false;
        }
        if (!bytes.empty()) {
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        output.close();
        if (!output) {
            error = L"failed while writing " + std::wstring(description) + L" file";
            remove_quietly(temporary);
            return false;
        }
    }

    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE || !FlushFileBuffers(file)) {
        const DWORD code = GetLastError();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        error = L"cannot flush " + std::wstring(description) + L" file (Win32 error " +
                std::to_wstring(code) + L")";
        remove_quietly(temporary);
        return false;
    }
    CloseHandle(file);
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = L"cannot replace " + std::wstring(description) + L" file (Win32 error " +
            std::to_wstring(GetLastError()) + L")";
    remove_quietly(temporary);
    return false;
}

bool write_utf8_atomically(const std::filesystem::path& target, const std::wstring& text,
                           const wchar_t* description, std::wstring& error) {
    const std::string bytes = to_utf8(text);
    if (!text.empty() && bytes.empty()) {
        error = L"cannot encode " + std::wstring(description) + L" as UTF-8";
        return false;
    }
    return write_atomically(target, bytes, description, error);
}

}  // namespace gamma_changer::file_io
