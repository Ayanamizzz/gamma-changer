#pragma once

#include "app_profile_rules.h"
#include "calibration_controller.h"
#include "gamma_types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gamma_changer {

// What a given executable should show on a given display. `profile` is null when
// the display should return to its desktop calibration.
struct AppProfileResolution {
    const AppProfileRule* rule = nullptr;  // null for the unassigned-app default
    const Profile* profile = nullptr;
    bool is_fallback() const { return profile != nullptr && rule == nullptr; }
};

// Pure decision function: first enabled rule for (executable, display) whose
// profile still exists and is saved; otherwise the display's unassigned-app
// default; otherwise desktop settings. Disabled configurations always resolve
// to desktop settings.
AppProfileResolution resolve_app_profile(const AppProfileConfig& config,
                                         const std::vector<Profile>& profiles,
                                         const std::wstring& executable_path,
                                         const std::wstring& display_id);

// Per-display runtime overlay currently on screen, plus the exact desktop ramp
// to return to. The desktop ramp is captured once, on the first switch away from
// the desktop, and kept across subsequent app-to-app switches.
struct RuntimeDisplayState {
    GammaRamp desktop_ramp{};
    std::wstring rule_id;  // empty when the unassigned-app default is active
    std::wstring profile_id;
    CalibrationSettings settings{};
};

enum class RuntimeEventKind {
    applied,          // a profile was written; `label` names it
    restored,         // the desktop ramp was written back
    restore_failed,
    capture_failed,   // could not snapshot the desktop ramp; nothing was written
    apply_failed,
};

struct RuntimeEvent {
    RuntimeEventKind kind = RuntimeEventKind::applied;
    std::wstring display_id;
    std::wstring label;
    std::wstring error;
};

struct RuntimeRestoreResult {
    bool success = true;
    bool restored_any = false;
    std::wstring error;  // last failure, when !success
};

// Owns the "which application overlay is on which display" state. It has no
// window, timer, or status-bar dependency: the UI decides *when* to evaluate
// and how to present the returned events; this class decides *what* to write.
class AppProfileRuntime {
public:
    explicit AppProfileRuntime(CalibrationController& controller) : controller_(controller) {}

    // Brings every display in line with `executable_path`. Without `force`, a
    // display already showing the resolved profile with identical settings is
    // left alone (no redundant LUT writes while the user alt-tabs).
    std::vector<RuntimeEvent> apply(const AppProfileConfig& config,
                                    const std::vector<Profile>& profiles,
                                    const std::vector<DisplayInfo>& displays,
                                    const std::wstring& executable_path, bool force);

    // Returns every connected display that carries an overlay to its desktop
    // ramp. Disconnected displays keep their entry until they return.
    RuntimeRestoreResult restore_all(const std::vector<DisplayInfo>& displays);

    const RuntimeDisplayState* active(const std::wstring& display_id) const;
    bool empty() const { return active_.empty(); }

private:
    CalibrationController& controller_;
    std::unordered_map<std::wstring, RuntimeDisplayState> active_;
};

}  // namespace gamma_changer
