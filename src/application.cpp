#include "ui/gui_internal.h"

using namespace gamma_changer;

#if defined(_MSC_VER)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace gamma_changer {

GuiState* state(HWND window) {
    return reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void begin_profile_rename(GuiState& gui, std::size_t index);
void finish_profile_rename(GuiState& gui, bool commit);
void layout_controls(GuiState& gui, int width, int height);
bool cancel_active_preview(GuiState& gui);
void preview_selected(GuiState& gui);
void set_status(GuiState& gui, const std::wstring& text, StatusTone tone);
const DisplayInfo* selected_display(const GuiState& gui);
void ensure_profile_buttons(GuiState& gui);
void scroll_profile_into_view(GuiState& gui, std::size_t index);
void layout_controls_for_current_size(GuiState& gui);


void set_status(GuiState& gui, const std::wstring& text,
                StatusTone tone = StatusTone::idle) {
    gui.status_tone = tone;
    SetWindowTextW(gui.status, text.c_str());
    invalidate_status_area(gui);
    if (tone == StatusTone::error) log_message(LogLevel::error, text);
}

void mark_changed(GuiState& gui) {
    gui.session.dirty = !settings_equal(params_from_sliders(gui), gui.session.committed_settings);
    EnableWindow(gui.before_after_button, gui.session.dirty && gui.live_preview ? TRUE : FALSE);
    if (gui.session.dirty) {
        SetWindowTextW(gui.apply_button, L"Saving...");
        EnableWindow(gui.apply_button, FALSE);
        SetTimer(gui.window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
    } else {
        KillTimer(gui.window, kAutoSaveTimer);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
    }
    if (gui.session.profile_switch_undo_available) {
        set_status(gui, L"Profile switched  |  Ctrl+Z restores previous adjustments",
                   StatusTone::success);
    } else {
        set_status(gui,
                   gui.session.dirty
                       ? (gui.live_preview ? L"Previewing changes live" : L"Changes ready to apply")
                       : L"Ready",
                   gui.session.dirty && gui.live_preview ? StatusTone::success : StatusTone::idle);
    }
    invalidate_preview_curve(gui);
    if (gui.live_preview) SetTimer(gui.window, kPreviewTimer, 80, nullptr);
    else KillTimer(gui.window, kPreviewTimer);
}

void enable_modern_backdrop(HWND window) {
    constexpr DWORD kWindowCornerPreference = 33;
    constexpr DWORD kSystemBackdropType = 38;
    constexpr DWORD kBorderColor = 34;
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;
    const int rounded_corners = 2;
    const int mica_backdrop = 2;
    const COLORREF caption = ui::Theme::sidebar;
    const COLORREF caption_text = ui::Theme::sidebar_text;
    const COLORREF border = ui::Theme::sidebar_selected;
    const HRESULT corner_result = DwmSetWindowAttribute(
        window, kWindowCornerPreference, &rounded_corners, sizeof(rounded_corners));
    const HRESULT backdrop_result = DwmSetWindowAttribute(
        window, kSystemBackdropType, &mica_backdrop, sizeof(mica_backdrop));
    const HRESULT caption_result = DwmSetWindowAttribute(
        window, kCaptionColor, &caption, sizeof(caption));
    const HRESULT text_result = DwmSetWindowAttribute(
        window, kTextColor, &caption_text, sizeof(caption_text));
    const HRESULT border_result = DwmSetWindowAttribute(
        window, kBorderColor, &border, sizeof(border));
    if (FAILED(corner_result) || FAILED(backdrop_result) || FAILED(caption_result) ||
        FAILED(text_result) || FAILED(border_result)) {
        log_message(LogLevel::warning,
                    L"Some Windows 11 backdrop attributes are unavailable; using the layered fallback");
    }
}

std::wstring display_label(const DisplayInfo& display, std::size_t index) {
    std::wstring name = display.device_string;
    if (name.empty() || name.find(L"Generic") != std::wstring::npos ||
        name.find(L"PnP") != std::wstring::npos) {
        return L"Screen " + std::to_wstring(index + 1);
    }
    return name + L"  (Screen " + std::to_wstring(index + 1) + L")";
}

std::wstring display_metadata(const DisplayInfo& display) {
    std::wstring text = display.device_string.empty() ? display.device_name : display.device_string;
    if (display.width > 0 && display.height > 0) {
        text += L"\r\n" + std::to_wstring(display.width) + L" x " +
                std::to_wstring(display.height);
        if (display.refresh_rate > 1) {
            text += L"  |  " + std::to_wstring(display.refresh_rate) + L" Hz";
        }
        if (display.primary) text += L"  |  Primary";
        if (display.hdr_active) text += L"  |  HDR";
    }
    return text;
}






bool save_and_apply_current(GuiState& gui, bool automatic) {
    if (!gui.session.dirty) return true;

    // When profiles.v1 is damaged the profile collection is read-only, but the
    // per-display calibration is independent and must remain saveable.
    if (!gui.profile_store_available) {
        const auto* display = selected_display(gui);
        if (!display) {
            set_status(gui, L"No display selected", StatusTone::warning);
            return false;
        }
        const CalibrationSettings settings = params_from_sliders(gui);
        const CommitResult commit = gui.controller.commit(*display, settings);
        if (!commit.succeeded()) {
            set_status(gui, commit.error, StatusTone::error);
            SetWindowTextW(gui.apply_button, L"Retry save");
            EnableWindow(gui.apply_button, TRUE);
            return false;
        }
        gui.session.committed_settings = settings;
        gui.session.profile_switch_undo_available = false;
        gui.session.dirty = false;
        KillTimer(gui.window, kAutoSaveTimer);
        EnableWindow(gui.before_after_button, FALSE);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        set_status(gui, L"Calibration saved (profiles are read-only)",
                   StatusTone::success);
        return true;
    }

    if (gui.active_preset >= gui.profiles.size()) return false;
    const Profile previous = gui.profiles[gui.active_preset];
    const CalibrationSettings previous_settings = gui.session.committed_settings;
    auto& profile = gui.profiles[gui.active_preset];
    profile.saved = true;
    if (profile.name.empty()) {
        profile.name = L"Custom " + std::to_wstring(gui.active_preset + 1);
    }
    profile.settings = params_from_sliders(gui);
    const auto* display = selected_display(gui);
    if (!display) {
        profile = previous;
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }
    const CommitResult commit = gui.controller.commit(*display, profile.settings);
    if (!commit.succeeded()) {
        profile = previous;
        refresh_preset_buttons(gui);
        set_status(gui, commit.error, StatusTone::error);
        SetWindowTextW(gui.apply_button, L"Retry save");
        EnableWindow(gui.apply_button, TRUE);
        return false;
    }
    if (!persist_presets(gui)) {
        profile = previous;
        const CommitResult rollback = gui.controller.commit(*display, previous_settings);
        if (!rollback.succeeded()) {
            set_status(gui, L"Profile save failed and display rollback failed: " +
                            rollback.error, StatusTone::error);
        }
        refresh_preset_buttons(gui);
        SetWindowTextW(gui.apply_button, L"Retry save");
        EnableWindow(gui.apply_button, TRUE);
        return false;
    }
    gui.session.committed_settings = profile.settings;
    gui.session.profile_switch_undo_available = false;
    gui.session.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    set_status(gui, automatic ? L"Saved automatically" : L"Saved", StatusTone::success);
    return true;
}

bool preserve_pending_adjustment(GuiState& gui) {
    if (!gui.session.dirty) return true;
    const DisplayInfo* display = selected_display(gui);
    if (!display) return false;
    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kAutoSaveTimer);
    const CalibrationSettings settings = params_from_sliders(gui);
    if (!gui.profile_store_available) {
        std::wstring error;
        if (!gui.controller.save_for_offline_display(*display, settings, error)) {
            set_status(gui, L"Could not preserve pending adjustments: " + error,
                       StatusTone::error);
            return false;
        }
        gui.pending_adjustments[display_storage_id(*display)] = settings;
        gui.session.committed_settings = settings;
        gui.session.dirty = false;
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        log_message(LogLevel::info, L"Pending calibration preserved for " +
                                    display_storage_id(*display));
        return true;
    }
    if (gui.active_preset >= gui.profiles.size()) return false;
    const Profile previous = gui.profiles[gui.active_preset];
    auto& profile = gui.profiles[gui.active_preset];
    profile.saved = true;
    if (profile.name.empty()) profile.name = L"Custom " + std::to_wstring(gui.active_preset + 1);
    profile.settings = settings;
    std::wstring error;
    if (!gui.controller.save_for_offline_display(*display, settings, error)) {
        profile = previous;
        set_status(gui, L"Could not preserve pending adjustments: " + error,
                   StatusTone::error);
        return false;
    }
    if (!persist_presets(gui)) {
        profile = previous;
        std::wstring rollback_error;
        gui.controller.save_for_offline_display(*display, gui.session.committed_settings,
                                                rollback_error);
        return false;
    }
    gui.pending_adjustments[display_storage_id(*display)] = settings;
    gui.session.committed_settings = settings;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    log_message(LogLevel::info, L"Pending calibration preserved for " +
                                display_storage_id(*display));
    return true;
}

bool flush_before_exit(GuiState& gui) {
    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kAutoSaveTimer);
    KillTimer(gui.window, kDisplayRefreshTimer);
    KillTimer(gui.window, kRecoveryRetryTimer);
    normalize_all_edits(gui);
    return !gui.session.dirty || save_and_apply_current(gui, true);
}

bool confirm_close_after_save_failure(GuiState& gui) {
    for (;;) {
        if (flush_before_exit(gui)) return true;
        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Retry"},
            {1002, L"Exit without saving"},
            {IDCANCEL, L"Cancel"},
        };
        TASKDIALOGCONFIG config{sizeof(config)};
        config.hwndParent = gui.window;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Gamma Changer";
        config.pszMainInstruction = L"The latest adjustments could not be saved.";
        config.pszContent = L"Retry saving, exit without the latest changes, or return to Gamma Changer.";
        config.pszMainIcon = TD_WARNING_ICON;
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = 1001;
        int selected = IDCANCEL;
        if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr))) return false;
        if (selected == 1001) continue;
        return selected == 1002;
    }
}


int selected_display_index(const GuiState& gui) {
    const LRESULT item = SendMessageW(gui.display_combo, CB_GETCURSEL, 0, 0);
    if (item == CB_ERR) return -1;
    const LRESULT data = SendMessageW(gui.display_combo, CB_GETITEMDATA,
                                     static_cast<WPARAM>(item), 0);
    return data == CB_ERR ? static_cast<int>(item) : static_cast<int>(data);
}

void select_display_item(GuiState& gui, int display_index) {
    const LRESULT count = SendMessageW(gui.display_combo, CB_GETCOUNT, 0, 0);
    for (LRESULT item = 0; item < count; ++item) {
        const LRESULT data = SendMessageW(gui.display_combo, CB_GETITEMDATA,
                                          static_cast<WPARAM>(item), 0);
        if (static_cast<int>(data) == display_index) {
            SendMessageW(gui.display_combo, CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            return;
        }
    }
}

void load_selected_profile(GuiState& gui) {
    const int index = selected_display_index(gui);
    if (index < 0 || index >= static_cast<int>(gui.displays.size())) return;
    if (gui.active_display_index != index) {
        // Undo snapshots are display-scoped: switching displays invalidates them
        // so Ctrl+Z can never apply one display's adjustments to another.
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
    }
    gui.active_display_index = index;
    gui.session.committed_settings = gui.controller.load_settings(gui.displays[index]);
    set_params_to_controls(gui, gui.session.committed_settings);
    gui.session.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
}

const DisplayInfo* selected_display(const GuiState& gui) {
    const int index = selected_display_index(gui);
    if (index < 0 || index >= static_cast<int>(gui.displays.size())) return nullptr;
    return &gui.displays[index];
}

bool cancel_active_preview(GuiState& gui) {
    const int index = gui.active_display_index;
    if (index < 0 || index >= static_cast<int>(gui.displays.size())) return true;
    std::wstring error;
    if (!gui.controller.cancel_preview(gui.displays[index], error)) {
        set_status(gui, L"Could not restore the display before preview: " + error,
                   StatusTone::error);
        return false;
    }
    return true;
}

void preview_selected(GuiState& gui) {
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return;
    }

    std::wstring error;
    if (!gui.controller.preview(*display, params_from_sliders(gui), error)) {
        set_status(gui, error, StatusTone::error);
        return;
    }
    if (!gui.session.profile_switch_undo_available) {
        set_status(gui, L"Previewing changes live", StatusTone::success);
    }
}

void reset_selected(GuiState& gui) {
    if (gui.active_preset >= gui.profiles.size()) return;
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return;
    }

    if (!cancel_active_preview(gui)) return;
    const GammaParams previous_params = params_from_sliders(gui);
    if (settings_equal(previous_params, default_params()) &&
        settings_equal(gui.session.committed_settings, default_params())) {
        gui.session.dirty = false;
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        set_status(gui, L"Default settings are already active", StatusTone::idle);
        return;
    }

    KillTimer(gui.window, kAutoSaveTimer);
    set_params_to_controls(gui, default_params());
    std::wstring error;
    if (!gui.controller.apply_and_save(*display, default_params(), error)) {
        set_params_to_controls(gui, previous_params);
        gui.session.dirty = !settings_equal(previous_params, gui.session.committed_settings);
        SetWindowTextW(gui.apply_button, gui.session.dirty ? L"Retry save" : L"Saved");
        EnableWindow(gui.apply_button, gui.session.dirty ? TRUE : FALSE);
        set_status(gui, error, StatusTone::error);
        return;
    }

    // Restoring defaults changes only the selected display and its persisted
    // per-display settings; the selected profile definition stays untouched.
    gui.session.committed_settings = default_params();
    gui.session.dirty = false;
    gui.session.profile_switch_undo_available = true;
    gui.session.undo_display_id = display_storage_id(*display);
    gui.session.undo_preset = gui.active_preset;
    gui.session.undo_params = previous_params;
    gui.session.undo_profile = gui.profiles[gui.active_preset];
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    set_status(gui, L"Default settings restored  |  Ctrl+Z to undo",
               StatusTone::success);
}

void refresh_displays(GuiState& gui) {
    // Restore any live preview before touching saved state. If the restore write
    // fails we keep the preview active and leave dirty/committed unchanged, so the
    // UI never claims "saved" while the display still shows a preview ramp.
    if (!cancel_active_preview(gui)) {
        log_message(LogLevel::error,
                    L"Display refresh aborted because the active preview could not be restored");
        return;
    }
    if (!preserve_pending_adjustment(gui)) {
        log_message(LogLevel::warning,
                    L"Display refresh aborted because pending adjustments could not be preserved");
        return;
    }
    std::wstring previous_device;
    std::wstring previous_stable_id;
    std::wstring previous_storage_id;
    if (const auto* display = selected_display(gui)) {
        previous_device = display->device_name;
        previous_stable_id = display->stable_id;
        previous_storage_id = display_storage_id(*display);
    }

    gui.displays = DisplayManager::enumerate();
    SendMessageW(gui.display_combo, CB_RESETCONTENT, 0, 0);
    int selected = 0;
    for (int i = 0; i < static_cast<int>(gui.displays.size()); ++i) {
        const std::wstring label = display_label(gui.displays[i], static_cast<std::size_t>(i));
        const LRESULT item = SendMessageW(gui.display_combo, CB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(label.c_str()));
        if (item != CB_ERR && item != CB_ERRSPACE) {
            SendMessageW(gui.display_combo, CB_SETITEMDATA,
                         static_cast<WPARAM>(item), static_cast<LPARAM>(i));
        }
        const bool stable_match = !previous_stable_id.empty() &&
                                  gui.displays[i].stable_id == previous_stable_id;
        if (stable_match || (previous_stable_id.empty() &&
                             gui.displays[i].device_name == previous_device)) {
            selected = i;
        }
    }
    if (!gui.displays.empty()) {
        EnableWindow(gui.display_combo, TRUE);
        set_adjustment_enabled(gui, true);
        SendMessageW(gui.display_combo, CB_SETCURSEL, selected, 0);
        if (!previous_storage_id.empty() &&
            previous_storage_id != display_storage_id(gui.displays[selected])) {
            gui.session.profile_switch_undo_available = false;
            gui.session.undo_display_id.clear();
        }
        load_selected_profile(gui);
        const std::wstring selected_id = display_storage_id(gui.displays[selected]);
        const auto pending = gui.pending_adjustments.find(selected_id);
        if (pending != gui.pending_adjustments.end()) {
            std::wstring error;
            if (gui.controller.reapply_committed(gui.displays[selected], pending->second, error)) {
                gui.session.committed_settings = pending->second;
                set_params_to_controls(gui, pending->second);
                gui.pending_adjustments.erase(pending);
                log_message(LogLevel::info, L"Pending calibration applied to " + selected_id);
            } else {
                log_message(LogLevel::warning, L"Pending calibration remains queued for " +
                                               selected_id + L": " + error);
            }
        }
        const std::wstring metadata = display_metadata(gui.displays[selected]);
        SetWindowTextW(gui.display_status, metadata.c_str());
        if (gui.displays[selected].hdr_active) {
            set_status(gui, L"HDR is active; Windows or the GPU may limit Gamma adjustments",
                       StatusTone::warning);
        } else {
            set_status(gui, L"Ready", StatusTone::success);
        }
    } else {
        gui.active_display_index = -1;
        EnableWindow(gui.display_combo, FALSE);
        set_adjustment_enabled(gui, false);
        SetWindowTextW(gui.display_status, L"No display connected");
        set_status(gui, L"No attached displays found", StatusTone::warning);
    }
}

bool reapply_all_committed(GuiState& gui) {
    bool success = true;
    std::wstring first_error;
    for (const auto& display : gui.displays) {
        // Never write a neutral/default LUT to a display that the user has not
        // configured: that would overwrite any pre-existing Windows, driver, or
        // colorimeter calibration with no saved base ramp to restore it.
        if (!gui.controller.has_saved_settings(display)) {
            gui.pending_adjustments.erase(display_storage_id(display));
            log_message(LogLevel::info,
                        L"Skipping unconfigured display during calibration restore: " +
                            display.device_name);
            continue;
        }
        std::wstring error;
        const CalibrationSettings settings = gui.controller.load_settings(display);
        if (!gui.controller.reapply_committed(display, settings, error)) {
            success = false;
            if (first_error.empty()) first_error = error;
        } else {
            gui.pending_adjustments.erase(display_storage_id(display));
        }
    }
    if (!success) {
        set_status(gui, L"Some displays could not restore calibration: " + first_error,
                   StatusTone::error);
    } else {
        set_status(gui, L"Calibration restored after display change", StatusTone::success);
    }
    return success;
}

void schedule_recovery_retry(GuiState& gui) {
    gui.recovery_retry_attempt = 0;
    KillTimer(gui.window, kRecoveryRetryTimer);
    SetTimer(gui.window, kRecoveryRetryTimer, 200, nullptr);
}

void run_recovery_retry(GuiState& gui) {
    KillTimer(gui.window, kRecoveryRetryTimer);
    ++gui.recovery_retry_attempt;
    log_message(LogLevel::info, L"Display calibration recovery attempt " +
                                std::to_wstring(gui.recovery_retry_attempt) + L" of 3");
    if (reapply_all_committed(gui)) {
        gui.recovery_retry_attempt = 0;
        return;
    }
    if (gui.recovery_retry_attempt < 3) {
        const UINT delay = static_cast<UINT>(200 * gui.recovery_retry_attempt);
        SetTimer(gui.window, kRecoveryRetryTimer, delay, nullptr);
    } else {
        set_status(gui, L"The display driver did not accept calibration after 3 attempts",
                   StatusTone::error);
    }
}
}  // namespace gamma_changer
