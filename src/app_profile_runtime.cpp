#include "app_profile_runtime.h"

#include "gamma_lut.h"

#include <algorithm>
#include <utility>

namespace gamma_changer {
namespace {

const Profile* saved_profile(const std::vector<Profile>& profiles, const std::wstring& id) {
    const auto found = std::find_if(profiles.begin(), profiles.end(),
                                    [&](const Profile& profile) { return profile.id == id; });
    return found != profiles.end() && found->saved ? &*found : nullptr;
}

std::wstring resolution_label(const AppProfileResolution& resolution) {
    if (resolution.is_fallback()) return L"Default: " + resolution.profile->name;
    return resolution.rule->name.empty() ? resolution.profile->name : resolution.rule->name;
}

}  // namespace

AppProfileResolution resolve_app_profile(const AppProfileConfig& config,
                                         const std::vector<Profile>& profiles,
                                         const std::wstring& executable_path,
                                         const std::wstring& display_id) {
    AppProfileResolution resolution;
    if (!config.enabled) return resolution;
    if (const auto* rule = find_app_profile_rule(config, executable_path, display_id)) {
        if (const auto* profile = saved_profile(profiles, rule->profile_id)) {
            resolution.rule = rule;
            resolution.profile = profile;
            return resolution;
        }
    }
    if (const auto* preference = find_app_fallback_profile(config, display_id)) {
        resolution.profile = saved_profile(profiles, preference->profile_id);
    }
    return resolution;
}

std::vector<RuntimeEvent> AppProfileRuntime::apply(const AppProfileConfig& config,
                                                   const std::vector<Profile>& profiles,
                                                   const std::vector<DisplayInfo>& displays,
                                                   const std::wstring& executable_path,
                                                   bool force) {
    std::vector<RuntimeEvent> events;
    for (const auto& display : displays) {
        const std::wstring id = display_storage_id(display);
        const AppProfileResolution resolution =
            resolve_app_profile(config, profiles, executable_path, id);
        const auto active = active_.find(id);
        std::wstring error;

        if (!resolution.profile) {
            if (active == active_.end()) continue;
            if (controller_.restore_ramp(display, active->second.desktop_ramp, error)) {
                active_.erase(active);
                events.push_back({RuntimeEventKind::restored, id, {}, {}});
            } else {
                events.push_back({RuntimeEventKind::restore_failed, id, {}, error});
            }
            continue;
        }

        const std::wstring rule_id = resolution.rule ? resolution.rule->id : std::wstring{};
        if (!force && active != active_.end() &&
            active->second.profile_id == resolution.profile->id &&
            settings_equal(active->second.settings, resolution.profile->settings)) {
            active->second.rule_id = rule_id;
            continue;
        }

        RuntimeDisplayState next;
        if (active == active_.end()) {
            if (!controller_.capture_ramp(display, next.desktop_ramp, error)) {
                events.push_back({RuntimeEventKind::capture_failed, id, {}, error});
                continue;
            }
        } else {
            next = active->second;
        }
        if (!controller_.apply_runtime(display, resolution.profile->settings, error)) {
            events.push_back({RuntimeEventKind::apply_failed, id, {}, error});
            continue;
        }
        next.profile_id = resolution.profile->id;
        next.rule_id = rule_id;
        next.settings = resolution.profile->settings;
        active_[id] = std::move(next);
        events.push_back({RuntimeEventKind::applied, id, resolution_label(resolution), {}});
    }
    return events;
}

RuntimeRestoreResult AppProfileRuntime::restore_all(const std::vector<DisplayInfo>& displays) {
    RuntimeRestoreResult result;
    for (auto active = active_.begin(); active != active_.end();) {
        const auto display = std::find_if(displays.begin(), displays.end(),
                                          [&](const DisplayInfo& item) {
            return display_storage_id(item) == active->first;
        });
        if (display == displays.end()) {
            ++active;
            continue;
        }
        std::wstring error;
        if (controller_.restore_ramp(*display, active->second.desktop_ramp, error)) {
            active = active_.erase(active);
            result.restored_any = true;
        } else {
            result.success = false;
            result.error = std::move(error);
            ++active;
        }
    }
    return result;
}

const RuntimeDisplayState* AppProfileRuntime::active(const std::wstring& display_id) const {
    const auto found = active_.find(display_id);
    return found == active_.end() ? nullptr : &found->second;
}

}  // namespace gamma_changer
