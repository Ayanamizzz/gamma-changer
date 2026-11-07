#include "calibration_controller.h"

#include "logger.h"

namespace gamma_changer {

CalibrationController::CalibrationController(ProfileStore& store)
    : CalibrationController(store, system_display_ramp_backend()) {}

CalibrationController::CalibrationController(ProfileStore& store,
                                             DisplayRampBackend& ramp_backend)
    : store_(store), ramp_backend_(ramp_backend) {}

namespace {

// Displays configured before stable monitor paths existed were stored under the
// GDI device name (\\.\DISPLAY1). Returns that legacy key, or empty when the
// display has no distinct legacy identity.
std::wstring legacy_storage_id(const DisplayInfo& display) {
    if (display.stable_id.empty() || display.stable_id == display.device_name) return {};
    return display.device_name;
}

}  // namespace

bool CalibrationController::try_load_settings(const DisplayInfo& display,
                                              CalibrationSettings& settings) const {
    if (store_.try_load_params(display_storage_id(display), settings)) return true;
    const std::wstring legacy = legacy_storage_id(display);
    return !legacy.empty() && store_.try_load_params(legacy, settings);
}

CalibrationSettings CalibrationController::load_settings(const DisplayInfo& display) const {
    CalibrationSettings settings;
    return try_load_settings(display, settings) ? settings : default_params();
}

bool CalibrationController::has_saved_settings(const DisplayInfo& display) const {
    CalibrationSettings ignored;
    return try_load_settings(display, ignored);
}

bool CalibrationController::load_base_ramp(const DisplayInfo& display, GammaRamp& ramp,
                                           std::wstring& error) {
    const std::wstring id = display_storage_id(display);
    if (store_.load_base_ramp(id, ramp)) return true;
    const std::wstring legacy = legacy_storage_id(display);
    if (legacy.empty() || !store_.load_base_ramp(legacy, ramp)) {
        error = L"No saved base ramp for this display";
        return false;
    }
    // Migrate the legacy device-name ramp so later resets no longer depend on it.
    std::wstring migration_error;
    if (!store_.save_base_ramp(id, ramp, migration_error)) {
        log_message(LogLevel::warning, L"Could not migrate the legacy base ramp for " +
                                           display.device_name + L": " + migration_error);
    }
    return true;
}

bool CalibrationController::ensure_original_ramp(const DisplayInfo& display,
                                                  std::wstring& error) {
    const std::wstring id = display_storage_id(display);
    if (captured_base_ramps_.count(id) != 0) return true;
    GammaRamp original;
    std::wstring ignored;
    if (!load_base_ramp(display, original, ignored)) {
        // First contact with this display: whatever is on screen now is the
        // pre-Gamma-Changer state that "Restore original" must return to.
        if (!ramp_backend_.read(display, original, error)) return false;
        if (!store_.save_base_ramp(id, original, error)) return false;
    }
    captured_base_ramps_.insert(id);
    return true;
}

bool CalibrationController::preview_owned_by(const DisplayInfo& display) const {
    return preview_active_ && preview_display_id_ == display_storage_id(display);
}

void CalibrationController::end_preview_on(const DisplayInfo& display) {
    if (!preview_owned_by(display)) return;
    preview_active_ = false;
    preview_display_id_.clear();
}

bool CalibrationController::preview(const DisplayInfo& display,
                                    const CalibrationSettings& settings,
                                    std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    if (!ensure_original_ramp(display, error)) return false;

    if (preview_active_ && !preview_owned_by(display)) {
        error = L"Finish or discard the active preview before changing displays";
        return false;
    }
    if (!preview_active_) {
        if (!ramp_backend_.read(display, preview_origin_, error)) return false;
        preview_display_id_ = display_storage_id(display);
        preview_active_ = true;
    }

    const GammaRamp ramp = lut_generator_.generate(settings);
    return ramp_backend_.write(display, ramp, error);
}

bool CalibrationController::cancel_preview(const DisplayInfo& display,
                                           std::wstring& error) {
    if (!preview_active_) return true;
    if (!preview_owned_by(display)) {
        error = L"The active preview belongs to a different display";
        return false;
    }
    if (!ramp_backend_.write(display, preview_origin_, error)) return false;
    preview_active_ = false;
    preview_display_id_.clear();
    return true;
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
        store_.try_load_params(display_storage_id(display), previous_settings);

    const GammaRamp ramp = lut_generator_.generate(settings);
    std::wstring apply_error;
    if (!ramp_backend_.write(display, ramp, apply_error)) {
        return {CommitStatus::rolled_back, apply_error};
    }

    std::wstring save_error;
    if (!store_.save_params(display_storage_id(display), settings, save_error)) {
        std::wstring rollback_error;
        const bool ramp_restored = ramp_backend_.write(display, previous_ramp, rollback_error);
        if (had_previous_settings) {
            std::wstring settings_rollback_error;
            if (!store_.save_params(display_storage_id(display), previous_settings,
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

    end_preview_on(display);
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
    return store_.save_params(display_storage_id(display), settings, error);
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

bool CalibrationController::apply_runtime(const DisplayInfo& display,
                                           const CalibrationSettings& settings,
                                           std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    if (preview_active_) {
        error = L"Finish or discard the active preview before automatic switching";
        return false;
    }
    if (!ensure_original_ramp(display, error)) return false;
    return ramp_backend_.write(display, lut_generator_.generate(settings), error);
}

bool CalibrationController::restore_original(const DisplayInfo& display,
                                             std::wstring& error) {
    GammaRamp original;
    if (!load_base_ramp(display, original, error)) return false;
    if (!ramp_backend_.write(display, original, error)) return false;
    if (!store_.save_params(display_storage_id(display), default_params(), error)) return false;
    end_preview_on(display);
    return true;
}

bool CalibrationController::capture_ramp(const DisplayInfo& display, GammaRamp& ramp,
                                         std::wstring& error) {
    return ramp_backend_.read(display, ramp, error);
}

bool CalibrationController::restore_ramp(const DisplayInfo& display, const GammaRamp& ramp,
                                         std::wstring& error) {
    return ramp_backend_.write(display, ramp, error);
}

GammaRamp CalibrationController::generate_preview(
    const CalibrationSettings& settings) const {
    return lut_generator_.generate(settings);
}

}  // namespace gamma_changer
