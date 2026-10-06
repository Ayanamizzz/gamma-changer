#pragma once

#include "app_profile_rules.h"
#include "gamma_types.h"

#include <filesystem>
#include <array>
#include <string>
#include <vector>

namespace gamma_changer {

enum class ProfileLoadStatus {
    loaded,
    missing,
    corrupt,
    unsupported_version,
};

class ProfileStore {
public:
    ProfileStore();
    explicit ProfileStore(std::filesystem::path root);

    GammaParams load_params(const std::wstring& display_id) const;
    bool try_load_params(const std::wstring& display_id, GammaParams& params) const;
    bool save_params(const std::wstring& display_id, const GammaParams& params, std::wstring& error) const;

    std::array<PresetSlot, kPresetCount> load_presets() const;
    bool save_presets(const std::array<PresetSlot, kPresetCount>& presets,
                     std::wstring& error) const;

    ProfileLoadStatus load_profiles(std::vector<Profile>& profiles) const;
    bool save_profiles(const std::vector<Profile>& profiles, std::wstring& error) const;

    std::vector<DisplayProfilePreference> load_profile_preferences() const;
    bool save_profile_preferences(
        const std::vector<DisplayProfilePreference>& preferences,
        std::wstring& error) const;

    // A missing or malformed rule file leaves automatic switching disabled.
    // Version 1 rules receive ids in memory; saves use version 2. Reading never
    // repairs or overwrites the source file.
    AppProfileConfig load_app_profile_config() const;
    bool save_app_profile_config(const AppProfileConfig& config,
                                 std::wstring& error) const;

    bool load_base_ramp(const std::wstring& display_id, GammaRamp& ramp) const;
    bool save_base_ramp(const std::wstring& display_id, const GammaRamp& ramp, std::wstring& error) const;

private:
    std::filesystem::path root_;
    std::filesystem::path params_path(const std::wstring& display_id) const;
    std::filesystem::path ramp_path(const std::wstring& display_id) const;
    std::filesystem::path profiles_path() const;
    std::filesystem::path profile_preferences_path() const;
    std::filesystem::path app_profile_rules_path() const;
};

}  // namespace gamma_changer
