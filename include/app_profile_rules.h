#pragma once

#include "gamma_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gamma_changer {

struct AppProfileRule {
    std::wstring executable_path;
    std::wstring display_id;
    std::wstring profile_id;
    bool enabled = true;
    std::wstring id{};
    std::wstring name{};
};

struct AppProfileConfig {
    bool enabled = false;
    std::vector<AppProfileRule> rules;
    std::vector<DisplayProfilePreference> fallback_profiles{};
};

// Rules match the complete executable path, not just its filename. Normalize
// the slash direction and case without resolving or opening the executable.
std::wstring normalize_executable_path(const std::wstring& executable_path);
const AppProfileRule* find_app_profile_rule(const AppProfileConfig& config,
                                          const std::wstring& executable_path,
                                          const std::wstring& display_id);
const AppProfileRule* find_app_profile_rule_by_id(const AppProfileConfig& config,
                                                const std::wstring& rule_id);
AppProfileRule* find_app_profile_rule_by_id(AppProfileConfig& config,
                                          const std::wstring& rule_id);
// No entry means restore that display's desktop settings for unbound apps.
const DisplayProfilePreference* find_app_fallback_profile(const AppProfileConfig& config,
                                                          const std::wstring& display_id);
bool set_app_fallback_profile(AppProfileConfig& config, const std::wstring& display_id,
                              const std::wstring& profile_id);

// The first enabled rule for an executable and display is its default filter.
// A new rule gets a stable id and appends; an existing id updates in place
// without changing its priority.
// Multiple rules may bind the same executable and display to different profiles.
void set_app_profile_rule(AppProfileConfig& config, AppProfileRule rule);
// Binding a saved filter also makes it the app's default and enables switching.
// Rebinding the same app/display/profile reuses its rule instead of duplicating it.
std::wstring bind_app_profile_rule(AppProfileConfig& config, AppProfileRule rule);
bool select_app_profile_rule(AppProfileConfig& config, const std::wstring& rule_id);

// ---------------------------------------------------------------------------
// Simplified assignment view used by the UI: "on this display, this app uses
// this profile". The rule list can still hold several rules for one app and
// display (older versions allowed that); the view shows the one that actually
// wins, and editing an app collapses its group back to that single rule.
// ---------------------------------------------------------------------------
struct AppAssignment {
    std::wstring executable_path;
    std::wstring profile_id;
    // False only for legacy groups whose rules are all disabled: listed, but
    // currently inactive until the user assigns a profile again.
    bool enabled = true;
};

// One entry per executable on `display_id`, in rule order.
std::vector<AppAssignment> app_assignments(const AppProfileConfig& config,
                                           const std::wstring& display_id);
// Makes the app use `profile_id` on `display_id`: updates (and keeps the id of)
// the winning rule, or adds a rule. Other rules for the same app/display are
// removed. Never changes `config.enabled`.
void assign_app_profile(AppProfileConfig& config, const std::wstring& executable_path,
                        const std::wstring& display_id, const std::wstring& profile_id);
// Removes every rule for the app on `display_id`.
bool unassign_app_profile(AppProfileConfig& config, const std::wstring& executable_path,
                          const std::wstring& display_id);
// Every display id referenced by a rule or an unassigned-app default, so the UI
// can still offer displays that are currently disconnected.
std::vector<std::wstring> app_profile_display_ids(const AppProfileConfig& config);

// The apps that switch to `profile_id` on `display_id`: the assignment view
// filtered to one profile, in rule order.
std::vector<AppAssignment> apps_using_profile(const AppProfileConfig& config,
                                              const std::wstring& display_id,
                                              const std::wstring& profile_id);

// Which profile unlisted apps should keep when `assigned_profile_id` gets an
// app on a display that has no "other apps" profile yet. Preference order: the
// profile the display shows now, the one it showed before that, the built-in
// default; the first that is saved and is not the assigned profile itself.
// Empty when nothing qualifies (unlisted apps then keep the display's own
// calibration).
std::wstring default_other_apps_profile(const std::vector<Profile>& profiles,
                                        const std::wstring& assigned_profile_id,
                                        const std::wstring& shown_profile_id,
                                        const std::wstring& previous_profile_id,
                                        const std::wstring& builtin_profile_id);

// Remove both software bindings and unassigned-app defaults referencing a profile.
std::size_t remove_app_profile_rules_for_profile(AppProfileConfig& config,
                                                const std::wstring& profile_id);

}  // namespace gamma_changer
