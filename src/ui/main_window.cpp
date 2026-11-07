#include "gui_internal.h"

namespace gamma_changer {

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* gui = state(window);
    static const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == taskbar_created) {
        add_tray_icon(window);
        return 0;
    }
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        gui = state(window);
        gui->window = window;
    }

    switch (message) {
    case kForegroundProfileMessage:
        if (gui) schedule_automatic_profiles(*gui, wparam != 0);
        return 0;
    case WM_ACTIVATEAPP:
        if (gui) {
            if (wparam && IsWindowVisible(window) && !IsIconic(window)) {
                restore_automatic_profiles(*gui);
                update_automatic_profile_controls(*gui);
            } else {
                schedule_automatic_profiles(*gui);
            }
        }
        break;
    case kUndoProfileSwitchMessage:
        if (gui) undo_profile_switch(*gui);
        return 0;
    case kProfileRenameMessage:
        if (gui) begin_profile_rename(*gui, static_cast<std::size_t>(wparam));
        return 0;
    case kProfileCommitRenameMessage:
        if (gui) finish_profile_rename(*gui, wparam != FALSE);
        return 0;
    case kProfileContextMessage:
        if (gui) {
            const std::size_t index = static_cast<std::size_t>(wparam);
            if (index >= gui->profiles.size()) return 0;
            const bool builtin = gui->profiles[index].id == kBuiltinProfileId;
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | (builtin ? MF_GRAYED : MF_ENABLED), 1, L"Rename");
            AppendMenuW(menu, MF_STRING | MF_ENABLED, 2, L"Duplicate");
            const bool can_bind = gui->profile_store_available && selected_display(*gui) &&
                                  gui->profiles[index].saved;
            AppendMenuW(menu, MF_STRING | (can_bind ? MF_ENABLED : MF_GRAYED), 4,
                        L"Bind application...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (builtin ? MF_GRAYED : MF_ENABLED), 3, L"Delete");
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                x, y, 0, window, nullptr);
            DestroyMenu(menu);
            if (command == 1) begin_profile_rename(*gui, index);
            else if (command == 2) duplicate_profile(*gui, index);
            else if (command == 3) delete_preset(*gui, index);
            else if (command == 4) bind_application_profile(*gui, index);
        }
        return 0;
    case WM_CREATE: {
        auto* current = state(window);
        enable_modern_backdrop(window);
        ui::BackgroundOptions background_options;
        background_options.image_opacity = 232;
        background_options.sidebar_overlay_color = ui::Theme::wallpaper_sidebar_overlay;
        background_options.main_overlay_color = ui::Theme::wallpaper_main_overlay;
        background_options.sidebar_overlay = 216;
        background_options.main_overlay = 208;
        background_options.blur_radius = 0;  // Reserved for a future appearance setting.
        background_options.position_x = 0.5;
        background_options.position_y = 0.5;
        current->background.set_options(background_options);
        current->background.load_resource(GetModuleHandleW(nullptr), IDR_UI_WALLPAPER);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&common);
        create_controls(*current);
        std::wstring startup_error;
        StartupState startup = startup_state(startup_error);
        if (startup == StartupState::enabled_stale_path) {
            if (repair_startup_registration(startup_error)) {
                startup = StartupState::enabled_current_path;
                log_message(LogLevel::info, L"Updated the Windows startup path");
            } else {
                log_message(LogLevel::warning, startup_error);
            }
        }
        current->startup_enabled = startup == StartupState::enabled_current_path;
        InvalidateRect(current->startup_button, nullptr, FALSE);
        std::wstring profile_error;
        if (!current->profile_manager.load(profile_error)) {
            current->profile_store_available = false;
            // Keep the legacy presets usable read-only instead of showing an
            // empty profile list when only profiles.v1 is damaged.
            current->profiles = current->profile_manager.read_only_legacy_profiles();
            set_status(*current, L"Profile file is damaged; legacy profiles are read-only: " +
                                       profile_error,
                       StatusTone::error);
        } else {
            current->profiles = current->profile_manager.profiles();
        }
        current->preferred_profile_ids.clear();
        for (const auto& preference : current->store.load_profile_preferences()) {
            const bool known_profile = std::any_of(
                current->profiles.begin(), current->profiles.end(),
                [&](const Profile& profile) { return profile.id == preference.profile_id; });
            if (known_profile) {
                current->preferred_profile_ids[preference.display_id] =
                    preference.profile_id;
            }
        }
        if (current->active_preset >= current->profiles.size()) current->active_preset = 0;
        current->profile_scroll_offset = 0;
        refresh_preset_buttons(*current);
        add_tray_icon(window);
        refresh_displays(*current);
        if (current->startup_launch && !current->displays.empty()) {
            reapply_all_committed(*current);
            set_status(*current, L"Saved display settings restored", StatusTone::success);
        }
        if (!current->profile_store_available) {
            EnableWindow(current->preset_save, FALSE);
            EnableWindow(current->preset_delete, FALSE);
            set_status(*current,
                       L"Profile file is damaged; legacy profiles are read-only, "
                       L"calibration saving remains available",
                       StatusTone::error);
        }
        initialize_automatic_profiles(*current);
        log_message(LogLevel::info,
                    std::wstring(kApplicationName) + L" " + kApplicationVersion + L" started");
        return 0;
    }
    case WM_PAINT:
        if (gui) {
            paint_background(window, *gui);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED:
        if (gui) {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            recreate_fonts(*gui, HIWORD(wparam));
            RECT client{};
            GetClientRect(window, &client);
            layout_controls(*gui, client.right - client.left, client.bottom - client.top);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_DISPLAYCHANGE:
        if (gui) {
            gui->reapply_after_display_refresh = true;
            log_message(LogLevel::info, L"Display configuration change detected");
            KillTimer(window, kPreviewTimer);
            SetTimer(window, kDisplayRefreshTimer, 250, nullptr);
            return 0;
        }
        break;
    case WM_POWERBROADCAST:
        if (gui && (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)) {
            log_message(LogLevel::info, L"System resume detected");
            gui->reapply_after_display_refresh = true;
            KillTimer(window, kPreviewTimer);
            SetTimer(window, kDisplayRefreshTimer, 500, nullptr);
            return TRUE;
        }
        break;
    case WM_QUERYENDSESSION:
        if (gui && !flush_before_exit(*gui)) {
            log_message(LogLevel::error,
                        L"Could not save the final adjustments during Windows shutdown");
            if (const auto* display = selected_display(*gui)) {
                std::wstring rollback_error;
                gui->controller.reapply_committed(*display, gui->session.committed_settings,
                                                  rollback_error);
            }
        }
        if (gui) restore_automatic_profiles(*gui);
        return TRUE;
    case WM_ENDSESSION:
        if (gui && wparam) cancel_active_preview(*gui);
        return 0;
    case WM_DRAWITEM:
        if (gui && lparam) {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (item->CtlID == kApplyButton || item->CtlID == kResetButton ||
                 item->CtlID == kBeforeAfter ||
                 item->CtlID == kRefreshButton || item->CtlID == kPresetSave ||
                 item->CtlID == kPresetDelete || item->CtlID == kStartupToggle ||
                 item->CtlID == kAutoSwitchToggle || item->CtlID == kBindApplication ||
                 item->CtlID == kAppRules ||
                (item->CtlID >= kProfileIdBase &&
                 static_cast<std::size_t>(item->CtlID - kProfileIdBase) < gui->profiles.size())) {
                draw_owner_button(*item, *gui);
                return TRUE;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (gui) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            HWND control = reinterpret_cast<HWND>(lparam);
            SetBkMode(dc, TRANSPARENT);
            if (control == gui->title) {
                SetTextColor(dc, ui::Theme::sidebar_text);
            } else if (control == gui->main_heading || control == gui->tone_heading ||
                       control == gui->color_heading || control == gui->preview_heading) {
                SetTextColor(dc, ui::Theme::text);
            } else if (control == gui->display_caption || control == gui->preset_caption) {
                SetTextColor(dc, ui::Theme::primary_soft);
            } else if (control == gui->display_status || control == gui->auto_switch_status) {
                SetTextColor(dc, ui::Theme::sidebar_secondary);
            } else if (control == gui->subtitle || control == gui->main_subtitle ||
                       control == gui->status || control == gui->preview_caption ||
                       control == gui->tone_caption || control == gui->color_caption) {
                SetTextColor(dc, ui::Theme::text_secondary);
            } else {
                SetTextColor(dc, ui::Theme::text);
            }
            const AdjustmentBindings bindings = adjustment_bindings(*gui);
            const bool trackbar = std::any_of(
                bindings.begin(), bindings.end(),
                [control](const AdjustmentBinding& binding) { return binding.slider == control; });
            // Native trackbars require an opaque erase brush; text labels inherit
            // the already-composited parent layer without rectangular islands.
            return reinterpret_cast<LRESULT>(trackbar ? gui->background_brush
                                                      : GetStockObject(HOLLOW_BRUSH));
        }
        break;
    case WM_CTLCOLOREDIT:
        if (gui) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, ui::Theme::text);
            SetBkColor(dc, ui::Theme::control_surface);
            return reinterpret_cast<LRESULT>(gui->card_brush);
        }
        break;
    case WM_COMMAND:
        if (!gui) break;
        if (!gui->syncing_controls &&
            (HIWORD(wparam) == EN_CHANGE || HIWORD(wparam) == EN_KILLFOCUS)) {
            const AdjustmentBindings bindings = adjustment_bindings(*gui);
            if (const auto* binding =
                    find_adjustment(bindings, reinterpret_cast<HWND>(lparam))) {
                if (HIWORD(wparam) == EN_CHANGE) edit_changed(*gui, *binding);
                else normalize_edit(*gui, *binding);
                return 0;
            }
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= kProfileIdBase &&
            static_cast<std::size_t>(LOWORD(wparam) - kProfileIdBase) < gui->profiles.size()) {
            select_preset(*gui, static_cast<std::size_t>(LOWORD(wparam) - kProfileIdBase));
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kPresetSave) {
            create_profile_from_current(*gui);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kPresetDelete) {
            delete_active_preset(*gui);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kStartupToggle) {
            const bool enabling = !gui->startup_enabled;
            std::wstring error;
            if (!set_startup_enabled(enabling, error)) {
                set_status(*gui, error, StatusTone::error);
                return 0;
            }
            gui->startup_enabled = enabling;
            set_status(*gui, enabling ? L"Gamma Changer will start with Windows"
                                      : L"Start with Windows disabled",
                       enabling ? StatusTone::success : StatusTone::idle);
            InvalidateRect(gui->startup_button, nullptr, FALSE);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kAutoSwitchToggle) {
            toggle_automatic_profiles(*gui);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kBindApplication) {
            bind_application_profile(*gui, gui->active_preset);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kAppRules) {
            show_application_rules(*gui);
            return 0;
        }
        if (LOWORD(wparam) == kDisplayCombo && HIWORD(wparam) == CBN_SELCHANGE) {
            KillTimer(window, kPreviewTimer);
            KillTimer(window, kAutoSaveTimer);
            if (gui->session.dirty && !save_and_apply_current(*gui, true)) {
                select_display_item(*gui, gui->active_display_index);
                return 0;
            }
            const bool loaded = load_selected_profile(*gui);
            const int index = selected_display_index(*gui);
            if (index >= 0 && index < static_cast<int>(gui->displays.size())) {
                const std::wstring metadata = display_metadata(gui->displays[index]);
                set_display_status(*gui, metadata);
                if (loaded) set_status(*gui, L"Ready", StatusTone::success);
            }
            update_automatic_profile_controls(*gui);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kApplyButton) {
            save_and_apply_current(*gui, false);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kResetButton) {
            reset_selected(*gui);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == kRefreshButton) {
            refresh_displays(*gui);
            evaluate_automatic_profiles(*gui, true);
            return 0;
        }
        break;
    case WM_HSCROLL:
        if (gui) {
            update_value_labels(*gui);
            mark_changed(*gui);
        }
        return 0;
    case WM_TIMER:
        if (wparam == kForegroundProfileTimer && gui) {
            KillTimer(window, kForegroundProfileTimer);
            gui->foreground_evaluation_pending = false;
            const bool force = gui->foreground_reapply_requested;
            gui->foreground_reapply_requested = false;
            evaluate_automatic_profiles(*gui, force);
        } else if (wparam == kPreviewTimer && gui) {
            KillTimer(window, kPreviewTimer);
            if (gui->live_preview) preview_selected(*gui);
        } else if (wparam == kDisplayRefreshTimer && gui) {
            KillTimer(window, kDisplayRefreshTimer);
            refresh_displays(*gui);
            if (gui->reapply_after_display_refresh) {
                gui->reapply_after_display_refresh = false;
                schedule_recovery_retry(*gui);
            }
            evaluate_automatic_profiles(*gui, true);
        } else if (wparam == kAutoSaveTimer && gui) {
            KillTimer(window, kAutoSaveTimer);
            save_and_apply_current(*gui, true);
        } else if (wparam == kRecoveryRetryTimer && gui) {
            run_recovery_retry(*gui);
            evaluate_automatic_profiles(*gui, true);
        }
        return 0;
    case kTrayMessage:
    {
        // NOTIFYICON_VERSION_4 packs the event into LOWORD(lParam) and the icon ID
        // into HIWORD(lParam). Older shell versions pass the event directly in lParam.
        // Reading LOWORD works for both layouts.
        const UINT notification = LOWORD(static_cast<DWORD_PTR>(lparam));
        const UINT packed_icon_id = HIWORD(static_cast<DWORD_PTR>(lparam));
        if (packed_icon_id != 0 && packed_icon_id != kTrayId) return 0;

        if ((notification == NIN_SELECT || notification == NIN_KEYSELECT ||
             notification == WM_LBUTTONUP || notification == WM_LBUTTONDBLCLK) && gui) {
            show_main_window(window);
        } else if (notification == WM_CONTEXTMENU || notification == WM_RBUTTONUP) {
            show_tray_menu(window);
        }
        return 0;
    }
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            log_message(LogLevel::info, L"Main window minimized to the notification area");
            ShowWindow(window, SW_HIDE);
        } else if (gui) {
            RECT client{};
            GetClientRect(window, &client);
            layout_controls(*gui, client.right - client.left, client.bottom - client.top);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const UINT dpi = GetDpiForWindow(window);
        limits->ptMinTrackSize.x = MulDiv(960, static_cast<int>(dpi), 96);
        limits->ptMinTrackSize.y = MulDiv(760, static_cast<int>(dpi), 96);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0u) == SC_CLOSE) {
            log_message(LogLevel::info, L"Title-bar close requested");
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        log_message(LogLevel::info, L"Application exit requested");
        if (gui && !confirm_close_after_save_failure(*gui)) return 0;
        if (gui) restore_automatic_profiles(*gui);
        log_message(LogLevel::info, L"Final adjustments saved; destroying main window");
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (gui) gui->foreground_watcher.stop();
        if (gui) cancel_active_preview(*gui);
        remove_tray_icon(window);
        if (gui && gui->normal_font) DeleteObject(gui->normal_font);
        if (gui && gui->title_font) DeleteObject(gui->title_font);
        if (gui && gui->heading_font) DeleteObject(gui->heading_font);
        if (gui && gui->panel_font) DeleteObject(gui->panel_font);
        if (gui && gui->section_font) DeleteObject(gui->section_font);
        if (gui && gui->caption_font) DeleteObject(gui->caption_font);
        if (gui && gui->small_font) DeleteObject(gui->small_font);
        if (gui && gui->background_brush) DeleteObject(gui->background_brush);
        if (gui && gui->card_brush) DeleteObject(gui->card_brush);
        if (gui && gui->sidebar_brush) DeleteObject(gui->sidebar_brush);
        if (gui && gui->paint_buffer_dc) {
            if (gui->paint_buffer_original) {
                SelectObject(gui->paint_buffer_dc, gui->paint_buffer_original);
            }
            if (gui->paint_buffer_bitmap) DeleteObject(gui->paint_buffer_bitmap);
            DeleteDC(gui->paint_buffer_dc);
            gui->paint_buffer_dc = nullptr;
            gui->paint_buffer_bitmap = nullptr;
        }
        log_message(LogLevel::info, L"Main window destroyed; process is exiting");
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEWHEEL:
        if (gui && gui->profiles.size() > kProfileVisibleRows) {
            POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &cursor);
            const UINT dpi = GetDpiForWindow(window);
            const auto scale = [dpi](int value) {
                return MulDiv(value, static_cast<int>(dpi), 96);
            };
            const int content_x = scale(ui::Metrics::profile_content_inset);
            const int content_right = scale(ui::Metrics::sidebar_width) -
                                      scale(ui::Metrics::profile_section_inset);
            const int row_height = scale(ui::Metrics::profile_row_height);
            const int row_stride = row_height + scale(ui::Metrics::profile_row_gap);
            const int first_y = scale(252) + scale(18) +
                                scale(ui::Metrics::profile_title_gap);
            const int list_bottom = first_y + kProfileVisibleRows * row_height +
                                    (kProfileVisibleRows - 1) * scale(ui::Metrics::profile_row_gap);
            const bool over_profile_list = cursor.x >= content_x && cursor.x <= content_right &&
                                           cursor.y >= first_y && cursor.y <= list_bottom;
            if (over_profile_list) {
                const int wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
                gui->profile_scroll_offset -= (wheel_delta / WHEEL_DELTA) * row_stride;
                layout_controls_for_current_size(*gui);
                return 0;
            }
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace gamma_changer
