#pragma once

#include "display_manager.h"
#include "display_ramp_backend.h"
#include "gamma_lut.h"
#include "profile_store.h"

#include <string>
#include <unordered_set>

namespace gamma_changer {

enum class CommitStatus {
    success,
    rolled_back,
    rollback_failed,
    deferred_offline,
};

struct CommitResult {
    CommitStatus status = CommitStatus::success;
    std::wstring error;

    bool succeeded() const {
        return status == CommitStatus::success || status == CommitStatus::deferred_offline;
    }
};

// Coordinates the calibration use-cases without exposing Windows gamma-ramp
// details to the UI or command-line front ends.
class CalibrationController {
public:
    explicit CalibrationController(ProfileStore& store);
    CalibrationController(ProfileStore& store, DisplayRampBackend& ramp_backend);

    CalibrationSettings load_settings(const DisplayInfo& display) const;

    bool preview(const DisplayInfo& display, const CalibrationSettings& settings,
                 std::wstring& error);
    bool cancel_preview(const DisplayInfo& display, std::wstring& error);
    void abandon_preview();
    bool apply_and_save(const DisplayInfo& display, const CalibrationSettings& settings,
                        std::wstring& error);
    CommitResult commit(const DisplayInfo& display, const CalibrationSettings& settings);
    bool save_for_offline_display(const DisplayInfo& display,
                                  const CalibrationSettings& settings,
                                  std::wstring& error);
    bool reapply_committed(const DisplayInfo& display,
                           const CalibrationSettings& settings,
                           std::wstring& error);
    bool restore_original(const DisplayInfo& display, std::wstring& error);

    GammaRamp generate_preview(const CalibrationSettings& settings) const;

private:
    bool ensure_original_ramp(const DisplayInfo& display, std::wstring& error);

    ProfileStore& store_;
    DisplayRampBackend& ramp_backend_;
    LutGenerator lut_generator_;
    std::unordered_set<std::wstring> captured_base_ramps_;
    bool preview_active_ = false;
    std::wstring preview_display_id_;
    GammaRamp preview_origin_{};
};

}  // namespace gamma_changer
