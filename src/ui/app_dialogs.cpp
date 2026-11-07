#include "app_dialogs.h"

#include "resource.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>

namespace gamma_changer {
namespace {

const Profile* find_profile(const std::vector<Profile>& profiles, const std::wstring& id) {
    const auto found = std::find_if(profiles.begin(), profiles.end(),
                                    [&](const Profile& profile) { return profile.id == id; });
    return found == profiles.end() ? nullptr : &*found;
}

bool same_path(const std::wstring& left, const std::wstring& right) {
    return normalize_executable_path(left) == normalize_executable_path(right);
}

std::vector<RunningApplication> query_running_apps(const RunningAppSource& source) {
    return source ? source() : running_applications();
}

void add_columns(HWND list, std::initializer_list<std::pair<const wchar_t*, int>> columns) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                LVS_EX_LABELTIP);
    SetWindowTheme(list, L"Explorer", nullptr);
    RECT client{};
    GetClientRect(list, &client);
    const int width = client.right - GetSystemMetrics(SM_CXVSCROLL);
    int index = 0;
    for (const auto& [title, percent] : columns) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(title);
        column.cx = MulDiv(width, percent, 100);
        ListView_InsertColumn(list, index++, &column);
    }
    ListView_SetColumnWidth(list, index - 1, LVSCW_AUTOSIZE_USEHEADER);
}

int selected_row(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

void select_row(HWND list, int row) {
    if (row < 0) return;
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, row, FALSE);
}

int insert_row(HWND list, int index, std::wstring first, std::wstring second) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.pszText = first.data();
    const int row = ListView_InsertItem(list, &item);
    ListView_SetItemText(list, row, 1, second.data());
    return row;
}

// Fills a combo with saved profiles, optionally preceded by one extra entry
// (empty id). Returns the ids in combo order and selects `selected_id`.
std::vector<std::wstring> fill_profile_combo(HWND combo, const std::vector<Profile>& profiles,
                                             const std::wstring& selected_id,
                                             const wchar_t* leading_entry) {
    std::vector<std::wstring> ids;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (leading_entry) {
        ids.emplace_back();
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(leading_entry));
    }
    int selected = leading_entry ? 0 : -1;
    for (const auto& profile : profiles) {
        if (!profile.saved) continue;
        if (profile.id == selected_id) selected = static_cast<int>(ids.size());
        ids.push_back(profile.id);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.name.c_str()));
    }
    if (selected < 0 && !ids.empty()) selected = 0;
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    return ids;
}

std::wstring combo_choice(HWND combo, const std::vector<std::wstring>& ids) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return index >= 0 && static_cast<std::size_t>(index) < ids.size()
               ? ids[static_cast<std::size_t>(index)]
               : std::wstring{};
}

std::wstring choose_executable(HWND owner) {
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&picker)))) {
        return {};
    }
    const COMDLG_FILTERSPEC filter{L"Applications (*.exe)", L"*.exe"};
    picker->SetTitle(L"Choose an application");
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

// ============================================================================
// Choose app
// ============================================================================

void build_assign_rows(AppAssignDraft& draft, std::vector<RunningApplication> running) {
    // Last active external app first: it is almost always the one being bound.
    if (!draft.last_active_path.empty()) {
        const auto active = std::find_if(running.begin(), running.end(),
                                         [&](const RunningApplication& app) {
            return same_path(app.executable_path, draft.last_active_path);
        });
        if (active != running.end()) std::rotate(running.begin(), active, active + 1);
    }
    // An app chosen earlier (Change, Browse) stays selectable when not running.
    if (!draft.executable_path.empty() &&
        std::none_of(running.begin(), running.end(), [&](const RunningApplication& app) {
            return same_path(app.executable_path, draft.executable_path);
        })) {
        running.insert(running.begin(), {draft.executable_path, L"Not running", 0, ERROR_SUCCESS});
    }
    draft.rows = std::move(running);
}

const RunningApplication* assign_selection(HWND dialog, const AppAssignDraft& draft) {
    const int row = selected_row(GetDlgItem(dialog, IDC_ASSIGN_APP_LIST));
    return row >= 0 && static_cast<std::size_t>(row) < draft.rows.size()
               ? &draft.rows[static_cast<std::size_t>(row)]
               : nullptr;
}

void update_assign_state(HWND dialog, const AppAssignDraft& draft) {
    const auto* app = assign_selection(dialog, draft);
    const bool readable = app && !app->executable_path.empty();
    std::wstring path;
    if (readable) {
        path = app->executable_path;
    } else if (app) {
        path = L"Windows did not allow reading this app's location (error " +
               std::to_wstring(app->error) + L"). Use Browse to pick its .exe file.";
    } else if (draft.rows.empty()) {
        path = L"No apps with an open window. Use Browse to pick an .exe file.";
    }
    SetDlgItemTextW(dialog, IDC_ASSIGN_PATH, path.c_str());
    const bool has_profile =
        !combo_choice(GetDlgItem(dialog, IDC_ASSIGN_PROFILE), draft.profile_ids).empty();
    SetDlgItemTextW(dialog, IDC_ASSIGN_HINT,
                    has_profile ? L"" : L"Create a profile first; apps can only use saved profiles.");
    EnableWindow(GetDlgItem(dialog, IDOK), readable && has_profile);
}

void fill_assign_list(HWND dialog, AppAssignDraft& draft, const std::wstring& preferred) {
    draft.filling = true;
    HWND list = GetDlgItem(dialog, IDC_ASSIGN_APP_LIST);
    ListView_DeleteAllItems(list);
    int selected = -1;
    for (std::size_t index = 0; index < draft.rows.size(); ++index) {
        const auto& app = draft.rows[index];
        std::wstring name = app.executable_path.empty()
                                ? L"Unknown app"
                                : executable_display_name(app.executable_path);
        if (!draft.last_active_path.empty() &&
            same_path(app.executable_path, draft.last_active_path)) {
            name += L"  \u2022 last active";
        }
        const int row = insert_row(list, static_cast<int>(index), name, app.window_title);
        if (app.executable_path.empty()) continue;
        if (selected < 0) selected = row;
        if (!preferred.empty() && same_path(app.executable_path, preferred)) selected = row;
    }
    select_row(list, selected);
    draft.filling = false;
    update_assign_state(dialog, draft);
}

}  // namespace

std::wstring executable_display_name(const std::wstring& path) {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

INT_PTR CALLBACK app_assign_dialog_proc(HWND dialog, UINT message, WPARAM wparam,
                                        LPARAM lparam) {
    auto* draft = reinterpret_cast<AppAssignDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        draft = reinterpret_cast<AppAssignDraft*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(draft));
        SetWindowTextW(dialog, draft->title.c_str());
        add_columns(GetDlgItem(dialog, IDC_ASSIGN_APP_LIST), {{L"App", 38}, {L"Window", 62}});
        draft->profile_ids = fill_profile_combo(GetDlgItem(dialog, IDC_ASSIGN_PROFILE),
                                                draft->profiles, draft->profile_id, nullptr);
        build_assign_rows(*draft, query_running_apps(draft->running_apps));
        fill_assign_list(dialog, *draft,
                         draft->executable_path.empty() ? draft->last_active_path
                                                        : draft->executable_path);
        return TRUE;
    }
    if (!draft) return FALSE;
    switch (message) {
    case WM_NOTIFY: {
        const auto* notice = reinterpret_cast<const NMHDR*>(lparam);
        if (notice->idFrom != IDC_ASSIGN_APP_LIST || draft->filling) break;
        if (notice->code == LVN_ITEMCHANGED) update_assign_state(dialog, *draft);
        if (notice->code == NM_DBLCLK && IsWindowEnabled(GetDlgItem(dialog, IDOK))) {
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_ASSIGN_PROFILE:
            if (HIWORD(wparam) == CBN_SELCHANGE) update_assign_state(dialog, *draft);
            return TRUE;
        case IDC_ASSIGN_REFRESH: {
            const auto* app = assign_selection(dialog, *draft);
            const std::wstring keep = app ? app->executable_path : std::wstring{};
            draft->executable_path = keep;
            build_assign_rows(*draft, query_running_apps(draft->running_apps));
            fill_assign_list(dialog, *draft, keep);
            return TRUE;
        }
        case IDC_ASSIGN_BROWSE: {
            const std::wstring path = choose_executable(dialog);
            if (path.empty()) return TRUE;
            draft->executable_path = path;
            std::vector<RunningApplication> current = draft->rows;
            build_assign_rows(*draft, std::move(current));
            fill_assign_list(dialog, *draft, path);
            return TRUE;
        }
        case IDOK: {
            const auto* app = assign_selection(dialog, *draft);
            const std::wstring profile =
                combo_choice(GetDlgItem(dialog, IDC_ASSIGN_PROFILE), draft->profile_ids);
            if (!app || app->executable_path.empty() || profile.empty()) return TRUE;
            draft->executable_path = app->executable_path;
            draft->profile_id = profile;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

bool run_app_assign_dialog(HWND owner, AppAssignDraft& draft) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_ASSIGN), owner,
                           app_assign_dialog_proc, reinterpret_cast<LPARAM>(&draft)) == IDOK;
}

// ============================================================================
// App profiles
// ============================================================================
namespace {

std::wstring first_saved_profile(const std::vector<Profile>& profiles) {
    for (const auto& profile : profiles) {
        if (profile.saved) return profile.id;
    }
    return {};
}

const AppAssignment* selected_assignment(HWND dialog, const AppProfilesDraft& draft) {
    const int row = selected_row(GetDlgItem(dialog, IDC_APP_LIST));
    return row >= 0 && static_cast<std::size_t>(row) < draft.rows.size()
               ? &draft.rows[static_cast<std::size_t>(row)]
               : nullptr;
}

void update_profiles_state(HWND dialog, const AppProfilesDraft& draft) {
    const bool selected = selected_assignment(dialog, draft) != nullptr;
    const bool can_add = !draft.display_id.empty() && !first_saved_profile(draft.profiles).empty();
    EnableWindow(GetDlgItem(dialog, IDC_APP_ADD), can_add);
    EnableWindow(GetDlgItem(dialog, IDC_APP_CHANGE), selected && can_add);
    EnableWindow(GetDlgItem(dialog, IDC_APP_REMOVE), selected);

    const bool has_apps = !draft.config.rules.empty();
    const wchar_t* hint = L"";
    if (!draft.config.enabled && (has_apps || !draft.config.fallback_profiles.empty())) {
        hint = L"Automatic switching is off: these settings are kept but not used.";
    } else if (draft.config.enabled && !has_apps && draft.config.fallback_profiles.empty()) {
        hint = L"Add an app to start switching automatically.";
    }
    SetDlgItemTextW(dialog, IDC_APP_HINT, hint);
}

void fill_app_rows(HWND dialog, AppProfilesDraft& draft, const std::wstring& preferred = {}) {
    draft.rows = app_assignments(draft.config, draft.display_id);
    HWND list = GetDlgItem(dialog, IDC_APP_LIST);
    ListView_DeleteAllItems(list);
    int selected = draft.rows.empty() ? -1 : 0;
    for (std::size_t index = 0; index < draft.rows.size(); ++index) {
        const auto& row = draft.rows[index];
        const auto* profile = find_profile(draft.profiles, row.profile_id);
        std::wstring profile_name = profile ? profile->name : L"Deleted profile";
        if (!row.enabled) profile_name += L"  (off)";
        const int item = insert_row(list, static_cast<int>(index),
                                    executable_display_name(row.executable_path), profile_name);
        if (!preferred.empty() && same_path(row.executable_path, preferred)) selected = item;
    }
    select_row(list, selected);

    const auto* fallback = find_app_fallback_profile(draft.config, draft.display_id);
    draft.other_profile_ids =
        fill_profile_combo(GetDlgItem(dialog, IDC_APP_OTHER_PROFILE), draft.profiles,
                           fallback ? fallback->profile_id : std::wstring{},
                           L"Desktop calibration (no change)");
    EnableWindow(GetDlgItem(dialog, IDC_APP_OTHER_PROFILE), !draft.display_id.empty());
    update_profiles_state(dialog, draft);
}

void fill_display_choices(HWND dialog, AppProfilesDraft& draft) {
    for (const auto& id : app_profile_display_ids(draft.config)) {
        const bool known = std::any_of(draft.displays.begin(), draft.displays.end(),
                                       [&](const DisplayChoice& choice) { return choice.id == id; });
        if (!known) draft.displays.push_back({id, L"Disconnected display"});
    }
    HWND combo = GetDlgItem(dialog, IDC_APP_DISPLAY);
    int selected = draft.displays.empty() ? -1 : 0;
    for (std::size_t index = 0; index < draft.displays.size(); ++index) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(draft.displays[index].label.c_str()));
        if (draft.displays[index].id == draft.display_id) selected = static_cast<int>(index);
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    draft.display_id =
        selected >= 0 ? draft.displays[static_cast<std::size_t>(selected)].id : std::wstring{};
    // One display: the selector is noise.
    const int show = draft.displays.size() > 1 ? SW_SHOW : SW_HIDE;
    ShowWindow(combo, show);
    ShowWindow(GetDlgItem(dialog, IDC_APP_DISPLAY_LABEL), show);
}

void set_automatic(HWND dialog, AppProfilesDraft& draft, bool enabled) {
    draft.config.enabled = enabled;
    CheckDlgButton(dialog, IDC_APP_AUTO_SWITCH, enabled ? BST_CHECKED : BST_UNCHECKED);
}

void edit_assignment(HWND dialog, AppProfilesDraft& draft, const AppAssignment* existing) {
    AppAssignDraft pick;
    pick.profiles = draft.profiles;
    pick.last_active_path = draft.last_active_path;
    pick.running_apps = draft.running_apps;
    if (existing) {
        pick.title = L"Change app";
        pick.executable_path = existing->executable_path;
        pick.profile_id = existing->profile_id;
    } else {
        pick.title = L"Add app";
        pick.profile_id = first_saved_profile(draft.profiles);
    }
    const std::wstring original = existing ? existing->executable_path : std::wstring{};
    if (!draft.choose || !draft.choose(dialog, pick)) return;
    if (!original.empty() && !same_path(original, pick.executable_path)) {
        unassign_app_profile(draft.config, original, draft.display_id);
    }
    assign_app_profile(draft.config, pick.executable_path, draft.display_id, pick.profile_id);
    // Adding an app is a request for it to work: turn switching on (visible
    // in the checkbox, and nothing is saved until Save).
    if (!existing) set_automatic(dialog, draft, true);
    fill_app_rows(dialog, draft, pick.executable_path);
}

}  // namespace

INT_PTR CALLBACK app_profiles_dialog_proc(HWND dialog, UINT message, WPARAM wparam,
                                          LPARAM lparam) {
    auto* draft = reinterpret_cast<AppProfilesDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        draft = reinterpret_cast<AppProfilesDraft*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(draft));
        add_columns(GetDlgItem(dialog, IDC_APP_LIST), {{L"App", 50}, {L"Profile", 50}});
        set_automatic(dialog, *draft, draft->config.enabled);
        fill_display_choices(dialog, *draft);
        fill_app_rows(dialog, *draft);
        return TRUE;
    }
    if (!draft) return FALSE;
    switch (message) {
    case WM_NOTIFY: {
        const auto* notice = reinterpret_cast<const NMHDR*>(lparam);
        if (notice->idFrom != IDC_APP_LIST) break;
        if (notice->code == LVN_ITEMCHANGED) update_profiles_state(dialog, *draft);
        if (notice->code == NM_DBLCLK) SendMessageW(dialog, WM_COMMAND, IDC_APP_CHANGE, 0);
        if (notice->code == LVN_KEYDOWN &&
            reinterpret_cast<const NMLVKEYDOWN*>(lparam)->wVKey == VK_DELETE) {
            SendMessageW(dialog, WM_COMMAND, IDC_APP_REMOVE, 0);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_APP_AUTO_SWITCH:
            draft->config.enabled = IsDlgButtonChecked(dialog, IDC_APP_AUTO_SWITCH) == BST_CHECKED;
            update_profiles_state(dialog, *draft);
            return TRUE;
        case IDC_APP_DISPLAY:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                const LRESULT index = SendDlgItemMessageW(dialog, IDC_APP_DISPLAY, CB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<std::size_t>(index) < draft->displays.size()) {
                    draft->display_id = draft->displays[static_cast<std::size_t>(index)].id;
                    fill_app_rows(dialog, *draft);
                }
            }
            return TRUE;
        case IDC_APP_OTHER_PROFILE:
            if (HIWORD(wparam) == CBN_SELCHANGE && !draft->display_id.empty()) {
                set_app_fallback_profile(
                    draft->config, draft->display_id,
                    combo_choice(GetDlgItem(dialog, IDC_APP_OTHER_PROFILE),
                                 draft->other_profile_ids));
                update_profiles_state(dialog, *draft);
            }
            return TRUE;
        case IDC_APP_ADD:
            if (IsWindowEnabled(GetDlgItem(dialog, IDC_APP_ADD))) {
                edit_assignment(dialog, *draft, nullptr);
            }
            return TRUE;
        case IDC_APP_CHANGE:
            if (const auto* row = selected_assignment(dialog, *draft)) {
                const AppAssignment copy = *row;  // rows are rebuilt by the edit
                edit_assignment(dialog, *draft, &copy);
            }
            return TRUE;
        case IDC_APP_REMOVE:
            if (const auto* row = selected_assignment(dialog, *draft)) {
                unassign_app_profile(draft->config, row->executable_path, draft->display_id);
                fill_app_rows(dialog, *draft);
            }
            return TRUE;
        case IDOK: {
            std::wstring error;
            if (draft->commit && !draft->commit(draft->config, error)) {
                SetDlgItemTextW(dialog, IDC_APP_HINT, (L"Could not save: " + error).c_str());
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

}  // namespace gamma_changer
