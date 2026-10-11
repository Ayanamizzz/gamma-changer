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

bool update_app_filter_settings(AppProfileConfig& config, std::vector<Profile>& profiles,
                                const std::wstring& rule_id,
                                const CalibrationSettings& settings) {
    auto* rule = find_app_profile_rule_by_id(config, rule_id);
    if (!rule) return false;
    const auto current = std::find_if(profiles.begin(), profiles.end(),
                                     [&](const Profile& profile) { return profile.id == rule->profile_id; });
    if (current == profiles.end()) return false;
    const bool shared = std::count_if(config.rules.begin(), config.rules.end(),
                                     [&](const AppProfileRule& item) {
        return item.profile_id == current->id;
    }) > 1 || std::any_of(config.fallback_profiles.begin(), config.fallback_profiles.end(),
                          [&](const DisplayProfilePreference& item) { return item.profile_id == current->id; });
    if (current->id.rfind(L"app-filter-", 0) == 0 && !shared) {
        current->settings = settings;
        return true;
    }

    Profile copy = *current;
    for (std::size_t number = 1;; ++number) {
        copy.id = L"app-filter-" + std::to_wstring(number);
        const bool used = std::any_of(profiles.begin(), profiles.end(),
                                     [&](const Profile& profile) { return profile.id == copy.id; });
        if (!used) break;
    }
    const auto separator = rule->executable_path.find_last_of(L"\\/");
    const std::wstring executable = separator == std::wstring::npos ? rule->executable_path
        : rule->executable_path.substr(separator + 1);
    copy.name = executable + L" - " + (rule->name.empty() ? current->name : rule->name);
    copy.settings = settings;
    rule->profile_id = copy.id;
    profiles.push_back(std::move(copy));
    return true;
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
