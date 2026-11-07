#include "gui_internal.h"

#include <shobjidl.h>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace gamma_changer {
namespace {

std::wstring executable_name(const std::wstring& path) {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool editor_in_foreground(const GuiState& gui) {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId() && IsWindowVisible(gui.window) &&
           !IsIconic(gui.window);
}

const Profile* profile_by_id(const std::vector<Profile>& profiles, const std::wstring& id) {
    const auto found = std::find_if(profiles.begin(), profiles.end(),
                                   [&](const Profile& profile) { return profile.id == id; });
    return found == profiles.end() ? nullptr : &*found;
}

const Profile* profile_by_id(const GuiState& gui, const std::wstring& id) {
    return profile_by_id(gui.profiles, id);
}

bool save_app_config(GuiState& gui, const AppProfileConfig& config) {
    std::wstring error;
    if (!gui.store.save_app_profile_config(config, error)) {
        set_status(gui, L"Could not save application profiles: " + error, StatusTone::error);
        return false;
    }
    gui.app_profile_config = config;
    update_automatic_profile_controls(gui);
    return true;
}

bool ensure_foreground_watcher(GuiState& gui) {
    if (gui.foreground_watcher_running) return true;
    gui.foreground_watcher_running =
        gui.foreground_watcher.start(gui.window, kForegroundProfileMessage);
    if (!gui.foreground_watcher_running) {
        set_status(gui, L"Could not monitor the foreground application", StatusTone::error);
    }
    return gui.foreground_watcher_running;
}

std::wstring choose_executable(HWND owner) {
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&picker)))) return {};
    const COMDLG_FILTERSPEC filter{L"Applications (*.exe)", L"*.exe"};
    picker->SetTitle(L"Bind an application to this profile");
    picker->SetFileTypes(1, &filter);
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    std::wstring path;
    if (SUCCEEDED(picker->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(picker->GetResult(&item))) {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
                path = selected;
                CoTaskMemFree(selected);
            }
            item->Release();
        }
    }
    picker->Release();
    return path;
}

struct RunningAppPickerState {
    std::vector<RunningApplication> applications;
    std::wstring preferred_path;
    std::wstring selected_path;
    bool filling = false;
};

const RunningApplication* selected_running_app(HWND dialog, const RunningAppPickerState& state) {
    const int row = ListView_GetNextItem(GetDlgItem(dialog, IDC_RUNNING_APP_LIST), -1, LVNI_SELECTED);
    return row >= 0 && static_cast<std::size_t>(row) < state.applications.size()
        ? &state.applications[static_cast<std::size_t>(row)] : nullptr;
}

void update_running_app_selection(HWND dialog, const RunningAppPickerState& state) {
    const auto* application = selected_running_app(dialog, state);
    const bool can_select = application && !application->executable_path.empty();
    EnableWindow(GetDlgItem(dialog, IDOK), can_select);
    std::wstring hint = state.applications.empty()
        ? L"No running application windows found. Use Browse to select an executable."
        : L"Select an application to bind all its windows to a filter.";
    if (can_select) hint = application->executable_path;
    else if (application) {
        hint = L"Cannot read this application's executable (process " + std::to_wstring(application->process_id) +
               L", Win32 error " + std::to_wstring(application->error) + L"). Use Browse to select it manually.";
    }
    SetDlgItemTextW(dialog, IDC_RUNNING_APP_HINT, hint.c_str());
}

void fill_running_app_list(HWND dialog, RunningAppPickerState& state) {
    state.filling = true;
    HWND list = GetDlgItem(dialog, IDC_RUNNING_APP_LIST);
    ListView_DeleteAllItems(list);
    int selected = -1;
    const std::wstring preferred = normalize_executable_path(state.preferred_path);
    for (std::size_t index = 0; index < state.applications.size(); ++index) {
        const auto& application = state.applications[index];
        std::wstring name = application.executable_path.empty() ? L"Unavailable" : executable_name(application.executable_path);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = name.data();
        const int row = ListView_InsertItem(list, &item);
        std::wstring title = application.window_title;
        std::wstring path = application.executable_path.empty()
            ? L"Unavailable (error " + std::to_wstring(application.error) + L")" : application.executable_path;
        ListView_SetItemText(list, row, 1, title.data());
        ListView_SetItemText(list, row, 2, path.data());
        if (!application.executable_path.empty()) {
            if (selected < 0) selected = row;
            if (!preferred.empty() && normalize_executable_path(application.executable_path) == preferred) selected = row;
        }
    }
    if (selected < 0 && !state.applications.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
    }
    state.filling = false;
    update_running_app_selection(dialog, state);
}

INT_PTR CALLBACK running_app_picker_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RunningAppPickerState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<RunningAppPickerState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        HWND list = GetDlgItem(dialog, IDC_RUNNING_APP_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        RECT client{};
        GetClientRect(list, &client);
        const wchar_t* titles[] = {L"Application", L"Window", L"Executable path"};
        const int portions[] = {20, 35, 45};
        for (int column = 0; column < 3; ++column) {
            LVCOLUMNW item{};
            item.mask = LVCF_TEXT | LVCF_WIDTH;
            item.pszText = const_cast<wchar_t*>(titles[column]);
            item.cx = MulDiv(client.right - 20, portions[column], 100);
            ListView_InsertColumn(list, column, &item);
        }
        fill_running_app_list(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_NOTIFY) {
        const auto* notice = reinterpret_cast<NMHDR*>(lparam);
        if (notice->idFrom == IDC_RUNNING_APP_LIST && !state->filling) {
            if (notice->code == LVN_ITEMCHANGED) update_running_app_selection(dialog, *state);
            else if (notice->code == NM_DBLCLK) SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
    } else if (message == WM_COMMAND) {
        switch (LOWORD(wparam)) {
        case IDC_RUNNING_APP_REFRESH:
            if (const auto* current = selected_running_app(dialog, *state)) state->preferred_path = current->executable_path;
            state->applications = running_applications();
            fill_running_app_list(dialog, *state);
            return TRUE;
        case IDC_RUNNING_APP_BROWSE: {
            const std::wstring path = choose_executable(dialog);
            if (!path.empty()) {
                state->selected_path = path;
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        }
        case IDOK: {
            const auto* application = selected_running_app(dialog, *state);
            if (application && !application->executable_path.empty()) {
                state->selected_path = application->executable_path;
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    } else if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

std::wstring choose_running_application(HWND owner, const std::wstring& preferred_path) {
    RunningAppPickerState state{running_applications(), preferred_path, {}, false};
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RUNNING_APPS), owner,
                                           running_app_picker_proc, reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? state.selected_path : L"";
}

std::wstring control_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), length + 1)));
    return text;
}

std::wstring rule_name(const std::vector<Profile>& profiles, const AppProfileRule& rule) {
    if (!rule.name.empty()) return rule.name;
    const auto* profile = profile_by_id(profiles, rule.profile_id);
    return profile ? profile->name : L"Removed profile";
}

std::wstring rule_display_name(const GuiState& gui, const std::wstring& id) {
    for (std::size_t index = 0; index < gui.displays.size(); ++index) {
        if (display_storage_id(gui.displays[index]) == id) {
            return display_label(gui.displays[index], index);
        }
    }
    return L"Disconnected display";
}

struct RuleEditorState {
    GuiState& gui;
    const std::vector<Profile>& profiles;
    AppProfileRule rule;
    std::vector<std::wstring> profile_ids;
    std::vector<std::wstring> display_ids;
};

void fill_rule_choices(HWND dialog, RuleEditorState& state) {
    HWND profiles = GetDlgItem(dialog, IDC_RULE_PROFILE);
    int selected_profile = state.rule.id.empty() ? 0 : -1;
    for (const auto& profile : state.profiles) {
        if (!profile.saved) continue;
        if (profile.id == state.rule.profile_id) {
            selected_profile = static_cast<int>(state.profile_ids.size());
        }
        state.profile_ids.push_back(profile.id);
        SendMessageW(profiles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.name.c_str()));
    }
    SendMessageW(profiles, CB_SETCURSEL, selected_profile, 0);

    HWND displays = GetDlgItem(dialog, IDC_RULE_DISPLAY);
    int selected_display = 0;
    for (std::size_t index = 0; index < state.gui.displays.size(); ++index) {
        const auto& display = state.gui.displays[index];
        const std::wstring id = display_storage_id(display);
        if (id == state.rule.display_id) selected_display = static_cast<int>(index);
        state.display_ids.push_back(id);
        const std::wstring label = display_label(display, index);
        SendMessageW(displays, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!state.rule.display_id.empty() &&
        std::find(state.display_ids.begin(), state.display_ids.end(), state.rule.display_id) ==
            state.display_ids.end()) {
        selected_display = static_cast<int>(state.display_ids.size());
        state.display_ids.push_back(state.rule.display_id);
        SendMessageW(displays, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Disconnected display"));
    }
    SendMessageW(displays, CB_SETCURSEL, selected_display, 0);
}

INT_PTR CALLBACK rule_editor_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RuleEditorState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<RuleEditorState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(dialog, state->rule.id.empty() ? L"Add application filter"
                                                    : L"Edit application filter");
        SetDlgItemTextW(dialog, IDC_RULE_NAME, rule_name(state->profiles, state->rule).c_str());
        SetDlgItemTextW(dialog, IDC_RULE_EXECUTABLE, state->rule.executable_path.c_str());
        CheckDlgButton(dialog, IDC_RULE_ENABLED, state->rule.enabled ? BST_CHECKED : BST_UNCHECKED);
        fill_rule_choices(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND) {
        switch (LOWORD(wparam)) {
        case IDC_RULE_BROWSE: {
            const std::wstring path = choose_executable(dialog);
            if (!path.empty()) SetDlgItemTextW(dialog, IDC_RULE_EXECUTABLE, path.c_str());
            return TRUE;
        }
        case IDC_RULE_CAPTURE_APP:
            if (state->gui.foreground_application.empty()) {
                SetDlgItemTextW(dialog, IDC_RULE_HINT,
                                L"Activate the running application or game, then return here to capture its executable.");
            } else {
                SetDlgItemTextW(dialog, IDC_RULE_EXECUTABLE, state->gui.foreground_application.c_str());
            }
            return TRUE;
        case IDOK: {
            const std::wstring path = control_text(GetDlgItem(dialog, IDC_RULE_EXECUTABLE));
            const LRESULT profile = SendDlgItemMessageW(dialog, IDC_RULE_PROFILE, CB_GETCURSEL, 0, 0);
            const LRESULT display = SendDlgItemMessageW(dialog, IDC_RULE_DISPLAY, CB_GETCURSEL, 0, 0);
            if (path.empty() || profile == CB_ERR || display == CB_ERR) {
                SetDlgItemTextW(dialog, IDC_RULE_HINT, L"Choose an application, profile, and display.");
                return TRUE;
            }
            state->rule.executable_path = path;
            state->rule.profile_id = state->profile_ids[static_cast<std::size_t>(profile)];
            state->rule.display_id = state->display_ids[static_cast<std::size_t>(display)];
            state->rule.name = control_text(GetDlgItem(dialog, IDC_RULE_NAME));
            if (state->rule.name.empty()) {
                state->rule.name = profile_by_id(state->profiles, state->rule.profile_id)->name;
            }
            state->rule.enabled = IsDlgButtonChecked(dialog, IDC_RULE_ENABLED) == BST_CHECKED;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

bool edit_application_rule(GuiState& gui, HWND owner, AppProfileRule& rule,
                           const std::vector<Profile>& profiles) {
    RuleEditorState state{gui, profiles, rule, {}, {}};
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDD_APP_RULE_EDITOR), owner,
                                           rule_editor_proc, reinterpret_cast<LPARAM>(&state));
    if (result != IDOK) return false;
    rule = std::move(state.rule);
    return true;
}

AppProfileRule new_application_rule(const GuiState& gui, const std::wstring& path) {
    AppProfileRule rule;
    rule.executable_path = path;
    if (const auto* display = selected_display(gui)) rule.display_id = display_storage_id(*display);
    if (gui.active_preset < gui.profiles.size()) {
        rule.profile_id = gui.profiles[gui.active_preset].id;
        rule.name = gui.profiles[gui.active_preset].name;
    }
    return rule;
}

struct RulesDialogState {
    GuiState& gui;
    AppProfileConfig config;
    std::vector<Profile> profiles;
    bool profiles_changed = false;
    std::vector<std::wstring> applications;
    std::wstring selected_application;
    std::vector<std::size_t> visible_rules;
    bool filling = false;
    bool resume_on_save = false;
    std::vector<std::wstring> fallback_display_ids{};
    std::vector<std::wstring> fallback_profile_ids{};
};

std::wstring fallback_display_id(HWND dialog, const RulesDialogState& state) {
    const LRESULT selected = SendDlgItemMessageW(dialog, IDC_FALLBACK_DISPLAY, CB_GETCURSEL, 0, 0);
    return selected >= 0 && static_cast<std::size_t>(selected) < state.fallback_display_ids.size()
        ? state.fallback_display_ids[static_cast<std::size_t>(selected)] : L"";
}

bool read_fallback_choice(HWND dialog, RulesDialogState& state) {
    const LRESULT selected = SendDlgItemMessageW(dialog, IDC_FALLBACK_PROFILE, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<std::size_t>(selected) >= state.fallback_profile_ids.size()) return false;
    const std::wstring display_id = fallback_display_id(dialog, state);
    if (display_id.empty()) return false;
    set_app_fallback_profile(state.config, display_id, state.fallback_profile_ids[static_cast<std::size_t>(selected)]);
    return true;
}

void fill_fallback_profiles(HWND dialog, RulesDialogState& state) {
    HWND combo = GetDlgItem(dialog, IDC_FALLBACK_PROFILE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    state.fallback_profile_ids = {L""};
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Desktop settings (default)"));
    const std::wstring display_id = fallback_display_id(dialog, state);
    const auto* preference = find_app_fallback_profile(state.config, display_id);
    int selected = 0;
    for (const auto& profile : state.profiles) {
        if (!profile.saved) continue;
        if (preference && preference->profile_id == profile.id) selected = static_cast<int>(state.fallback_profile_ids.size());
        state.fallback_profile_ids.push_back(profile.id);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.name.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
    EnableWindow(combo, !display_id.empty());
}

void fill_fallback_controls(HWND dialog, RulesDialogState& state) {
    HWND combo = GetDlgItem(dialog, IDC_FALLBACK_DISPLAY);
    int selected = 0;
    for (std::size_t index = 0; index < state.gui.displays.size(); ++index) {
        const auto& display = state.gui.displays[index];
        const std::wstring label = display_label(display, index);
        state.fallback_display_ids.push_back(display_storage_id(display));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (static_cast<int>(index) == state.gui.active_display_index) selected = static_cast<int>(index);
    }
    for (const auto& preference : state.config.fallback_profiles) {
        if (std::find(state.fallback_display_ids.begin(), state.fallback_display_ids.end(), preference.display_id) !=
            state.fallback_display_ids.end()) continue;
        state.fallback_display_ids.push_back(preference.display_id);
        const std::wstring label = L"Disconnected display - " + preference.display_id;
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!state.fallback_display_ids.empty()) SendMessageW(combo, CB_SETCURSEL, selected, 0);
    fill_fallback_profiles(dialog, state);
}

std::wstring assign_application_filter(RulesDialogState& state, AppProfileRule rule) {
    if (rule.enabled) {
        state.resume_on_save = true;
        return bind_app_profile_rule(state.config, std::move(rule));
    }
    const std::wstring existing_id = rule.id;
    set_app_profile_rule(state.config, std::move(rule));
    return existing_id.empty() ? state.config.rules.back().id : existing_id;
}

struct FilterSettingsState {
    CalibrationSettings settings;
    std::wstring title;
};

INT_PTR CALLBACK filter_settings_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<FilterSettingsState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<FilterSettingsState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(dialog, state->title.c_str());
        const std::array<double, 6> values{state->settings.gamma, state->settings.brightness,
            state->settings.contrast, state->settings.r_gain, state->settings.g_gain,
            state->settings.b_gain};
        for (std::size_t index = 0; index < values.size(); ++index) {
            std::wostringstream text;
            text << std::setprecision(12) << values[index];
            SetDlgItemTextW(dialog, IDC_APP_FILTER_VALUE_BASE + static_cast<int>(index),
                            text.str().c_str());
        }
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        CalibrationSettings settings;
        const std::array<double*, 6> values{&settings.gamma, &settings.brightness,
            &settings.contrast, &settings.r_gain, &settings.g_gain, &settings.b_gain};
        for (std::size_t index = 0; index < values.size(); ++index) {
            std::wistringstream input(control_text(GetDlgItem(dialog,
                IDC_APP_FILTER_VALUE_BASE + static_cast<int>(index))));
            if (!(input >> *values[index]) || !(input >> std::ws).eof()) {
                SetDlgItemTextW(dialog, IDC_APP_FILTER_HINT, L"Enter a number in each field.");
                return TRUE;
            }
        }
        std::wstring error;
        if (!validate_params(settings, error)) {
            SetDlgItemTextW(dialog, IDC_APP_FILTER_HINT, error.c_str());
            return TRUE;
        }
        state->settings = settings;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if ((message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) || message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void edit_filter_settings(HWND dialog, RulesDialogState& state, std::size_t index) {
    auto& rule = state.config.rules[index];
    const auto* current = profile_by_id(state.profiles, rule.profile_id);
    if (!current) return;
    FilterSettingsState settings{current->settings,
        executable_name(rule.executable_path) + L" - " + rule_name(state.profiles, rule)};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_FILTER_SETTINGS),
                        dialog, filter_settings_proc, reinterpret_cast<LPARAM>(&settings)) != IDOK) return;
    if (settings_equal(settings.settings, current->settings)) return;

    const std::wstring rule_id = rule.id;
    const bool enabled = rule.enabled;
    const bool changed = update_app_filter_settings(state.config, state.profiles, rule_id, settings.settings);
    state.profiles_changed = changed || state.profiles_changed;
    if (changed && enabled) {
        select_app_profile_rule(state.config, rule_id);
        state.resume_on_save = true;
    }
}

void update_default_labels(HWND dialog, const RulesDialogState& state) {
    std::unordered_set<std::wstring> displays;
    HWND list = GetDlgItem(dialog, IDC_APP_RULE_LIST);
    for (std::size_t row = 0; row < state.visible_rules.size(); ++row) {
        const auto& rule = state.config.rules[state.visible_rules[row]];
        std::wstring label = rule.enabled && displays.insert(rule.display_id).second
            ? L"Default" : L"";
        ListView_SetItemText(list, static_cast<int>(row), 3, label.data());
    }
}

bool save_rules_dialog(RulesDialogState& state) {
    if (state.config.enabled && !ensure_foreground_watcher(state.gui)) return false;
    if (state.profiles_changed) {
        std::wstring error;
        if (!state.gui.profile_manager.replace_profiles(state.profiles, error)) {
            set_status(state.gui, L"Could not save application filter settings: " + error,
                       StatusTone::error);
            return false;
        }
    }
    if (!save_app_config(state.gui, state.config)) {
        if (state.profiles_changed) {
            std::wstring error;
            if (!state.gui.profile_manager.replace_profiles(state.gui.profiles, error)) {
                set_status(state.gui, L"Could not roll back filter settings: " + error,
                           StatusTone::error);
            }
        }
        return false;
    }
    if (state.profiles_changed) {
        state.gui.profiles = state.profiles;
        refresh_preset_buttons(state.gui);
        state.profiles_changed = false;
    }
    if (state.resume_on_save) state.gui.auto_switch_paused = false;
    return true;
}

void update_rule_actions(HWND dialog, const RulesDialogState& state) {
    const int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_APP_RULE_LIST), -1, LVNI_SELECTED);
    const bool has_rule = selected >= 0;
    EnableWindow(GetDlgItem(dialog, IDC_EDIT_APP_FILTER), has_rule);
    EnableWindow(GetDlgItem(dialog, IDC_REMOVE_APP_RULE), has_rule);
    EnableWindow(GetDlgItem(dialog, IDC_USE_APP_FILTER), has_rule);
    const auto* profile = has_rule ? profile_by_id(state.profiles,
        state.config.rules[state.visible_rules[static_cast<std::size_t>(selected)]].profile_id) : nullptr;
    EnableWindow(GetDlgItem(dialog, IDC_EDIT_FILTER_SETTINGS),
                 profile && profile->saved && state.gui.profile_store_available);
    EnableWindow(GetDlgItem(dialog, IDC_MOVE_RULE_UP), selected > 0);
    EnableWindow(GetDlgItem(dialog, IDC_MOVE_RULE_DOWN),
                 has_rule && static_cast<std::size_t>(selected + 1) < state.visible_rules.size());
    const bool can_add = !state.gui.displays.empty() && std::any_of(
        state.profiles.begin(), state.profiles.end(), [](const Profile& profile) {
            return profile.saved;
        });
    EnableWindow(GetDlgItem(dialog, IDC_ADD_APPLICATION), can_add);
    EnableWindow(GetDlgItem(dialog, IDC_ADD_ACTIVE_APPLICATION), can_add);
    EnableWindow(GetDlgItem(dialog, IDC_ADD_APP_FILTER), can_add && !state.selected_application.empty());
    EnableWindow(GetDlgItem(dialog, IDC_REMOVE_APPLICATION), !state.selected_application.empty());
}

void fill_rule_list(HWND dialog, RulesDialogState& state, const std::wstring& preferred_id = {}) {
    state.filling = true;
    HWND list = GetDlgItem(dialog, IDC_APP_RULE_LIST);
    ListView_DeleteAllItems(list);
    state.visible_rules.clear();
    const std::wstring application = normalize_executable_path(state.selected_application);
    int selected = 0;
    for (std::size_t index = 0; index < state.config.rules.size(); ++index) {
        const auto& rule = state.config.rules[index];
        if (normalize_executable_path(rule.executable_path) != application) continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(state.visible_rules.size());
        std::wstring name = rule_name(state.profiles, rule);
        item.pszText = name.data();
        const int row = ListView_InsertItem(list, &item);
        state.visible_rules.push_back(index);
        if (rule.id == preferred_id) selected = row;
        const auto* profile = profile_by_id(state.profiles, rule.profile_id);
        std::wstring profile_name = profile ? profile->name : L"Removed profile";
        ListView_SetItemText(list, row, 1, profile_name.data());
        std::wstring display_name = rule_display_name(state.gui, rule.display_id);
        ListView_SetItemText(list, row, 2, display_name.data());
        ListView_SetCheckState(list, row, rule.enabled);
    }
    update_default_labels(dialog, state);
    if (!state.visible_rules.empty()) {
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
    }
    const std::wstring title = state.selected_application.empty() ? L"Application profiles"
        : L"Application profiles - " + executable_name(state.selected_application);
    SetWindowTextW(dialog, title.c_str());
    state.filling = false;
    update_rule_actions(dialog, state);
}

void fill_application_list(HWND dialog, RulesDialogState& state,
                           const std::wstring& preferred_path = {},
                           const std::wstring& preferred_rule = {}) {
    const std::wstring preferred = normalize_executable_path(
        preferred_path.empty() ? state.selected_application : preferred_path);
    HWND list = GetDlgItem(dialog, IDC_APPLICATION_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    state.applications.clear();
    int selected = 0;
    int horizontal_extent = 0;
    HDC dc = GetDC(list);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    const auto old_font = font ? SelectObject(dc, font) : nullptr;
    for (const auto& rule : state.config.rules) {
        const std::wstring path = normalize_executable_path(rule.executable_path);
        const bool exists = std::any_of(state.applications.begin(), state.applications.end(),
                                        [&](const std::wstring& app) {
            return normalize_executable_path(app) == path;
        });
        if (exists) continue;
        if (path == preferred) selected = static_cast<int>(state.applications.size());
        state.applications.push_back(rule.executable_path);
        const std::wstring label = executable_name(rule.executable_path) + L" - " + rule.executable_path;
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SIZE size{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
        horizontal_extent = std::max(horizontal_extent, static_cast<int>(size.cx) + 12);
    }
    if (old_font) SelectObject(dc, old_font);
    ReleaseDC(list, dc);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, horizontal_extent, 0);
    SendMessageW(list, LB_SETCURSEL, selected, 0);
    state.selected_application = state.applications.empty() ? L"" : state.applications[selected];
    fill_rule_list(dialog, state, preferred_rule);
}

void read_rule_checks(HWND dialog, RulesDialogState& state) {
    HWND list = GetDlgItem(dialog, IDC_APP_RULE_LIST);
    for (std::size_t row = 0; row < state.visible_rules.size(); ++row) {
        auto& rule = state.config.rules[state.visible_rules[row]];
        const bool enabled = ListView_GetCheckState(list, static_cast<int>(row)) != FALSE;
        if (enabled && !rule.enabled) {
            state.config.enabled = true;
            state.resume_on_save = true;
        }
        rule.enabled = enabled;
    }
}

INT_PTR CALLBACK rules_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RulesDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<RulesDialogState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        HWND list = GetDlgItem(dialog, IDC_APP_RULE_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES |
                                               LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        RECT client{};
        GetClientRect(list, &client);
        const wchar_t* titles[] = {L"Filter", L"Profile", L"Display", L"Default"};
        const int portions[] = {25, 25, 32, 18};
        for (int column = 0; column < 4; ++column) {
            LVCOLUMNW item{};
            item.mask = LVCF_TEXT | LVCF_WIDTH;
            item.pszText = const_cast<wchar_t*>(titles[column]);
            item.cx = MulDiv(client.right - 24, portions[column], 100);
            ListView_InsertColumn(list, column, &item);
        }
        fill_application_list(dialog, *state);
        fill_fallback_controls(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_NOTIFY) {
        const auto* notice = reinterpret_cast<NMHDR*>(lparam);
        if (notice->idFrom == IDC_APP_RULE_LIST && !state->filling) {
            if (notice->code == LVN_ITEMCHANGED) {
                read_rule_checks(dialog, *state);
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lparam);
                if (((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                    update_default_labels(dialog, *state);
                }
                update_rule_actions(dialog, *state);
            }
            else if (notice->code == NM_DBLCLK) {
                SendMessageW(dialog, WM_COMMAND, IDC_EDIT_APP_FILTER, 0);
            }
        }
        return FALSE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDC_FALLBACK_DISPLAY && HIWORD(wparam) == CBN_SELCHANGE) {
            fill_fallback_profiles(dialog, *state);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_FALLBACK_PROFILE &&
            (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_SELENDOK)) {
            if (read_fallback_choice(dialog, *state)) {
                state->config.enabled = true;
                state->resume_on_save = true;
            }
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_APPLICATION_LIST && HIWORD(wparam) == LBN_SELCHANGE) {
            read_rule_checks(dialog, *state);
            const LRESULT selected = SendDlgItemMessageW(dialog, IDC_APPLICATION_LIST, LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR) {
                state->selected_application = state->applications[static_cast<std::size_t>(selected)];
                fill_rule_list(dialog, *state);
            }
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        read_rule_checks(dialog, *state);
        const int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_APP_RULE_LIST), -1, LVNI_SELECTED);
        const std::size_t index = selected < 0 ? kNoProfile
            : state->visible_rules[static_cast<std::size_t>(selected)];
        switch (LOWORD(wparam)) {
        case IDC_ADD_APPLICATION:
        case IDC_ADD_ACTIVE_APPLICATION:
        case IDC_ADD_APP_FILTER: {
            const std::wstring path = LOWORD(wparam) == IDC_ADD_APPLICATION
                ? choose_running_application(dialog, state->gui.foreground_application) : LOWORD(wparam) == IDC_ADD_ACTIVE_APPLICATION
                ? state->gui.foreground_application : state->selected_application;
            if (path.empty()) {
                if (LOWORD(wparam) == IDC_ADD_ACTIVE_APPLICATION) {
                    SetDlgItemTextW(dialog, IDC_APP_RULES_HINT,
                                    L"Activate the running application or game, then return here and click Add active app.");
                }
                return TRUE;
            }
            AppProfileRule rule = new_application_rule(state->gui, path);
            if (edit_application_rule(state->gui, dialog, rule, state->profiles)) {
                const std::wstring id = assign_application_filter(*state, rule);
                fill_application_list(dialog, *state, rule.executable_path, id);
            }
            return TRUE;
        }
        case IDC_EDIT_APP_FILTER:
            if (index != kNoProfile) {
                AppProfileRule rule = state->config.rules[index];
                if (edit_application_rule(state->gui, dialog, rule, state->profiles)) {
                    const std::wstring id = assign_application_filter(*state, rule);
                    fill_application_list(dialog, *state, rule.executable_path, id);
                }
            }
            return TRUE;
        case IDC_USE_APP_FILTER:
            if (index != kNoProfile) {
                const std::wstring id = state->config.rules[index].id;
                select_app_profile_rule(state->config, id);
                state->resume_on_save = true;
                fill_rule_list(dialog, *state, id);
            }
            return TRUE;
        case IDC_EDIT_FILTER_SETTINGS:
            if (index != kNoProfile) {
                const std::wstring id = state->config.rules[index].id;
                edit_filter_settings(dialog, *state, index);
                fill_rule_list(dialog, *state, id);
                fill_fallback_profiles(dialog, *state);
            }
            return TRUE;
        case IDC_REMOVE_APP_RULE:
            if (index != kNoProfile) {
                state->config.rules.erase(state->config.rules.begin() + static_cast<std::ptrdiff_t>(index));
                fill_application_list(dialog, *state);
            }
            return TRUE;
        case IDC_REMOVE_APPLICATION: {
            const std::wstring path = normalize_executable_path(state->selected_application);
            auto& rules = state->config.rules;
            rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const AppProfileRule& rule) {
                return normalize_executable_path(rule.executable_path) == path;
            }), rules.end());
            fill_application_list(dialog, *state);
            return TRUE;
        }
        case IDC_MOVE_RULE_UP:
        case IDC_MOVE_RULE_DOWN:
            if (index != kNoProfile) {
                const int next = selected + (LOWORD(wparam) == IDC_MOVE_RULE_UP ? -1 : 1);
                if (next >= 0 && static_cast<std::size_t>(next) < state->visible_rules.size()) {
                    const std::wstring id = state->config.rules[index].id;
                    std::swap(state->config.rules[index],
                              state->config.rules[state->visible_rules[static_cast<std::size_t>(next)]]);
                    fill_rule_list(dialog, *state, id);
                }
            }
            return TRUE;
        case IDOK:
            read_fallback_choice(dialog, *state);
            if (save_rules_dialog(*state)) EndDialog(dialog, IDOK);
            else SetDlgItemTextW(dialog, IDC_APP_RULES_HINT, control_text(state->gui.status).c_str());
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

void update_automatic_profile_controls(GuiState& gui) {
    const wchar_t* button = !gui.app_profile_config.enabled ? L"Auto-switch: Off"
                           : gui.auto_switch_paused ? L"Resume auto-switch"
                                                    : L"Auto-switch: On";
    SetWindowTextW(gui.auto_switch_button, button);
    invalidate_control_background(gui, gui.auto_switch_button);
    const auto* display = selected_display(gui);
    const bool can_bind = display && gui.profile_store_available &&
                          gui.active_preset < gui.profiles.size() &&
                          gui.profiles[gui.active_preset].saved;
    EnableWindow(gui.bind_application_button, can_bind);
    std::wstring text;
    if (!gui.app_profile_config.enabled) text = L"Bind an app to follow its profile";
    else if (gui.auto_switch_paused) text = L"Paused for manual adjustments";
    else if (gui.app_rules_dialog_open || editor_in_foreground(gui)) {
        text = L"Paused while editing";
    } else if (gui.foreground_lookup_error != ERROR_SUCCESS) {
        text = L"App identification failed (error " + std::to_wstring(gui.foreground_lookup_error) + L")";
    } else {
        const RuntimeDisplayState* active =
            display ? gui.app_runtime.active(display_storage_id(*display)) : nullptr;
        const auto* profile = active ? profile_by_id(gui, active->profile_id) : nullptr;
        const auto* rule =
            active ? find_app_profile_rule_by_id(gui.app_profile_config, active->rule_id) : nullptr;
        const std::wstring name = rule && !rule->name.empty() ? rule->name
            : profile ? profile->name : L"";
        text = profile ? executable_name(gui.foreground_application) + L" \u2192 " + name
                       : gui.foreground_application.empty() ? L"Desktop settings"
                       : executable_name(gui.foreground_application) + L": no assigned filter";
        if (profile && active->rule_id.empty()) text += L" (default)";
    }
    SetWindowTextW(gui.auto_switch_status, text.c_str());
    invalidate_control_background(gui, gui.auto_switch_status);
}

void initialize_automatic_profiles(GuiState& gui) {
    gui.app_profile_config = gui.store.load_app_profile_config();
    if (!ensure_foreground_watcher(gui)) gui.app_profile_config.enabled = false;
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void schedule_automatic_profiles(GuiState& gui, bool force) {
    gui.foreground_reapply_requested = gui.foreground_reapply_requested || force;
    if (!gui.foreground_evaluation_pending) {
        gui.foreground_evaluation_pending =
            SetTimer(gui.window, kForegroundProfileTimer, 150, nullptr) != 0;
    }
}

bool restore_automatic_profiles(GuiState& gui) {
    const RuntimeRestoreResult result = gui.app_runtime.restore_all(gui.displays);
    if (!result.success) {
        set_status(gui, L"Could not restore desktop settings: " + result.error, StatusTone::error);
    } else if (result.restored_any) {
        set_status(gui, L"Desktop settings restored", StatusTone::success);
    }
    return result.success;
}

void evaluate_automatic_profiles(GuiState& gui, bool force) {
    DWORD process = 0;
    HWND foreground = GetForegroundWindow();
    GetWindowThreadProcessId(foreground, &process);
    const bool own_process = process == GetCurrentProcessId();
    const std::wstring path = own_process ? gui.foreground_application
        : foreground ? foreground_executable_path(foreground, &gui.foreground_lookup_error) : std::wstring{};
    // Keep the last external executable available while editing app assignments.
    if (!own_process && foreground) gui.foreground_application = path;
    if (gui.app_rules_dialog_open) return;
    if (own_process) {
        // The visible editor works with the user's desktop profile. Hidden tray
        // menus retain the last application's effect until focus returns.
        const bool editing = editor_in_foreground(gui);
        if (editing) restore_automatic_profiles(gui);
        update_automatic_profile_controls(gui);
        // Display recovery may have overwritten the LUT while a hidden tray
        // menu owns focus. Reapply the last external app before deduplicating.
        if (editing || !force) return;
    }
    if (!gui.app_profile_config.enabled || gui.auto_switch_paused) {
        restore_automatic_profiles(gui);
        update_automatic_profile_controls(gui);
        return;
    }
    if (path.empty() && !foreground) return;
    normalize_all_edits(gui);
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    KillTimer(gui.window, kPreviewTimer);
    if (!cancel_active_preview(gui)) return;
    apply_application_profiles(gui, path, force);
    if (path.empty()) {
        set_status(gui, L"Could not identify the active application (Win32 error " +
                        std::to_wstring(gui.foreground_lookup_error) + L"); using the unassigned-app default",
                   StatusTone::error);
    }
}

void apply_application_profiles(GuiState& gui, const std::wstring& path, bool force) {
    gui.foreground_application = path;
    const auto events = gui.app_runtime.apply(gui.app_profile_config, gui.profiles,
                                              gui.displays, path, force);
    for (const RuntimeEvent& event : events) {
        switch (event.kind) {
        case RuntimeEventKind::applied:
            set_status(gui, L"Auto: " + executable_name(path) + L" \u2192 " + event.label,
                       StatusTone::success);
            break;
        case RuntimeEventKind::restored:
            set_status(gui, L"Desktop settings restored", StatusTone::success);
            break;
        case RuntimeEventKind::restore_failed:
            set_status(gui, L"Could not restore desktop settings: " + event.error,
                       StatusTone::error);
            break;
        case RuntimeEventKind::capture_failed:
            set_status(gui, L"Could not read desktop settings: " + event.error,
                       StatusTone::error);
            break;
        case RuntimeEventKind::apply_failed:
            set_status(gui, L"Automatic profile switch failed: " + event.error,
                       StatusTone::error);
            break;
        }
    }
    update_automatic_profile_controls(gui);
}

bool pause_automatic_profiles(GuiState& gui) {
    if (!restore_automatic_profiles(gui)) return false;
    if (gui.app_profile_config.enabled) gui.auto_switch_paused = true;
    update_automatic_profile_controls(gui);
    return true;
}

void resume_automatic_profiles(GuiState& gui) {
    if (!ensure_foreground_watcher(gui)) return;
    if (!gui.app_profile_config.enabled) {
        AppProfileConfig config = gui.app_profile_config;
        config.enabled = true;
        if (!save_app_config(gui, config)) return;
    }
    gui.auto_switch_paused = false;
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void toggle_automatic_profiles(GuiState& gui) {
    if (!gui.app_profile_config.enabled || gui.auto_switch_paused) {
        resume_automatic_profiles(gui);
        return;
    }
    if (!restore_automatic_profiles(gui)) return;
    AppProfileConfig config = gui.app_profile_config;
    config.enabled = false;
    save_app_config(gui, config);
}

void bind_application_profile(GuiState& gui, std::size_t profile_index) {
    const auto* display = selected_display(gui);
    if (!display || profile_index >= gui.profiles.size() ||
        !gui.profiles[profile_index].saved) return;
    if (!restore_automatic_profiles(gui)) return;
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    const std::wstring display_id = display_storage_id(*display);
    const std::wstring profile_id = gui.profiles[profile_index].id;
    const std::wstring profile_name = gui.profiles[profile_index].name;
    gui.app_rules_dialog_open = true;
    const std::wstring path = choose_running_application(gui.window, gui.foreground_application);
    AppProfileRule rule{path, display_id, profile_id, true, {}, profile_name};
    const bool accepted = !path.empty() && edit_application_rule(gui, gui.window, rule, gui.profiles);
    gui.app_rules_dialog_open = false;
    if (accepted && ensure_foreground_watcher(gui)) {
        AppProfileConfig config = gui.app_profile_config;
        if (rule.enabled) bind_app_profile_rule(config, rule);
        else set_app_profile_rule(config, rule);
        if (save_app_config(gui, config)) {
            if (rule.enabled) gui.auto_switch_paused = false;
            set_status(gui, executable_name(rule.executable_path) + L" bound to " + rule.name,
                       StatusTone::success);
        }
    }
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void show_application_rules(GuiState& gui) {
    if (!restore_automatic_profiles(gui)) return;
    normalize_all_edits(gui);
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    RulesDialogState state{gui, gui.app_profile_config, gui.profiles};
    gui.app_rules_dialog_open = true;
    update_automatic_profile_controls(gui);
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDD_APP_RULES), gui.window,
                                           rules_dialog_proc, reinterpret_cast<LPARAM>(&state));
    gui.app_rules_dialog_open = false;
    if (result == -1) {
        set_status(gui, L"Could not open application profiles", StatusTone::error);
    } else if (result == IDOK) {
        set_status(gui, L"Application profiles saved", StatusTone::success);
    }
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void remove_application_profile_rules(GuiState& gui, const std::wstring& profile_id) {
    AppProfileConfig config = gui.app_profile_config;
    if (remove_app_profile_rules_for_profile(config, profile_id) > 0) save_app_config(gui, config);
    schedule_automatic_profiles(gui);
}

}  // namespace gamma_changer
