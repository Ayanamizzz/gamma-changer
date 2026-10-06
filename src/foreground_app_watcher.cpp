#include "foreground_app_watcher.h"

namespace gamma_changer {

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
        PostMessageW(active_->target_, active_->message_, 0, 0);
    }
}

std::wstring foreground_executable_path(HWND window) {
    if (!window) return {};
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (!process_id) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried) return {};
    path.resize(length);
    return path;
}

}  // namespace gamma_changer
