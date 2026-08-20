#pragma once

#include <windows.h>

namespace gamma_changer {

inline constexpr UINT kActivateExistingWindowMessage = WM_APP + 6;

class SingleInstanceLock {
public:
    SingleInstanceLock();
    ~SingleInstanceLock();
    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    bool is_primary() const { return primary_; }

private:
    HANDLE mutex_ = nullptr;
    bool primary_ = false;
};

bool activate_existing_window(const wchar_t* window_class);

}  // namespace gamma_changer
