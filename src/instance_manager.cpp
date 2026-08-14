#include "instance_manager.h"

namespace gamma_changer {

SingleInstanceLock::SingleInstanceLock() {
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\GammaChangerCpp.Gui.v2");
    primary_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstanceLock::~SingleInstanceLock() {
    if (mutex_) CloseHandle(mutex_);
}

bool activate_existing_window(const wchar_t* window_class) {
    HWND window = nullptr;
    for (int attempt = 0; attempt < 20 && !window; ++attempt) {
        window = FindWindowW(window_class, nullptr);
        if (!window) Sleep(25);
    }
    if (!window) return false;
    ShowWindow(window, SW_RESTORE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    FlashWindow(window, TRUE);
    return true;
}

}  // namespace gamma_changer
