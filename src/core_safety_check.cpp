#include "calibration_session.h"
#include "gamma_lut.h"
#include "profile_list_logic.h"
#include "profile_manager.h"
#include "profile_store.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
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
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) throw std::runtime_error("could not create the safety-check temp directory");
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

    DisplayInfo display;
    display.device_name = L"\\\\.\\DISPLAY1";
    ok &= expect(display_storage_id(display) == display.device_name,
                 L"storage identity must fall back to the GDI device name");
    display.stable_id = L"stable-monitor-id";
    ok &= expect(display_storage_id(display) == L"stable-monitor-id",
                 L"storage identity must prefer the stable monitor id");
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

    const double nan = std::numeric_limits<double>::quiet_NaN();
    CalibrationSettings invalid{nan, nan, nan, nan, nan, nan};
    const GammaRamp sanitized = build_ramp(invalid);
    ok &= expect(sanitized.channel == build_ramp(default_params()).channel,
                 L"LUT generation must defensively clamp invalid values to defaults");
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
        const std::wstring long_id(120, L'X');
        std::wstring legacy = long_id;
        for (auto& ch : legacy) {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
                ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
                ch = L'_';
            }
        }
        const std::size_t legacy_hash = std::hash<std::wstring>{}(long_id);
        if (legacy.size() > 80) {
            legacy.resize(80);
            legacy += L"-" + std::to_wstring(legacy_hash);
        }
        std::wofstream output(temporary.path / (legacy + L".profile"), std::ios::trunc);
        output << std::setprecision(17)
               << L"gamma " << settings.gamma << L'\n'
               << L"brightness " << settings.brightness << L'\n'
               << L"contrast " << settings.contrast << L'\n'
               << L"r_gain " << settings.r_gain << L'\n'
               << L"g_gain " << settings.g_gain << L'\n'
               << L"b_gain " << settings.b_gain << L'\n';
    }
    ok &= expect(same_settings(store.load_params(std::wstring(120, L'X')), settings),
                 L"legacy std::hash long-name settings must remain readable");

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

    {
        std::array<PresetSlot, kPresetCount> invalid_presets{};
        CalibrationSettings invalid_settings = settings;
        invalid_settings.gamma = 99.0;
        invalid_presets[0] = {true, L"Broken", invalid_settings};
        error.clear();
        ok &= expect(!store.save_presets(invalid_presets, error),
                     L"save_presets must reject out-of-range occupied slots");
        ok &= expect(store.load_presets()[0].name == L"Test Profile",
                     L"rejected preset writes must leave the previous file intact");
    }
    {
        std::array<PresetSlot, kPresetCount> long_name_presets{};
        long_name_presets[0] = {true, std::wstring(65, L'X'), settings};
        error.clear();
        ok &= expect(!store.save_presets(long_name_presets, error),
                     L"save_presets must reject overlong profile names");
    }

    const auto empty_presets = [&] {
        const auto parsed = store.load_presets();
        return std::none_of(parsed.begin(), parsed.end(),
                            [](const PresetSlot& slot) { return slot.occupied; });
    };
    {
        std::wofstream output(temporary.path / L"presets.profile", std::ios::trunc);
        output << L"0 2 \"Bad\" 1 0 1 1 1 1\n";
    }
    ok &= expect(empty_presets(), L"presets with occupied values other than 0/1 must be rejected");
    {
        std::wofstream output(temporary.path / L"presets.profile", std::ios::trunc);
        output << L"0 1 \"A\" 1 0 1 1 1 1\n"
                  L"0 1 \"B\" 1 0 1 1 1 1\n";
    }
    ok &= expect(empty_presets(), L"duplicate preset rows must be rejected");
    {
        std::wofstream output(temporary.path / L"presets.profile", std::ios::trunc);
        output << L"0 1 \"A\" 1 0 1 1 1 1 trailing\n";
    }
    ok &= expect(empty_presets(), L"preset rows with trailing tokens must be rejected");
    {
        std::wofstream output(temporary.path / L"presets.profile", std::ios::trunc);
        output << L"0 1 \"A\" 1 0 1 1 1 1\n"
                  L"1 1 \"B\" 1 0 1 1 1 1\n"
                  L"2 1 \"C\" 1 0 1 1 1 1\n"
                  L"3 1 \"D\" 1 0 1 1 1 1\n"
                  L"4 1 \"E\" 1 0 1 1 1 1\n";
    }
    ok &= expect(empty_presets(), L"preset files with extra rows must be rejected");

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

    std::vector<Profile> duplicate_profiles = profiles;
    duplicate_profiles.push_back(profiles[0]);
    error.clear();
    ok &= expect(!store.save_profiles(duplicate_profiles, error),
                 L"duplicate profile ids must be rejected before writing");
    std::vector<Profile> empty_profiles;
    ok &= expect(!store.save_profiles(empty_profiles, error),
                 L"an empty profile collection must be rejected before writing");
    std::wifstream still_valid_input(valid_profiles_path);
    const std::wstring still_valid_contents((std::istreambuf_iterator<wchar_t>(still_valid_input)),
                                            std::istreambuf_iterator<wchar_t>());
    ok &= expect(still_valid_contents == valid_contents,
                 L"rejected profile writes must leave the previous file intact");

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

    {
        std::wofstream output(temporary.path / L"profiles.v1", std::ios::trunc);
        output << L"GammaChangerProfiles 1 trailing-junk\n";
    }
    ok &= expect(store.load_profiles(loaded_profiles) == ProfileLoadStatus::corrupt,
                 L"profile header trailing data must be rejected");

    {
        std::ofstream output(temporary.path / L"oversized.profile", std::ios::binary);
        output.seekp(64 * 1024);
        output.put('\0');
    }
    CalibrationSettings rejected{};
    ok &= expect(!store.try_load_params(L"oversized", rejected),
                 L"oversized per-display settings must be rejected");

    {
        std::ofstream output(temporary.path / L"profiles.v1", std::ios::binary);
        output.seekp(4 * 1024 * 1024);
        output.put('\0');
    }
    loaded_profiles.clear();
    ok &= expect(store.load_profiles(loaded_profiles) == ProfileLoadStatus::corrupt &&
                     loaded_profiles.empty(),
                 L"oversized profiles files must be reported as corrupt");

    {
        std::ofstream output(temporary.path / L"presets.profile", std::ios::binary);
        output.seekp(64 * 1024);
        output.put('\0');
    }
    const auto rejected_presets = store.load_presets();
    ok &= expect(std::none_of(rejected_presets.begin(), rejected_presets.end(),
                              [](const PresetSlot& slot) { return slot.occupied; }),
                 L"oversized presets files must fall back to empty slots");
    return ok;
}

bool check_profile_list_logic() {
    bool ok = true;
    const std::vector<Profile> profiles{
        {L"profile-1", L"Custom 1", default_params(), true},
        {L"profile-2", L"Custom 2", default_params(), true},
        {L"legacy-slot-1", L"Legacy", default_params(), true},
    };
    ok &= expect(next_profile_id(profiles) == L"profile-3",
                 L"next_profile_id must skip existing ids");
    ok &= expect(next_custom_profile_name(profiles) == L"Custom 3",
                 L"next_custom_profile_name must skip existing names");
    ok &= expect(active_index_after_delete(5, 2, 4) == 3,
                 L"deleting before the active profile must shift the index left");
    ok &= expect(active_index_after_delete(5, 4, 4) == 3,
                 L"deleting the active profile must select the previous one");
    ok &= expect(active_index_after_delete(5, 4, 2) == 2,
                 L"deleting after the active profile must keep its index");
    ok &= expect(active_index_after_delete(1, 0, 0) == 0,
                 L"deleting the only profile must fall back to index zero");
    ok &= expect(clamp_profile_scroll(-10, 10, 36, 142) == 0,
                 L"negative profile scroll must clamp to zero");
    ok &= expect(clamp_profile_scroll(1000, 10, 36, 142) == 218,
                 L"profile scroll must clamp to the last full row");
    ok &= expect(profile_scroll_to_show(9, 10, 0, 279, 421, 36, 34) == 216,
                 L"scroll-to-show must bring the last row into view");
    ok &= expect(profile_scroll_to_show(0, 10, 216, 279, 421, 36, 34) == 0,
                 L"scroll-to-show must bring the first row back into view");

    CalibrationSession session;
    ok &= expect(!session.undo_applies_to(L"display-1"),
                 L"an empty undo snapshot must not apply to any display");
    session.profile_switch_undo_available = true;
    session.undo_display_id = L"display-1";
    ok &= expect(session.undo_applies_to(L"display-1"),
                 L"an undo snapshot must apply to its captured display");
    ok &= expect(!session.undo_applies_to(L"display-2"),
                 L"an undo snapshot must be display-scoped");
    session.mark_clean();
    ok &= expect(!session.dirty, L"mark_clean must clear the dirty flag");
    session.clear_undo();
    ok &= expect(!session.profile_switch_undo_available &&
                     session.undo_display_id.empty(),
                 L"clear_undo must clear the whole undo snapshot");
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

    {
        std::vector<Profile> replaced = manager.profiles();
        replaced[0].name = L"Default Renamed";
        ok &= expect(manager.replace_profiles(replaced, error),
                     L"replace_profiles must persist the whole profile collection");
        profiles.clear();
        ok &= expect(store.load_profiles(profiles) == ProfileLoadStatus::loaded &&
                         !profiles.empty() && profiles[0].name == L"Default Renamed",
                     L"replace_profiles must round-trip the updated collection");
    }

    {
        std::wofstream output(temporary.path / L"profiles.v1", std::ios::trunc);
        output << L"GammaChangerProfiles 1\n\"broken";
    }
    profiles.clear();
    ok &= expect(store.load_profiles(profiles) == ProfileLoadStatus::corrupt,
                 L"damaged profiles must be reported as corrupt");
    const auto legacy_fallback = store.load_presets();
    ok &= expect(legacy_fallback[1].occupied &&
                     legacy_fallback[1].name == L"Legacy Gaming" &&
                     same_settings(legacy_fallback[1].params, settings),
                 L"legacy presets must remain available when profiles.v1 is corrupt");
    return ok;
}

}  // namespace

int wmain() {
    const bool ok = check_validation() && check_lut() && check_store() &&
                    check_profile_migration() && check_profile_list_logic();
    if (!ok) return 1;
    std::wcout << L"core safety checks passed\n";
    return 0;
}
