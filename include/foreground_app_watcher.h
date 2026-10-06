#pragma once

#include <windows.h>

#include <string>

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

std::wstring foreground_executable_path(HWND window);

}  // namespace gamma_changer
