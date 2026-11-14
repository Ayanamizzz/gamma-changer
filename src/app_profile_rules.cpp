#include "app_profile_rules.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace gamma_changer {

std::wstring normalize_executable_path(const std::wstring& executable_path) {
    std::wstring normalized = executable_path;
    for (auto& character : normalized) {
        if (character == L'/') character = L'\\';
        character = static_cast<wchar_t>(std::towlower(character));
    }
    return normalized;
}

const AppProfileRule* find_app_profile_rule(const AppProfileConfig& config,
                                          const std::wstring& executable_path,
                                          const std::wstring& display_id) {
    if (!config.enabled || executable_path.empty() || display_id.empty()) return nullptr;
    const std::wstring normalized = normalize_executable_path(executable_path);
    const auto found = std::find_if(config.rules.begin(), config.rules.end(),
                                  [&](const AppProfileRule& rule) {
        return rule.enabled && rule.display_id == display_id &&
               normalize_executable_path(rule.executable_path) == normalized;
    });
    return found == config.rules.end() ? nullptr : &*found;
}

const AppProfileRule* find_app_profile_rule_by_id(const AppProfileConfig& config,
                                                const std::wstring& rule_id) {
    if (rule_id.empty()) return nullptr;
    const auto found = std::find_if(config.rules.begin(), config.rules.end(),
                                  [&](const AppProfileRule& rule) {
        return rule.id == rule_id;
    });
    return found == config.rules.end() ? nullptr : &*found;
}

AppProfileRule* find_app_profile_rule_by_id(AppProfileConfig& config,
                                          const std::wstring& rule_id) {
    if (rule_id.empty()) return nullptr;
    const auto found = std::find_if(config.rules.begin(), config.rules.end(),
                                  [&](const AppProfileRule& rule) {
        return rule.id == rule_id;
    });
    return found == config.rules.end() ? nullptr : &*found;
}

void set_app_profile_rule(AppProfileConfig& config, AppProfileRule rule) {
    if (!rule.id.empty()) {
        if (auto* existing = find_app_profile_rule_by_id(config, rule.id)) {
            *existing = std::move(rule);
            return;
        }
    } else {
        for (std::size_t number = 1;; ++number) {
            rule.id = L"rule-" + std::to_wstring(number);
            if (!find_app_profile_rule_by_id(config, rule.id)) break;
        }
    }
    config.rules.push_back(std::move(rule));
}

const DisplayProfilePreference* find_app_fallback_profile(const AppProfileConfig& config,
                                                          const std::wstring& display_id) {
    const auto found = std::find_if(config.fallback_profiles.begin(), config.fallback_profiles.end(),
                                   [&](const DisplayProfilePreference& item) { return item.display_id == display_id; });
    return found == config.fallback_profiles.end() ? nullptr : &*found;
}

bool set_app_fallback_profile(AppProfileConfig& config, const std::wstring& display_id,
                              const std::wstring& profile_id) {
    if (display_id.empty()) return false;
    const auto found = std::find_if(config.fallback_profiles.begin(), config.fallback_profiles.end(),
                                   [&](const DisplayProfilePreference& item) { return item.display_id == display_id; });
    if (profile_id.empty()) {
        if (found == config.fallback_profiles.end()) return false;
        config.fallback_profiles.erase(found);
    } else if (found == config.fallback_profiles.end()) {
        config.fallback_profiles.push_back({display_id, profile_id});
    } else {
        if (found->profile_id == profile_id) return false;
        found->profile_id = profile_id;
    }
    return true;
}

bool select_app_profile_rule(AppProfileConfig& config, const std::wstring& rule_id) {
    const auto chosen = std::find_if(config.rules.begin(), config.rules.end(),
                                    [&](const AppProfileRule& rule) { return rule.id == rule_id; });
    if (chosen == config.rules.end()) return false;
    chosen->enabled = true;
    const std::wstring path = normalize_executable_path(chosen->executable_path);
    const std::wstring display = chosen->display_id;
    const auto first = std::find_if(config.rules.begin(), config.rules.end(),
                                   [&](const AppProfileRule& rule) {
        return rule.display_id == display && normalize_executable_path(rule.executable_path) == path;
    });
    std::rotate(first, chosen, chosen + 1);
    config.enabled = true;
    return true;
}

std::wstring bind_app_profile_rule(AppProfileConfig& config, AppProfileRule rule) {
    if (rule.id.empty()) {
        const std::wstring path = normalize_executable_path(rule.executable_path);
        const auto existing = std::find_if(config.rules.begin(), config.rules.end(),
                                           [&](const AppProfileRule& item) {
            return item.display_id == rule.display_id && item.profile_id == rule.profile_id &&
                   normalize_executable_path(item.executable_path) == path;
        });
        if (existing != config.rules.end()) rule.id = existing->id;
    }
    const std::wstring existing_id = rule.id;
    set_app_profile_rule(config, std::move(rule));
    const std::wstring id = existing_id.empty() ? config.rules.back().id : existing_id;
    select_app_profile_rule(config, id);
    return id;
}

namespace {

bool same_app_on_display(const AppProfileRule& rule, const std::wstring& normalized_path,
                         const std::wstring& display_id) {
    return rule.display_id == display_id &&
           normalize_executable_path(rule.executable_path) == normalized_path;
}

}  // namespace

std::vector<AppAssignment> app_assignments(const AppProfileConfig& config,
                                           const std::wstring& display_id) {
    std::vector<AppAssignment> assignments;
    std::vector<std::wstring> seen;
    for (const auto& rule : config.rules) {
        if (rule.display_id != display_id) continue;
        const std::wstring path = normalize_executable_path(rule.executable_path);
        if (std::find(seen.begin(), seen.end(), path) != seen.end()) continue;
        seen.push_back(path);
        const auto winner = std::find_if(config.rules.begin(), config.rules.end(),
                                         [&](const AppProfileRule& item) {
            return item.enabled && same_app_on_display(item, path, display_id);
        });
        if (winner != config.rules.end()) {
            assignments.push_back({winner->executable_path, winner->profile_id, true});
        } else {
            assignments.push_back({rule.executable_path, rule.profile_id, false});
        }
    }
    return assignments;
}

void assign_app_profile(AppProfileConfig& config, const std::wstring& executable_path,
                        const std::wstring& display_id, const std::wstring& profile_id) {
    const std::wstring path = normalize_executable_path(executable_path);
    auto winner = std::find_if(config.rules.begin(), config.rules.end(),
                               [&](const AppProfileRule& rule) {
        return rule.enabled && same_app_on_display(rule, path, display_id);
    });
    if (winner == config.rules.end()) {
        winner = std::find_if(config.rules.begin(), config.rules.end(),
                              [&](const AppProfileRule& rule) {
            return same_app_on_display(rule, path, display_id);
        });
    }
    if (winner == config.rules.end()) {
        set_app_profile_rule(config, {executable_path, display_id, profile_id, true});
        return;
    }
    const std::wstring kept_id = winner->id;
    if (winner->profile_id != profile_id) winner->name.clear();  // name described the old profile
    winner->executable_path = executable_path;
    winner->profile_id = profile_id;
    winner->enabled = true;
    config.rules.erase(std::remove_if(config.rules.begin(), config.rules.end(),
                                      [&](const AppProfileRule& rule) {
        return rule.id != kept_id && same_app_on_display(rule, path, display_id);
    }), config.rules.end());
}

bool unassign_app_profile(AppProfileConfig& config, const std::wstring& executable_path,
                          const std::wstring& display_id) {
    const std::wstring path = normalize_executable_path(executable_path);
    const std::size_t before = config.rules.size();
    config.rules.erase(std::remove_if(config.rules.begin(), config.rules.end(),
                                      [&](const AppProfileRule& rule) {
        return same_app_on_display(rule, path, display_id);
    }), config.rules.end());
    return config.rules.size() != before;
}

std::vector<std::wstring> app_profile_display_ids(const AppProfileConfig& config) {
    std::vector<std::wstring> ids;
    const auto add = [&](const std::wstring& id) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    for (const auto& rule : config.rules) add(rule.display_id);
    for (const auto& preference : config.fallback_profiles) add(preference.display_id);
    return ids;
}

std::vector<AppAssignment> apps_using_profile(const AppProfileConfig& config,
                                              const std::wstring& display_id,
                                              const std::wstring& profile_id) {
    std::vector<AppAssignment> apps = app_assignments(config, display_id);
    apps.erase(std::remove_if(apps.begin(), apps.end(),
                              [&](const AppAssignment& app) {
        return app.profile_id != profile_id;
    }), apps.end());
    return apps;
}

std::wstring default_other_apps_profile(const std::vector<Profile>& profiles,
                                        const std::wstring& assigned_profile_id,
                                        const std::wstring& shown_profile_id,
                                        const std::wstring& previous_profile_id,
                                        const std::wstring& builtin_profile_id) {
    for (const std::wstring* candidate :
         {&shown_profile_id, &previous_profile_id, &builtin_profile_id}) {
        if (candidate->empty() || *candidate == assigned_profile_id) continue;
        const bool usable = std::any_of(profiles.begin(), profiles.end(),
                                        [&](const Profile& profile) {
            return profile.id == *candidate && profile.saved;
        });
        if (usable) return *candidate;
    }
    return {};
}

std::size_t remove_app_profile_rules_for_profile(AppProfileConfig& config,
                                                const std::wstring& profile_id) {
    const std::size_t previous_count = config.rules.size() + config.fallback_profiles.size();
    config.rules.erase(std::remove_if(config.rules.begin(), config.rules.end(),
                                     [&](const AppProfileRule& rule) {
        return rule.profile_id == profile_id;
    }), config.rules.end());
    config.fallback_profiles.erase(std::remove_if(config.fallback_profiles.begin(), config.fallback_profiles.end(),
                                                  [&](const DisplayProfilePreference& item) {
        return item.profile_id == profile_id;
    }), config.fallback_profiles.end());
    return previous_count - config.rules.size() - config.fallback_profiles.size();
}

}  // namespace gamma_changer
