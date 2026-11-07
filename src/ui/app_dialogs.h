#pragma once

// The two application-profile dialogs. They work on drafts and never touch the
// display or GuiState directly, so tests and the snapshot tool can drive them
// with plain data.
//
//   App profiles  (IDD_APP_PROFILES)  automatic switching on/off, per-display
//                                     "app -> profile" list, profile for other apps
//   Choose app    (IDD_APP_ASSIGN)    pick one app (running list or Browse) and
//                                     the profile it should use

#include "app_profile_rules.h"
#include "foreground_app_watcher.h"
#include "gamma_types.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace gamma_changer {

struct DisplayChoice {
    std::wstring id;     // display_storage_id
    std::wstring label;  // "S65  (Screen 1)" or "Disconnected display"
};

using RunningAppSource = std::function<std::vector<RunningApplication>()>;

// ---- Choose app ------------------------------------------------------------
struct AppAssignDraft {
    std::vector<Profile> profiles;      // saved profiles offered in the combo
    std::wstring executable_path;       // in: preselection, out: chosen app
    std::wstring profile_id;            // in: preselection, out: chosen profile
    std::wstring last_active_path;      // listed first and marked as last active
    std::wstring title = L"Choose app";
    RunningAppSource running_apps;      // refresh source (defaults to the system list)

    // Dialog-owned state.
    std::vector<RunningApplication> rows;
    std::vector<std::wstring> profile_ids;
    bool filling = false;
};

INT_PTR CALLBACK app_assign_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
bool run_app_assign_dialog(HWND owner, AppAssignDraft& draft);

// ---- App profiles ----------------------------------------------------------
struct AppProfilesDraft {
    AppProfileConfig config;
    std::vector<Profile> profiles;
    std::vector<DisplayChoice> displays;
    std::wstring display_id;            // display currently shown
    std::wstring last_active_path;
    RunningAppSource running_apps;
    // Persists the draft. Returning false keeps the dialog open with `error`.
    std::function<bool(const AppProfileConfig&, std::wstring& error)> commit;
    // Picks an app/profile for Add and Change. Defaults to the Choose app dialog.
    std::function<bool(HWND owner, AppAssignDraft&)> choose = run_app_assign_dialog;

    // Dialog-owned state.
    std::vector<AppAssignment> rows;
    std::vector<std::wstring> other_profile_ids;
};

INT_PTR CALLBACK app_profiles_dialog_proc(HWND dialog, UINT message, WPARAM wparam,
                                          LPARAM lparam);

std::wstring executable_display_name(const std::wstring& path);

}  // namespace gamma_changer
