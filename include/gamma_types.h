#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gamma_changer {

constexpr std::size_t kRampSize = 256;
constexpr std::size_t kPresetCount = 4;

struct CalibrationRange {
    double minimum;
    double maximum;
    double default_value;
};

namespace calibration_ranges {

inline constexpr CalibrationRange gamma{0.30, 4.40, 1.00};
inline constexpr CalibrationRange brightness{-1.00, 1.00, 0.00};
inline constexpr CalibrationRange contrast{0.10, 3.00, 1.00};
inline constexpr CalibrationRange gain{0.01, 2.00, 1.00};

}  // namespace calibration_ranges

struct CalibrationSettings {
    double gamma = 1.0;
    double brightness = 0.0;
    double contrast = 1.0;
    double r_gain = 1.0;
    double g_gain = 1.0;
    double b_gain = 1.0;
};

// Transitional alias: existing configuration files and UI code keep their
// current behavior while the application moves to the calibration model.
using GammaParams = CalibrationSettings;

struct PresetSlot {
    bool occupied = false;
    std::wstring name;
    GammaParams params{};
};

struct Profile {
    std::wstring id;
    std::wstring name;
    CalibrationSettings settings{};
    bool saved = false;
};

struct DisplayProfilePreference {
    std::wstring display_id;
    std::wstring profile_id;
};

struct GammaRamp {
    std::array<std::array<std::uint16_t, kRampSize>, 3> channel{};
};

struct DisplayInfo {
    std::wstring device_name;  // e.g. \\.\DISPLAY1
    std::wstring stable_id;    // DisplayConfig monitor device path when available.
    std::wstring device_string;
    std::uint32_t state_flags = 0;
    int width = 0;
    int height = 0;
    int refresh_rate = 0;
    bool primary = false;
    bool hdr_active = false;
};

// Single storage identity used by profile, ramp, and undo code. Prefer the
// stable monitor path and fall back to the GDI device name only when Windows
// does not expose a stable path.
inline std::wstring display_storage_id(const DisplayInfo& display) {
    return display.stable_id.empty() ? display.device_name : display.stable_id;
}

}  // namespace gamma_changer
