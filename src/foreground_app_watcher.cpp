#include "foreground_app_watcher.h"

#include "app_profile_rules.h"

#include <dwmapi.h>
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace gamma_changer {
namespace {

struct RunningApplicationEnumeration {
    DWORD excluded_process_id;
    std::vector<RunningApplication> applications;
    std::unordered_set<std::wstring> paths;
    std::unordered_set<DWORD> unavailable_processes;
};

BOOL CALLBACK collect_running_application(HWND window, LPARAM parameter) {
    auto& enumeration = *reinterpret_cast<RunningApplicationEnumeration*>(parameter);
    if (!IsWindowVisible(window) || window == GetShellWindow() || window == GetDesktopWindow() ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (!process || process == enumeration.excluded_process_id) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return TRUE;
    RunningApplication application;
    application.process_id = process;
    application.window_title.resize(static_cast<std::size_t>(length) + 1);
    const int copied = GetWindowTextW(window, application.window_title.data(), length + 1);
    if (copied <= 0) return TRUE;
    application.window_title.resize(static_cast<std::size_t>(copied));
    application.executable_path = foreground_executable_path(window, &application.error);
    const bool added = application.executable_path.empty()
        ? enumeration.unavailable_processes.insert(process).second
        : enumeration.paths.insert(normalize_executable_path(application.executable_path)).second;
    if (added) enumeration.applications.push_back(std::move(application));
    return TRUE;
}

}  // namespace

ForegroundAppWatcher* ForegroundAppWatcher::active_ = nullptr;

ForegroundAppWatcher::~ForegroundAppWatcher() {
    stop();
}

bool ForegroundAppWatcher::start(HWND target, UINT message) {
    if (active_ && active_ != this) return false;
    stop();
    target_ = target;
    message_ = message;
    active_ = this;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                          nullptr, on_foreground_changed, 0, 0,
                          WINEVENT_OUTOFCONTEXT);
    if (!hook_) {
        stop();
        return false;
    }
    return true;
}

void ForegroundAppWatcher::stop() {
    if (active_ == this) active_ = nullptr;
    if (hook_) UnhookWinEvent(hook_);
    hook_ = nullptr;
    target_ = nullptr;
    message_ = 0;
}

void CALLBACK ForegroundAppWatcher::on_foreground_changed(
    HWINEVENTHOOK hook, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (active_ && active_->hook_ == hook) {
        PostMessageW(active_->target_, active_->message_, 1, 0);
    }
}

std::wstring foreground_executable_path(HWND window, DWORD* error) {
    if (error) *error = ERROR_SUCCESS;
    if (!window) {
        if (error) *error = ERROR_INVALID_WINDOW_HANDLE;
        return {};
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (!process_id) {
        if (error) *error = ERROR_INVALID_WINDOW_HANDLE;
        return {};
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        if (error) *error = GetLastError();
        return {};
    }
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    const DWORD query_error = queried ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    if (!queried) {
        if (error) *error = query_error;
        return {};
    }
    path.resize(length);
    return path;
}

std::vector<RunningApplication> running_applications(DWORD excluded_process_id) {
    RunningApplicationEnumeration enumeration{excluded_process_id, {}, {}, {}};
    EnumWindows(collect_running_application, reinterpret_cast<LPARAM>(&enumeration));
    std::sort(enumeration.applications.begin(), enumeration.applications.end(),
              [](const RunningApplication& left, const RunningApplication& right) {
        const auto label = [](const RunningApplication& application) {
            const auto& path = application.executable_path;
            const auto separator = path.find_last_of(L"\\/");
            return path.empty() ? normalize_executable_path(application.window_title)
                : normalize_executable_path(path.substr(separator == std::wstring::npos ? 0 : separator + 1));
        };
        const std::wstring left_label = label(left);
        const std::wstring right_label = label(right);
        return left_label == right_label ? left.executable_path < right.executable_path : left_label < right_label;
    });
    return enumeration.applications;
}

}  // namespace gamma_changer
