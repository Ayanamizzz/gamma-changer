#pragma once

#include "gamma_types.h"

#include <cstddef>
#include <string>

namespace gamma_changer {

// Pure UI-facing calibration state. This object deliberately has no Win32
// dependency; the window layer mirrors it into controls.
struct CalibrationSession {
    CalibrationSettings committed_settings{};
    bool dirty = false;

    // One-step Ctrl+Z for profile switches and "Restore defaults".
    bool profile_switch_undo_available = false;
    std::wstring undo_display_id;
    std::size_t undo_preset = 0;
    GammaParams undo_params{};
    Profile undo_profile{};
    bool undo_profile_linked = false;

    bool comparing_original = false;

    void clear_undo();
    bool undo_applies_to(const std::wstring& display_id) const;
    void mark_clean();
};

}  // namespace gamma_changer
