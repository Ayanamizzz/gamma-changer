#include "gui_internal.h"

namespace gamma_changer {

LRESULT CALLBACK profile_rename_proc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(edit));
    if (message == WM_KEYDOWN && wparam == VK_RETURN && gui) {
        SendMessageW(gui->window, kProfileCommitRenameMessage, TRUE, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && gui) {
        SendMessageW(gui->window, kProfileCommitRenameMessage, FALSE, 0);
        return 0;
    }
    if (message == WM_KILLFOCUS && gui && IsWindowVisible(edit)) {
        SendMessageW(gui->window, kProfileCommitRenameMessage, TRUE, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, profile_rename_proc, 1);
    return DefSubclassProc(edit, message, wparam, lparam);
}


LRESULT CALLBACK profile_proc(HWND item, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR data) {
    auto* gui = state(GetParent(item));
    if (message == WM_LBUTTONDOWN && gui) {
        gui->profile_keyboard_focus = false;
        InvalidateRect(item, nullptr, FALSE);
    }
    if (message == WM_KEYDOWN && gui &&
        (wparam == VK_TAB || wparam == VK_UP || wparam == VK_DOWN ||
         wparam == VK_LEFT || wparam == VK_RIGHT)) {
        gui->profile_keyboard_focus = true;
        InvalidateRect(item, nullptr, FALSE);
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
    const bool can_create = gui.profile_store_available;
    const bool can_delete = gui.profile_store_available &&
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
    if (gui.renaming_preset != kNoProfile && gui.renaming_preset != index &&
        gui.renaming_preset < gui.preset_buttons.size()) {
        ShowWindow(gui.preset_buttons[gui.renaming_preset], SW_SHOW);
    }
    gui.renaming_preset = index;
    // Inline-rename architecture: the target profile button is hidden for the
    // lifetime of the rename so the edit occupies that rectangle exclusively.
    ShowWindow(gui.preset_buttons[index], SW_HIDE);
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

void finish_profile_rename(GuiState& gui, bool commit) {
    if (gui.renaming_preset == kNoProfile || gui.renaming_preset >= gui.profiles.size()) return;
    const std::size_t index = gui.renaming_preset;
    gui.renaming_preset = kNoProfile;
    ShowWindow(gui.profile_rename, SW_HIDE);
    ShowWindow(gui.preset_buttons[index], SW_SHOW);
    InvalidateRect(gui.preset_buttons[index], nullptr, TRUE);
    if (!commit) {
        SetFocus(gui.preset_buttons[index]);
        return;
    }
    wchar_t text[96]{};
    GetWindowTextW(gui.profile_rename, text, static_cast<int>(std::size(text)));
    std::wstring name = text;
    for (auto& ch : name) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    const auto first = name.find_first_not_of(L' ');
    const auto last = name.find_last_not_of(L' ');
    if (first == std::wstring::npos) {
        set_status(gui, L"Profile name cannot be empty", StatusTone::warning);
        SetFocus(gui.preset_buttons[index]);
        return;
    }
    name = name.substr(first, last - first + 1);
    if (name.size() > 48) name.resize(48);
    const std::wstring previous = gui.profiles[index].name;
    gui.profiles[index].name = name;
    if (!persist_presets(gui)) gui.profiles[index].name = previous;
    else set_status(gui, name + L" renamed", StatusTone::success);
    refresh_preset_buttons(gui);
    SetFocus(gui.preset_buttons[index]);
}

void duplicate_profile(GuiState& gui, std::size_t source_index) {
    if (source_index >= gui.profiles.size()) return;
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
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
    Profile profile;
    profile.id = next_profile_id(gui.profiles);
    profile.name = next_custom_profile_name(gui.profiles);
    profile.settings = default_params();
    profile.saved = true;
    const auto previous = gui.profiles;
    gui.profiles.push_back(profile);
    if (!persist_presets(gui)) {
        gui.profiles = previous;
        return;
    }
    // Creating a new profile must not select or apply it. The user stays on the
    // current profile; the new profile is simply appended to the list.
    refresh_preset_buttons(gui);
    layout_controls_for_current_size(gui);
    set_status(gui, profile.name + L" created with default settings", StatusTone::success);
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
    if (index == gui.active_preset) return;
    KillTimer(gui.window, kAutoSaveTimer);
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    if (!cancel_active_preview(gui)) return;
    gui.session.profile_switch_undo_available = true;
    gui.session.undo_display_id.clear();
    if (const auto* current_display = selected_display(gui)) {
        gui.session.undo_display_id = display_storage_id(*current_display);
    }
    gui.session.undo_preset = gui.active_preset;
    gui.session.undo_params = params_from_sliders(gui);
    gui.session.undo_profile = gui.profiles[gui.active_preset];
    gui.active_preset = index;
    const GammaParams next_params = gui.profiles[index].settings;
    set_params_to_controls(gui, next_params);
    const auto* display = selected_display(gui);
    std::wstring error;
    if (!display || !gui.controller.apply_and_save(*display, next_params, error)) {
        gui.active_preset = gui.session.undo_preset;
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
        set_params_to_controls(gui, gui.session.undo_params);
        refresh_preset_buttons(gui);
        set_status(gui, display ? L"Profile switch failed: " + error : L"No display selected",
                   StatusTone::error);
        return;
    }
    gui.session.committed_settings = next_params;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, gui.profiles[index].name + L" applied  |  Ctrl+Z to go back",
               StatusTone::success);
}

void undo_profile_switch(GuiState& gui) {
    if (!gui.session.profile_switch_undo_available) {
        set_status(gui, L"Nothing to restore", StatusTone::idle);
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
    if (gui.session.undo_preset >= gui.profiles.size()) {
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
        return;
    }
    const std::size_t preset = gui.session.undo_preset;
    const GammaParams params = gui.session.undo_params;
    const Profile previous_profile = gui.session.undo_profile;
    gui.session.profile_switch_undo_available = false;
    gui.session.undo_display_id.clear();
    gui.active_preset = preset;
    const Profile current_profile = gui.profiles[preset];
    gui.profiles[preset] = previous_profile;
    set_params_to_controls(gui, params);
    std::wstring error;
    if (!display || !gui.controller.apply_and_save(*display, params, error)) {
        gui.profiles[preset] = current_profile;
        set_status(gui, display ? L"Could not restore previous adjustments: " + error
                                : L"No display selected",
                   StatusTone::error);
        refresh_preset_buttons(gui);
        return;
    }
    if (!persist_presets(gui)) {
        gui.profiles[preset] = current_profile;
        if (display) {
            const CommitResult rollback = gui.controller.commit(*display, gui.session.committed_settings);
            if (!rollback.succeeded()) {
                set_status(gui, L"Could not roll back the display after profile save failed: " +
                                rollback.error, StatusTone::error);
            }
        }
        refresh_preset_buttons(gui);
        return;
    }
    gui.session.committed_settings = params;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, L"Previous profile adjustments restored", StatusTone::success);
}

void delete_preset(GuiState& gui, std::size_t index) {
    if (index >= gui.profiles.size()) return;
    if (gui.profiles[index].id == kBuiltinProfileId) {
        set_status(gui, L"The built-in Default profile cannot be deleted",
                   StatusTone::warning);
        return;
    }
    int choice = IDCANCEL;
    if (FAILED(TaskDialog(gui.window, nullptr, L"Delete this profile?",
                          L"The profile and its saved settings will be removed permanently.",
                          L"Your current display adjustments will not change.",
                          TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, TD_WARNING_ICON, &choice))) {
        log_message(LogLevel::warning, L"Could not show the delete-profile confirmation dialog");
        return;
    }
    if (choice != IDYES) return;
    if (gui.renaming_preset == index) {
        ShowWindow(gui.profile_rename, SW_HIDE);
        gui.renaming_preset = kNoProfile;
    }
    const auto previous = gui.profiles;
    const std::wstring deleted_name = gui.profiles[index].name;
    gui.profiles.erase(std::next(gui.profiles.begin(), static_cast<std::ptrdiff_t>(index)));
    if (!persist_presets(gui)) {
        gui.profiles = previous;
        refresh_preset_buttons(gui);
        return;
    }
    gui.active_preset =
        active_index_after_delete(gui.profiles.size(), index, gui.active_preset);
    if (gui.session.undo_preset >= gui.profiles.size()) gui.session.profile_switch_undo_available = false;
    scroll_profile_into_view(gui, gui.active_preset);
    refresh_preset_buttons(gui);
    layout_controls_for_current_size(gui);
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
        HWND button = make_control(WS_CHILD | BS_OWNERDRAW, L"BUTTON", L"",
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
