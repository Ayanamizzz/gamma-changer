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

// The first enabled rule for an executable and display is its default filter.
// A new rule gets a stable id and appends; an existing id updates in place
// without changing its priority.
// Multiple rules may bind the same executable and display to different profiles.
void set_app_profile_rule(AppProfileConfig& config, AppProfileRule rule);
bool select_app_profile_rule(AppProfileConfig& config, const std::wstring& rule_id);

// Application filters share library templates until their first edit. Copies
// are created only when needed to keep other applications' settings unchanged.
bool update_app_filter_settings(AppProfileConfig& config, std::vector<Profile>& profiles,
                                const std::wstring& rule_id,
                                const CalibrationSettings& settings);
std::size_t remove_app_profile_rules_for_profile(AppProfileConfig& config,
                                                const std::wstring& profile_id);

}  // namespace gamma_changer
