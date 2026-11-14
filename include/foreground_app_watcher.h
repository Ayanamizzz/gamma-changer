#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace gamma_changer {

class ForegroundAppWatcher {
public:
    ForegroundAppWatcher() = default;
    ~ForegroundAppWatcher();
    ForegroundAppWatcher(const ForegroundAppWatcher&) = delete;
    ForegroundAppWatcher& operator=(const ForegroundAppWatcher&) = delete;

    bool start(HWND target, UINT message);
    void stop();

private:
    static void CALLBACK on_foreground_changed(HWINEVENTHOOK hook, DWORD event,
                                               HWND window, LONG object_id,
                                               LONG child_id, DWORD event_thread,
                                               DWORD event_time);
    static ForegroundAppWatcher* active_;
    HWINEVENTHOOK hook_ = nullptr;
    HWND target_ = nullptr;
    UINT message_ = 0;
};

std::wstring foreground_executable_path(HWND window, DWORD* error = nullptr);

// The window in front right now, as "Follow apps" needs it.
struct ForegroundApp {
    bool has_window = false;
    bool own_process = false;      // a window of this process
    std::wstring executable_path;  // empty for this process or when the lookup failed
    DWORD error = ERROR_SUCCESS;   // why the lookup failed (other processes only)
};

ForegroundApp current_foreground_app();

struct RunningApplication {
    std::wstring executable_path;
    std::wstring window_title;
    DWORD process_id = 0;
    DWORD error = ERROR_SUCCESS;
};

// One entry per executable with an open or minimized desktop window.
std::vector<RunningApplication> running_applications(DWORD excluded_process_id = GetCurrentProcessId());

}  // namespace gamma_changer
