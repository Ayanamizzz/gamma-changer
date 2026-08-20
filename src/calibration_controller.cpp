#include "calibration_controller.h"

#include "logger.h"

namespace gamma_changer {

CalibrationController::CalibrationController(ProfileStore& store)
    : CalibrationController(store, system_display_ramp_backend()) {}

CalibrationController::CalibrationController(ProfileStore& store,
                                             DisplayRampBackend& ramp_backend)
    : store_(store), ramp_backend_(ramp_backend) {}

CalibrationSettings CalibrationController::load_settings(const DisplayInfo& display) const {
    CalibrationSettings settings;
    if (store_.try_load_params(display_storage_id(display), settings)) return settings;
    if (!display.stable_id.empty() && display.stable_id != display.device_name &&
        store_.try_load_params(display.device_name, settings)) {
        return settings;
    }
    return default_params();
}

bool CalibrationController::has_saved_settings(const DisplayInfo& display) const {
    CalibrationSettings settings;
    if (store_.try_load_params(display_storage_id(display), settings)) return true;
    if (!display.stable_id.empty() && display.stable_id != display.device_name &&
        store_.try_load_params(display.device_name, settings)) {
        return true;
    }
    return false;
}

bool CalibrationController::ensure_original_ramp(const DisplayInfo& display,
                                                  std::wstring& error) {
    const std::wstring id = display_storage_id(display);
    GammaRamp original;
    if (captured_base_ramps_.find(id) != captured_base_ramps_.end()) {
        if (store_.load_base_ramp(id, original)) return true;
        // The file may have been removed by cleanup software while the process
        // remained open. Drop the memory-only hint and safely capture it again.
        captured_base_ramps_.erase(id);
    }
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

    const std::wstring display_id = display_storage_id(display);
    if (preview_active_ && preview_display_id_ != display_id) {
        error = L"Finish or discard the active preview before changing displays";
        return false;
    }
    const GammaRamp ramp = lut_generator_.generate(settings);
    if (!preview_active_) {
        GammaRamp origin{};
        if (!ramp_backend_.read(display, origin, error)) return false;
        if (!ramp_backend_.write(display, ramp, error)) return false;
        // Publish preview ownership only after the first hardware write succeeds.
        // Otherwise a rejected first preview would incorrectly block every other
        // display as though a live preview still existed.
        preview_origin_ = origin;
        preview_display_id_ = display_id;
        preview_active_ = true;
        return true;
    }
    return ramp_backend_.write(display, ramp, error);
}

bool CalibrationController::cancel_preview(const DisplayInfo& display,
                                           std::wstring& error) {
    if (!preview_active_) return true;
    if (preview_display_id_ != display_storage_id(display)) {
        error = L"The active preview belongs to a different display";
        return false;
    }
    if (!ramp_backend_.write(display, preview_origin_, error)) return false;
    preview_active_ = false;
    preview_display_id_.clear();
    return true;
}

void CalibrationController::abandon_preview_for_offline_display(
    const DisplayInfo& display) {
    if (!preview_active_ || preview_display_id_ != display_storage_id(display)) return;
    log_message(LogLevel::warning,
                L"Discarding preview bookkeeping without another Ramp write: " +
                    display_storage_id(display));
    preview_active_ = false;
    preview_display_id_.clear();
    preview_origin_ = {};
}

bool CalibrationController::migrate_display_identity(const DisplayInfo& previous,
                                                      const DisplayInfo& current,
                                                      std::wstring& error) {
    if (!is_display_identity_upgrade(previous, current)) return true;
    const std::wstring old_id = display_storage_id(previous);
    const std::wstring new_id = display_storage_id(current);

    CalibrationSettings migrated_settings{};
    CalibrationSettings existing_settings{};
    if (store_.try_load_params(old_id, migrated_settings) &&
        !store_.try_load_params(new_id, existing_settings) &&
        !store_.save_params(new_id, migrated_settings, error)) {
        return false;
    }

    GammaRamp migrated_ramp{};
    GammaRamp existing_ramp{};
    if (store_.load_base_ramp(old_id, migrated_ramp) &&
        !store_.load_base_ramp(new_id, existing_ramp) &&
        !store_.save_base_ramp(new_id, migrated_ramp, error)) {
        return false;
    }
    if (store_.load_base_ramp(new_id, existing_ramp)) {
        captured_base_ramps_.insert(new_id);
    }
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
    if (preview_active_ && preview_display_id_ != display_storage_id(display)) {
        return {CommitStatus::rolled_back,
                L"The pending preview belongs to another display; switch was cancelled"};
    }
    if (!ensure_original_ramp(display, validation_error)) {
        return {CommitStatus::rolled_back, validation_error};
    }

    GammaRamp previous_ramp{};
    if (!ramp_backend_.read(display, previous_ramp, validation_error)) {
        return {CommitStatus::rolled_back,
                L"Could not capture the current display state: " + validation_error};
    }
    const GammaRamp ramp = lut_generator_.generate(settings);
    std::wstring apply_error;
    if (!ramp_backend_.write(display, ramp, apply_error)) {
        return {CommitStatus::rolled_back, apply_error};
    }

    std::wstring save_error;
    if (!store_.save_params(display_storage_id(display), settings, save_error)) {
        std::wstring rollback_error;
        const bool ramp_restored = ramp_backend_.write(display, previous_ramp, rollback_error);
        // ProfileStore writes through a temporary file and only replaces the
        // target on success, so a failed save leaves the previous settings intact.
        if (!ramp_restored) {
            return {CommitStatus::rollback_failed,
                    L"Save failed: " + save_error + L"; rollback failed: " + rollback_error};
        }
        return {CommitStatus::rolled_back, L"Save failed; display was restored: " + save_error};
    }

    if (preview_active_ && preview_display_id_ == display_storage_id(display)) {
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
    return store_.save_params(display_storage_id(display), settings, error);
}

bool CalibrationController::reapply_committed(const DisplayInfo& display,
                                              const CalibrationSettings& settings,
                                              std::wstring& error) {
    if (!lut_generator_.validate(settings, error)) return false;
    if (!ensure_original_ramp(display, error)) return false;
    const GammaRamp ramp = lut_generator_.generate(settings);
    if (!ramp_backend_.write(display, ramp, error)) return false;
    log_message(LogLevel::info, L"Committed calibration restored on " + display.device_name);
    return true;
}

bool CalibrationController::restore_original(const DisplayInfo& display,
                                             std::wstring& error) {
    GammaRamp original;
    const std::wstring id = display_storage_id(display);
    if (!store_.load_base_ramp(id, original)) {
        if (!display.stable_id.empty() && display.stable_id != display.device_name &&
            store_.load_base_ramp(display.device_name, original)) {
            // Migrate the legacy device-name ramp to the stable identity so the
            // next reset no longer depends on the legacy file.
            std::wstring migration_error;
            if (!store_.save_base_ramp(id, original, migration_error)) {
                log_message(LogLevel::warning,
                            L"Could not migrate the legacy base ramp for " +
                                display.device_name + L": " + migration_error);
            }
        } else {
            error = L"No saved base ramp for this display";
            return false;
        }
    }
    GammaRamp previous_ramp{};
    if (!ramp_backend_.read(display, previous_ramp, error)) {
        error = L"Could not capture the current display state before restoring defaults: " +
                error;
        return false;
    }
    if (!ramp_backend_.write(display, original, error)) return false;
    std::wstring save_error;
    if (!store_.save_params(id, default_params(), save_error)) {
        std::wstring rollback_error;
        if (!ramp_backend_.write(display, previous_ramp, rollback_error)) {
            error = L"Could not save default settings: " + save_error +
                    L"; display rollback also failed: " + rollback_error;
            return false;
        }
        error = L"Could not save default settings; the display was restored: " + save_error;
        return false;
    }
    if (preview_active_ && preview_display_id_ == display_storage_id(display)) {
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
