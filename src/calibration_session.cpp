#include "calibration_session.h"

#include "gamma_lut.h"

#include <algorithm>
#include <cmath>

namespace gamma_changer {
namespace {

double quantize(double value, const CalibrationRange& range) {
    if (!std::isfinite(value)) value = range.default_value;
    value = std::clamp(value, range.minimum, range.maximum);
    return steps_to_setting(setting_to_steps(value));
}

}  // namespace

CalibrationSettings quantize_settings(const CalibrationSettings& settings) {
    CalibrationSettings result;
    result.gamma = quantize(settings.gamma, calibration_ranges::gamma);
    result.brightness = quantize(settings.brightness, calibration_ranges::brightness);
    result.contrast = quantize(settings.contrast, calibration_ranges::contrast);
    result.r_gain = quantize(settings.r_gain, calibration_ranges::gain);
    result.g_gain = quantize(settings.g_gain, calibration_ranges::gain);
    result.b_gain = quantize(settings.b_gain, calibration_ranges::gain);
    return result;
}

void CalibrationSession::load(const CalibrationSettings& settings) {
    working = quantize_settings(settings);
    committed_settings = working;
    dirty = false;
}

bool CalibrationSession::edit(const CalibrationSettings& settings) {
    working = quantize_settings(settings);
    dirty = !settings_equal(working, committed_settings);
    return dirty;
}

void CalibrationSession::show(const CalibrationSettings& settings) {
    working = quantize_settings(settings);
}

void CalibrationSession::mark_committed(const CalibrationSettings& settings) {
    // Same grid as working, so a legacy off-grid value saved on disk never
    // reads as a pending change.
    committed_settings = quantize_settings(settings);
    dirty = false;
}

void CalibrationSession::clear_undo() {
    profile_switch_undo_available = false;
    undo_display_id.clear();
}

bool CalibrationSession::undo_applies_to(const std::wstring& display_id) const {
    return profile_switch_undo_available && undo_display_id == display_id;
}

}  // namespace gamma_changer
