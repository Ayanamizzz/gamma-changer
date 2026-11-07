// Regression coverage for the per-application runtime engine and the
// architecture-review fixes. Every display write goes to a fake backend.
#include "app_profile_runtime.h"
#include "calibration_controller.h"
#include "file_io.h"
#include "gamma_lut.h"
#include "profile_manager.h"
#include "profile_store.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
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
               (L"GammaChangerRuntimeCheck-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) throw std::runtime_error("could not create the runtime-check temp directory");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct FakeBackend final : DisplayRampBackend {
    GammaRamp current = build_ramp(default_params());
    unsigned writes = 0;
    bool fail_reads = false;
    bool read(const DisplayInfo&, GammaRamp& ramp, std::wstring& error) override {
        if (fail_reads) {
            error = L"simulated read failure";
            return false;
        }
        ramp = current;
        return true;
    }
    bool write(const DisplayInfo&, const GammaRamp& ramp, std::wstring&) override {
        current = ramp;
        ++writes;
        return true;
    }
};

bool expect(bool condition, const wchar_t* message) {
    if (!condition) std::wcerr << L"FAIL: " << message << L'\n';
    return condition;
}

CalibrationSettings with_gamma(double gamma) {
    CalibrationSettings settings;
    settings.gamma = gamma;
    return settings;
}

bool check_resolution() {
    const std::wstring app = L"C:\\Games\\Game.exe";
    const std::wstring screen = L"screen-a";
    std::vector<Profile> profiles{{L"saved", L"Saved", with_gamma(1.4), true},
                                  {L"draft", L"Draft", with_gamma(1.8), false},
                                  {L"fallback", L"Fallback", with_gamma(1.2), true}};
    AppProfileConfig config;
    config.enabled = true;
    bool ok = true;

    ok &= expect(!resolve_app_profile(config, profiles, app, screen).profile,
                 L"no rule and no fallback must resolve to desktop settings");

    bind_app_profile_rule(config, {app, screen, L"saved", true, {}, L"Night"});
    auto resolution = resolve_app_profile(config, profiles, L"c:/games/GAME.exe", screen);
    ok &= expect(resolution.rule && resolution.profile && resolution.profile->id == L"saved" &&
                     !resolution.is_fallback(),
                 L"a matching rule must resolve case- and slash-insensitively");
    ok &= expect(!resolve_app_profile(config, profiles, app, L"screen-b").profile,
                 L"rules are display-scoped");

    config.rules.front().profile_id = L"draft";
    ok &= expect(!resolve_app_profile(config, profiles, app, screen).profile,
                 L"a rule pointing at an unsaved profile must not apply");
    set_app_fallback_profile(config, screen, L"fallback");
    resolution = resolve_app_profile(config, profiles, app, screen);
    ok &= expect(resolution.is_fallback() && resolution.profile->id == L"fallback",
                 L"an unusable rule must fall through to the display default");

    set_app_fallback_profile(config, screen, L"deleted");
    ok &= expect(!resolve_app_profile(config, profiles, app, screen).profile,
                 L"a default pointing at a deleted profile must restore desktop settings");

    set_app_fallback_profile(config, screen, L"fallback");
    config.enabled = false;
    ok &= expect(!resolve_app_profile(config, profiles, app, screen).profile,
                 L"a disabled configuration must always resolve to desktop settings");
    return ok;
}

bool check_runtime(const std::filesystem::path& root) {
    FakeBackend backend;
    ProfileStore store(root / L"runtime");
    CalibrationController controller(store, backend);
    AppProfileRuntime runtime(controller);

    DisplayInfo display;
    display.device_name = L"\\\\.\\DISPLAY1";
    display.stable_id = L"monitor-path-a";
    const std::vector<DisplayInfo> displays{display};
    const std::wstring id = display_storage_id(display);

    const GammaRamp desktop = build_ramp(with_gamma(1.05));
    backend.current = desktop;
    std::vector<Profile> profiles{{L"browse", L"Browse", with_gamma(1.2), true},
                                  {L"game", L"Game", with_gamma(1.7), true}};
    AppProfileConfig config;
    bind_app_profile_rule(config, {L"C:\\browser.exe", id, L"browse", true, {}, L""});
    bind_app_profile_rule(config, {L"C:\\game.exe", id, L"game", true, {}, L"Raid night"});
    bool ok = true;

    auto events = runtime.apply(config, profiles, displays, L"C:\\browser.exe", false);
    ok &= expect(events.size() == 1 && events[0].kind == RuntimeEventKind::applied &&
                     events[0].label == L"Browse" &&
                     backend.current.channel == build_ramp(with_gamma(1.2)).channel,
                 L"first switch must apply the bound profile");

    events = runtime.apply(config, profiles, displays, L"C:\\game.exe", false);
    ok &= expect(events.size() == 1 && events[0].label == L"Raid night" &&
                     runtime.active(id)->desktop_ramp.channel == desktop.channel,
                 L"app-to-app switches must keep the original desktop snapshot");

    const unsigned writes = backend.writes;
    events = runtime.apply(config, profiles, displays, L"C:\\game.exe", false);
    ok &= expect(events.empty() && backend.writes == writes,
                 L"re-evaluating the same app must not rewrite the LUT");
    events = runtime.apply(config, profiles, displays, L"C:\\game.exe", true);
    ok &= expect(events.size() == 1 && backend.writes == writes + 1,
                 L"force must rewrite the LUT (display pipeline resets)");

    events = runtime.apply(config, profiles, displays, L"C:\\editor.exe", false);
    ok &= expect(events.size() == 1 && events[0].kind == RuntimeEventKind::restored &&
                     backend.current.channel == desktop.channel && runtime.empty(),
                 L"an unbound app must restore the exact desktop ramp");

    set_app_fallback_profile(config, id, L"browse");
    events = runtime.apply(config, profiles, displays, L"C:\\editor.exe", false);
    ok &= expect(events.size() == 1 && events[0].label == L"Browse (other apps)" &&
                     runtime.active(id) && runtime.active(id)->rule_id.empty(),
                 L"the unassigned-app default must be labelled and carry no rule id");

    // A display that disappears keeps its overlay entry until it returns.
    RuntimeRestoreResult restored = runtime.restore_all({});
    ok &= expect(restored.success && !restored.restored_any && !runtime.empty(),
                 L"restore_all must skip disconnected displays");
    restored = runtime.restore_all(displays);
    ok &= expect(restored.success && restored.restored_any && runtime.empty() &&
                     backend.current.channel == desktop.channel,
                 L"restore_all must return connected displays to the desktop ramp");

    // Capture failure: nothing may be written without a snapshot to return to.
    backend.fail_reads = true;
    const unsigned before_capture = backend.writes;
    events = runtime.apply(config, profiles, displays, L"C:\\game.exe", false);
    ok &= expect(events.size() == 1 && events[0].kind == RuntimeEventKind::capture_failed &&
                     backend.writes == before_capture && runtime.empty(),
                 L"a failed desktop snapshot must leave the display untouched");
    backend.fail_reads = false;

    // A manual preview owns the display: runtime switching must back off.
    std::wstring error;
    ok &= expect(controller.preview(display, with_gamma(2.0), error),
                 L"preview fixture must start");
    events = runtime.apply(config, profiles, displays, L"C:\\game.exe", false);
    ok &= expect(events.size() == 1 && events[0].kind == RuntimeEventKind::apply_failed &&
                     backend.current.channel == build_ramp(with_gamma(2.0)).channel,
                 L"runtime switching must not overwrite an active preview");
    return ok;
}

bool check_preview_identity(const std::filesystem::path& root) {
    // Windows renumbers \\.\DISPLAYn when another screen is unplugged. The
    // preview belongs to the physical monitor, not to the GDI device name.
    FakeBackend backend;
    ProfileStore store(root / L"identity");
    CalibrationController controller(store, backend);
    DisplayInfo before;
    before.device_name = L"\\\\.\\DISPLAY2";
    before.stable_id = L"monitor-path-b";
    DisplayInfo after = before;
    after.device_name = L"\\\\.\\DISPLAY1";

    const GammaRamp original = backend.current;
    std::wstring error;
    bool ok = expect(controller.preview(before, with_gamma(1.9), error),
                     L"preview must start on the original device name");
    ok &= expect(controller.cancel_preview(after, error) &&
                     backend.current.channel == original.channel,
                 L"a renumbered monitor must still be able to cancel its own preview");
    return ok;
}

bool check_setting_steps() {
    bool ok = expect(setting_to_steps(1.15) == 115, L"1.15 must map to step 115, not 114");
    ok &= expect(setting_to_steps(-0.29) == -29, L"negative values must round, not truncate");
    ok &= expect(setting_to_steps(0.29) == 29 && setting_to_steps(4.40) == 440,
                 L"range edges must map exactly");
    for (int steps = -100; steps <= 440; ++steps) {
        if (setting_to_steps(steps_to_setting(steps)) != steps) {
            return expect(false, L"steps must round-trip through settings");
        }
    }
    return ok;
}

bool check_file_io(const std::filesystem::path& root) {
    const auto target = root / L"atomic.txt";
    std::wstring error;
    bool ok = expect(file_io::write_utf8_atomically(target, L"\u4e2d\u6587 v1", L"test", error),
                     L"first atomic write must succeed");
    ok &= expect(file_io::write_utf8_atomically(target, L"\u4e2d\u6587 v2", L"test", error),
                 L"replacing an existing file must succeed");
    std::wstring text;
    ok &= expect(file_io::read_utf8_text(target, text) && text == L"\u4e2d\u6587 v2",
                 L"UTF-8 text must round-trip");
    ok &= expect(!std::filesystem::exists(target.wstring() + L".tmp"),
                 L"a successful write must not leave a temp file behind");
    return ok;
}

bool check_locked_profiles_not_missing(const std::filesystem::path& root) {
    // A profiles.v1 that exists but cannot be opened (locked by a backup tool or
    // antivirus) is not "missing": treating it as missing would run the legacy
    // migration and overwrite the user's profiles.
    const auto data = root / L"locked";
    ProfileStore store(data);
    std::wstring error;
    std::vector<Profile> profiles{builtin_default_profile(),
                                  {L"mine", L"Mine", with_gamma(1.3), true}};
    bool ok = expect(store.save_profiles(profiles, error), L"profile fixture must save");
    const auto path = data / L"profiles.v1";
    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    ok &= expect(lock != INVALID_HANDLE_VALUE, L"fixture must lock profiles.v1");
    std::vector<Profile> loaded;
    ok &= expect(store.load_profiles(loaded) == ProfileLoadStatus::corrupt,
                 L"an unreadable profiles.v1 must report corrupt, not missing");
    ProfileManager manager(store);
    ok &= expect(!manager.load(error), L"the manager must refuse to migrate over it");
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    ok &= expect(store.load_profiles(loaded) == ProfileLoadStatus::loaded && loaded.size() == 2,
                 L"the original profiles must survive");
    return ok;
}

}  // namespace

int wmain() {
    TemporaryDirectory temporary;
    bool ok = check_resolution();
    ok &= check_runtime(temporary.path);
    ok &= check_preview_identity(temporary.path);
    ok &= check_setting_steps();
    ok &= check_file_io(temporary.path);
    ok &= check_locked_profiles_not_missing(temporary.path);
    if (!ok) return 1;
    std::wcout << L"application runtime checks passed\n";
    return 0;
}
