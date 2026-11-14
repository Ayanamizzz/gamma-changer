#pragma once

#include "gamma_types.h"

#include <cstddef>
#include <string>

namespace gamma_changer {

// Snaps every field into its valid range and onto the 0.01 grid the editor
// works in (I8). Non-finite values fall back to the neutral value.
CalibrationSettings quantize_settings(const CalibrationSettings& settings);

// The editor's calibration state. No Win32 dependency: the window mirrors
// `working` into its controls and feeds user input back through edit().
struct CalibrationSession {
    // What the editor shows and previews; the single source of truth for the
    // current adjustments. Always quantized.
    CalibrationSettings working{};
    // What is saved for the selected display/profile.
    CalibrationSettings committed_settings{};
    // working differs from committed_settings: a preview is pending auto-save.
    bool dirty = false;

    // One-step Ctrl+Z for profile switches and "Restore defaults".
    bool profile_switch_undo_available = false;
    std::wstring undo_display_id;
    std::size_t undo_preset = 0;
    GammaParams undo_params{};
    Profile undo_profile{};

    bool comparing_original = false;

    // Saved settings arrive (profile or display switch): show them, nothing pending.
    void load(const CalibrationSettings& settings);
    // The user changed an adjustment. Returns the new dirty state.
    bool edit(const CalibrationSettings& settings);
    // Show settings without deciding whether they are pending; the caller sets
    // dirty explicitly (a use case in the middle of a transaction).
    void show(const CalibrationSettings& settings);
    // The settings were saved and applied.
    void mark_committed(const CalibrationSettings& settings);

    void clear_undo();
    bool undo_applies_to(const std::wstring& display_id) const;
};

}  // namespace gamma_changer
