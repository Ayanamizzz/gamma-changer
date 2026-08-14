#pragma once

#include <windows.h>

namespace gamma_changer {

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
