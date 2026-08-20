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
bool finish_profile_rename(GuiState& gui, bool commit, bool restore_focus);
void layout_controls(GuiState& gui, int width, int height);
bool cancel_active_preview(GuiState& gui);
void preview_selected(GuiState& gui);
void set_status(GuiState& gui, const std::wstring& text, StatusTone tone);
const DisplayInfo* selected_display(const GuiState& gui);
void ensure_profile_buttons(GuiState& gui);
void scroll_profile_into_view(GuiState& gui, std::size_t index);
void layout_controls_for_current_size(GuiState& gui);
bool finalize_pending_metadata(GuiState& gui, const std::wstring& display_id,
                               PendingAdjustment& adjustment, std::wstring& error);
bool detach_pending_profile_association(GuiState& gui,
                                        const std::wstring& display_id,
                                        PendingAdjustment& adjustment,
                                        std::wstring& error);


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
        SetWindowTextW(gui.apply_button, L"Auto-saving...");
        EnableWindow(gui.apply_button, FALSE);
        if (!gui.background_suspended) {
            SetTimer(gui.window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
        }
    } else {
        KillTimer(gui.window, kAutoSaveTimer);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        if (gui.profile_propagation_pending && !gui.background_suspended) {
            schedule_recovery_retry(gui);
        }
    }
    if (gui.session.profile_switch_undo_available) {
        set_status(gui, L"Profile switched  |  Ctrl+Z restores previous adjustments",
                   StatusTone::success);
    } else {
        set_status(gui,
                   gui.session.dirty
                       ? (gui.live_preview ? L"Previewing changes live  |  Auto-save in progress"
                                           : L"Changes ready to apply  |  Auto-save in progress")
                       : L"Ready",
                   gui.session.dirty && gui.live_preview ? StatusTone::success : StatusTone::idle);
    }
    invalidate_preview_curve(gui);
    if (gui.live_preview && !gui.background_suspended) {
        SetTimer(gui.window, kPreviewTimer, 80, nullptr);
    }
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

void set_display_status(GuiState& gui, const std::wstring& text) {
    SetWindowTextW(gui.display_status, text.c_str());
    // The status label paints transparently over the composited sidebar. Text
    // updates repaint only the control, so explicitly redraw the parent layer
    // underneath as well; otherwise the previous display's metadata stays
    // visible and overlaps the new one.
    invalidate_control_background(gui, gui.display_status, 0);
}

bool display_identity_unresolved(const GuiState& gui, const DisplayInfo& display) {
    return display.stable_id.empty() &&
           gui.unresolved_display_devices.find(display.device_name) !=
               gui.unresolved_display_devices.end();
}

void suspend_background_activity(GuiState& gui) {
    gui.background_suspended = true;
    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kAutoSaveTimer);
    KillTimer(gui.window, kDisplayRefreshTimer);
    KillTimer(gui.window, kRecoveryRetryTimer);
    KillTimer(gui.window, kTrayRetryTimer);
}

void resume_after_cancelled_exit(GuiState& gui) {
    gui.background_suspended = false;
    if (gui.display_refresh_timer_pending) {
        gui.display_refresh_timer_pending =
            SetTimer(gui.window, kDisplayRefreshTimer, 250, nullptr) != 0;
    }
    if (gui.recovery_retry_timer_pending) {
        gui.recovery_retry_timer_pending =
            SetTimer(gui.window, kRecoveryRetryTimer, 200, nullptr) != 0;
    }
    if (gui.tray_retry_timer_pending) {
        gui.tray_retry_timer_pending =
            SetTimer(gui.window, kTrayRetryTimer, 400, nullptr) != 0;
    }
    if (gui.session.dirty) {
        SetTimer(gui.window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
        if (gui.live_preview) SetTimer(gui.window, kPreviewTimer, 80, nullptr);
    }
}

void queue_recovery_after_failed_rollback(
    GuiState& gui, const DisplayInfo& display,
    const CalibrationSettings& desired_settings, std::size_t desired_profile,
    bool desired_profile_linked, bool desired_profile_persisted,
    bool desired_settings_persisted,
    const std::wstring& failure) {
    const std::wstring display_id = display_storage_id(display);
    PendingAdjustment adjustment;
    adjustment.settings = desired_settings;
    adjustment.settings_persisted = desired_settings_persisted;
    adjustment.profile_persisted = !desired_profile_linked || desired_profile_persisted;
    // Take durable ownership of this display before retrying. Even an unlinked
    // recovery must remove any stale association that could overwrite it on the
    // next launch.
    adjustment.preference_persisted =
        !desired_profile_linked && !gui.profile_preferences_available;
    adjustment.association_detached = adjustment.preference_persisted;
    if (desired_profile_linked && desired_profile < gui.profiles.size()) {
        adjustment.profile_id = gui.profiles[desired_profile].id;
        adjustment.profile_base_settings = gui.profiles[desired_profile].settings;
        adjustment.has_profile_base = true;
    }
    gui.pending_adjustments[display_id] = adjustment;

    std::wstring detach_error;
    PendingAdjustment& queued = gui.pending_adjustments[display_id];
    const bool detached = detach_pending_profile_association(
        gui, display_id, queued, detach_error);

    if (desired_profile < gui.profiles.size()) {
        gui.active_preset = desired_profile;
    }
    gui.active_profile_linked = false;
    gui.profile_binding_display_id.clear();
    if (desired_settings_persisted) {
        gui.session.committed_settings = desired_settings;
    }
    gui.session.dirty = false;
    gui.session.clear_undo();
    set_params_to_controls(gui, desired_settings);
    set_adjustment_enabled(gui, false);
    SetWindowTextW(gui.apply_button, L"Queued");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui,
               failure +
                   (detached ? L"; the previous state is queued for recovery"
                             : L"; recovery is waiting to detach the old Profile association: " +
                                   detach_error),
               StatusTone::error);
    schedule_recovery_retry(gui);
}






bool save_and_apply_current(GuiState& gui, bool automatic) {
    // Numeric edits may still contain an out-of-range or higher-precision value
    // while the slider already holds the clamped two-decimal representation.
    // Normalize before every commit so the text, UI state, and persisted value agree.
    normalize_all_edits(gui, false);
    if (!gui.session.dirty) return true;

    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }
    const std::wstring selected_id = display_storage_id(*display);
    if (gui.profile_binding_display_id != selected_id) {
        set_status(gui,
                   L"This display is still restoring its saved state; changes were not saved",
                   StatusTone::warning);
        SetWindowTextW(gui.apply_button, L"Waiting...");
        EnableWindow(gui.apply_button, FALSE);
        return false;
    }

    if (gui.active_profile_linked) {
        const auto preference = gui.preferred_profile_ids.find(selected_id);
        const bool durable_link =
            gui.profile_preferences_available &&
            gui.active_preset < gui.profiles.size() &&
            preference != gui.preferred_profile_ids.end() &&
            preference->second == gui.profiles[gui.active_preset].id;
        if (!durable_link) {
            // Never update a global Profile on the strength of UI state alone.
            // If its durable association is unavailable, preserve this edit as
            // display-specific calibration instead. Remove any different stale
            // association first, otherwise recovery would overwrite the new
            // display-specific values on the next launch.
            if (preference != gui.preferred_profile_ids.end()) {
                if (!gui.profile_preferences_available) {
                    set_status(gui,
                               L"The existing display/Profile association is read-only; calibration was not changed",
                               StatusTone::error);
                    SetWindowTextW(gui.apply_button, L"Retry save");
                    EnableWindow(gui.apply_button, TRUE);
                    return false;
                }
                const auto previous_preferences = gui.preferred_profile_ids;
                gui.preferred_profile_ids.erase(selected_id);
                if (!persist_profile_preferences(gui)) {
                    gui.preferred_profile_ids = previous_preferences;
                    set_status(gui,
                               L"Could not remove a stale display/Profile association; calibration was not changed",
                               StatusTone::error);
                    SetWindowTextW(gui.apply_button, L"Retry save");
                    EnableWindow(gui.apply_button, TRUE);
                    return false;
                }
            }
            gui.active_profile_linked = false;
            refresh_preset_buttons(gui);
        }
    }

    const bool detaching_builtin =
        gui.active_profile_linked && gui.active_preset < gui.profiles.size() &&
        gui.profiles[gui.active_preset].id == kBuiltinProfileId;
    const auto previous_preferences = gui.preferred_profile_ids;
    bool detached_preference_persisted = false;
    if (detaching_builtin) {
        // Default is an immutable neutral Profile. A user adjustment made while
        // it is selected becomes display-specific instead of silently changing
        // Default for every monitor.
        detached_preference_persisted =
            gui.preferred_profile_ids.erase(selected_id) != 0;
        if (detached_preference_persisted &&
            (!gui.profile_preferences_available ||
             !persist_profile_preferences(gui))) {
            gui.preferred_profile_ids = previous_preferences;
            set_status(gui, L"Could not detach the immutable Default profile",
                       StatusTone::error);
            SetWindowTextW(gui.apply_button, L"Retry save");
            EnableWindow(gui.apply_button, TRUE);
            return false;
        }
        gui.active_profile_linked = false;
        refresh_preset_buttons(gui);
    }

    // When profiles.v1 is damaged the profile collection is read-only, but the
    // per-display calibration is independent and must remain saveable. The same
    // path is used for a deliberately unlinked legacy calibration: editing it
    // must not mutate whichever Profile merely happens to have keyboard focus.
    if (!gui.profile_store_available || !gui.active_profile_linked) {
        const CalibrationSettings settings = params_from_sliders(gui);
        const CommitResult commit = gui.controller.commit(*display, settings);
        if (!commit.succeeded()) {
            if (detaching_builtin ||
                commit.status == CommitStatus::rollback_failed) {
                queue_recovery_after_failed_rollback(
                    gui, *display, settings, gui.active_preset, false, true, false,
                    L"Calibration save failed after its Profile association was detached: " +
                        commit.error);
                return false;
            }
            set_status(gui, commit.error, StatusTone::error);
            SetWindowTextW(gui.apply_button, L"Retry save");
            EnableWindow(gui.apply_button, TRUE);
            return false;
        }
        gui.session.committed_settings = settings;
        gui.session.profile_switch_undo_available = false;
        gui.session.dirty = false;
        gui.pending_adjustments.erase(display_storage_id(*display));
        KillTimer(gui.window, kAutoSaveTimer);
        EnableWindow(gui.before_after_button, FALSE);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        if (gui.profile_propagation_pending && !gui.background_suspended) {
            schedule_recovery_retry(gui);
        }
        set_status(gui,
                   detaching_builtin
                       ? L"Calibration saved for this display; Default remains unchanged"
                       : gui.profile_store_available
                       ? L"Calibration saved (not linked to a profile)"
                       : L"Calibration saved (profiles are read-only)",
                   StatusTone::success);
        return true;
    }

    if (gui.active_preset >= gui.profiles.size()) return false;
    const Profile previous = gui.profiles[gui.active_preset];
    const std::wstring profile_id = previous.id;
    // Detach first. Every later crash point is then safe: startup will use the
    // per-display calibration rather than an older global Profile snapshot.
    if (gui.preferred_profile_ids.erase(selected_id) == 0 ||
        !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = previous_preferences;
        set_status(gui,
                   L"Could not safely detach this display before updating its Profile",
                   StatusTone::error);
        SetWindowTextW(gui.apply_button, L"Retry save");
        EnableWindow(gui.apply_button, TRUE);
        return false;
    }
    auto& profile = gui.profiles[gui.active_preset];
    profile.saved = true;
    if (profile.name.empty()) {
        profile.name = L"Custom " + std::to_wstring(gui.active_preset + 1);
    }
    profile.settings = params_from_sliders(gui);
    const CalibrationSettings desired_settings = profile.settings;
    const CommitResult commit = gui.controller.commit(*display, profile.settings);
    if (!commit.succeeded()) {
        profile = previous;
        queue_recovery_after_failed_rollback(
            gui, *display, desired_settings, gui.active_preset, true, false, false,
            L"Profile update is queued after calibration commit failed: " +
                commit.error);
        return false;
    }
    if (!persist_presets(gui)) {
        profile = previous;
        queue_recovery_after_failed_rollback(
            gui, *display, desired_settings, gui.active_preset, true, false, true,
            L"Calibration was saved, but the Profile update is still queued");
        return false;
    }
    // The global Profile is now durable even if reattaching this display fails.
    // Keep propagation pending so a close immediately after this point still
    // synchronizes every other associated online display.
    gui.profile_propagation_pending = true;
    gui.preferred_profile_ids[selected_id] = profile_id;
    if (!persist_profile_preferences(gui)) {
        // The preceding detach is the last durable preference state. Keep RAM in
        // that same state and let the pending transaction reattach after retry.
        gui.preferred_profile_ids.erase(selected_id);
        queue_recovery_after_failed_rollback(
            gui, *display, desired_settings, gui.active_preset, true, true, true,
            L"Calibration and Profile were saved, but their association is queued");
        return false;
    }
    gui.session.committed_settings = profile.settings;
    gui.pending_adjustments.erase(display_storage_id(*display));
    gui.session.profile_switch_undo_available = false;
    gui.session.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    set_status(gui, automatic ? L"Saved automatically" : L"Saved", StatusTone::success);
    // Profiles are global templates. Queue a bounded recovery pass so every
    // currently connected display associated with this Profile receives the
    // same settings and updates its per-display durable copy.
    gui.profile_propagation_pending = true;
    schedule_recovery_retry(gui);
    return true;
}

bool preserve_pending_adjustment(GuiState& gui) {
    if (!gui.session.dirty) return true;
    const DisplayInfo* display = selected_display(gui);
    if (!display) return false;
    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kAutoSaveTimer);
    const CalibrationSettings settings = params_from_sliders(gui);
    const std::wstring storage_id = display_storage_id(*display);
    const bool builtin_selected =
        gui.active_profile_linked && gui.active_preset < gui.profiles.size() &&
        gui.profiles[gui.active_preset].id == kBuiltinProfileId;
    if (builtin_selected) {
        const auto previous_preferences = gui.preferred_profile_ids;
        const bool association_removed =
            gui.preferred_profile_ids.erase(storage_id) != 0;
        if (association_removed &&
            (!gui.profile_preferences_available ||
             !persist_profile_preferences(gui))) {
            gui.preferred_profile_ids = previous_preferences;
            set_status(gui, L"Could not detach the immutable Default profile",
                       StatusTone::error);
            return false;
        }
        gui.active_profile_linked = false;
        refresh_preset_buttons(gui);
    }
    std::wstring profile_id;
    if (gui.active_profile_linked && gui.active_preset < gui.profiles.size()) {
        profile_id = gui.profiles[gui.active_preset].id;
    }
    // Preserve the edit in memory before touching disk. A transient file lock or
    // an unplugged monitor must not make the refresh path overwrite the only copy
    // of the user's latest values.
    PendingAdjustment pending;
    pending.settings = settings;
    pending.profile_id = profile_id;
    pending.profile_persisted =
        !gui.profile_store_available || !gui.active_profile_linked;
    const bool durable_association_absent =
        !gui.profile_preferences_available ||
        gui.preferred_profile_ids.find(storage_id) ==
            gui.preferred_profile_ids.end();
    pending.preference_persisted =
        profile_id.empty() && durable_association_absent;
    pending.association_detached = pending.preference_persisted;
    if (!profile_id.empty() && gui.active_preset < gui.profiles.size()) {
        pending.profile_base_settings = gui.profiles[gui.active_preset].settings;
        pending.has_profile_base = true;
    }
    gui.pending_adjustments[storage_id] = pending;
    if (gui.active_display_id == storage_id) {
        gui.profile_binding_display_id.clear();
        gui.active_profile_linked = false;
        set_adjustment_enabled(gui, false);
        refresh_preset_buttons(gui);
    }
    // Ownership of this edit has moved to the display-keyed pending queue. Do
    // not leave the session dirty: after topology selection changes it would be
    // interpreted as an edit for the newly selected display during shutdown.
    gui.session.dirty = false;
    EnableWindow(gui.before_after_button, FALSE);
    SetWindowTextW(gui.apply_button, L"Queued");
    EnableWindow(gui.apply_button, FALSE);
    if (!gui.pending_adjustments[storage_id].association_detached) {
        std::wstring detach_error;
        if (!detach_pending_profile_association(
                gui, storage_id, gui.pending_adjustments[storage_id],
                detach_error)) {
            set_status(gui,
                       L"Could not safely queue the Profile update: " + detach_error,
                       StatusTone::error);
            return false;
        }
    }
    if (!gui.profile_store_available || profile_id.empty()) {
        std::wstring error;
        if (!gui.controller.save_for_offline_display(*display, settings, error)) {
            set_status(gui, L"Could not preserve pending adjustments: " + error,
                       StatusTone::error);
            return false;
        }
        gui.pending_adjustments[storage_id].settings_persisted = true;
        gui.session.committed_settings = settings;
        gui.session.dirty = false;
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        log_message(LogLevel::info, L"Pending calibration preserved for " +
                                    storage_id);
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
    gui.pending_adjustments[storage_id].settings_persisted = true;
    if (!persist_presets(gui)) {
        profile = previous;
        // Keep the already-durable per-display settings and queue the remaining
        // Profile/preference metadata. Rolling params back here would make the
        // settings_persisted flag lie and could lose the edit during shutdown.
        return false;
    }
    gui.pending_adjustments[storage_id].profile_persisted = true;
    gui.profile_propagation_pending = true;
    gui.session.committed_settings = settings;
    gui.session.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    log_message(LogLevel::info, L"Pending calibration preserved for " +
                                storage_id);
    return true;
}

bool flush_pending_adjustments(GuiState& gui) {
    for (auto pending = gui.pending_adjustments.begin();
         pending != gui.pending_adjustments.end();) {
        const std::wstring display_id = pending->first;
        PendingAdjustment& adjustment = pending->second;
        std::wstring error;
        if (!detach_pending_profile_association(gui, display_id, adjustment,
                                                error)) {
            set_status(gui,
                       L"Could not safely save queued display adjustments: " + error,
                       StatusTone::error);
            return false;
        }
        const auto online_display = std::find_if(
            gui.displays.begin(), gui.displays.end(),
            [&](const DisplayInfo& display) {
                return display_storage_id(display) == display_id &&
                       !display_identity_unresolved(gui, display);
            });
        if (online_display != gui.displays.end()) {
            // An online target must reach hardware consistency before exit. A
            // params-only save would claim success while leaving its actual Ramp
            // stale until the next launch.
            if (!gui.controller.apply_and_save(
                    *online_display, adjustment.settings, error)) {
                set_status(gui,
                           L"Could not apply queued adjustments before exit: " + error,
                           StatusTone::error);
                return false;
            }
            adjustment.settings_persisted = true;
        } else if (!adjustment.settings_persisted) {
            DisplayInfo offline_display;
            offline_display.device_name = display_id;
            if (display_id.rfind(L"\\\\.\\DISPLAY", 0) != 0) {
                offline_display.stable_id = display_id;
            }
            if (!gui.controller.save_for_offline_display(
                    offline_display, adjustment.settings, error)) {
                set_status(gui,
                           L"Could not save queued adjustments for an offline display: " +
                               error,
                           StatusTone::error);
                return false;
            }
            adjustment.settings_persisted = true;
        }
        if (!finalize_pending_metadata(gui, display_id, adjustment, error)) {
            set_status(gui,
                       L"Could not finish saving queued display adjustments: " + error,
                       StatusTone::error);
            return false;
        }
        if (gui.active_display_id == display_id) {
            gui.session.committed_settings = adjustment.settings;
            gui.session.dirty = false;
            gui.profile_binding_display_id = display_id;
            const auto preference = gui.preferred_profile_ids.find(display_id);
            gui.active_profile_linked = false;
            if (!adjustment.profile_id.empty() &&
                preference != gui.preferred_profile_ids.end() &&
                preference->second == adjustment.profile_id) {
                const auto profile = std::find_if(
                    gui.profiles.begin(), gui.profiles.end(),
                    [&](const Profile& item) {
                        return item.id == adjustment.profile_id &&
                               settings_equal(item.settings, adjustment.settings);
                    });
                if (profile != gui.profiles.end()) {
                    gui.active_preset = static_cast<std::size_t>(
                        std::distance(gui.profiles.begin(), profile));
                    gui.active_profile_linked = true;
                }
            }
            set_adjustment_enabled(gui, true);
            refresh_preset_buttons(gui);
        }
        pending = gui.pending_adjustments.erase(pending);
    }
    return true;
}

bool flush_before_exit(GuiState& gui) {
    suspend_background_activity(gui);
    normalize_all_edits(gui, false);
    const bool current_saved_initially =
        !gui.session.dirty || save_and_apply_current(gui, true);
    // Settle a live Ramp before flushing display-keyed pending entries. This
    // avoids one display's preview blocking recovery for another display.
    const bool preview_settled = cancel_active_preview(gui);
    const bool pending_saved = preview_settled && flush_pending_adjustments(gui);
    const bool active_pending =
        !gui.active_display_id.empty() &&
        gui.pending_adjustments.find(gui.active_display_id) !=
            gui.pending_adjustments.end();
    const bool current_saved =
        current_saved_initially ||
        (pending_saved && !gui.session.dirty && !active_pending);
    bool profiles_propagated = true;
    if (current_saved && preview_settled && pending_saved &&
        gui.profile_propagation_pending && !gui.displays.empty()) {
        profiles_propagated = reapply_all_committed(gui);
    }
    return current_saved && preview_settled && pending_saved &&
           profiles_propagated;
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
        if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr))) {
            resume_after_cancelled_exit(gui);
            return false;
        }
        if (selected == 1001) continue;
        if (selected == 1002) {
            if (const auto* display = selected_display(gui)) {
                std::wstring restore_error;
                if (gui.controller.reapply_committed(
                        *display, gui.session.committed_settings,
                        restore_error)) {
                    gui.controller.abandon_preview_for_offline_display(*display);
                } else {
                    log_message(LogLevel::error,
                                L"Exit without saving could not restore the committed display "
                                L"state: " + restore_error);
                }
            }
            return true;
        }
        resume_after_cancelled_exit(gui);
        return false;
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

bool persist_profile_preferences(GuiState& gui) {
    if (!gui.profile_preferences_available) {
        log_message(LogLevel::warning,
                    L"Display profile preferences are read-only because the file is damaged");
        return false;
    }
    std::vector<DisplayProfilePreference> preferences;
    preferences.reserve(gui.preferred_profile_ids.size());
    for (const auto& [display_id, profile_id] : gui.preferred_profile_ids) {
        preferences.push_back({display_id, profile_id});
    }
    std::wstring error;
    if (!gui.store.save_profile_preferences(preferences, error)) {
        log_message(LogLevel::warning,
                    L"Could not save display profile preferences: " + error);
        return false;
    }
    return true;
}

bool remember_active_profile_for_display(GuiState& gui, const DisplayInfo& display) {
    if (gui.active_preset >= gui.profiles.size()) return false;
    if (!gui.profile_preferences_available) {
        set_status(gui,
                   L"Profile associations are read-only until the damaged preference file is repaired",
                   StatusTone::error);
        return false;
    }
    const std::wstring storage_id = display_storage_id(display);
    const std::wstring& profile_id = gui.profiles[gui.active_preset].id;
    const auto existing = gui.preferred_profile_ids.find(storage_id);
    const bool had_existing = existing != gui.preferred_profile_ids.end();
    const std::wstring previous_id = had_existing ? existing->second : std::wstring{};
    gui.preferred_profile_ids[storage_id] = profile_id;
    if (!persist_profile_preferences(gui)) {
        if (had_existing) gui.preferred_profile_ids[storage_id] = previous_id;
        else gui.preferred_profile_ids.erase(storage_id);
        return false;
    }
    return true;
}

bool detach_pending_profile_association(GuiState& gui,
                                        const std::wstring& display_id,
                                        PendingAdjustment& adjustment,
                                        std::wstring& error) {
    if ((adjustment.profile_persisted && adjustment.preference_persisted) ||
        adjustment.association_detached) {
        return true;
    }
    if (!gui.profile_preferences_available) {
        error = L"display/Profile associations are read-only";
        return false;
    }
    const auto previous_preferences = gui.preferred_profile_ids;
    const auto preference = gui.preferred_profile_ids.find(display_id);
    const bool changed = preference != gui.preferred_profile_ids.end();
    if (changed) {
        // Pending state owns this display identity. A different association is
        // just as unsafe as the expected one because startup would otherwise
        // overwrite the queued calibration with that Profile.
        gui.preferred_profile_ids.erase(preference);
    }
    if (changed && !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = previous_preferences;
        error = L"the previous display/Profile association could not be detached";
        return false;
    }
    adjustment.association_detached = true;
    return true;
}

bool finalize_pending_metadata(GuiState& gui, const std::wstring& display_id,
                               PendingAdjustment& adjustment, std::wstring& error) {
    if (!detach_pending_profile_association(gui, display_id, adjustment, error)) {
        return false;
    }
    if (adjustment.profile_id.empty()) {
        const auto preference = gui.preferred_profile_ids.find(display_id);
        if (preference != gui.preferred_profile_ids.end() &&
            gui.profile_preferences_available) {
            const auto previous_preferences = gui.preferred_profile_ids;
            gui.preferred_profile_ids.erase(preference);
            if (!persist_profile_preferences(gui)) {
                gui.preferred_profile_ids = previous_preferences;
                error = L"the obsolete display/profile association could not be removed";
                return false;
            }
        }
        adjustment.preference_persisted = true;
        return true;
    }
    const auto profile = std::find_if(
        gui.profiles.begin(), gui.profiles.end(),
        [&](const Profile& item) { return item.id == adjustment.profile_id; });
    if (profile == gui.profiles.end()) {
        log_message(LogLevel::warning,
                    L"Pending calibration references a profile that no longer exists: " +
                        adjustment.profile_id);
        const auto previous_preferences = gui.preferred_profile_ids;
        const auto preference = gui.preferred_profile_ids.find(display_id);
        if (preference != gui.preferred_profile_ids.end() &&
            preference->second == adjustment.profile_id) {
            gui.preferred_profile_ids.erase(preference);
            if (gui.profile_preferences_available &&
                !persist_profile_preferences(gui)) {
                gui.preferred_profile_ids = previous_preferences;
                error = L"the deleted Profile association could not be removed";
                return false;
            }
        }
        adjustment.profile_id.clear();
        adjustment.profile_persisted = true;
        adjustment.preference_persisted = true;
        return true;
    }

    if (adjustment.profile_persisted &&
        !settings_equal(profile->settings, adjustment.settings)) {
        log_message(LogLevel::warning,
                    L"A queued display adjustment was detached because its Profile changed "
                    L"after the adjustment was queued: " + adjustment.profile_id);
        adjustment.profile_id.clear();
        adjustment.preference_persisted = true;
        return true;
    }

    if (!adjustment.profile_persisted) {
        const std::size_t profile_index = static_cast<std::size_t>(
            std::distance(gui.profiles.begin(), profile));
        if (adjustment.has_profile_base &&
            !settings_equal(gui.profiles[profile_index].settings,
                            adjustment.profile_base_settings)) {
            log_message(LogLevel::warning,
                        L"A queued display adjustment was detached because its Profile was "
                        L"modified elsewhere: " + adjustment.profile_id);
            adjustment.profile_id.clear();
            adjustment.profile_persisted = true;
            adjustment.preference_persisted = true;
            return true;
        }
        const Profile previous_profile = gui.profiles[profile_index];
        gui.profiles[profile_index].settings = adjustment.settings;
        gui.profiles[profile_index].saved = true;
        if (gui.profiles[profile_index].name.empty()) {
            gui.profiles[profile_index].name =
                L"Custom " + std::to_wstring(profile_index + 1);
        }
        if (!persist_presets(gui)) {
            gui.profiles[profile_index] = previous_profile;
            refresh_preset_buttons(gui);
            error = L"the profile update could not be saved";
            return false;
        }
        adjustment.profile_persisted = true;
        gui.profile_propagation_pending = true;
    }

    if (adjustment.preference_persisted ||
        !gui.profile_preferences_available) {
        adjustment.preference_persisted = true;
        return true;
    }
    const auto previous_preferences = gui.preferred_profile_ids;
    gui.preferred_profile_ids[display_id] = adjustment.profile_id;
    if (!persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = previous_preferences;
        error = L"the display/profile association could not be saved";
        return false;
    }
    adjustment.preference_persisted = true;
    adjustment.association_detached = false;
    return true;
}

bool migrate_display_identity(GuiState& gui, const DisplayInfo& previous,
                              const DisplayInfo& current) {
    if (!is_display_identity_upgrade(previous, current)) return true;
    const std::wstring old_id = display_storage_id(previous);
    const std::wstring new_id = display_storage_id(current);
    std::wstring migration_error;
    if (!gui.controller.migrate_display_identity(previous, current, migration_error)) {
        set_status(gui, L"Could not migrate saved calibration to the stable display identity: " +
                            migration_error,
                   StatusTone::error);
        return false;
    }
    const auto previous_pending = gui.pending_adjustments;
    const auto previous_preferences = gui.preferred_profile_ids;

    const auto pending = gui.pending_adjustments.find(old_id);
    if (pending != gui.pending_adjustments.end()) {
        if (gui.pending_adjustments.find(new_id) == gui.pending_adjustments.end()) {
            gui.pending_adjustments[new_id] = pending->second;
        }
        gui.pending_adjustments.erase(old_id);
    }

    const auto preference = gui.preferred_profile_ids.find(old_id);
    bool preference_changed = false;
    if (preference != gui.preferred_profile_ids.end()) {
        if (gui.preferred_profile_ids.find(new_id) == gui.preferred_profile_ids.end()) {
            gui.preferred_profile_ids[new_id] = preference->second;
        }
        gui.preferred_profile_ids.erase(old_id);
        preference_changed = true;
    }
    if (preference_changed && gui.profile_preferences_available &&
        !persist_profile_preferences(gui)) {
        gui.pending_adjustments = previous_pending;
        gui.preferred_profile_ids = previous_preferences;
        set_status(gui, L"Could not migrate the display identity; retrying...",
                   StatusTone::error);
        return false;
    }

    if (gui.active_display_id == old_id) gui.active_display_id = new_id;
    if (gui.profile_binding_display_id == old_id) {
        gui.profile_binding_display_id = new_id;
    }
    if (gui.session.undo_display_id == old_id) gui.session.undo_display_id = new_id;
    log_message(LogLevel::info,
                L"Display identity upgraded from " + old_id + L" to " + new_id);
    return true;
}

bool restore_unlinked_calibration(GuiState& gui, const DisplayInfo& display,
                                  std::wstring& error) {
    gui.active_profile_linked = false;
    refresh_preset_buttons(gui);
    if (!gui.controller.has_saved_settings(display)) return true;
    return gui.controller.reapply_committed(display, gui.session.committed_settings, error);
}

bool apply_preferred_profile_to_display(GuiState& gui, const DisplayInfo& display) {
    if (gui.profiles.empty()) return true;
    const std::wstring storage_id = display_storage_id(display);

    // A damaged/newer preference file is deliberately read-only. Restore the
    // display-specific calibration, but never guess which global Profile owns it.
    if (!gui.profile_preferences_available) {
        std::wstring error;
        if (!restore_unlinked_calibration(gui, display, error)) {
            set_status(gui, L"Could not restore the unlinked display calibration: " + error,
                       StatusTone::error);
            return false;
        }
        return true;
    }

    const auto preference = gui.preferred_profile_ids.find(storage_id);
    std::size_t profile_index = kNoProfile;
    if (preference != gui.preferred_profile_ids.end()) {
        for (std::size_t index = 0; index < gui.profiles.size(); ++index) {
            if (gui.profiles[index].id == preference->second) {
                profile_index = index;
                break;
            }
        }
    }

    if (preference == gui.preferred_profile_ids.end() || profile_index == kNoProfile) {
        // Legacy versions stored per-display parameters without recording a
        // Profile association. Numerical equality is not Profile identity (many
        // Profiles may intentionally have the same values), so never guess a
        // binding. Restore the display-specific settings as explicitly unlinked.
        std::wstring restore_error;
        if (!restore_unlinked_calibration(gui, display, restore_error)) {
            set_status(gui, L"Could not restore the saved display calibration: " +
                                restore_error,
                       StatusTone::error);
            return false;
        }

        const auto previous_preferences = gui.preferred_profile_ids;
        const bool invalid_preference = preference != gui.preferred_profile_ids.end();
        if (invalid_preference) gui.preferred_profile_ids.erase(storage_id);
        if (invalid_preference && !persist_profile_preferences(gui)) {
            gui.preferred_profile_ids = previous_preferences;
            log_message(LogLevel::warning,
                        L"The display calibration was restored, but its invalid Profile "
                        L"association could not be removed");
        }
        return true;
    }

    const std::size_t previous_preset = gui.active_preset;
    const bool previous_linked = gui.active_profile_linked;
    const GammaParams previous_params = params_from_sliders(gui);
    const CalibrationSettings previous_committed = gui.session.committed_settings;

    gui.active_preset = profile_index;
    gui.active_profile_linked = false;
    const GammaParams profile_params = gui.profiles[profile_index].settings;
    set_params_to_controls(gui, profile_params);
    const CommitResult apply = gui.controller.commit(display, profile_params);
    if (!apply.succeeded()) {
        if (apply.status == CommitStatus::rollback_failed) {
            queue_recovery_after_failed_rollback(
                gui, display, profile_params, profile_index, true, true, false,
                L"Preferred Profile application failed and its display rollback failed: " +
                    apply.error);
            return false;
        }
        gui.active_preset = previous_preset;
        gui.active_profile_linked = previous_linked;
        set_params_to_controls(gui, previous_params);
        gui.session.committed_settings = previous_committed;
        refresh_preset_buttons(gui);
        set_status(gui, L"Could not apply the preferred profile: " + apply.error,
                   StatusTone::error);
        return false;
    }

    gui.active_profile_linked = true;
    gui.session.committed_settings = profile_params;
    gui.session.dirty = false;
    gui.session.profile_switch_undo_available = false;
    gui.session.undo_display_id.clear();
    KillTimer(gui.window, kAutoSaveTimer);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    return true;
}

bool load_selected_profile(GuiState& gui) {
    const int index = selected_display_index(gui);
    if (index < 0 || index >= static_cast<int>(gui.displays.size())) return false;
    if (display_identity_unresolved(gui, gui.displays[index])) {
        gui.active_display_index = index;
        gui.active_display_id.clear();
        gui.profile_binding_display_id.clear();
        gui.active_profile_linked = false;
        set_adjustment_enabled(gui, false);
        refresh_preset_buttons(gui);
        set_status(gui, L"Windows is still identifying this display; retrying...",
                   StatusTone::warning);
        return false;
    }
    const std::wstring selected_id = display_storage_id(gui.displays[index]);
    const bool selection_changed = gui.active_display_id != selected_id;
    const bool profile_binding_changed =
        gui.profile_binding_display_id != selected_id;
    if (profile_binding_changed) {
        gui.active_profile_linked = false;
        set_adjustment_enabled(gui, false);
        refresh_preset_buttons(gui);
    }
    if (selection_changed) {
        // Undo snapshots are display-scoped: switching displays invalidates them
        // so Ctrl+Z can never apply one display's adjustments to another.
        gui.session.profile_switch_undo_available = false;
        gui.session.undo_display_id.clear();
    }
    gui.active_display_index = index;
    gui.active_display_id = selected_id;
    gui.session.committed_settings = gui.controller.load_settings(gui.displays[index]);
    set_params_to_controls(gui, gui.session.committed_settings);
    gui.session.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);

    const auto pending = gui.pending_adjustments.find(selected_id);
    if (pending != gui.pending_adjustments.end()) {
        std::wstring error;
        if (!detach_pending_profile_association(gui, selected_id,
                                                pending->second, error)) {
            gui.profile_binding_display_id.clear();
            gui.active_profile_linked = false;
            set_adjustment_enabled(gui, false);
            refresh_preset_buttons(gui);
            set_status(gui,
                       L"Pending adjustments are waiting for a safe Profile detach: " +
                           error,
                       StatusTone::error);
            return false;
        }
        const PendingAdjustment adjustment = pending->second;
        const auto profile = std::find_if(
            gui.profiles.begin(), gui.profiles.end(),
            [&](const Profile& item) { return item.id == adjustment.profile_id; });
        if (profile != gui.profiles.end()) {
            gui.active_preset = static_cast<std::size_t>(
                std::distance(gui.profiles.begin(), profile));
        }
        set_params_to_controls(gui, adjustment.settings);
        if (!gui.controller.apply_and_save(gui.displays[index], adjustment.settings, error)) {
            // The display-keyed pending entry already owns these values. Marking
            // the session dirty would make the next refresh overwrite that rich
            // pending metadata with an unlinked replacement.
            gui.session.dirty = false;
            gui.profile_binding_display_id.clear();
            gui.active_profile_linked = false;
            set_adjustment_enabled(gui, false);
            SetWindowTextW(gui.apply_button, L"Queued");
            EnableWindow(gui.apply_button, FALSE);
            refresh_preset_buttons(gui);
            set_status(gui, L"Pending display adjustments are still queued: " + error,
                       StatusTone::error);
            return false;
        }
        gui.session.committed_settings = adjustment.settings;
        gui.session.dirty = false;
        pending->second.settings_persisted = true;
        if (!finalize_pending_metadata(gui, selected_id, pending->second, error)) {
            gui.profile_binding_display_id.clear();
            gui.active_profile_linked = false;
            set_adjustment_enabled(gui, false);
            set_status(gui, L"Calibration restored, but " + error + L"; it remains queued",
                       StatusTone::warning);
            return false;
        }
        const auto linked_profile = std::find_if(
            gui.profiles.begin(), gui.profiles.end(),
            [&](const Profile& item) {
                return !pending->second.profile_id.empty() &&
                       item.id == pending->second.profile_id;
            });
        const auto saved_preference = gui.preferred_profile_ids.find(selected_id);
        gui.active_profile_linked =
            linked_profile != gui.profiles.end() &&
            gui.profile_preferences_available &&
            saved_preference != gui.preferred_profile_ids.end() &&
            saved_preference->second == pending->second.profile_id;
        if (gui.active_profile_linked) {
            gui.active_preset = static_cast<std::size_t>(
                std::distance(gui.profiles.begin(), linked_profile));
        }
        gui.pending_adjustments.erase(pending);
        gui.profile_binding_display_id = selected_id;
        set_adjustment_enabled(gui, true);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        refresh_preset_buttons(gui);
        log_message(LogLevel::info, L"Pending calibration applied to " + selected_id);
        return true;
    }

    // Apply the remembered profile only after a successful binding for this
    // physical display. A transient driver failure leaves the binding identity
    // unchanged so the next refresh retries instead of silently accepting it.
    if (profile_binding_changed &&
        !apply_preferred_profile_to_display(gui, gui.displays[index])) {
        return false;
    }
    gui.profile_binding_display_id = selected_id;
    set_adjustment_enabled(gui, true);
    return true;
}

const DisplayInfo* selected_display(const GuiState& gui) {
    const int index = selected_display_index(gui);
    if (index < 0 || index >= static_cast<int>(gui.displays.size())) return nullptr;
    return &gui.displays[index];
}

bool selected_display_ready(const GuiState& gui) {
    const DisplayInfo* display = selected_display(gui);
    if (!display || display_identity_unresolved(gui, *display)) return false;
    const std::wstring storage_id = display_storage_id(*display);
    return gui.active_display_id == storage_id &&
           gui.profile_binding_display_id == storage_id;
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
    if (!selected_display_ready(gui)) {
        set_status(gui, L"This display is still being identified or restored",
                   StatusTone::warning);
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
    if (!selected_display_ready(gui)) {
        set_status(gui, L"This display is still being identified or restored",
                   StatusTone::warning);
        return;
    }

    const auto builtin = std::find_if(
        gui.profiles.begin(), gui.profiles.end(),
        [](const Profile& profile) { return profile.id == kBuiltinProfileId; });
    if (builtin == gui.profiles.end()) {
        set_status(gui, L"The built-in Default profile is unavailable", StatusTone::error);
        return;
    }
    // Make the undo snapshot durable before reset. Otherwise Ctrl+Z could restore
    // the edited Ramp/params while writing an older Profile snapshot to disk.
    if (gui.session.dirty) {
        KillTimer(gui.window, kAutoSaveTimer);
        if (!save_and_apply_current(gui, true)) return;
    }
    const std::size_t builtin_index = static_cast<std::size_t>(
        std::distance(gui.profiles.begin(), builtin));
    const std::size_t previous_preset = gui.active_preset;
    const bool previous_linked = gui.active_profile_linked;
    const auto previous_preferences = gui.preferred_profile_ids;

    if (!cancel_active_preview(gui)) return;
    const GammaParams previous_params = params_from_sliders(gui);
    const std::wstring storage_id = display_storage_id(*display);
    const bool association_removed =
        gui.profile_preferences_available &&
        gui.preferred_profile_ids.erase(storage_id) != 0;
    if (association_removed && !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids = previous_preferences;
        set_status(gui,
                   L"Could not safely detach the current Profile before restoring defaults",
                   StatusTone::error);
        return;
    }

    KillTimer(gui.window, kAutoSaveTimer);
    set_params_to_controls(gui, default_params());
    const CommitResult apply = gui.controller.commit(*display, default_params());
    if (!apply.succeeded()) {
        if (apply.status == CommitStatus::rollback_failed) {
            queue_recovery_after_failed_rollback(
                gui, *display, default_params(), builtin_index,
                gui.profile_preferences_available, true, false,
                L"Restoring defaults failed and the display rollback failed: " +
                    apply.error);
            return;
        }
        bool association_restored = true;
        if (association_removed) {
            gui.preferred_profile_ids = previous_preferences;
            association_restored = persist_profile_preferences(gui);
        }
        if (!association_restored) {
            gui.preferred_profile_ids.erase(storage_id);
            queue_recovery_after_failed_rollback(
                gui, *display, default_params(), builtin_index, true, true, false,
                L"Restoring defaults failed and the previous Profile association could not be restored: " +
                    apply.error);
            return;
        }
        set_params_to_controls(gui, previous_params);
        gui.session.dirty = !settings_equal(previous_params, gui.session.committed_settings);
        SetWindowTextW(gui.apply_button, gui.session.dirty ? L"Retry save" : L"Saved");
        EnableWindow(gui.apply_button, gui.session.dirty ? TRUE : FALSE);
        set_status(gui, apply.error, StatusTone::error);
        return;
    }

    if (gui.profile_preferences_available) {
        gui.preferred_profile_ids[storage_id] = kBuiltinProfileId;
    }
    if (gui.profile_preferences_available && !persist_profile_preferences(gui)) {
        gui.preferred_profile_ids.erase(storage_id);
        queue_recovery_after_failed_rollback(
            gui, *display, default_params(), builtin_index, true, true, true,
            L"Defaults were restored, but the Default Profile association is queued");
        return;
    }

    // Restoring defaults binds this display to the built-in neutral profile so
    // switching away and back cannot silently reapply the previous profile.
    gui.active_preset = builtin_index;
    gui.active_profile_linked = gui.profile_preferences_available;
    gui.profile_binding_display_id = storage_id;
    gui.session.committed_settings = default_params();
    gui.session.dirty = false;
    gui.session.profile_switch_undo_available = true;
    gui.session.undo_display_id = storage_id;
    gui.session.undo_preset = previous_preset;
    gui.session.undo_params = previous_params;
    gui.session.undo_profile = gui.profiles[previous_preset];
    gui.session.undo_profile_linked = previous_linked;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui,
               gui.active_profile_linked
                   ? L"Default settings restored  |  Ctrl+Z to undo"
                   : L"Default settings restored; Profile associations are read-only  |  Ctrl+Z to undo",
               StatusTone::success);
}

bool refresh_displays(GuiState& gui) {
    std::wstring previous_storage_id;
    DisplayInfo previous_display;
    bool had_previous_display = false;
    if (const auto* display = selected_display(gui)) {
        previous_display = *display;
        had_previous_display = true;
        previous_storage_id = display_storage_id(*display);
    }

    for (const auto& display : gui.displays) {
        if (!display.stable_id.empty()) {
            gui.last_stable_id_by_device[display.device_name] = display.stable_id;
        }
    }

    // Enumerate first. An unplugged preview target cannot accept the restore write;
    // knowing the new topology lets us preserve its values and safely abandon only
    // the controller's stale preview bookkeeping.
    std::vector<DisplayInfo> refreshed_displays = DisplayManager::enumerate();
    for (const auto& display : refreshed_displays) {
        if (!display.stable_id.empty()) {
            gui.last_stable_id_by_device[display.device_name] = display.stable_id;
        }
    }
    gui.unresolved_display_devices.clear();
    for (const auto& display : refreshed_displays) {
        if (display.stable_id.empty() &&
            gui.last_stable_id_by_device.find(display.device_name) !=
                gui.last_stable_id_by_device.end()) {
            gui.unresolved_display_devices.insert(display.device_name);
            log_message(LogLevel::warning,
                        L"Stable display identity is temporarily unavailable for " +
                            display.device_name + L"; calibration writes are paused");
        }
    }
    const auto previous_online = std::find_if(
        refreshed_displays.begin(), refreshed_displays.end(),
        [&](const DisplayInfo& display) {
            return had_previous_display &&
                   matches_previous_display(previous_display, display);
        });
    bool preservation_ok = true;
    const bool identity_upgrade =
        had_previous_display && previous_online != refreshed_displays.end() &&
        is_display_identity_upgrade(previous_display, *previous_online);
    if (had_previous_display && previous_online == refreshed_displays.end()) {
        preservation_ok = preserve_pending_adjustment(gui);
        gui.controller.abandon_preview_for_offline_display(previous_display);
        if (!preservation_ok) {
            log_message(LogLevel::warning,
                        L"Offline adjustments remain queued in memory because they could not be saved");
        }
    } else {
        if (had_previous_display) {
            std::wstring preview_error;
            const DisplayInfo& preview_target =
                identity_upgrade ? previous_display : *previous_online;
            if (!gui.controller.cancel_preview(preview_target, preview_error)) {
                set_status(gui, L"Could not restore the display before refresh: " +
                                    preview_error,
                           StatusTone::error);
                return false;
            }
        }
        preservation_ok = preserve_pending_adjustment(gui);
        if (!preservation_ok) {
            log_message(LogLevel::warning,
                        L"Pending adjustments remain queued in memory because they could not be saved");
        }
        if (identity_upgrade &&
            !migrate_display_identity(gui, previous_display, *previous_online)) {
            return false;
        }
        if (identity_upgrade) {
            previous_storage_id = display_storage_id(*previous_online);
        }
    }

    gui.displays = std::move(refreshed_displays);
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
        if (had_previous_display &&
            matches_previous_display(previous_display, gui.displays[i])) {
            selected = i;
        }
    }
    if (!gui.displays.empty()) {
        EnableWindow(gui.display_combo, TRUE);
        SendMessageW(gui.display_combo, CB_SETCURSEL, selected, 0);
        const std::wstring metadata = display_metadata(gui.displays[selected]);
        set_display_status(gui, metadata);
        if (display_identity_unresolved(gui, gui.displays[selected])) {
            gui.active_display_index = selected;
            gui.active_display_id.clear();
            gui.profile_binding_display_id.clear();
            gui.active_profile_linked = false;
            set_adjustment_enabled(gui, false);
            refresh_preset_buttons(gui);
            set_status(gui, L"Windows is still identifying this display; retrying...",
                       StatusTone::warning);
            return false;
        }
        set_adjustment_enabled(gui, true);
        if (!previous_storage_id.empty() &&
            previous_storage_id != display_storage_id(gui.displays[selected])) {
            gui.session.profile_switch_undo_available = false;
            gui.session.undo_display_id.clear();
        }
        const bool loaded = load_selected_profile(gui);
        if (!loaded) {
            log_message(LogLevel::warning,
                        L"Display selected but the preferred profile could not be applied");
        } else if (gui.displays[selected].hdr_active) {
            set_status(gui, L"HDR is active; Windows or the GPU may limit Gamma adjustments",
                       StatusTone::warning);
        } else if (!gui.active_profile_linked) {
            set_status(gui,
                       L"Display calibration restored (not linked to a Profile)",
                       StatusTone::idle);
        } else {
            set_status(gui, L"Ready", StatusTone::success);
        }
        if (!preservation_ok) {
            set_status(gui, L"A pending adjustment is queued for retry", StatusTone::warning);
        }
        return loaded && preservation_ok;
    } else {
        gui.active_display_index = -1;
        gui.active_display_id.clear();
        gui.profile_binding_display_id.clear();
        gui.active_profile_linked = false;
        EnableWindow(gui.display_combo, FALSE);
        set_adjustment_enabled(gui, false);
        set_display_status(gui, L"No display connected");
        set_status(gui, L"No attached displays found", StatusTone::warning);
        // An empty enumeration is commonly transient during a GPU reset. Treat
        // it as retryable so the recovery state machine uses all three attempts.
        return false;
    }
}

bool reapply_all_committed(GuiState& gui) {
    bool success = true;
    bool profile_collection_changed = false;
    bool profile_propagation_deferred = false;
    std::wstring first_error;
    const auto mark_selected_restore_incomplete =
        [&](const std::wstring& display_id) {
            if (gui.active_display_id != display_id) return;
            gui.profile_binding_display_id.clear();
            gui.active_profile_linked = false;
            set_adjustment_enabled(gui, false);
            SetWindowTextW(gui.apply_button, L"Waiting...");
            EnableWindow(gui.apply_button, FALSE);
            EnableWindow(gui.before_after_button, FALSE);
            refresh_preset_buttons(gui);
        };
    const auto synchronize_selected = [&](const std::wstring& display_id,
                                          const CalibrationSettings& settings) {
        if (gui.active_display_id != display_id || gui.session.dirty) return;
        gui.session.committed_settings = settings;
        gui.session.dirty = false;
        gui.profile_binding_display_id = display_id;
        gui.active_profile_linked = false;
        const auto preference = gui.preferred_profile_ids.find(display_id);
        if (gui.profile_preferences_available &&
            preference != gui.preferred_profile_ids.end()) {
            const auto profile = std::find_if(
                gui.profiles.begin(), gui.profiles.end(),
                [&](const Profile& item) {
                    return item.id == preference->second &&
                           settings_equal(item.settings, settings);
                });
            if (profile != gui.profiles.end()) {
                gui.active_preset = static_cast<std::size_t>(
                    std::distance(gui.profiles.begin(), profile));
                gui.active_profile_linked = true;
            }
        }
        set_params_to_controls(gui, settings);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        EnableWindow(gui.before_after_button, FALSE);
        set_adjustment_enabled(gui, true);
        refresh_preset_buttons(gui);
    };
    for (const auto& display : gui.displays) {
        if (display_identity_unresolved(gui, display)) {
            success = false;
            mark_selected_restore_incomplete(display.device_name);
            if (first_error.empty()) {
                first_error = L"Windows has not resolved a stable identity for " +
                              display.device_name;
            }
            continue;
        }
        const std::wstring storage_id = display_storage_id(display);
        const auto pending = gui.pending_adjustments.find(storage_id);
        if (pending != gui.pending_adjustments.end()) {
            std::wstring error;
            const bool profile_was_persisted = pending->second.profile_persisted;
            if (!detach_pending_profile_association(gui, storage_id,
                                                    pending->second, error)) {
                success = false;
                mark_selected_restore_incomplete(storage_id);
                if (first_error.empty()) first_error = error;
            } else if (!gui.controller.apply_and_save(
                           display, pending->second.settings, error)) {
                success = false;
                mark_selected_restore_incomplete(storage_id);
                if (first_error.empty()) first_error = error;
            } else {
                pending->second.settings_persisted = true;
                if (!finalize_pending_metadata(gui, storage_id, pending->second,
                                               error)) {
                    success = false;
                    mark_selected_restore_incomplete(storage_id);
                    if (first_error.empty()) first_error = error;
                } else {
                    if (!profile_was_persisted &&
                        !pending->second.profile_id.empty()) {
                        profile_collection_changed = true;
                    }
                    synchronize_selected(storage_id, pending->second.settings);
                    gui.pending_adjustments.erase(pending);
                }
            }
            continue;
        }
        if (storage_id == gui.active_display_id && gui.session.dirty) {
            profile_propagation_deferred = gui.profile_propagation_pending;
            log_message(LogLevel::info,
                        L"Deferring calibration recovery while the selected display has an active edit: " +
                            display.device_name);
            continue;
        }
        std::wstring error;
        CalibrationSettings settings{};
        bool linked_profile = false;
        if (gui.profile_preferences_available) {
            const auto preference = gui.preferred_profile_ids.find(storage_id);
            if (preference != gui.preferred_profile_ids.end()) {
                const auto profile = std::find_if(
                    gui.profiles.begin(), gui.profiles.end(),
                    [&](const Profile& item) {
                        return item.id == preference->second;
                    });
                if (profile != gui.profiles.end()) {
                    settings = profile->settings;
                    linked_profile = true;
                }
            }
        }
        // A durable Profile association is itself configuration and must restore
        // even if an older version never wrote the per-display params copy. Only
        // truly unconfigured, unlinked displays are skipped to preserve any
        // external ICC/driver ramp.
        if (!linked_profile && !gui.controller.has_saved_settings(display)) {
            log_message(LogLevel::info,
                        L"Skipping unconfigured display during calibration restore: " +
                            display.device_name);
            continue;
        }
        if (!linked_profile) settings = gui.controller.load_settings(display);
        const bool applied = linked_profile
                                 ? gui.controller.apply_and_save(display, settings, error)
                                 : gui.controller.reapply_committed(display, settings, error);
        if (!applied) {
            success = false;
            mark_selected_restore_incomplete(storage_id);
            if (first_error.empty()) first_error = error;
        } else {
            gui.pending_adjustments.erase(storage_id);
            synchronize_selected(storage_id, settings);
        }
    }
    if (success && profile_collection_changed) {
        log_message(LogLevel::info,
                    L"A queued Profile changed; synchronizing all associated displays again");
        return reapply_all_committed(gui);
    }
    if (!success) {
        set_status(gui, L"Some displays could not restore calibration: " + first_error,
                   StatusTone::error);
    } else if (profile_propagation_deferred) {
        set_status(gui,
                   L"Profile synchronization will resume after the active edit is saved",
                   StatusTone::warning);
    } else {
        gui.profile_propagation_pending = false;
        set_status(gui, L"Calibration restored after display change", StatusTone::success);
    }
    return success;
}

void schedule_recovery_retry(GuiState& gui) {
    gui.recovery_retry_attempt = 0;
    KillTimer(gui.window, kRecoveryRetryTimer);
    gui.recovery_retry_timer_pending = true;
    if (!gui.background_suspended) {
        gui.recovery_retry_timer_pending =
            SetTimer(gui.window, kRecoveryRetryTimer, 200, nullptr) != 0;
    }
}

void run_recovery_retry(GuiState& gui) {
    KillTimer(gui.window, kRecoveryRetryTimer);
    gui.recovery_retry_timer_pending = false;
    ++gui.recovery_retry_attempt;
    log_message(LogLevel::info, L"Display calibration recovery attempt " +
                                std::to_wstring(gui.recovery_retry_attempt) + L" of 3");
    if (refresh_displays(gui) && reapply_all_committed(gui)) {
        gui.recovery_retry_attempt = 0;
        return;
    }
    if (gui.recovery_retry_attempt < 3) {
        const UINT delay = static_cast<UINT>(200 * gui.recovery_retry_attempt);
        gui.recovery_retry_timer_pending =
            SetTimer(gui.window, kRecoveryRetryTimer, delay, nullptr) != 0;
    } else {
        set_status(gui, L"The display driver did not accept calibration after 3 attempts",
                   StatusTone::error);
    }
}
}  // namespace gamma_changer
