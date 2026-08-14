#pragma once

#include <string>

namespace gamma_changer {

enum class StartupState {
    disabled,
    enabled_current_path,
    enabled_stale_path,
    unavailable,
};

std::wstring current_executable_path(std::wstring& error);
StartupState startup_state(std::wstring& error);
bool set_startup_enabled(bool enabled, std::wstring& error);
bool repair_startup_registration(std::wstring& error);

}  // namespace gamma_changer
