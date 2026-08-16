#pragma once

#include "profile_store.h"

#include <array>
#include <string>
#include <vector>

namespace gamma_changer {

// Owns the user-facing profile collection and provides a compatibility bridge
// for the original four preset slots during the incremental UI migration.
class ProfileManager {
public:
    explicit ProfileManager(ProfileStore& store);

    bool load(std::wstring& error);
    const std::vector<Profile>& profiles() const { return profiles_; }

    std::array<PresetSlot, kPresetCount> legacy_slots() const;
    bool update_legacy_slots(const std::array<PresetSlot, kPresetCount>& slots,
                             std::wstring& error);

    // Replaces the whole profile collection atomically. The GUI keeps its own
    // working copy and uses this only as a validated persist boundary.
    bool replace_profiles(const std::vector<Profile>& profiles, std::wstring& error);

private:
    void migrate_legacy_slots();
    Profile* find_profile(const std::wstring& id);
    const Profile* find_profile(const std::wstring& id) const;
    static std::wstring legacy_id(std::size_t index);

    ProfileStore& store_;
    std::vector<Profile> profiles_;
};

}  // namespace gamma_changer
