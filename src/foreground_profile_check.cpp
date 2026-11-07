// Native GUI regression checks: real dialogs and the real window procedure,
// with isolated storage and a fake gamma backend (no display is ever changed).
#include "ui/app_dialogs.h"
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

bool controls_inside(HWND dialog) {
    RECT client{};
    GetClientRect(dialog, &client);
    bool ok = true;
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
        ok &= rect.left >= 0 && rect.top >= 0 && rect.right <= client.right &&
              rect.bottom <= client.bottom;
    }
    return ok;
}

void select_list_row(HWND list, int row) {
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

std::wstring list_text(HWND list, int row, int column) {
    wchar_t text[256]{};
    ListView_GetItemText(list, row, column, text, static_cast<int>(std::size(text)));
    return text;
}

void choose_combo(HWND dialog, int id, int index) {
    HWND combo = GetDlgItem(dialog, id);
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
}

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

bool check_assign_dialog() {
    const std::vector<RunningApplication> running{
        {L"", L"Protected game", 11, ERROR_ACCESS_DENIED},
        {L"C:\\Games\\Game.exe", L"Game window", 22, ERROR_SUCCESS},
        {L"D:\\Portable\\Game.exe", L"Another installation", 33, ERROR_SUCCESS},
        {L"C:\\Apps\\Browser.exe", L"Browser", 44, ERROR_SUCCESS},
    };
    const std::vector<Profile> profiles{{L"common", L"Common", default_params(), true},
                                        {L"draft", L"Draft", default_params(), false},
                                        {L"gaming", L"Gaming", default_params(), true}};
    bool ok = true;
    unsigned refreshes = 0;

    // New assignment: last active app first and preselected; only saved profiles.
    AppAssignDraft draft;
    draft.profiles = profiles;
    draft.profile_id = L"gaming";
    draft.last_active_path = L"C:\\Apps\\Browser.exe";
    draft.running_apps = [&] { ++refreshes; return running; };
    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_ASSIGN),
                                     nullptr, app_assign_dialog_proc,
                                     reinterpret_cast<LPARAM>(&draft));
    if (!dialog) return false;
    HWND list = GetDlgItem(dialog, IDC_ASSIGN_APP_LIST);
    ok &= controls_inside(dialog) && ListView_GetItemCount(list) == 4 &&
          ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0 &&
          draft.rows[0].executable_path == L"C:\\Apps\\Browser.exe";
    ok &= SendDlgItemMessageW(dialog, IDC_ASSIGN_PROFILE, CB_GETCOUNT, 0, 0) == 2 &&
          SendDlgItemMessageW(dialog, IDC_ASSIGN_PROFILE, CB_GETCURSEL, 0, 0) == 1;
    // Unreadable process: OK disabled and pressing it does nothing.
    select_list_row(list, 1);
    ok &= draft.rows[1].executable_path.empty() && !IsWindowEnabled(GetDlgItem(dialog, IDOK));
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    ok &= IsWindow(dialog) != FALSE;
    // Same file name, different installation: the exact path is chosen.
    select_list_row(list, 3);
    choose_combo(dialog, IDC_ASSIGN_PROFILE, 0);
    NMHDR double_click{list, IDC_ASSIGN_APP_LIST, NM_DBLCLK};
    SendMessageW(dialog, WM_NOTIFY, IDC_ASSIGN_APP_LIST, reinterpret_cast<LPARAM>(&double_click));
    ok &= draft.executable_path == L"D:\\Portable\\Game.exe" && draft.profile_id == L"common";
    if (IsWindow(dialog)) DestroyWindow(dialog);

    // Change: an app that is not running stays listed and selected; refresh
    // keeps the selection.
    AppAssignDraft change;
    change.profiles = profiles;
    change.executable_path = L"E:\\Old\\Tool.exe";
    change.profile_id = L"common";
    change.running_apps = [&] { ++refreshes; return running; };
    dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_ASSIGN),
                                nullptr, app_assign_dialog_proc, reinterpret_cast<LPARAM>(&change));
    if (!dialog) return false;
    list = GetDlgItem(dialog, IDC_ASSIGN_APP_LIST);
    ok &= ListView_GetItemCount(list) == 5 && list_text(list, 0, 1) == L"Not running" &&
          ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0 &&
          IsWindowEnabled(GetDlgItem(dialog, IDOK)) != FALSE;
    const unsigned before = refreshes;
    SendMessageW(dialog, WM_COMMAND, IDC_ASSIGN_REFRESH, 0);
    ok &= refreshes == before + 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0;
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    ok &= change.executable_path == L"E:\\Old\\Tool.exe";
    if (IsWindow(dialog)) DestroyWindow(dialog);

    std::cout << (ok ? "PASS" : "FAIL")
              << ": choose-app dialog; last-active preselection; saved profiles only; unreadable "
                 "rows gated; exact-path selection; not-running app kept across refresh\n";
    return ok;
}

bool check_profiles_dialog() {
    const std::wstring screen = L"screen-a";
    AppProfilesDraft draft;
    draft.profiles = {{L"common", L"Common", default_params(), true},
                      {L"gaming", L"Gaming", default_params(), true}};
    draft.displays = {{screen, L"Screen 1"}};
    draft.display_id = screen;
    // Legacy config: a second display that is disconnected now.
    draft.config.rules = {{L"C:\\Apps\\Browser.exe", screen, L"common", true, L"rule-1", L""},
                          {L"C:\\Apps\\Browser.exe", L"screen-b", L"gaming", true, L"rule-2", L""}};
    std::wstring next_path = L"C:\\Games\\Game.exe";
    std::wstring next_profile = L"gaming";
    unsigned chooser_calls = 0;
    draft.choose = [&](HWND, AppAssignDraft& pick) {
        ++chooser_calls;
        pick.executable_path = next_path;
        pick.profile_id = next_profile;
        return true;
    };
    unsigned commits = 0;
    bool commit_result = false;
    AppProfileConfig committed;
    draft.commit = [&](const AppProfileConfig& config, std::wstring& error) {
        ++commits;
        committed = config;
        if (!commit_result) error = L"disk full";
        return commit_result;
    };

    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_PROFILES),
                                     nullptr, app_profiles_dialog_proc,
                                     reinterpret_cast<LPARAM>(&draft));
    if (!dialog) return false;
    HWND list = GetDlgItem(dialog, IDC_APP_LIST);
    bool ok = true;
    int step_number = 0;
    const auto step = [&](bool condition) {
        ++step_number;
        if (!condition) std::cout << "  app-profiles dialog step " << step_number << " failed\n";
        ok &= condition;
    };
    step(controls_inside(dialog));
    step(IsDlgButtonChecked(dialog, IDC_APP_AUTO_SWITCH) == BST_UNCHECKED &&
          ListView_GetItemCount(list) == 1 && list_text(list, 0, 1) == L"Common");
    step(SendDlgItemMessageW(dialog, IDC_APP_DISPLAY, CB_GETCOUNT, 0, 0) == 2 &&
          (GetWindowLongPtrW(GetDlgItem(dialog, IDC_APP_DISPLAY), GWL_STYLE) & WS_VISIBLE) != 0);

    // Add: one dialog step, switching turned on (visible, not yet saved).
    SendMessageW(dialog, WM_COMMAND, IDC_APP_ADD, 0);
    step(chooser_calls == 1 && ListView_GetItemCount(list) == 2 &&
          list_text(list, 1, 0) == L"Game.exe" && list_text(list, 1, 1) == L"Gaming" &&
          IsDlgButtonChecked(dialog, IDC_APP_AUTO_SWITCH) == BST_CHECKED && draft.config.enabled);

    // Change the browser to Gaming via the selected row.
    select_list_row(list, 0);
    next_path = L"C:\\Apps\\Browser.exe";
    SendMessageW(dialog, WM_COMMAND, IDC_APP_CHANGE, 0);
    step(list_text(list, 0, 1) == L"Gaming" &&
          find_app_profile_rule_by_id(draft.config, L"rule-1")->profile_id == L"gaming");

    // Remove the game with the Delete key.
    select_list_row(list, 1);
    NMLVKEYDOWN key{{list, IDC_APP_LIST, LVN_KEYDOWN}, VK_DELETE, 0};
    SendMessageW(dialog, WM_NOTIFY, IDC_APP_LIST, reinterpret_cast<LPARAM>(&key));
    step(ListView_GetItemCount(list) == 1);

    // Other apps -> Common on this display only.
    choose_combo(dialog, IDC_APP_OTHER_PROFILE, 1);
    const auto* fallback = find_app_fallback_profile(draft.config, screen);
    step(fallback && fallback->profile_id == L"common");

    // The disconnected display keeps its own assignment.
    choose_combo(dialog, IDC_APP_DISPLAY, 1);
    step(draft.display_id == L"screen-b" && ListView_GetItemCount(list) == 1 &&
          list_text(list, 0, 1) == L"Gaming");

    // A failed save keeps the dialog open; a successful one closes it.
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    wchar_t hint[128]{};
    GetDlgItemTextW(dialog, IDC_APP_HINT, hint, static_cast<int>(std::size(hint)));
    step(commits == 1 && std::wstring(hint).find(L"disk full") != std::wstring::npos);
    commit_result = true;
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    step(commits == 2 && committed.enabled && committed.rules.size() == 2);
    if (IsWindow(dialog)) DestroyWindow(dialog);

    // Cancel never commits.
    AppProfilesDraft cancel = draft;
    commits = 0;
    dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_PROFILES),
                                nullptr, app_profiles_dialog_proc, reinterpret_cast<LPARAM>(&cancel));
    if (!dialog) return false;
    SendMessageW(dialog, WM_COMMAND, IDC_APP_AUTO_SWITCH, 0);
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    step(commits == 0);
    if (IsWindow(dialog)) DestroyWindow(dialog);

    std::cout << (ok ? "PASS" : "FAIL")
              << ": app-profiles dialog; add turns switching on; change/remove; other-apps "
                 "default; disconnected display kept; failed save stays open; cancel never saves\n";
    return ok;
}

// Drives the real modal dialog opened by show_application_rules().
constexpr UINT_PTR kModalAutomationTimer = 0x7001;
struct ModalAutomation {
    HWND owner = nullptr;
    bool toggle = false;
    UINT button = IDOK;
    bool done = false;
};
ModalAutomation* modal_automation = nullptr;

void CALLBACK drive_modal(HWND window, UINT, UINT_PTR timer, DWORD) {
    if (!modal_automation) return;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM) -> BOOL {
        if (GetWindow(candidate, GW_OWNER) != modal_automation->owner ||
            !GetDlgItem(candidate, IDC_APP_AUTO_SWITCH)) {
            return TRUE;
        }
        ShowWindow(candidate, SW_HIDE);  // this fixture's own dialog
        if (modal_automation->toggle) {
            SendDlgItemMessageW(candidate, IDC_APP_AUTO_SWITCH, BM_CLICK, 0, 0);
        }
        SendMessageW(candidate, WM_COMMAND, modal_automation->button, 0);
        modal_automation->done = true;
        return FALSE;
    }, 0);
    if (modal_automation->done) KillTimer(window, timer);
}

bool check_profiles_dialog_commit(const std::filesystem::path& root) {
    WNDCLASSW owner_class{};
    owner_class.lpfnWndProc = DefWindowProcW;
    owner_class.hInstance = GetModuleHandleW(nullptr);
    owner_class.lpszClassName = L"GammaChangerAppProfilesOwner";
    if (!RegisterClassW(&owner_class)) return false;
    bool ok = true;
    for (const UINT button : {static_cast<UINT>(IDCANCEL), static_cast<UINT>(IDOK)}) {
        FakeBackend backend;
        GuiState gui(root / std::to_wstring(button), backend);
        DisplayInfo display;
        display.device_name = L"FAKE-DISPLAY";
        display.stable_id = L"fake-screen";
        gui.displays.push_back(display);
        gui.profiles = {{L"common", L"Common", default_params(), true}};
        gui.app_profile_config.rules = {
            {L"C:\\Apps\\Browser.exe", display.stable_id, L"common", true, L"rule-1", L""}};
        gui.auto_switch_paused = true;
        gui.window = CreateWindowExW(0, owner_class.lpszClassName, L"Owner", WS_OVERLAPPED, 0, 0,
                                     100, 100, nullptr, nullptr, owner_class.hInstance, nullptr);
        if (!gui.window) { ok = false; break; }
        ModalAutomation automation{gui.window, true, button};
        modal_automation = &automation;
        SetTimer(gui.window, kModalAutomationTimer, 10, drive_modal);
        show_application_rules(gui);
        KillTimer(gui.window, kModalAutomationTimer);
        modal_automation = nullptr;
        const bool saved = button == IDOK;
        const auto persisted = gui.store.load_app_profile_config();
        ok &= automation.done && gui.app_profile_config.enabled == saved &&
              gui.auto_switch_paused != saved && persisted.enabled == saved &&
              backend.writes == 0;
        gui.foreground_watcher.stop();
        KillTimer(gui.window, kForegroundProfileTimer);
        DestroyWindow(gui.window);
    }
    UnregisterClassW(owner_class.lpszClassName, owner_class.hInstance);
    std::cout << (ok ? "PASS" : "FAIL")
              << ": modal app-profiles dialog; turning switching on and saving persists and "
                 "resumes; cancel changes nothing; no display writes\n";
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
    ok = check_assign_dialog() && ok;
    ok = check_profiles_dialog() && ok;
    ok = check_profiles_dialog_commit(data / L"dialog") && ok;
    ok = check_schedule(data) && ok;
    std::error_code cleanup_error;
    std::filesystem::remove_all(data, cleanup_error);
    if (cleanup_error) {
        std::cerr << "FAIL: cannot remove fixture temporary directory: " << cleanup_error.message() << '\n';
        ok = false;
    }
    return ok ? 0 : 1;
}
