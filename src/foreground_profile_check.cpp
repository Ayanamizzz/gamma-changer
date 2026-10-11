#include "ui/gui_internal.h"
// Keep the actual native dialogs and anonymous draft helpers under regression
// coverage without introducing a production library solely for test access.
#include "ui/app_profiles.cpp"
#include <filesystem>
#include <iostream>

namespace gamma_changer {
LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
}
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
    bool read(const DisplayInfo&, GammaRamp& ramp, std::wstring&) override {
        ramp = current;
        return true;
    }
    bool write(const DisplayInfo&, const GammaRamp& ramp, std::wstring&) override {
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
    ok &= backend.current.channel == build_ramp(desktop).channel && gui.automatic_display_profiles.empty();

    set_app_fallback_profile(gui.app_profile_config, display.stable_id, L"common");
    if (!gui.store.save_app_profile_config(gui.app_profile_config, error)) return false;
    gui.app_profile_config = gui.store.load_app_profile_config();
    apply_application_profiles(gui, game, true);
    ok &= backend.current.channel == build_ramp(gaming).channel;
    apply_application_profiles(gui, L"C:\\Apps\\UnboundBrowser.exe", true);
    ok &= backend.current.channel == build_ramp(common).channel &&
          gui.automatic_display_profiles.at(display.stable_id).rule_id.empty();
    apply_application_profiles(gui, L"C:\\Apps\\UnboundEditor.exe", true);
    ok &= backend.current.channel == build_ramp(common).channel;
    apply_application_profiles(gui, L"", true);
    ok &= backend.current.channel == build_ramp(common).channel;
    gui.app_profile_config.enabled = false;
    apply_application_profiles(gui, L"C:\\Apps\\Unbound.exe", true);
    ok &= backend.current.channel == build_ramp(desktop).channel && gui.automatic_display_profiles.empty();
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

bool check_default_dialog(const std::filesystem::path& root) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&controls)) return false;
    HMODULE resources = GetModuleHandleW(nullptr);
    if (!resources) return false;
    FakeBackend backend;
    GuiState gui(root, backend);
    DisplayInfo display;
    display.device_name = L"FAKE-DISPLAY";
    display.stable_id = L"fake-screen";
    gui.displays.push_back(display);
    gui.profiles = {{L"common", L"Common", default_params(), true}};
    gui.auto_switch_paused = true;
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerHiddenRulesOwner";
    if (!RegisterClassW(&owner_class)) { return false; }
    gui.window = CreateWindowExW(0, owner_class.lpszClassName, L"Hidden rules owner", WS_OVERLAPPED,
                                  0, 0, 100, 100, nullptr, nullptr, owner_class.hInstance, nullptr);
    bool ok = gui.window != nullptr;
    for (const int step : {0, 1, 2}) {
        const bool save = step != 0;
        if (step == 2) {
            gui.app_profile_config.enabled = false;
            gui.auto_switch_paused = true;
        }
        RulesDialogState draft{gui, gui.app_profile_config, gui.profiles};
        HWND dialog = CreateDialogParamW(resources, MAKEINTRESOURCEW(IDD_APP_RULES), gui.window,
                                          rules_dialog_proc, reinterpret_cast<LPARAM>(&draft));
        if (!dialog) { ok = false; break; }
        ok &= !draft.resume_on_save && draft.config.enabled == gui.app_profile_config.enabled;
        RECT client{};
        GetClientRect(dialog, &client);
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT rectangle{};
            GetWindowRect(child, &rectangle);
            MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rectangle), 2);
            ok &= rectangle.left >= 0 && rectangle.top >= 0 && rectangle.right <= client.right && rectangle.bottom <= client.bottom;
        }
        HWND combo = GetDlgItem(dialog, IDC_FALLBACK_PROFILE);
        ok &= combo && SendMessageW(combo, CB_GETCOUNT, 0, 0) == 2;
        SendMessageW(combo, CB_SETCURSEL, 1, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_FALLBACK_PROFILE, step == 2 ? CBN_SELENDOK : CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(combo));
        if (step < 2) ok &= !find_app_fallback_profile(gui.app_profile_config, display.stable_id);
        SendMessageW(dialog, WM_COMMAND, save ? IDOK : IDCANCEL, 0);
        const auto* preference = find_app_fallback_profile(gui.app_profile_config, display.stable_id);
        ok &= save ? preference && preference->profile_id == L"common" && gui.app_profile_config.enabled && !gui.auto_switch_paused
                   : !preference && gui.auto_switch_paused;
        if (IsWindow(dialog)) DestroyWindow(dialog);
    }
    gui.foreground_watcher.stop();
    if (gui.window) DestroyWindow(gui.window);
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);

    std::cout << (ok ? "PASS" : "FAIL") << ": hidden native default chooser; control bounds; cancel preserves preference; save and reselect persist/resume\n";
    return ok;
}

bool check_running_picker() {
    HMODULE resources = GetModuleHandleW(nullptr);
    if (!resources) return false;
    bool ok = true;
    for (const bool refresh : {false, true}) {
        RunningAppPickerState picker{{
            {L"", L"Protected game", 11, ERROR_ACCESS_DENIED},
            {L"C:\\Games\\Game.exe", L"Game window", 22, ERROR_SUCCESS},
            {L"D:\\Portable\\Game.exe", L"Another installation", 33, ERROR_SUCCESS},
        }, L"C:\\Games\\Game.exe", {}, false};
        HWND dialog = CreateDialogParamW(resources, MAKEINTRESOURCEW(IDD_RUNNING_APPS), nullptr,
                                          running_app_picker_proc, reinterpret_cast<LPARAM>(&picker));
        if (!dialog) { ok = false; break; }
        RECT client{};
        GetClientRect(dialog, &client);
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT rectangle{};
            GetWindowRect(child, &rectangle);
            MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rectangle), 2);
            ok &= rectangle.left >= 0 && rectangle.top >= 0 && rectangle.right <= client.right && rectangle.bottom <= client.bottom;
        }
        HWND list = GetDlgItem(dialog, IDC_RUNNING_APP_LIST);
        ok &= ListView_GetItemCount(list) == 3 && selected_running_app(dialog, picker)->executable_path == L"C:\\Games\\Game.exe";
        if (refresh) {
            SendMessageW(dialog, WM_COMMAND, IDC_RUNNING_APP_REFRESH, 0);
            ok &= ListView_GetItemCount(list) == static_cast<int>(picker.applications.size());
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            ok &= picker.selected_path.empty();
        } else {
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ok &= !IsWindowEnabled(GetDlgItem(dialog, IDOK));
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            ok &= picker.selected_path.empty();
            ListView_SetItemState(list, 2, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ok &= IsWindowEnabled(GetDlgItem(dialog, IDOK)) != FALSE;
            NMHDR double_click{list, IDC_RUNNING_APP_LIST, NM_DBLCLK};
            SendMessageW(dialog, WM_NOTIFY, IDC_RUNNING_APP_LIST, reinterpret_cast<LPARAM>(&double_click));
            ok &= picker.selected_path == L"D:\\Portable\\Game.exe";
        }
        if (IsWindow(dialog)) DestroyWindow(dialog);
    }

    std::cout << (ok ? "PASS" : "FAIL") << ": hidden running-app picker; preselection; unreadable-path gating; exact-path double-click selection; refresh/cancel; control bounds\n";
    return ok;
}

constexpr UINT_PTR kSettingsAutomationTimer = 0x7001;
constexpr wchar_t kSettingsAutomationProperty[] = L"GammaChanger.Check.SettingsAutomation";

struct SettingsAutomation {
    HWND rules_dialog;
    bool submitted = false;
};

BOOL CALLBACK submit_settings_window(HWND window, LPARAM parameter) {
    auto& automation = *reinterpret_cast<SettingsAutomation*>(parameter);
    if (GetWindow(window, GW_OWNER) != automation.rules_dialog ||
        !GetDlgItem(window, IDC_APP_FILTER_VALUE_BASE)) return TRUE;
    // This is a dialog belonging to this fixture and thread, not another app.
    ShowWindow(window, SW_HIDE);
    const wchar_t* values[] = {L"1.90", L"0", L"1", L"1", L"1", L"1"};
    for (int index = 0; index < 6; ++index) {
        SetDlgItemTextW(window, IDC_APP_FILTER_VALUE_BASE + index, values[index]);
    }
    automation.submitted = true;
    SendMessageW(window, WM_COMMAND, IDOK, 0);
    return FALSE;
}

void CALLBACK submit_settings_timer(HWND window, UINT, UINT_PTR timer, DWORD) {
    auto* automation = reinterpret_cast<SettingsAutomation*>(
        GetPropW(window, kSettingsAutomationProperty));
    if (!automation) return;
    EnumThreadWindows(GetCurrentThreadId(), submit_settings_window,
                      reinterpret_cast<LPARAM>(automation));
    if (automation->submitted) KillTimer(window, timer);
}

bool check_rule_dialog_changes(const std::filesystem::path& root) {
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerRuleRegressionOwner";
    if (!RegisterClassW(&owner_class)) return false;
    bool ok = true;
    // Re-enable an existing disabled rule, both cancel and save. Then exercise
    // the real Edit settings -> modal editor -> Save/Cancel path for enabled
    // and disabled rules, including copy-on-write of a shared library filter.
    for (int step = 0; step < 5; ++step) {
        const bool editing = step >= 2;
        const bool enabled_rule = step == 2 || step == 3;
        const bool save = step == 1 || step == 3 || step == 4;
        FakeBackend backend;
        GuiState gui(root / std::to_wstring(step), backend);
        DisplayInfo display;
        display.device_name = L"FAKE-DISPLAY";
        display.stable_id = L"fake-screen";
        gui.displays.push_back(display);
        CalibrationSettings original;
        original.gamma = 1.20;
        gui.profiles = {{L"common", L"Common", original, true}};
        gui.auto_switch_paused = true;
        gui.app_profile_config.rules = {
            {L"C:\\Apps\\Browser.exe", display.stable_id, L"common", enabled_rule,
             L"existing-rule", L"Browser filter"},
        };
        if (editing) {
            gui.app_profile_config.rules.insert(gui.app_profile_config.rules.begin(),
                {L"C:\\Apps\\Browser.exe", display.stable_id, L"common", true,
                 L"priority-rule", L"Previously selected filter"});
        }
        std::wstring error;
        if (!gui.profile_manager.replace_profiles(gui.profiles, error) ||
            !gui.store.save_app_profile_config(gui.app_profile_config, error)) {
            std::wcerr << L"FAIL: rule fixture persistence: " << error << L'\n';
            ok = false;
            break;
        }
        gui.window = CreateWindowExW(0, owner_class.lpszClassName, L"Hidden rule regression owner",
                                      WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr,
                                      owner_class.hInstance, nullptr);
        if (!gui.window) { ok = false; break; }
        RulesDialogState draft{gui, gui.app_profile_config, gui.profiles};
        HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_RULES),
                                          gui.window, rules_dialog_proc,
                                          reinterpret_cast<LPARAM>(&draft));
        if (!dialog) {
            DestroyWindow(gui.window);
            ok = false;
            break;
        }
        HWND list = GetDlgItem(dialog, IDC_APP_RULE_LIST);
        ok &= ListView_GetItemCount(list) == (editing ? 2 : 1) &&
              !draft.resume_on_save && !draft.config.enabled;
        if (editing) {
            fill_rule_list(dialog, draft, L"existing-rule");
            ok &= ListView_GetNextItem(list, -1, LVNI_SELECTED) == 1;
            SettingsAutomation automation{dialog};
            if (!SetPropW(dialog, kSettingsAutomationProperty,
                          reinterpret_cast<HANDLE>(&automation)) ||
                !SetTimer(dialog, kSettingsAutomationTimer, 10, submit_settings_timer)) {
                ok = false;
            } else {
                SendMessageW(dialog, WM_COMMAND, IDC_EDIT_FILTER_SETTINGS, 0);
                ok &= automation.submitted && draft.profiles_changed;
            }
            KillTimer(dialog, kSettingsAutomationTimer);
            RemovePropW(dialog, kSettingsAutomationProperty);
            ok &= draft.resume_on_save == enabled_rule && draft.config.enabled == enabled_rule;
            const auto* edited_rule = find_app_profile_rule_by_id(draft.config, L"existing-rule");
            const auto* edited_profile = edited_rule
                ? profile_by_id(draft.profiles, edited_rule->profile_id) : nullptr;
            ok &= edited_profile && edited_profile->settings.gamma == 1.90 &&
                  edited_profile->id != L"common" &&
                  profile_by_id(draft.profiles, L"common")->settings.gamma == original.gamma;
        } else {
            ListView_SetCheckState(list, 0, TRUE);
            ok &= draft.resume_on_save && draft.config.enabled && draft.config.rules[0].enabled;
        }
        // Native control changes are draft-only until the outer Save button.
        ok &= !gui.app_profile_config.enabled && gui.auto_switch_paused && backend.writes == 0;
        SendMessageW(dialog, WM_COMMAND, save ? IDOK : IDCANCEL, 0);
        const auto persisted = gui.store.load_app_profile_config();
        const auto* persisted_rule = find_app_profile_rule_by_id(persisted, L"existing-rule");
        const bool activates = save && (!editing || enabled_rule);
        ok &= gui.app_profile_config.enabled == activates && persisted.enabled == activates &&
              gui.auto_switch_paused != activates && persisted_rule && backend.writes == 0;
        if (editing) {
            ok &= !persisted.rules.empty() &&
                  persisted.rules.front().id == (activates ? L"existing-rule" : L"priority-rule");
            // Inspect the preserved priority independently of global Off.
            auto priority_probe = persisted;
            priority_probe.enabled = true;
            const auto* resolved = find_app_profile_rule(priority_probe,
                L"C:\\Apps\\Browser.exe", display.stable_id);
            ok &= resolved && resolved->id == (activates ? L"existing-rule" : L"priority-rule");
        }
        if (persisted_rule) {
            ok &= persisted_rule->enabled == (save && !editing ? true : enabled_rule);
            std::vector<Profile> saved_profiles;
            ok &= gui.store.load_profiles(saved_profiles) == ProfileLoadStatus::loaded;
            const auto* saved_profile = profile_by_id(saved_profiles, persisted_rule->profile_id);
            ok &= saved_profile && saved_profile->settings.gamma == (save && editing ? 1.90 : original.gamma);
        }
        if (activates) {
            const auto* resolved = find_app_profile_rule(gui.app_profile_config,
                L"C:\\Apps\\Browser.exe", display.stable_id);
            ok &= resolved && resolved->id == L"existing-rule";
            apply_application_profiles(gui, L"C:\\Apps\\Browser.exe", true);
            CalibrationSettings expected = original;
            if (editing) expected.gamma = 1.90;
            ok &= backend.current.channel == build_ramp(expected).channel;
        }
        gui.foreground_watcher.stop();
        if (IsWindow(dialog)) DestroyWindow(dialog);
        DestroyWindow(gui.window);
    }
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": existing-disabled rule checkbox/save enables and resumes; cancel preserves state; "
                 "native Edit settings/Save promotes the non-default enabled filter; "
                 "cancel and disabled edits preserve priority/global Off\n";
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
    bool ok = check_application_switching(data / L"assignments");
    ok = check_default_dialog(data / L"defaults") && ok;
    ok = check_running_picker() && ok;
    ok = check_rule_dialog_changes(data / L"rules") && ok;
    ok = check_schedule(data) && ok;
    std::error_code cleanup_error;
    std::filesystem::remove_all(data, cleanup_error);
    if (cleanup_error) {
        std::cerr << "FAIL: cannot remove fixture temporary directory: " << cleanup_error.message() << '\n';
        ok = false;
    }
    return ok ? 0 : 1;
}
