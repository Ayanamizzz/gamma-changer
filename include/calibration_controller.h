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
};

struct CommitResult {
    CommitStatus status = CommitStatus::success;
    std::wstring error;

    bool succeeded() const { return status == CommitStatus::success; }
};

// The only component that writes gamma ramps to displays. Every path that
// changes what a screen shows (manual preview/commit, display recovery,
// per-application runtime switching) goes through this class so the
// preview/committed/runtime states cannot silently overwrite each other.
class CalibrationController {
public:
    explicit CalibrationController(ProfileStore& store);
    CalibrationController(ProfileStore& store, DisplayRampBackend& ramp_backend);

    CalibrationSettings load_settings(const DisplayInfo& display) const;
    bool has_saved_settings(const DisplayInfo& display) const;

    bool preview(const DisplayInfo& display, const CalibrationSettings& settings,
                 std::wstring& error);
    bool cancel_preview(const DisplayInfo& display, std::wstring& error);
    bool apply_and_save(const DisplayInfo& display, const CalibrationSettings& settings,
                        std::wstring& error);
    CommitResult commit(const DisplayInfo& display, const CalibrationSettings& settings);
    bool save_for_offline_display(const DisplayInfo& display,
                                  const CalibrationSettings& settings,
                                  std::wstring& error);
    bool reapply_committed(const DisplayInfo& display,
                           const CalibrationSettings& settings,
                           std::wstring& error);
    bool apply_runtime(const DisplayInfo& display,
                       const CalibrationSettings& settings,
                       std::wstring& error);
    bool restore_original(const DisplayInfo& display, std::wstring& error);

    // Snapshot/restore of the raw on-screen ramp, used by runtime switching to
    // return a display to exactly what the desktop showed before.
    bool capture_ramp(const DisplayInfo& display, GammaRamp& ramp, std::wstring& error);
    bool restore_ramp(const DisplayInfo& display, const GammaRamp& ramp, std::wstring& error);

    GammaRamp generate_preview(const CalibrationSettings& settings) const;

private:
    bool try_load_settings(const DisplayInfo& display, CalibrationSettings& settings) const;
    bool load_base_ramp(const DisplayInfo& display, GammaRamp& ramp, std::wstring& error);
    bool ensure_original_ramp(const DisplayInfo& display, std::wstring& error);
    bool preview_owned_by(const DisplayInfo& display) const;
    void end_preview_on(const DisplayInfo& display);

    ProfileStore& store_;
    DisplayRampBackend& ramp_backend_;
    LutGenerator lut_generator_;
    std::unordered_set<std::wstring> captured_base_ramps_;
    bool preview_active_ = false;
    std::wstring preview_display_id_;  // display_storage_id of the preview target
    GammaRamp preview_origin_{};
};

}  // namespace gamma_changer
