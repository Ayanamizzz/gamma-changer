#include "calibration_controller.h"

#include "logger.h"

namespace gamma_changer {
namespace {

const std::wstring& storage_id(const DisplayInfo& display) {
    return display.stable_id.empty() ? display.device_name : display.stable_id;
}

}  // namespace

CalibrationController::CalibrationController(ProfileStore& store)
    : CalibrationController(store, system_display_ramp_backend()) {}

CalibrationController::CalibrationController(ProfileStore& store,
                                             DisplayRampBackend& ramp_backend)
    : store_(store), ramp_backend_(ramp_backend) {}

CalibrationSettings CalibrationController::load_settings(const DisplayInfo& display) const {
    CalibrationSettings settings;
    if (store_.try_load_params(storage_id(display), settings)) return settings;
    if (!display.stable_id.empty() && display.stable_id != display.device_name &&
        store_.try_load_params(display.device_name, settings)) {
        return settings;
    }
    return default_params();
}

bool CalibrationController::ensure_original_ramp(const DisplayInfo& display,
                                                  std::wstring& error) {
    const std::wstring id = storage_id(display);
    if (captured_base_ramps_.find(id) != captured_base_ramps_.end()) return true;
    GammaRamp original;
    if (store_.load_base_ramp(id, original)) {
        captured_base_ramps_.insert(id);
        return true;
    }
    if (!display.stable_id.empty() && display.stable_id != display.device_name &&
        store_.load_base_ramp(display.device_name, original)) {
        if (!store_.save_base_ramp(id, original, error)) return false;
        captured_base_ramps_.insert(id);
        return true;
    }
    if (!ramp_backend_.read(display, original, error)) return false;
    if (!store_.save_base_ramp(id, original, error)) return false;
    captured_base_ramps_.insert(id);
    return true;
}

bool CalibrationController::preview(const DisplayInfo& display,
                                    const CalibrationSettings& settings,
                                    std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    if (!ensure_original_ramp(display, error)) return false;

    if (preview_active_ && preview_display_id_ != display.device_name) {
        error = L"Finish or discard the active preview before changing displays";
        return false;
    }
    if (!preview_active_) {
        if (!ramp_backend_.read(display, preview_origin_, error)) return false;
        preview_display_id_ = display.device_name;
        preview_active_ = true;
    }

    const GammaRamp ramp = lut_generator_.generate(settings);
    return ramp_backend_.write(display, ramp, error);
}

bool CalibrationController::cancel_preview(const DisplayInfo& display,
                                           std::wstring& error) {
    if (!preview_active_) return true;
    if (preview_display_id_ != display.device_name) {
        error = L"The active preview belongs to a different display";
        return false;
    }
    if (!ramp_backend_.write(display, preview_origin_, error)) return false;
    preview_active_ = false;
    preview_display_id_.clear();
    return true;
}

void CalibrationController::abandon_preview() {
    preview_active_ = false;
    preview_display_id_.clear();
}

bool CalibrationController::apply_and_save(const DisplayInfo& display,
                                           const CalibrationSettings& settings,
                                           std::wstring& error) {
    const CommitResult result = commit(display, settings);
    if (!result.succeeded()) {
        error = result.error;
        return false;
    }
    return true;
}

CommitResult CalibrationController::commit(const DisplayInfo& display,
                                           const CalibrationSettings& settings) {
    std::wstring validation_error;
    if (!lut_generator_.validate(settings, validation_error)) {
        return {CommitStatus::rolled_back, validation_error};
    }
    if (!ensure_original_ramp(display, validation_error)) {
        return {CommitStatus::rolled_back, validation_error};
    }

    GammaRamp previous_ramp{};
    if (!ramp_backend_.read(display, previous_ramp, validation_error)) {
        return {CommitStatus::rolled_back,
                L"Could not capture the current display state: " + validation_error};
    }
    CalibrationSettings previous_settings{};
    const bool had_previous_settings =
        store_.try_load_params(storage_id(display), previous_settings);

    const GammaRamp ramp = lut_generator_.generate(settings);
    std::wstring apply_error;
    if (!ramp_backend_.write(display, ramp, apply_error)) {
        return {CommitStatus::rolled_back, apply_error};
    }

    std::wstring save_error;
    if (!store_.save_params(storage_id(display), settings, save_error)) {
        std::wstring rollback_error;
        const bool ramp_restored = ramp_backend_.write(display, previous_ramp, rollback_error);
        if (had_previous_settings) {
            std::wstring settings_rollback_error;
            if (!store_.save_params(storage_id(display), previous_settings,
                                    settings_rollback_error)) {
                if (!rollback_error.empty()) rollback_error += L"; ";
                rollback_error += L"settings rollback failed: " + settings_rollback_error;
            }
        }
        if (!ramp_restored || !rollback_error.empty()) {
            return {CommitStatus::rollback_failed,
                    L"Save failed: " + save_error + L"; rollback failed: " + rollback_error};
        }
        return {CommitStatus::rolled_back, L"Save failed; display was restored: " + save_error};
    }

    if (preview_active_ && preview_display_id_ == display.device_name) {
        preview_active_ = false;
        preview_display_id_.clear();
    }
    GammaRamp verified{};
    std::wstring verification_error;
    if (!ramp_backend_.read(display, verified, verification_error)) {
        log_message(LogLevel::warning, L"Ramp verification unavailable on " +
                                       display.device_name + L": " + verification_error);
    } else if (verified.channel != ramp.channel) {
        log_message(LogLevel::warning, L"Display driver adjusted the requested ramp on " +
                                       display.device_name);
    }
    log_message(LogLevel::info, L"Calibration committed on " + display.device_name);
    return {CommitStatus::success, {}};
}

bool CalibrationController::save_for_offline_display(
    const DisplayInfo& display, const CalibrationSettings& settings,
    std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    return store_.save_params(storage_id(display), settings, error);
}

bool CalibrationController::reapply_committed(const DisplayInfo& display,
                                              const CalibrationSettings& settings,
                                              std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    const GammaRamp ramp = lut_generator_.generate(settings);
    if (!ramp_backend_.write(display, ramp, error)) return false;
    log_message(LogLevel::info, L"Committed calibration restored on " + display.device_name);
    return true;
}

bool CalibrationController::restore_original(const DisplayInfo& display,
                                             std::wstring& error) {
    GammaRamp original;
    if (!store_.load_base_ramp(storage_id(display), original)) {
        error = L"No saved base ramp for this display";
        return false;
    }
    if (!ramp_backend_.write(display, original, error)) return false;
    if (!store_.save_params(storage_id(display), default_params(), error)) return false;
    if (preview_active_ && preview_display_id_ == display.device_name) {
        preview_active_ = false;
        preview_display_id_.clear();
    }
    return true;
}

GammaRamp CalibrationController::generate_preview(
    const CalibrationSettings& settings) const {
    return lut_generator_.generate(settings);
}

}  // namespace gamma_changer
