#include "instance_manager.h"

#include "logger.h"

namespace gamma_changer {

SingleInstanceLock::SingleInstanceLock() {
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\GammaChangerCpp.Gui.v2");
    primary_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    if (mutex_ == nullptr) {
        log_message(LogLevel::error,
                    L"Could not create the single-instance mutex; the application will exit");
    }
}

SingleInstanceLock::~SingleInstanceLock() {
    if (mutex_) CloseHandle(mutex_);
}

bool activate_existing_window(const wchar_t* window_class) {
    HWND window = nullptr;
    // The primary process owns the mutex before it finishes creating the Win32
    // window. Allow slow profile/display initialization to reach the activation point.
    for (int attempt = 0; attempt < 80 && !window; ++attempt) {
        window = FindWindowW(window_class, nullptr);
        if (!window) Sleep(25);
    }
    if (!window) return false;
    DWORD primary_process_id = 0;
    GetWindowThreadProcessId(window, &primary_process_id);
    if (primary_process_id != 0) {
        // The manually launched secondary process normally owns foreground
        // activation rights. Hand them to the primary before asking its UI
        // thread to show the window, otherwise SetForegroundWindow in the
        // primary can be rejected and the restored window may stay behind the
        // user's current application.
        AllowSetForegroundWindow(primary_process_id);
    }
    // Queue activation onto the primary UI thread. If the secondary launch
    // arrives while the primary is still inside WM_CREATE, this message is
    // handled after wWinMain has made its startup hide/show decision and cannot
    // be overwritten by the --startup SW_HIDE path.
    return PostMessageW(window, kActivateExistingWindowMessage, 0, 0) != FALSE;
}

}  // namespace gamma_changer
