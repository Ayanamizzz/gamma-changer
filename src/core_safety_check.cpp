#include "gamma_lut.h"
#include "profile_manager.h"
#include "profile_store.h"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

using namespace gamma_changer;

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        wchar_t base[MAX_PATH]{};
        GetTempPathW(MAX_PATH, base);
        path = std::filesystem::path(base) /
               (L"GammaChangerSafetyCheck-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

bool expect(bool condition, const wchar_t* message) {
    if (condition) return true;
    std::wcerr << L"FAIL: " << message << L'\n';
    return false;
}

bool same_settings(const CalibrationSettings& left, const CalibrationSettings& right) {
    constexpr double epsilon = 1e-12;
    return std::abs(left.gamma - right.gamma) < epsilon &&
           std::abs(left.brightness - right.brightness) < epsilon &&
           std::abs(left.contrast - right.contrast) < epsilon &&
           std::abs(left.r_gain - right.r_gain) < epsilon &&
           std::abs(left.g_gain - right.g_gain) < epsilon &&
           std::abs(left.b_gain - right.b_gain) < epsilon;
}

bool check_validation() {
    LutGenerator generator;
    std::wstring error;
    bool ok = expect(generator.validate(default_params(), error), L"defaults must validate");

    CalibrationSettings settings = default_params();
    settings.gamma = calibration_ranges::gamma.minimum;
    settings.brightness = calibration_ranges::brightness.maximum;
    settings.contrast = calibration_ranges::contrast.maximum;
    settings.r_gain = calibration_ranges::gain.minimum;
    settings.g_gain = calibration_ranges::gain.maximum;
    ok &= expect(generator.validate(settings, error), L"range endpoints must validate");

    settings = default_params();
    settings.gamma = calibration_ranges::gamma.minimum - 0.01;
    ok &= expect(!generator.validate(settings, error), L"gamma below range must fail");
    settings = default_params();
    settings.contrast = calibration_ranges::contrast.maximum + 0.01;
    ok &= expect(!generator.validate(settings, error), L"contrast above range must fail");
    settings = default_params();
    settings.brightness = std::numeric_limits<double>::quiet_NaN();
    ok &= expect(!generator.validate(settings, error), L"NaN must fail");
    settings = default_params();
    settings.r_gain = std::numeric_limits<double>::infinity();
    ok &= expect(!generator.validate(settings, error), L"infinity must fail");
    settings = default_params();
    ok &= expect(settings_equal(settings, default_params()), L"equal settings must compare equal");
    settings.gamma += 0.01;
    ok &= expect(!settings_equal(settings, default_params()), L"changed settings must compare unequal");
    return ok;
}

bool check_lut() {
    const GammaRamp ramp = build_ramp(default_params());
    bool ok = true;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        ok &= expect(ramp.channel[channel].front() == 0, L"default LUT must begin at zero");
        ok &= expect(ramp.channel[channel].back() == 65535, L"default LUT must end at full scale");
        for (std::size_t index = 1; index < kRampSize; ++index) {
            ok &= expect(ramp.channel[channel][index] >= ramp.channel[channel][index - 1],
                         L"default LUT must be monotonic");
        }
    }
    return ok;
}

bool check_store() {
    TemporaryDirectory temporary;
    ProfileStore store(temporary.path);
    std::wstring error;
    bool ok = true;

    CalibrationSettings settings{1.25, -0.15, 1.20, 1.10, 0.95, 1.05};
    ok &= expect(store.save_params(L"stable-display", settings, error),
                 L"per-display settings must save");
    ok &= expect(same_settings(store.load_params(L"stable-display"), settings),
                 L"per-display settings must round-trip");
    ok &= expect(!std::filesystem::exists(temporary.path / L"stable-display.profile.tmp"),
                 L"successful settings save must not leave a temp file");

    {
        std::wofstream output(temporary.path / L"partial.profile", std::ios::trunc);
        output << L"gamma";
    }
    CalibrationSettings partial{};
    ok &= expect(!store.try_load_params(L"partial", partial),
                 L"a truncated per-display settings line must be rejected");

    {
        std::wofstream output(temporary.path / L"missing-field.profile", std::ios::trunc);
        output << L"gamma 1\nbrightness 0\ncontrast 1\nr_gain 1\ng_gain 1\n";
    }
    ok &= expect(!store.try_load_params(L"missing-field", partial),
                 L"settings with a missing field must be rejected");

    {
        std::wofstream output(temporary.path / L"duplicate-field.profile", std::ios::trunc);
        output << L"gamma 1\ngamma 1\nbrightness 0\ncontrast 1\nr_gain 1\ng_gain 1\nb_gain 1\n";
    }
    ok &= expect(!store.try_load_params(L"duplicate-field", partial),
                 L"settings with duplicate fields must be rejected");

    GammaRamp ramp = build_ramp(settings);
    ok &= expect(store.save_base_ramp(L"stable-display", ramp, error),
                 L"base ramp must save");
    GammaRamp loaded{};
    ok &= expect(store.load_base_ramp(L"stable-display", loaded),
                 L"complete base ramp must load");
    ok &= expect(loaded.channel == ramp.channel, L"base ramp must round-trip exactly");

    const auto ramp_path = temporary.path / L"ramps" / L"truncated.ramp";
    {
        std::ofstream output(ramp_path, std::ios::binary | std::ios::trunc);
        const std::uint16_t sample = 42;
        output.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    }
    ok &= expect(!store.load_base_ramp(L"truncated", loaded),
                 L"truncated base ramp must be rejected");

    const auto oversized_path = temporary.path / L"ramps" / L"oversized.ramp";
    {
        std::ofstream output(oversized_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(ramp.channel[0].data()), sizeof(ramp.channel));
        output.put('\0');
    }
    ok &= expect(!store.load_base_ramp(L"oversized", loaded),
                 L"oversized base ramp must be rejected");

    std::array<PresetSlot, kPresetCount> presets{};
    presets[0] = {true, L"Test Profile", settings};
    ok &= expect(store.save_presets(presets, error), L"legacy presets must save atomically");
    const auto loaded_presets = store.load_presets();
    ok &= expect(loaded_presets[0].occupied && loaded_presets[0].name == L"Test Profile" &&
                     same_settings(loaded_presets[0].params, settings),
                 L"legacy presets must round-trip");

    const std::vector<Profile> profiles{{L"test-id", L"Test Profile", settings, true}};
    ok &= expect(store.save_profiles(profiles, error), L"profiles v1 must save atomically");
    std::vector<Profile> loaded_profiles;
    ok &= expect(store.load_profiles(loaded_profiles) == ProfileLoadStatus::loaded &&
                     loaded_profiles.size() == 1 &&
                     loaded_profiles[0].id == L"test-id" &&
                     same_settings(loaded_profiles[0].settings, settings),
                  L"profiles v1 must round-trip");

    const auto valid_profiles_path = temporary.path / L"profiles.v1";
    std::wifstream valid_input(valid_profiles_path);
    const std::wstring valid_contents((std::istreambuf_iterator<wchar_t>(valid_input)),
                                      std::istreambuf_iterator<wchar_t>());
    std::vector<Profile> invalid_profiles = profiles;
    invalid_profiles.push_back({L"", L"Broken", settings, true});
    error.clear();
    ok &= expect(!store.save_profiles(invalid_profiles, error),
                 L"an invalid profile collection must be rejected as a whole");
    std::wifstream unchanged_input(valid_profiles_path);
    const std::wstring unchanged_contents((std::istreambuf_iterator<wchar_t>(unchanged_input)),
                                          std::istreambuf_iterator<wchar_t>());
    ok &= expect(valid_contents == unchanged_contents,
                 L"rejecting invalid profiles must not overwrite the existing file");

    {
        std::wofstream output(temporary.path / L"profiles.v1", std::ios::trunc);
        output << L"GammaChangerProfiles 1\n"
                  L"\"valid\" \"Valid\" 1 1 0 1 1 1 1\n"
                  L"\"broken";
    }
    loaded_profiles.clear();
    ok &= expect(store.load_profiles(loaded_profiles) == ProfileLoadStatus::corrupt &&
                     loaded_profiles.empty(),
                 L"damaged profiles must be reported without partial recovery");

    {
        std::wofstream output(temporary.path / L"profiles.v1", std::ios::trunc);
        output << L"GammaChangerProfiles 999\n";
    }
    ok &= expect(store.load_profiles(loaded_profiles) == ProfileLoadStatus::unsupported_version,
                 L"newer profile versions must not be overwritten");
    return ok;
}

bool check_profile_migration() {
    TemporaryDirectory temporary;
    ProfileStore store(temporary.path);
    std::array<PresetSlot, kPresetCount> presets{};
    CalibrationSettings settings{1.35, -0.10, 1.15, 1.05, 0.95, 1.10};
    presets[1] = {true, L"Legacy Gaming", settings};
    std::wstring error;
    bool ok = expect(store.save_presets(presets, error),
                     L"legacy migration fixture must save");

    ProfileManager manager(store);
    ok &= expect(manager.load(error), L"legacy profiles must migrate");
    const auto migrated = manager.legacy_slots();
    ok &= expect(migrated[1].occupied && migrated[1].name == L"Legacy Gaming" &&
                     same_settings(migrated[1].params, settings),
                 L"legacy slot content must survive migration");

    std::vector<Profile> profiles;
    ok &= expect(store.load_profiles(profiles) == ProfileLoadStatus::loaded &&
                     profiles.size() == kPresetCount + 1,
                 L"migration must create one builtin and four compatibility profiles");
    return ok;
}

}  // namespace

int wmain() {
    const bool ok = check_validation() && check_lut() && check_store() &&
                    check_profile_migration();
    if (!ok) return 1;
    std::wcout << L"core safety checks passed\n";
    return 0;
}
