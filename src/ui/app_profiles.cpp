// Glue between the sidebar, the two app dialogs (app_dialogs.cpp) and the
// runtime engine (AppProfileRuntime). Decisions live in core; presentation
// lives in the dialogs; this file only wires them to GuiState.
#include "gui_internal.h"

#include "app_dialogs.h"

namespace gamma_changer {
namespace {

bool editor_in_foreground(const GuiState& gui) {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId() && IsWindowVisible(gui.window) &&
           !IsIconic(gui.window);
}

const Profile* profile_by_id(const GuiState& gui, const std::wstring& id) {
    const auto found = std::find_if(gui.profiles.begin(), gui.profiles.end(),
                                    [&](const Profile& profile) { return profile.id == id; });
    return found == gui.profiles.end() ? nullptr : &*found;
}

std::wstring executable_name(const std::wstring& path) {
    return executable_display_name(path);
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

// The single persist path for app-profile configuration. Turning switching on
// always clears a manual pause: the user just asked for it to run.
bool commit_app_config(GuiState& gui, const AppProfileConfig& config, std::wstring& error) {
    if (config.enabled && !ensure_foreground_watcher(gui)) {
        error = L"cannot monitor the foreground application";
        return false;
    }
    if (!gui.store.save_app_profile_config(config, error)) return false;
    gui.app_profile_config = config;
    if (config.enabled) gui.auto_switch_paused = false;
    update_automatic_profile_controls(gui);
    return true;
}

bool save_app_config(GuiState& gui, const AppProfileConfig& config) {
    std::wstring error;
    if (commit_app_config(gui, config, error)) return true;
    set_status(gui, L"Could not save app profiles: " + error, StatusTone::error);
    return false;
}

std::vector<DisplayChoice> display_choices(const GuiState& gui) {
    std::vector<DisplayChoice> choices;
    for (std::size_t index = 0; index < gui.displays.size(); ++index) {
        choices.push_back({display_storage_id(gui.displays[index]),
                           display_label(gui.displays[index], index)});
    }
    return choices;
}

// Leaves the desktop calibration on screen and commits pending slider edits
// before a dialog takes over, so nothing on screen changes behind it.
bool prepare_for_app_dialog(GuiState& gui) {
    if (!restore_automatic_profiles(gui)) return false;
    normalize_all_edits(gui);
    return !gui.session.dirty || save_and_apply_current(gui, true);
}

}  // namespace

void update_automatic_profile_controls(GuiState& gui) {
    invalidate_control_background(gui, gui.auto_switch_button);
    const auto* display = selected_display(gui);
    std::wstring text;
    if (!gui.app_profile_config.enabled) {
        text = L"Use a profile automatically while an app is in front";
    } else if (gui.auto_switch_paused) {
        text = L"Paused after a manual change \u00b7 click the switch to resume";
    } else if (gui.app_rules_dialog_open || editor_in_foreground(gui)) {
        text = L"Paused while Gamma Changer is in front";
    } else if (gui.foreground_lookup_error != ERROR_SUCCESS) {
        text = L"Can't identify the active app (error " +
               std::to_wstring(gui.foreground_lookup_error) + L")";
    } else {
        const RuntimeDisplayState* active =
            display ? gui.app_runtime.active(display_storage_id(*display)) : nullptr;
        const auto* profile = active ? profile_by_id(gui, active->profile_id) : nullptr;
        const std::wstring app = executable_name(gui.foreground_application);
        if (profile) {
            text = app + L" \u2192 " + profile->name;
            if (active->rule_id.empty()) text += L" (other apps)";
        } else {
            text = app.empty() ? L"Waiting for an app" : app + L" \u00b7 desktop calibration";
        }
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
        !gui.profiles[profile_index].saved || !gui.profile_store_available) {
        return;
    }
    if (!prepare_for_app_dialog(gui)) return;
    const std::wstring display_id = display_storage_id(*display);
    AppAssignDraft pick;
    pick.profiles = gui.profiles;
    pick.profile_id = gui.profiles[profile_index].id;
    pick.last_active_path = gui.foreground_application;
    pick.title = L"Use \u201c" + gui.profiles[profile_index].name + L"\u201d for an app";

    gui.app_rules_dialog_open = true;
    update_automatic_profile_controls(gui);
    const bool accepted = run_app_assign_dialog(gui.window, pick);
    gui.app_rules_dialog_open = false;
    if (accepted) {
        AppProfileConfig config = gui.app_profile_config;
        assign_app_profile(config, pick.executable_path, display_id, pick.profile_id);
        config.enabled = true;
        if (save_app_config(gui, config)) {
            const auto* profile = profile_by_id(gui, pick.profile_id);
            set_status(gui, executable_name(pick.executable_path) + L" will use " +
                                (profile ? profile->name : std::wstring(L"the profile")),
                       StatusTone::success);
        }
    }
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void show_application_rules(GuiState& gui) {
    if (!prepare_for_app_dialog(gui)) return;
    AppProfilesDraft draft;
    draft.config = gui.app_profile_config;
    draft.profiles = gui.profiles;
    draft.displays = display_choices(gui);
    if (const auto* display = selected_display(gui)) draft.display_id = display_storage_id(*display);
    draft.last_active_path = gui.foreground_application;
    draft.commit = [&gui](const AppProfileConfig& config, std::wstring& error) {
        return commit_app_config(gui, config, error);
    };

    gui.app_rules_dialog_open = true;
    update_automatic_profile_controls(gui);
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDD_APP_PROFILES), gui.window,
                                           app_profiles_dialog_proc,
                                           reinterpret_cast<LPARAM>(&draft));
    gui.app_rules_dialog_open = false;
    if (result == -1) {
        set_status(gui, L"Could not open app profiles", StatusTone::error);
    } else if (result == IDOK) {
        set_status(gui, L"App profiles saved", StatusTone::success);
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
