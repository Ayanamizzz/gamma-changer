// Native GUI regression checks: the real window glue and window procedure,
// with isolated storage and a fake gamma backend (no display is ever changed).
#include "ui/gui_internal.h"

#include <filesystem>
#include <iostream>

using namespace gamma_changer;

namespace {

DWORD started = 0;
DWORD first_delay = 0;
unsigned evaluations = 0;
bool first_force = false;
bool last_force = false;

struct FakeBackend final : DisplayRampBackend {
    GammaRamp current = build_ramp(default_params());
    unsigned writes = 0;
    bool fail_writes = false;
    bool read(const DisplayInfo&, GammaRamp& ramp, std::wstring&) override {
        ramp = current;
        return true;
    }
    bool write(const DisplayInfo&, const GammaRamp& ramp, std::wstring& error) override {
        if (fail_writes) {
            error = L"the display rejected the ramp";
            return false;
        }
        current = ramp;
        ++writes;
        return true;
    }
};

bool check_application_switching(const std::filesystem::path& root) {
    FakeBackend backend;
    GuiState gui(root, backend);
    DisplayInfo display;
    display.device_name = L"FAKE-DISPLAY";
    display.stable_id = L"fake-screen";
    gui.displays.push_back(display);
    CalibrationSettings desktop;
    desktop.gamma = 1.05;
    CalibrationSettings common;
    common.gamma = 1.20;
    CalibrationSettings gaming;
    gaming.gamma = 1.65;
    CalibrationSettings old;
    old.gamma = 0.80;
    backend.current = build_ramp(desktop);
    gui.profiles = {{L"old", L"Old", old, true}, {L"common", L"Common", common, true},
                    {L"gaming", L"Gaming", gaming, true}};
    const std::wstring browser = L"C:\\Apps\\Browser.exe";
    const std::wstring game = L"C:\\Games\\Game.exe";
    bind_app_profile_rule(gui.app_profile_config, {browser, display.stable_id, L"old", true});
    bind_app_profile_rule(gui.app_profile_config, {game, display.stable_id, L"gaming", true});
    bind_app_profile_rule(gui.app_profile_config, {browser, display.stable_id, L"common", true});
    std::wstring error;
    if (!gui.store.save_params(display.stable_id, desktop, error) ||
        !gui.store.save_app_profile_config(gui.app_profile_config, error)) {
        std::wcerr << L"FAIL: assignment fixture persistence: " << error << L'\n';
        return false;
    }
    gui.app_profile_config = gui.store.load_app_profile_config();
    if (!gui.app_profile_config.enabled) return false;

    apply_application_profiles(gui, browser, true);
    bool ok = backend.current.channel == build_ramp(common).channel;
    apply_application_profiles(gui, game, true);
    ok &= backend.current.channel == build_ramp(gaming).channel;
    apply_application_profiles(gui, browser, true);
    ok &= backend.current.channel == build_ramp(common).channel;
    const unsigned before_duplicate = backend.writes;
    apply_application_profiles(gui, browser);
    ok &= backend.writes == before_duplicate;
    backend.current = build_ramp(default_params()); // Simulate a graphics pipeline reset.
    apply_application_profiles(gui, browser, true);
    ok &= backend.current.channel == build_ramp(common).channel && backend.writes == before_duplicate + 1;
    apply_application_profiles(gui, L"C:\\Apps\\Unbound.exe", true);
    ok &= backend.current.channel == build_ramp(desktop).channel && gui.app_runtime.empty();

    set_app_fallback_profile(gui.app_profile_config, display.stable_id, L"common");
    if (!gui.store.save_app_profile_config(gui.app_profile_config, error)) return false;
    gui.app_profile_config = gui.store.load_app_profile_config();
    apply_application_profiles(gui, game, true);
    ok &= backend.current.channel == build_ramp(gaming).channel;
    apply_application_profiles(gui, L"C:\\Apps\\UnboundBrowser.exe", true);
    ok &= backend.current.channel == build_ramp(common).channel &&
          gui.app_runtime.active(display.stable_id) &&
          gui.app_runtime.active(display.stable_id)->rule_id.empty();
    apply_application_profiles(gui, L"C:\\Apps\\UnboundEditor.exe", true);
    ok &= backend.current.channel == build_ramp(common).channel;
    apply_application_profiles(gui, L"", true);
    ok &= backend.current.channel == build_ramp(common).channel;
    gui.app_profile_config.enabled = false;
    apply_application_profiles(gui, L"C:\\Apps\\Unbound.exe", true);
    ok &= backend.current.channel == build_ramp(desktop).channel && gui.app_runtime.empty();
    gui.app_profile_config.enabled = true;
    set_app_fallback_profile(gui.app_profile_config, display.stable_id, L"");
    apply_application_profiles(gui, game, true);
    apply_application_profiles(gui, L"C:\\Apps\\Unbound.exe", true);
    ok &= backend.current.channel == build_ramp(desktop).channel;
    CalibrationSettings saved;
    ok &= gui.store.try_load_params(display.stable_id, saved) && settings_equal(saved, desktop);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": browser/game/browser -> Common/Gaming/Common; game/unbound app -> Gaming/Common; saved fallback; auto-off and desktop-default restore; saved desktop unchanged\n";
    return ok;
}

// The glue behind the Apps card and the tray: every change saves at once,
// adding an app turns Follow apps on, and other apps keep what they had.
bool check_app_assignment(const std::filesystem::path& root) {
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerAssignmentOwner";
    if (!RegisterClassW(&owner_class)) return false;

    DisplayInfo display;
    display.device_name = L"FAKE-DISPLAY";
    display.stable_id = L"fake-screen";
    CalibrationSettings reading;
    reading.gamma = 1.20;
    CalibrationSettings gaming;
    gaming.gamma = 1.65;
    const std::vector<Profile> profiles{builtin_default_profile(),
                                        {L"reading", L"Reading", reading, true},
                                        {L"gaming", L"Gaming", gaming, true}};
    const std::wstring game = L"C:\\Games\\Game.exe";
    const std::wstring other_app = L"C:\\Apps\\Editor.exe";
    const auto shows = [](const FakeBackend& backend, const CalibrationSettings& settings) {
        return backend.current.channel == build_ramp(settings).channel;
    };
    const auto other_apps_id = [&](const AppProfileConfig& config) {
        const auto* pinned = find_app_fallback_profile(config, display.stable_id);
        return pinned ? pinned->profile_id : std::wstring{};
    };
    const auto make_owner = [&] {
        return CreateWindowExW(0, owner_class.lpszClassName, L"Owner", WS_OVERLAPPED, 0, 0, 100,
                               100, nullptr, nullptr, owner_class.hInstance, nullptr);
    };
    const auto close = [](GuiState& gui) {
        gui.foreground_watcher.stop();
        KillTimer(gui.window, kForegroundProfileTimer);
        DestroyWindow(gui.window);
    };

    bool ok = true;
    int step_number = 0;
    const auto step = [&](bool condition) {
        ++step_number;
        if (!condition) std::cout << "  app assignment step " << step_number << " failed\n";
        ok &= condition;
    };

    {
        FakeBackend backend;
        GuiState gui(root / L"first", backend);
        gui.displays.push_back(display);
        gui.profiles = profiles;
        gui.active_preset = 1;  // Reading is what the desktop shows
        gui.window = make_owner();
        if (!gui.window) return false;

        // Add an app to a profile that is not open: one call, saved, switching
        // on, and everything else keeps the profile on screen.
        step(assign_app_to_profile(gui, 2, game));
        AppProfileConfig saved = gui.store.load_app_profile_config();
        step(saved.enabled && gui.app_profile_config.enabled &&
             apps_using_profile(saved, display.stable_id, L"gaming").size() == 1 &&
             other_apps_id(saved) == L"reading");

        // Opening Gaming to edit it shows it, but changes nothing for apps.
        select_preset(gui, 2);
        saved = gui.store.load_app_profile_config();
        step(gui.active_preset == 2 && saved.enabled && other_apps_id(saved) == L"reading" &&
             shows(backend, gaming));
        apply_application_profiles(gui, other_app, true);
        step(shows(backend, reading));
        apply_application_profiles(gui, game, true);
        step(shows(backend, gaming));

        // "All other apps" moves to the open profile; then the app is removed.
        step(use_profile_for_other_apps(gui, 2) &&
             other_apps_id(gui.store.load_app_profile_config()) == L"gaming");
        step(unassign_app(gui, game) &&
             apps_using_profile(gui.store.load_app_profile_config(), display.stable_id, L"gaming")
                 .empty() &&
             !unassign_app(gui, game));

        // A profile chosen from the tray is used for everything: switching off.
        apply_application_profiles(gui, other_app, true);
        step(apply_profile_from_tray(gui, 1) && !gui.store.load_app_profile_config().enabled &&
             gui.active_preset == 1 && gui.app_runtime.empty() && shows(backend, reading));

        // Turning it back on leaves the desktop as it is.
        step(set_automatic_profiles_enabled(gui, true) &&
             other_apps_id(gui.store.load_app_profile_config()) == L"gaming");
        close(gui);
    }
    {
        // The usual first run: make a profile, tune it, then give it an app.
        // The profile is the one on screen, so other apps keep the one before.
        FakeBackend backend;
        GuiState gui(root / L"second", backend);
        gui.displays.push_back(display);
        gui.profiles = profiles;
        gui.active_preset = 1;
        gui.window = make_owner();
        if (!gui.window) return false;
        select_preset(gui, 2);
        step(!gui.app_profile_config.enabled && other_apps_id(gui.app_profile_config).empty());
        step(assign_app_to_profile(gui, 2, game) && gui.app_profile_config.enabled &&
             other_apps_id(gui.app_profile_config) == L"reading");

        // Turning switching on with nothing pinned keeps the open profile.
        AppProfileConfig cleared = gui.app_profile_config;
        cleared.enabled = false;
        set_app_fallback_profile(cleared, display.stable_id, L"");
        gui.app_profile_config = cleared;
        step(set_automatic_profiles_enabled(gui, true) &&
             other_apps_id(gui.store.load_app_profile_config()) == L"gaming");

        // The "Add app" menu for the open profile (Gaming, which has the game):
        // last used app first, the game marked as already here, an app of
        // another profile announced as a move, protected apps not selectable.
        assign_app_to_profile(gui, 1, L"C:\\Apps\\Browser.exe");
        gui.foreground_application = L"c:/apps/editor.exe";
        gui.running_apps = [] {
            return std::vector<RunningApplication>{
                {L"", L"Protected game", 11, ERROR_ACCESS_DENIED},
                {L"C:\\Games\\Game.exe", L"Game & window", 22, ERROR_SUCCESS},
                {L"C:\\Apps\\Browser.exe", L"Browser", 33, ERROR_SUCCESS},
                {L"C:\\Apps\\Editor.exe", L"Editor", 44, ERROR_SUCCESS},
            };
        };
        const AddAppMenu add = build_add_app_menu(gui);
        const auto item_text = [&](int position) {
            wchar_t text[160]{};
            GetMenuStringW(add.menu, static_cast<UINT>(position), text,
                           static_cast<int>(std::size(text)), MF_BYPOSITION);
            return std::wstring(text);
        };
        const auto item_state = [&](int position) {
            return GetMenuState(add.menu, static_cast<UINT>(position), MF_BYPOSITION);
        };
        step(add.menu && GetMenuItemCount(add.menu) == 6 && add.paths.size() == 3 &&
             add.paths[0] == L"C:\\Apps\\Editor.exe" &&
             item_text(0) == L"Editor.exe\tEditor" && (item_state(0) & MF_GRAYED) == 0);
        step((item_state(1) & MF_GRAYED) != 0 &&
             item_text(1).find(L"Protected game") != std::wstring::npos);
        step(item_text(2) == L"Game.exe\tGame && window" &&
             (item_state(2) & (MF_GRAYED | MF_CHECKED)) == (MF_GRAYED | MF_CHECKED));
        step(item_text(3) == L"Browser.exe\tmoves from Reading" &&
             (item_state(3) & MF_GRAYED) == 0 &&
             GetMenuItemID(add.menu, 5) == kAddAppBrowseCommand);
        if (add.menu) DestroyMenu(add.menu);

        // Unsaved profiles and unknown indexes are refused without saving.
        gui.profiles.push_back({L"draft", L"Draft", default_params(), false});
        step(!assign_app_to_profile(gui, 3, game) && !assign_app_to_profile(gui, 9, game) &&
             !assign_app_to_profile(gui, 2, L""));
        close(gui);
    }
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": adding an app saves, turns Follow apps on and pins other apps; opening a "
                 "profile changes nothing for apps; tray choice turns switching off\n";
    return ok;
}

// What "Follow apps" does with whatever is in front, through the injectable
// foreground probe: an app with a rule, an app without one, this window
// hidden (a tray menu) or visible (the editor), a failed lookup, no window.
bool check_foreground_evaluation(const std::filesystem::path& root) {
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerEvaluationOwner";
    if (!RegisterClassW(&owner_class)) return false;

    FakeBackend backend;
    GuiState gui(root, backend);
    DisplayInfo display;
    display.device_name = L"FAKE-DISPLAY";
    display.stable_id = L"fake-screen";
    gui.displays.push_back(display);
    CalibrationSettings desktop;
    desktop.gamma = 1.05;
    CalibrationSettings gaming;
    gaming.gamma = 1.65;
    backend.current = build_ramp(desktop);
    gui.profiles = {builtin_default_profile(), {L"gaming", L"Gaming", gaming, true}};
    const std::wstring game = L"C:\\Games\\Game.exe";
    bind_app_profile_rule(gui.app_profile_config, {game, display.stable_id, L"gaming", true});
    std::wstring error;
    if (!gui.store.save_params(display.stable_id, desktop, error) ||
        !gui.store.save_app_profile_config(gui.app_profile_config, error)) {
        std::wcerr << L"FAIL: evaluation fixture persistence: " << error << L'\n';
        return false;
    }
    gui.app_profile_config = gui.store.load_app_profile_config();
    // Off-screen and never activated; only its visibility matters here.
    gui.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, owner_class.lpszClassName,
                                 L"Owner", WS_POPUP, -32000, -32000, 100, 100, nullptr, nullptr,
                                 owner_class.hInstance, nullptr);
    if (!gui.window || !gui.app_profile_config.enabled) return false;

    ForegroundApp front;
    gui.foreground_app = [&] { return front; };
    const auto external = [&](const std::wstring& path, DWORD lookup_error = ERROR_SUCCESS) {
        front = ForegroundApp{true, false, path, lookup_error};
        evaluate_automatic_profiles(gui);
    };
    const auto shows = [&](const CalibrationSettings& settings) {
        return backend.current.channel == build_ramp(settings).channel;
    };
    bool ok = true;
    int step_number = 0;
    const auto step = [&](bool condition) {
        ++step_number;
        if (!condition) std::cout << "  evaluation step " << step_number << " failed\n";
        ok &= condition;
    };

    external(game);
    step(shows(gaming) && gui.foreground_application == game);
    external(L"C:\\Apps\\Unbound.exe");
    step(shows(desktop) && gui.foreground_application == L"C:\\Apps\\Unbound.exe");

    // A hidden window of ours in front (a tray menu) keeps the last app's
    // profile and still remembers that app for the "Add app" menu.
    external(game);
    front = ForegroundApp{true, true, L"", ERROR_SUCCESS};
    evaluate_automatic_profiles(gui);
    step(shows(gaming) && gui.foreground_application == game);

    // The visible editor shows the selected profile instead.
    ShowWindow(gui.window, SW_SHOWNOACTIVATE);
    evaluate_automatic_profiles(gui);
    step(shows(desktop) && gui.app_runtime.empty() && gui.foreground_application == game);
    ShowWindow(gui.window, SW_HIDE);

    // A protected app: its path is unknown, so other apps' profile applies and
    // the reason is kept for the status line.
    external(game);
    external(L"", ERROR_ACCESS_DENIED);
    step(shows(desktop) && gui.foreground_lookup_error == ERROR_ACCESS_DENIED &&
         gui.status_tone == StatusTone::error);

    // Nothing in front (the secure desktop, a lock screen): nothing changes.
    external(game);
    const unsigned writes = backend.writes;
    front = ForegroundApp{};
    evaluate_automatic_profiles(gui);
    step(shows(gaming) && backend.writes == writes && gui.foreground_application == game);

    DestroyWindow(gui.window);
    gui.window = nullptr;
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": foreground app with a rule / without one; tray menu keeps the app's "
                 "profile; visible editor shows the selected one; protected app; no window\n";
    return ok;
}

// Reset and "Hold to compare" while an edit has not auto-saved yet. Real
// trackbars stand in for the editor; they mirror gui.session.working.
bool check_unsaved_edit_safety(const std::filesystem::path& root) {
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerEditOwner";
    if (!RegisterClassW(&owner_class)) return false;

    FakeBackend backend;
    GuiState gui(root, backend);
    DisplayInfo display;
    display.device_name = L"FAKE-DISPLAY";
    display.stable_id = L"fake-screen";
    gui.displays.push_back(display);
    gui.active_display_index = 0;
    CalibrationSettings reading;
    reading.gamma = 1.20;
    gui.profiles = {builtin_default_profile(), {L"reading", L"Reading", reading, true}};
    gui.active_preset = 1;
    gui.window = CreateWindowExW(0, owner_class.lpszClassName, L"Owner", WS_OVERLAPPED, 0, 0, 200,
                                 200, nullptr, nullptr, owner_class.hInstance, nullptr);
    if (!gui.window) return false;
    SetWindowLongPtrW(gui.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&gui));
    const auto child = [&](const wchar_t* class_name, int id, DWORD style) {
        return CreateWindowExW(0, class_name, L"", WS_CHILD | style, 0, 0, 100, 20, gui.window,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               owner_class.hInstance, nullptr);
    };
    gui.gamma_slider = child(TRACKBAR_CLASSW, kGammaSlider, 0);
    gui.brightness_slider = child(TRACKBAR_CLASSW, kBrightnessSlider, 0);
    gui.contrast_slider = child(TRACKBAR_CLASSW, kContrastSlider, 0);
    gui.r_gain_slider = child(TRACKBAR_CLASSW, kRGainSlider, 0);
    gui.g_gain_slider = child(TRACKBAR_CLASSW, kGGainSlider, 0);
    gui.b_gain_slider = child(TRACKBAR_CLASSW, kBGainSlider, 0);
    for (const auto& binding : adjustment_bindings(gui)) {
        if (!binding.slider) return false;
        SendMessageW(binding.slider, TBM_SETRANGEMIN, FALSE, setting_to_steps(binding.range.minimum));
        SendMessageW(binding.slider, TBM_SETRANGEMAX, TRUE, setting_to_steps(binding.range.maximum));
    }
    HWND compare = child(L"BUTTON", kBeforeAfter, BS_OWNERDRAW);
    if (!compare || !SetWindowSubclass(compare, compare_proc, 1, 0)) return false;

    bool ok = true;
    int step_number = 0;
    const auto step = [&](bool condition) {
        ++step_number;
        if (!condition) std::cout << "  unsaved edit step " << step_number << " failed\n";
        ok &= condition;
    };
    const auto shows = [&](const CalibrationSettings& settings) {
        return backend.current.channel == build_ramp(settings).channel;
    };
    const auto open_profile = [&](std::size_t index) {
        std::wstring error;
        gui.active_preset = index;
        const CalibrationSettings settings = gui.profiles[index].settings;
        const bool applied = gui.controller.apply_and_save(display, settings, error);
        set_params_to_controls(gui, settings);
        mark_committed(gui, settings);
        gui.session.clear_undo();
        return applied;
    };
    const auto drag_gamma = [&](double value) {
        SendMessageW(gui.gamma_slider, TBM_SETPOS, TRUE, setting_to_steps(value));
        mark_changed(gui);
    };
    const auto timer_pending = [&](UINT timer) { return KillTimer(gui.window, timer) != FALSE; };

    // Reset before the edit auto-saved: Ctrl+Z brings the edit back, not the
    // older saved value.
    step(open_profile(1));
    drag_gamma(1.50);
    CalibrationSettings edited = reading;
    edited.gamma = 1.50;
    step(gui.session.dirty && settings_equal(gui.session.working, edited));
    reset_selected(gui);
    step(!gui.session.dirty && settings_equal(gui.session.working, default_params()) &&
         settings_equal(gui.profiles[1].settings, default_params()) && shows(default_params()));
    undo_profile_switch(gui);
    step(!gui.session.dirty && settings_equal(gui.session.working, edited) &&
         settings_equal(gui.profiles[1].settings, edited) && shows(edited));
    // The session is the model; the controls are only its view.
    step(settings_equal(read_adjustment_controls(gui), gui.session.working));

    // A neutral profile with an unsaved edit: Reset still resets, and undoes.
    step(open_profile(0));
    drag_gamma(1.30);
    edited = default_params();
    edited.gamma = 1.30;
    reset_selected(gui);
    step(gui.session.profile_switch_undo_available &&
         settings_equal(gui.session.working, default_params()));
    undo_profile_switch(gui);
    step(settings_equal(gui.session.working, edited) &&
         settings_equal(gui.profiles[0].settings, edited) && shows(edited));
    // Nothing to reset and nothing pending: no undo step is created.
    step(open_profile(0));
    gui.profiles[0].settings = default_params();
    step(open_profile(0));
    reset_selected(gui);
    step(!gui.session.profile_switch_undo_available);

    // Hold to compare with a pending edit. If the display rejects the neutral
    // preview, the edit keeps its preview and auto-save timers.
    drag_gamma(1.40);
    edited = default_params();
    edited.gamma = 1.40;
    backend.fail_writes = true;
    SendMessageW(compare, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    step(!gui.session.comparing_original && gui.session.dirty && timer_pending(kPreviewTimer) &&
         timer_pending(kAutoSaveTimer));
    SendMessageW(compare, WM_LBUTTONUP, 0, 0);
    backend.fail_writes = false;

    // When it works: neutral while held with the timers paused, then back to
    // the edit with auto-save pending again.
    mark_changed(gui);
    SendMessageW(compare, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    step(gui.session.comparing_original);
    step(shows(default_params()));
    step(!timer_pending(kPreviewTimer));
    step(!timer_pending(kAutoSaveTimer));
    SendMessageW(compare, WM_LBUTTONUP, 0, 0);
    step(!gui.session.comparing_original && gui.session.dirty && shows(edited) &&
         timer_pending(kAutoSaveTimer));

    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kForegroundProfileTimer);
    DestroyWindow(gui.window);
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": Reset undo restores an edit that had not auto-saved; a failed compare "
                 "preview keeps the pending edit's timers; compare pauses and resumes them\n";
    return ok;
}

LRESULT CALLBACK fixture_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == kForegroundProfileMessage) return window_proc(window, message, wparam, lparam);
    if (message == WM_TIMER && wparam == kForegroundProfileTimer) {
        auto* gui = state(window);
        last_force = gui->foreground_reapply_requested;
        if (evaluations++ == 0) {
            first_delay = GetTickCount() - started;
            first_force = last_force;
        }
        return window_proc(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool check_schedule(const std::filesystem::path& data) {
    FakeBackend backend;
    GuiState gui(data / L"timers", backend); // No displays or bindings; all ramp access is fake.
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = fixture_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"GammaChangerScheduleFixture";
    if (!RegisterClassW(&window_class)) return false;
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"Schedule fixture", WS_OVERLAPPED,
                                   0, 0, 100, 100, nullptr, nullptr, window_class.hInstance, nullptr);
    if (!window) {
        UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
        return false;
    }
    gui.window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&gui));
    started = GetTickCount();
    SendMessageW(window, kForegroundProfileMessage, 1, 0);
    unsigned notifications = 0;
    while (GetTickCount() - started < 550) {
        SendMessageW(window, kForegroundProfileMessage, 0, 0);
        ++notifications;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    KillTimer(window, kForegroundProfileTimer);
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    const bool ok = evaluations >= 2 && first_delay < 350 && first_force && !last_force;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << notifications << " continuous schedule notifications; "
              << evaluations << " evaluations; first evaluation after " << first_delay
              << " ms; focus reapply preserved=" << first_force << "; later forced=" << last_force << '\n';
    return ok;
}

}  // namespace

int main() {
    // CTest runs in its writable build directory; keep PID-isolated temporary
    // files there so the fixture also works in restricted local workspaces.
    const auto temporary = std::filesystem::current_path();
    const auto data = temporary /
        (L"gamma-changer-foreground-check-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    // Every store is explicit and every LUT call uses FakeBackend. Redirect
    // logger output as well so this fixture cannot alter real user settings.
    std::error_code directory_error;
    if (!std::filesystem::equivalent(data.parent_path(), temporary, directory_error) ||
        directory_error || !SetEnvironmentVariableW(L"LOCALAPPDATA", data.c_str())) {
        std::cerr << "FAIL: cannot initialize isolated fixture directory\n";
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    bool ok = check_application_switching(data / L"assignments");
    ok = check_app_assignment(data / L"glue") && ok;
    ok = check_foreground_evaluation(data / L"evaluation") && ok;
    ok = check_unsaved_edit_safety(data / L"edits") && ok;
    ok = check_schedule(data) && ok;
    std::error_code cleanup_error;
    std::filesystem::remove_all(data, cleanup_error);
    if (cleanup_error) {
        std::cerr << "FAIL: cannot remove fixture temporary directory: " << cleanup_error.message() << '\n';
        ok = false;
    }
    return ok ? 0 : 1;
}
