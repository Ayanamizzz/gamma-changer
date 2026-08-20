#include "profile_manager.h"

#include "gamma_lut.h"

#include <algorithm>
#include <utility>

namespace gamma_changer {
namespace {

constexpr wchar_t kDefaultProfileId[] = L"builtin-default";

Profile default_profile() {
    return {kDefaultProfileId, L"Default", default_params(), true};
}

}  // namespace

ProfileManager::ProfileManager(ProfileStore& store) : store_(store) {}

std::wstring ProfileManager::legacy_id(std::size_t index) {
    return L"legacy-slot-" + std::to_wstring(index + 1);
}

Profile* ProfileManager::find_profile(const std::wstring& id) {
    const auto item = std::find_if(profiles_.begin(), profiles_.end(),
                                   [&](const Profile& profile) { return profile.id == id; });
    return item == profiles_.end() ? nullptr : &*item;
}

const Profile* ProfileManager::find_profile(const std::wstring& id) const {
    const auto item = std::find_if(profiles_.begin(), profiles_.end(),
                                   [&](const Profile& profile) { return profile.id == id; });
    return item == profiles_.end() ? nullptr : &*item;
}

void ProfileManager::migrate_legacy_slots() {
    profiles_.clear();
    profiles_.push_back(default_profile());

    const auto slots = store_.load_presets();
    for (std::size_t index = 0; index < kPresetCount; ++index) {
        const auto& slot = slots[index];
        Profile profile;
        profile.id = legacy_id(index);
        profile.name = slot.occupied && !slot.name.empty()
                           ? slot.name
                           : L"Default " + std::to_wstring(index + 1);
        profile.settings = slot.occupied ? slot.params : default_params();
        profile.saved = slot.occupied;
        profiles_.push_back(std::move(profile));
    }
}

bool ProfileManager::load(std::wstring& error) {
    const ProfileLoadStatus status = store_.load_profiles(profiles_);
    if (status == ProfileLoadStatus::missing) {
        migrate_legacy_slots();
        return store_.save_profiles(profiles_, error);
    }
    if (status == ProfileLoadStatus::corrupt) {
        error = L"profiles.v1 is damaged; the original file was left untouched";
        return false;
    }
    if (status == ProfileLoadStatus::unsupported_version) {
        error = L"profiles.v1 was created by an unsupported newer version";
        return false;
    }

    bool repaired = false;
    if (!find_profile(kDefaultProfileId)) {
        profiles_.insert(profiles_.begin(), default_profile());
        repaired = true;
    } else if (Profile* builtin = find_profile(kDefaultProfileId);
               builtin->name != L"Default" ||
               !settings_equal(builtin->settings, default_params()) ||
               !builtin->saved) {
        *builtin = default_profile();
        repaired = true;
    }
    for (std::size_t index = 0; index < kPresetCount; ++index) {
        if (find_profile(legacy_id(index))) continue;
        profiles_.push_back({legacy_id(index),
                             L"Default " + std::to_wstring(index + 1),
                             default_params(), false});
        repaired = true;
    }
    if (!repaired) return true;
    if (store_.save_profiles(profiles_, error)) return true;
    error = L"the profile collection was incomplete and could not be repaired: " + error;
    return false;
}

std::array<PresetSlot, kPresetCount> ProfileManager::legacy_slots() const {
    std::array<PresetSlot, kPresetCount> slots{};
    for (std::size_t index = 0; index < kPresetCount; ++index) {
        const Profile* profile = find_profile(legacy_id(index));
        if (!profile) continue;
        slots[index].occupied = profile->saved;
        slots[index].name = profile->name;
        slots[index].params = profile->settings;
    }
    return slots;
}

bool ProfileManager::replace_profiles(const std::vector<Profile>& profiles,
                                      std::wstring& error) {
    std::vector<Profile> normalized = profiles;
    const auto builtin = std::find_if(
        normalized.begin(), normalized.end(),
        [](const Profile& profile) { return profile.id == kDefaultProfileId; });
    if (builtin == normalized.end()) {
        error = L"the built-in Default profile cannot be removed";
        return false;
    }
    *builtin = default_profile();
    if (!store_.save_profiles(normalized, error)) return false;
    profiles_ = std::move(normalized);
    return true;
}

bool ProfileManager::update_legacy_slots(
    const std::array<PresetSlot, kPresetCount>& slots, std::wstring& error) {
    const std::vector<Profile> previous_profiles = profiles_;
    for (std::size_t index = 0; index < kPresetCount; ++index) {
        Profile* profile = find_profile(legacy_id(index));
        if (!profile) {
            profiles_.push_back({legacy_id(index), L"Default " + std::to_wstring(index + 1),
                                 default_params(), false});
            profile = &profiles_.back();
        }
        profile->name = slots[index].name.empty()
                            ? L"Default " + std::to_wstring(index + 1)
                            : slots[index].name;
        profile->settings = slots[index].occupied ? slots[index].params : default_params();
        profile->saved = slots[index].occupied;
    }

    if (!store_.save_profiles(profiles_, error)) {
        profiles_ = previous_profiles;
        return false;
    }
    if (store_.save_presets(slots, error)) return true;

    profiles_ = previous_profiles;
    std::wstring rollback_error;
    if (!store_.save_profiles(profiles_, rollback_error)) {
        error += L"; profile rollback also failed: " + rollback_error;
    }
    return false;
}

}  // namespace gamma_changer
