#include "startup_manager.h"

#include "app_version.h"

#include <windows.h>

#include <vector>

namespace gamma_changer {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring startup_command(const std::wstring& executable) {
    return L"\"" + executable + L"\" --startup";
}

}  // namespace

std::wstring current_executable_path(std::wstring& error) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            error = L"Windows could not locate Gamma Changer";
            return {};
        }
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) {
            error = L"Gamma Changer's executable path is too long";
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

StartupState startup_state(std::wstring& error) {
    HKEY key = nullptr;
    const LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return StartupState::disabled;
    if (opened != ERROR_SUCCESS) {
        error = L"Windows could not read the startup settings";
        return StartupState::unavailable;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS result = RegQueryValueExW(key, kApplicationName, nullptr, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return StartupState::disabled;
    }
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(key);
        error = L"The Gamma Changer startup entry is invalid";
        return StartupState::unavailable;
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    result = RegQueryValueExW(key, kApplicationName, nullptr, &type,
                              reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        error = L"Windows could not read the Gamma Changer startup command";
        return StartupState::unavailable;
    }

    std::wstring path_error;
    const std::wstring executable = current_executable_path(path_error);
    if (executable.empty()) {
        error = path_error;
        return StartupState::unavailable;
    }
    return _wcsicmp(value.data(), startup_command(executable).c_str()) == 0
               ? StartupState::enabled_current_path
               : StartupState::enabled_stale_path;
}

bool set_startup_enabled(bool enabled, std::wstring& error) {
    HKEY key = nullptr;
    const LSTATUS opened = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                            nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        error = L"Windows could not open the startup settings";
        return false;
    }
    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring executable = current_executable_path(error);
        if (executable.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = startup_command(executable);
        result = RegSetValueExW(key, kApplicationName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kApplicationName);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        error = L"Windows could not update the startup setting";
        return false;
    }
    return true;
}

bool repair_startup_registration(std::wstring& error) {
    const StartupState state = startup_state(error);
    if (state == StartupState::enabled_stale_path) return set_startup_enabled(true, error);
    return state != StartupState::unavailable;
}

}  // namespace gamma_changer
