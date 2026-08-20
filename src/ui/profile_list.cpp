#include "gui_internal.h"

namespace gamma_changer {

LRESULT CALLBACK profile_rename_proc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(edit));
    if (message == WM_KEYDOWN && wparam == VK_RETURN && gui) {
        SendMessageW(gui->window, kProfileCommitRenameMessage,
                     kRenameCommitFlag | kRenameRestoreFocusFlag,
                     static_cast<LPARAM>(gui->profile_rename_generation));
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && gui) {
        SendMessageW(gui->window, kProfileCommitRenameMessage,
                     kRenameRestoreFocusFlag,
                     static_cast<LPARAM>(gui->profile_rename_generation));
        return 0;
    }
    if (message == WM_KILLFOCUS && gui && !gui->destroying && IsWindowVisible(edit)) {
        // Commit after the new target receives focus. A synchronous commit
        // would move focus back to the Profile row and make click-away feel
        // broken or flash the control being clicked.
        PostMessageW(gui->window, kProfileCommitRenameMessage,
                     kRenameCommitFlag,
                     static_cast<LPARAM>(gui->profile_rename_generation));
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, profile_rename_proc, 1);
    return DefSubclassProc(edit, message, wparam, lparam);
}


LRESULT CALLBACK profile_proc(HWND item, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR data) {
    auto* gui = state(GetParent(item));
    if (message == WM_GETDLGCODE) {
        const LRESULT dialog_code = DefSubclassProc(item, message, wparam, lparam);
        if (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_LEFT ||
            wparam == VK_RIGHT) {
            return dialog_code | DLGC_WANTARROWS;
        }
        if (wparam == VK_HOME || wparam == VK_END) {
            return dialog_code | DLGC_WANTALLKEYS;
        }
        return dialog_code;
    }
    if (message == WM_LBUTTONDOWN && gui) {
        gui->profile_keyboard_focus = false;
        InvalidateRect(item, nullptr, FALSE);
    }
    if (message == WM_KEYDOWN && gui && (wparam == VK_F2 || wparam == VK_DELETE)) {
        const int index = GetDlgCtrlID(item) - kProfileIdBase;
        if (index >= 0 && index < static_cast<int>(gui->profiles.size())) {
            if (wparam == VK_F2) begin_profile_rename(*gui, static_cast<std::size_t>(index));
            else delete_preset(*gui, static_cast<std::size_t>(index));
        }
        return 0;
    }
    if (message == WM_KEYDOWN && gui &&
        (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_LEFT ||
         wparam == VK_RIGHT || wparam == VK_HOME || wparam == VK_END)) {
        gui->profile_keyboard_focus = true;
        const int current = GetDlgCtrlID(item) - kProfileIdBase;
        if (current >= 0 && current < static_cast<int>(gui->profiles.size())) {
            std::size_t target = static_cast<std::size_t>(current);
            if ((wparam == VK_UP || wparam == VK_LEFT) && target > 0) --target;
            if ((wparam == VK_DOWN || wparam == VK_RIGHT) &&
                target + 1 < gui->profiles.size()) {
                ++target;
            }
            if (wparam == VK_HOME) target = 0;
            if (wparam == VK_END) target = gui->profiles.size() - 1;
            scroll_profile_into_view(*gui, target);
            if (target < gui->preset_buttons.size()) SetFocus(gui->preset_buttons[target]);
        }
        return 0;
    }
    if (message == WM_CONTEXTMENU && gui) {
        const int index = GetDlgCtrlID(item) - kProfileIdBase;
        if (index >= 0 && index < static_cast<int>(gui->profiles.size())) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(item, &rect);
                point = {rect.left + 16, rect.top + 16};
            }
            PostMessageW(gui->window, kProfileContextMessage,
                         static_cast<WPARAM>(index), MAKELPARAM(point.x, point.y));
            return 0;
        }
    }
    if (message == WM_MOUSEMOVE && !data) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, item, 0};
        TrackMouseEvent(&tracking);
        SetWindowSubclass(item, profile_proc, 1, 1);
        if (gui) {
            gui->hovered_profile = item;
            InvalidateRect(item, nullptr, FALSE);
        }
    }
    if (message == WM_MOUSELEAVE) {
        SetWindowSubclass(item, profile_proc, 1, 0);
        if (gui) gui->hovered_profile = nullptr;
        InvalidateRect(item, nullptr, FALSE);
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(item, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(item, profile_proc, 1);
    return DefSubclassProc(item, message, wparam, lparam);
}



void refresh_preset_buttons(GuiState& gui) {
    ensure_profile_buttons(gui);
    for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
        const std::wstring text = gui.profiles[i].name.empty() ? L"Unnamed profile"
                                                               : gui.profiles[i].name;
        SetWindowTextW(gui.preset_buttons[i], text.c_str());
        invalidate_control_background(gui, gui.preset_buttons[i], 0);
    }
    const bool can_create =
        gui.profile_store_available && gui.profile_preferences_available;
    const bool can_delete = gui.profile_store_available &&
                            gui.profile_preferences_available &&
                            gui.active_profile_linked &&
                            gui.active_preset < gui.profiles.size() &&
                            gui.profiles[gui.active_preset].id != kBuiltinProfileId;
    EnableWindow(gui.preset_save, can_create ? TRUE : FALSE);
    EnableWindow(gui.preset_delete, can_delete ? TRUE : FALSE);
}

bool persist_presets(GuiState& gui);
bool cancel_active_preview(GuiState& gui);

void begin_profile_rename(GuiState& gui, std::size_t index) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return;
    }
    if (index >= gui.profiles.size()) return;
    if (gui.profiles[index].id == kBuiltinProfileId) {
        set_status(gui, L"The built-in Default profile cannot be renamed",
                   StatusTone::warning);
        return;
    }
    if (gui.renaming_preset != kNoProfile) {
        const bool same_profile = !gui.renaming_profile_id.empty()
                                      ? gui.renaming_profile_id == gui.profiles[index].id
                                      : gui.renaming_preset == index;
        if (same_profile) {
            SetWindowPos(gui.profile_rename, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                             SWP_SHOWWINDOW);
            SetFocus(gui.profile_rename);
            return;
        }
        if (!same_profile && !finish_profile_rename(gui, true, false)) return;
    }
    scroll_profile_into_view(gui, index);
    gui.renaming_preset = index;
    gui.renaming_profile_id = gui.profiles[index].id;
    ++gui.profile_rename_generation;
    // Keep the owner-drawn row as the visual host. It paints the rounded
    // editing surface and accent while the EDIT overlays only the text area.
    InvalidateRect(gui.preset_buttons[index], nullptr, FALSE);
    SetWindowTextW(gui.profile_rename, gui.profiles[index].name.c_str());
    RECT client{};
    GetClientRect(gui.window, &client);
    layout_controls(gui, client.right, client.bottom);
    SetWindowPos(gui.profile_rename, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(gui.profile_rename, nullptr, TRUE);
    SetFocus(gui.profile_rename);
    SendMessageW(gui.profile_rename, EM_SETSEL, 0, -1);
}

bool finish_profile_rename(GuiState& gui, bool commit, bool restore_focus) {
    if (gui.renaming_preset == kNoProfile) return true;
    std::size_t index = gui.renaming_preset;
    if (!gui.renaming_profile_id.empty()) {
        const auto profile = std::find_if(
            gui.profiles.begin(), gui.profiles.end(),
            [&](const Profile& item) {
                return item.id == gui.renaming_profile_id;
            });
        if (profile == gui.profiles.end()) {
            gui.renaming_preset = kNoProfile;
            gui.renaming_profile_id.clear();
            ShowWindow(gui.profile_rename, SW_HIDE);
            layout_controls_for_current_size(gui);
            return true;
        }
        index = static_cast<std::size_t>(
            std::distance(gui.profiles.begin(), profile));
        gui.renaming_preset = index;
    } else if (index >= gui.profiles.size()) {
        gui.renaming_preset = kNoProfile;
        gui.renaming_profile_id.clear();
        ShowWindow(gui.profile_rename, SW_HIDE);
        layout_controls_for_current_size(gui);
        return true;
    }
    if (!commit) {
        gui.renaming_preset = kNoProfile;
        gui.renaming_profile_id.clear();
        ShowWindow(gui.profile_rename, SW_HIDE);
        layout_controls_for_current_size(gui);
        InvalidateRect(gui.preset_buttons[index], nullptr, TRUE);
        if (restore_focus) SetFocus(gui.preset_buttons[index]);
        return true;
    }
    wchar_t text[96]{};
    GetWindowTextW(gui.profile_rename, text, static_cast<int>(std::size(text)));
    std::wstring name = text;
    for (auto& ch : name) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    const auto first = name.find_first_not_of(L' ');
    const auto last = name.find_last_not_of(L' ');
    if (first == std::wstring::npos) {
        set_status(gui, L"Profile name cannot be empty", StatusTone::warning);
        SetFocus(gui.profile_rename);
        return false;
    }
    name = name.substr(first, last - first + 1);
    if (name.size() > 48) name.resize(48);
    const std::wstring previous = gui.profiles[index].name;
    gui.profiles[index].name = name;
    if (!persist_presets(gui)) {
        gui.profiles[index].name = previous;
        SetFocus(gui.profile_rename);
        return false;
    }
    gui.renaming_preset = kNoProfile;
    gui.renaming_profile_id.clear();
    ShowWindow(gui.profile_rename, SW_HIDE);
    layout_controls_for_current_size(gui);
    InvalidateRect(gui.preset_buttons[index], nullptr, TRUE);
    set_status(gui, name + L" renamed", StatusTone::success);
    refresh_preset_buttons(gui);
    if (restore_focus) SetFocus(gui.preset_buttons[index]);
    return true;
}

void duplicate_profile(GuiState& gui, std::size_t source_index) {
    if (source_index >= gui.profiles.size()) return;
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return;
    }
    if (!gui.profile_preferences_available) {
        set_status(gui,
                   L"New Profiles are disabled until display associations can be repaired",
                   StatusTone::error);
        return;
    }
    Profile copy = gui.profiles[source_index];
    copy.id = next_profile_id(gui.profiles);
    copy.name += L" Copy";
    if (copy.name.size() > 48) copy.name.resize(48);
    const auto previous = gui.profiles;
    gui.profiles.push_back(copy);
    if (!persist_presets(gui)) {
        gui.profiles = previous;
        return;
    }
    // Creating a copy is a list operation only: keep the current selection and
    // current display calibration unchanged.
    refresh_preset_buttons(gui);
    layout_controls_for_current_size(gui);
    set_status(gui, copy.name + L" created", StatusTone::success);
}

void create_profile_from_current(GuiState& gui) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return;
    }
    if (!gui.profile_preferences_available) {
        set_status(gui,
                   L"New Profiles are disabled until display associations can be repaired",
                   StatusTone::error);
        return;
    }
    Profile profile;
    profile.id = next_profile_id(gui.profiles);
    profile.name = next_custom_profile_name(gui.profiles);
    profile.settings = params_from_sliders(gui);
    profile.saved = true;
    const auto previous = gui.profiles;
    gui.profiles.push_back(profile);
    if (!persist_presets(gui)) {
        gui.profiles = previous;
        return;
    }
    // A new Profile is a snapshot of the visible calibration. This gives an
    // unlinked legacy/display-specific setup a clear, lossless way to become a
    // named Profile instead of creating a surprising neutral placeholder.
    const std::size_t new_index = gui.profiles.size() - 1;
    select_preset(gui, new_index);
    if (gui.active_preset == new_index) {
        scroll_profile_into_view(gui, new_index);
        set_status(gui, profile.name + L" created and applied  |  Ctrl+Z to go back",
                   StatusTone::success);
    } else {
        layout_controls_for_current_size(gui);
    }
}

bool persist_presets(GuiState& gui) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return false;
    }
    std::wstring error;
    if (!gui.profile_manager.replace_profiles(gui.profiles, error)) {
        set_status(gui, L"Profile save failed: " + error, StatusTone::error);
        return false;
    }
    return true;
}

bool save_and_apply_current(GuiState& gui, bool automatic);

void select_preset(GuiState& gui, std::size_t index) {
    if (index >= gui.profiles.size() || gui.active_preset >= gui.profiles.size()) return;
    if (gui.active_profile_linked && index == gui.active_preset) return;
    if (!selected_display_ready(gui)) {
        set_status(gui, L"This display is still being identified or restored",
                   StatusTone::warning);
        return;
    }
    if (!gui.profile_preferences_available) {
        set_status(gui,
                   L"Profile associations are read-only until the damaged preference file is repaired",
                   StatusTone::error);
        return;
    }
    KillTimer(gui.window, kAutoSaveTimer);
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    if (!cancel_active_preview(gui)) return;
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return;
    }
    const std::wstring display_id = display_storage_id(*display);
    const auto previous_preferences = gui.preferred_profile_ids;
    const bool association_removed =
        gui.preferred_profile_ids.erase(display_id) != 0;
    if (association_removed && !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = previous_preferences;
        set_status(gui,
                   L"Could not safely detach the current Profile before switching",
                   StatusTone::error);
        return;
    }
    gui.session.profile_switch_undo_available = true;
    gui.session.undo_display_id.clear();
    if (const auto* current_display = selected_display(gui)) {
        gui.session.undo_display_id = display_storage_id(*current_display);
    }
    gui.session.undo_preset = gui.active_preset;
    gui.session.undo_params = params_from_sliders(gui);
    gui.session.undo_profile = gui.profiles[gui.active_preset];
    gui.session.undo_profile_linked = gui.active_profile_linked;
    const bool previous_linked = gui.active_profile_linked;
    gui.active_preset = index;
    gui.active_profile_linked = true;
    const GammaParams next_params = gui.profiles[index].settings;
    set_params_to_controls(gui, next_params);
    const CommitResult apply = gui.controller.commit(*display, next_params);
    if (!apply.succeeded()) {
        if (apply.status == CommitStatus::rollback_failed) {
            queue_recovery_after_failed_rollback(
                gui, *display, next_params, index, true, true, false,
                L"Profile switch failed and the display rollback failed: " +
                    apply.error);
            return;
        }
        bool association_restored = true;
        if (association_removed) {
            gui.preferred_profile_ids = previous_preferences;
            association_restored = persist_profile_preferences(gui);
        }
        if (!association_restored) {
            gui.preferred_profile_ids.erase(display_id);
            queue_recovery_after_failed_rollback(
                gui, *display, next_params, index, true, true, false,
                L"Profile switch failed and the previous association could not be restored: " +
                    apply.error);
            return;
        }
        gui.active_preset = gui.session.undo_preset;
        gui.active_profile_linked = previous_linked;
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
        set_params_to_controls(gui, gui.session.undo_params);
        refresh_preset_buttons(gui);
        set_status(gui, L"Profile switch failed: " + apply.error, StatusTone::error);
        return;
    }
    gui.session.committed_settings = next_params;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    if (!remember_active_profile_for_display(gui, *display)) {
        queue_recovery_after_failed_rollback(
            gui, *display, next_params, index, true, true, true,
            L"The Profile was applied, but its display association is queued");
        return;
    }
    set_status(gui, gui.profiles[index].name + L" applied  |  Ctrl+Z to go back",
               StatusTone::success);
}

void undo_profile_switch(GuiState& gui) {
    if (!gui.session.profile_switch_undo_available) {
        set_status(gui, L"Nothing to restore", StatusTone::idle);
        return;
    }
    if (!selected_display_ready(gui)) {
        set_status(gui, L"This display is still being identified or restored",
                   StatusTone::warning);
        return;
    }
    if (!cancel_active_preview(gui)) return;
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return;
    }
    if (gui.session.undo_display_id != display_storage_id(*display)) {
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
        set_status(gui, L"That undo action belongs to a different display",
                   StatusTone::warning);
        return;
    }
    const GammaParams params = gui.session.undo_params;
    const Profile previous_profile = gui.session.undo_profile;
    bool undo_linked = gui.session.undo_profile_linked &&
                       gui.profile_preferences_available;
    std::size_t preset = gui.active_preset < gui.profiles.size()
                             ? gui.active_preset
                             : 0;
    if (undo_linked) {
        const auto profile = std::find_if(
            gui.profiles.begin(), gui.profiles.end(),
            [&](const Profile& item) { return item.id == previous_profile.id; });
        if (profile == gui.profiles.end()) {
            gui.session.profile_switch_undo_available = false;
            gui.session.undo_display_id.clear();
            set_status(gui, L"The Profile needed for undo no longer exists",
                       StatusTone::warning);
            return;
        }
        preset = static_cast<std::size_t>(
            std::distance(gui.profiles.begin(), profile));
        // Undo restores display calibration and association, not an old copy of
        // the global Profile. If that Profile changed since the switch, retain
        // the captured display values as unlinked instead of overwriting newer
        // name/settings metadata.
        if (!settings_equal(profile->settings, params)) undo_linked = false;
    }
    const std::size_t current_preset = gui.active_preset;
    const bool current_linked = gui.active_profile_linked;
    const CalibrationSettings current_committed = gui.session.committed_settings;
    const auto current_preferences = gui.preferred_profile_ids;
    const std::wstring display_id = display_storage_id(*display);
    const bool association_removed =
        gui.profile_preferences_available &&
        gui.preferred_profile_ids.erase(display_id) != 0;
    if (association_removed && !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = current_preferences;
        set_status(gui, L"Could not safely detach the current Profile before undo",
                   StatusTone::error);
        return;
    }
    gui.session.profile_switch_undo_available = false;
    gui.session.undo_display_id.clear();
    gui.active_preset = preset;
    gui.active_profile_linked = undo_linked;
    set_params_to_controls(gui, params);
    const CommitResult apply = gui.controller.commit(*display, params);
    if (!apply.succeeded()) {
        if (apply.status == CommitStatus::rollback_failed) {
            queue_recovery_after_failed_rollback(
                gui, *display, params, preset, undo_linked, true, false,
                L"Undo failed and the display rollback failed: " + apply.error);
            return;
        }
        bool association_restored = true;
        if (association_removed) {
            gui.preferred_profile_ids = current_preferences;
            association_restored = persist_profile_preferences(gui);
        }
        if (!association_restored) {
            gui.preferred_profile_ids.erase(display_id);
            queue_recovery_after_failed_rollback(
                gui, *display, params, preset, undo_linked, true, false,
                L"Undo failed and the previous association could not be restored: " +
                    apply.error);
            return;
        }
        gui.active_preset = current_preset;
        gui.active_profile_linked = current_linked;
        gui.session.profile_switch_undo_available = true;
        gui.session.undo_display_id = display_id;
        set_params_to_controls(gui, current_committed);
        set_status(gui, L"Could not restore previous adjustments: " + apply.error,
                   StatusTone::error);
        refresh_preset_buttons(gui);
        return;
    }
    bool association_saved = true;
    if (undo_linked) {
        association_saved = remember_active_profile_for_display(gui, *display);
    }
    if (!association_saved) {
        queue_recovery_after_failed_rollback(
            gui, *display, params, preset, undo_linked, true, true,
            L"Undo was applied, but its Profile association is queued");
        return;
    }
    gui.session.committed_settings = params;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui,
               undo_linked ? L"Previous profile adjustments restored"
                           : L"Previous unlinked display calibration restored",
               StatusTone::success);
}

void delete_preset(GuiState& gui, std::size_t index) {
    if (!gui.profile_store_available || !gui.profile_preferences_available) {
        set_status(gui,
                   L"Profile deletion is disabled until profile storage is repaired",
                   StatusTone::error);
        return;
    }
    if (index >= gui.profiles.size()) return;
    if (gui.profiles[index].id == kBuiltinProfileId) {
        set_status(gui, L"The built-in Default profile cannot be deleted",
                   StatusTone::warning);
        return;
    }
    const bool restore_profile_focus = index < gui.preset_buttons.size() &&
                                       GetFocus() == gui.preset_buttons[index];
    int choice = IDCANCEL;
    if (FAILED(TaskDialog(gui.window, nullptr, L"Delete this profile?",
                          L"The profile and its saved settings will be removed permanently.",
                          L"Your current display adjustments will not change.",
                          TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, TD_WARNING_ICON, &choice))) {
        log_message(LogLevel::warning, L"Could not show the delete-profile confirmation dialog");
        return;
    }
    if (choice != IDYES) return;
    const auto previous = gui.profiles;
    const auto previous_preferences = gui.preferred_profile_ids;
    const std::size_t previous_active_preset = gui.active_preset;
    const bool previous_active_linked = gui.active_profile_linked;
    const std::wstring deleted_name = gui.profiles[index].name;
    const std::wstring deleted_id = gui.profiles[index].id;
    const bool deleted_was_being_renamed =
        (!gui.renaming_profile_id.empty() &&
         gui.renaming_profile_id == deleted_id) ||
        (gui.renaming_profile_id.empty() && gui.renaming_preset == index);
    const bool deleted_was_active =
        gui.active_profile_linked && index == gui.active_preset;
    gui.profiles.erase(std::next(gui.profiles.begin(), static_cast<std::ptrdiff_t>(index)));
    if (!persist_presets(gui)) {
        gui.profiles = previous;
        refresh_preset_buttons(gui);
        return;
    }
    gui.active_preset =
        active_index_after_delete(gui.profiles.size(), index, gui.active_preset);
    if (deleted_was_active) gui.active_profile_linked = false;
    bool preferences_changed = false;
    for (auto preference = gui.preferred_profile_ids.begin();
         preference != gui.preferred_profile_ids.end();) {
        if (preference->second == deleted_id) {
            preference = gui.preferred_profile_ids.erase(preference);
            preferences_changed = true;
        } else {
            ++preference;
        }
    }
    if (preferences_changed && !persist_profile_preferences(gui)) {
        const auto deleted_profiles = gui.profiles;
        const auto deleted_preferences = gui.preferred_profile_ids;
        const std::size_t deleted_active_preset = gui.active_preset;
        gui.preferred_profile_ids = previous_preferences;
        gui.profiles = previous;
        gui.active_preset = previous_active_preset;
        gui.active_profile_linked = previous_active_linked;
        if (!persist_presets(gui)) {
            // The deletion reached disk but the compensating profile write did
            // not. Keep RAM aligned with the durable deletion and stop treating
            // either cross-file store as writable for the rest of this run.
            gui.profiles = deleted_profiles;
            gui.preferred_profile_ids = deleted_preferences;
            gui.active_preset = deleted_active_preset;
            gui.active_profile_linked = false;
            gui.profile_binding_display_id.clear();
            set_adjustment_enabled(gui, false);
            gui.profile_store_available = false;
            gui.profile_preferences_available = false;
            gui.session.profile_switch_undo_available = false;
            for (auto& [display_id, adjustment] : gui.pending_adjustments) {
                (void)display_id;
                if (adjustment.profile_id == deleted_id) {
                    adjustment.profile_id.clear();
                    adjustment.profile_persisted = true;
                    adjustment.preference_persisted = false;
                }
            }
            log_message(
                LogLevel::error,
                L"Profile preference save failed and the deleted profile "
                L"could not be restored; profile editing is disabled until restart");
            set_status(gui,
                       L"Profile deletion is incomplete; restart before editing profiles",
                       StatusTone::error);
        } else {
            set_status(gui,
                       L"Profile preference save failed; deletion was rolled back",
                       StatusTone::error);
        }
        refresh_preset_buttons(gui);
        layout_controls_for_current_size(gui);
        return;
    }
    if (deleted_was_being_renamed) {
        ShowWindow(gui.profile_rename, SW_HIDE);
        gui.renaming_preset = kNoProfile;
        gui.renaming_profile_id.clear();
    } else if (gui.renaming_preset != kNoProfile &&
               index < gui.renaming_preset) {
        --gui.renaming_preset;
    }
    for (auto& [display_id, adjustment] : gui.pending_adjustments) {
        (void)display_id;
        if (adjustment.profile_id == deleted_id) {
            adjustment.profile_id.clear();
            adjustment.profile_persisted = true;
        }
    }
    if (gui.session.profile_switch_undo_available &&
        gui.session.undo_profile_linked) {
        if (gui.session.undo_profile.id == deleted_id) {
            gui.session.profile_switch_undo_available = false;
        }
        const auto undo_profile = std::find_if(
            gui.profiles.begin(), gui.profiles.end(),
            [&](const Profile& profile) {
                return profile.id == gui.session.undo_profile.id;
            });
        if (gui.session.profile_switch_undo_available &&
            undo_profile == gui.profiles.end()) {
            gui.session.profile_switch_undo_available = false;
        } else if (gui.session.profile_switch_undo_available) {
            gui.session.undo_preset = static_cast<std::size_t>(
                std::distance(gui.profiles.begin(), undo_profile));
        }
    }
    const std::size_t focus_index = gui.profiles.empty()
                                        ? 0
                                        : std::min(index, gui.profiles.size() - 1);
    scroll_profile_into_view(gui, restore_profile_focus ? focus_index : gui.active_preset);
    refresh_preset_buttons(gui);
    layout_controls_for_current_size(gui);
    if (restore_profile_focus && focus_index < gui.preset_buttons.size()) {
        SetFocus(gui.preset_buttons[focus_index]);
    }
    set_status(gui, deleted_name + L" deleted", StatusTone::success);
}

void delete_active_preset(GuiState& gui) {
    delete_preset(gui, gui.active_preset);
}

void scroll_profile_into_view(GuiState& gui, std::size_t index) {
    if (index >= gui.profiles.size()) return;
    const UINT dpi = gui.window ? GetDpiForWindow(gui.window) : 96;
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int row_height = scale(ui::Metrics::profile_row_height);
    const int row_stride = row_height + scale(ui::Metrics::profile_row_gap);
    const int profile_first_y = scale(252) + scale(18) + scale(ui::Metrics::profile_title_gap);
    const int list_bottom = profile_first_y + kProfileVisibleRows * row_height +
                            (kProfileVisibleRows - 1) * scale(ui::Metrics::profile_row_gap);
    gui.profile_scroll_offset =
        profile_scroll_to_show(index, gui.profiles.size(), gui.profile_scroll_offset,
                               profile_first_y, list_bottom, row_stride, row_height);
    layout_controls_for_current_size(gui);
}


void ensure_profile_buttons(GuiState& gui) {
    if (!gui.window) return;
    while (gui.preset_buttons.size() < gui.profiles.size()) {
        const std::size_t index = gui.preset_buttons.size();
        HWND button = make_control(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                   L"BUTTON", L"",
                                   gui.window, kProfileIdBase + static_cast<int>(index),
                                   0, 0, 0, 0);
        if (!button) return;
        gui.preset_buttons.push_back(button);
        SetWindowSubclass(button, profile_proc, 1, 0);
        apply_font(button, gui.normal_font);
    }
    while (gui.preset_buttons.size() > gui.profiles.size()) {
        DestroyWindow(gui.preset_buttons.back());
        gui.preset_buttons.pop_back();
    }
}

}  // namespace gamma_changer
