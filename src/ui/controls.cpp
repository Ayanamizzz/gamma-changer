#include "gui_internal.h"

namespace gamma_changer {

LRESULT CALLBACK compare_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(button));
    const bool keyboard_press = message == WM_KEYDOWN && wparam == VK_SPACE;
    const bool keyboard_release = message == WM_KEYUP && wparam == VK_SPACE;
    if ((message == WM_LBUTTONDOWN || keyboard_press) && gui && gui->live_preview &&
        gui->session.dirty && !gui->session.comparing_original) {
        if (cancel_active_preview(*gui)) {
            gui->session.comparing_original = true;
            if (message == WM_LBUTTONDOWN) SetCapture(button);
            set_status(*gui, L"Showing the committed result", StatusTone::idle);
            InvalidateRect(button, nullptr, FALSE);
        }
    }
    if ((message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || keyboard_release ||
         message == WM_KILLFOCUS) && gui &&
        gui->session.comparing_original) {
        gui->session.comparing_original = false;
        if (GetCapture() == button) ReleaseCapture();
        if (!gui->destroying) preview_selected(*gui);
        InvalidateRect(button, nullptr, FALSE);
    }
    if (keyboard_press || keyboard_release) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(button, compare_proc, 1);
    return DefSubclassProc(button, message, wparam, lparam);
}


void set_slider(HWND slider, int value) {
    SendMessageW(slider, TBM_SETPOS, TRUE, value);
}

void set_edit(HWND edit, double value, bool preserve_focused_state = false) {
    wchar_t text[64]{};
    swprintf_s(text, L"%.2f", value);
    wchar_t current[64]{};
    GetWindowTextW(edit, current, static_cast<int>(std::size(current)));
    if (wcscmp(current, text) == 0) return;

    if (!preserve_focused_state || GetFocus() != edit) {
        SetWindowTextW(edit, text);
        return;
    }

    DWORD selection_start = 0;
    DWORD selection_end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start),
                 reinterpret_cast<LPARAM>(&selection_end));
    const DWORD old_length = static_cast<DWORD>(wcslen(current));
    const DWORD new_length = static_cast<DWORD>(wcslen(text));
    const auto map_selection = [old_length, new_length](DWORD position) {
        return position >= old_length ? new_length : std::min(position, new_length);
    };
    selection_start = map_selection(selection_start);
    selection_end = map_selection(selection_end);

    SendMessageW(edit, EM_SETSEL, 0, -1);
    // EM_REPLACESEL keeps the normalization in the native Edit undo history;
    // SetWindowTextW would clear that history while an auto-save is running.
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
    SendMessageW(edit, EM_SETSEL, selection_start, selection_end);
}

bool read_edit(HWND edit, double& value) {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const double parsed = wcstod(text, &end);
    if (end == text || *end != L'\0') return false;
    value = parsed;
    return true;
}

int slider_value(HWND slider) {
    return static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
}

GammaParams params_from_sliders(const GuiState& gui) {
    GammaParams params;
    params.gamma = slider_value(gui.gamma_slider) / 100.0;
    params.brightness = slider_value(gui.brightness_slider) / 100.0;
    params.contrast = slider_value(gui.contrast_slider) / 100.0;
    params.r_gain = slider_value(gui.r_gain_slider) / 100.0;
    params.g_gain = slider_value(gui.g_gain_slider) / 100.0;
    params.b_gain = slider_value(gui.b_gain_slider) / 100.0;
    return params;
}


void edit_changed(GuiState& gui, HWND edit, HWND slider, double minimum, double maximum) {
    double value = 0.0;
    if (!read_edit(edit, value) || !std::isfinite(value)) return;
    const double clamped = std::clamp(value, minimum, maximum);
    const bool range_adjusted = clamped != value;
    gui.syncing_controls = true;
    set_slider(slider, static_cast<int>(std::lround(clamped * 100.0)));
    gui.syncing_controls = false;
    mark_changed(gui);
    // If clamping lands on the already committed value, mark_changed() sees a
    // clean session and normally cancels auto-save. Keep one debounce timer so
    // save_and_apply_current() can canonicalize the visible text without
    // rewriting an in-progress first character such as "0" immediately.
    if (range_adjusted && !gui.session.dirty) {
        SetTimer(gui.window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
    }
}

void normalize_edit(GuiState& gui, HWND edit, HWND slider, double minimum, double maximum) {
    double value = 0.0;
    if (!read_edit(edit, value) || !std::isfinite(value)) {
        gui.syncing_controls = true;
        set_edit(edit, slider_value(slider) / 100.0, true);
        gui.syncing_controls = false;
        return;
    }
    value = std::clamp(value, minimum, maximum);
    gui.syncing_controls = true;
    set_slider(slider, static_cast<int>(std::lround(value * 100.0)));
    set_edit(edit, slider_value(slider) / 100.0, true);
    gui.syncing_controls = false;
}

void normalize_all_edits(GuiState& gui, bool notify_change) {
    normalize_edit(gui, gui.gamma_value, gui.gamma_slider,
                   calibration_ranges::gamma.minimum, calibration_ranges::gamma.maximum);
    normalize_edit(gui, gui.brightness_value, gui.brightness_slider,
                   calibration_ranges::brightness.minimum, calibration_ranges::brightness.maximum);
    normalize_edit(gui, gui.contrast_value, gui.contrast_slider,
                   calibration_ranges::contrast.minimum, calibration_ranges::contrast.maximum);
    normalize_edit(gui, gui.r_gain_value, gui.r_gain_slider,
                   calibration_ranges::gain.minimum, calibration_ranges::gain.maximum);
    normalize_edit(gui, gui.g_gain_value, gui.g_gain_slider,
                   calibration_ranges::gain.minimum, calibration_ranges::gain.maximum);
    normalize_edit(gui, gui.b_gain_value, gui.b_gain_slider,
                   calibration_ranges::gain.minimum, calibration_ranges::gain.maximum);
    if (notify_change) {
        mark_changed(gui);
    } else {
        gui.session.dirty =
            !settings_equal(params_from_sliders(gui), gui.session.committed_settings);
    }
}


LRESULT CALLBACK numeric_edit_proc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam,
                                   UINT_PTR, DWORD_PTR data) {
    auto* gui = state(GetParent(edit));
    if (message == WM_MOUSEMOVE && !data) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, edit, 0};
        TrackMouseEvent(&tracking);
        SetWindowSubclass(edit, numeric_edit_proc, 1, 1);
        if (gui) {
            gui->hovered_numeric = edit;
            invalidate_control_background(*gui, edit);
        }
    }
    if (message == WM_MOUSELEAVE) {
        SetWindowSubclass(edit, numeric_edit_proc, 1, 0);
        if (gui) {
            gui->hovered_numeric = nullptr;
            invalidate_control_background(*gui, edit);
        }
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        if (gui) invalidate_control_background(*gui, edit);
    }
    if (message == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN)) {
        double minimum = calibration_ranges::gain.minimum;
        double maximum = calibration_ranges::gain.maximum;
        double fallback = calibration_ranges::gain.default_value;
        switch (GetDlgCtrlID(edit)) {
        case kGammaValue:
            minimum = calibration_ranges::gamma.minimum;
            maximum = calibration_ranges::gamma.maximum;
            fallback = calibration_ranges::gamma.default_value;
            break;
        case kBrightnessValue:
            minimum = calibration_ranges::brightness.minimum;
            maximum = calibration_ranges::brightness.maximum;
            fallback = calibration_ranges::brightness.default_value;
            break;
        case kContrastValue:
            minimum = calibration_ranges::contrast.minimum;
            maximum = calibration_ranges::contrast.maximum;
            fallback = calibration_ranges::contrast.default_value;
            break;
        default: break;
        }
        double value = 0.0;
        if (!read_edit(edit, value) || !std::isfinite(value)) value = fallback;
        value = std::clamp(value + (wparam == VK_UP ? 0.01 : -0.01), minimum, maximum);
        set_edit(edit, value);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        HWND parent = GetParent(edit);
        SetFocus(parent);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(kApplyButton, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(parent, kApplyButton)));
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && gui) {
        gui->syncing_controls = true;
        set_edit(edit, slider_value(edit == gui->gamma_value ? gui->gamma_slider :
                                    edit == gui->brightness_value ? gui->brightness_slider :
                                    edit == gui->contrast_value ? gui->contrast_slider :
                                    edit == gui->r_gain_value ? gui->r_gain_slider :
                                    edit == gui->g_gain_value ? gui->g_gain_slider :
                                                                gui->b_gain_slider) / 100.0);
        gui->syncing_controls = false;
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK && gui) {
        HWND slider = nullptr;
        double value = 1.0;
        switch (GetDlgCtrlID(edit)) {
        case kGammaValue:
            slider = gui->gamma_slider;
            value = calibration_ranges::gamma.default_value;
            break;
        case kBrightnessValue:
            slider = gui->brightness_slider;
            value = calibration_ranges::brightness.default_value;
            break;
        case kContrastValue:
            slider = gui->contrast_slider;
            value = calibration_ranges::contrast.default_value;
            break;
        case kRGainValue: slider = gui->r_gain_slider; break;
        case kGGainValue: slider = gui->g_gain_slider; break;
        case kBGainValue: slider = gui->b_gain_slider; break;
        default: break;
        }
        if (slider) {
            gui->syncing_controls = true;
            set_slider(slider, static_cast<int>(std::lround(value * 100.0)));
            set_edit(edit, value);
            gui->syncing_controls = false;
            mark_changed(*gui);
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, numeric_edit_proc, 1);
    return DefSubclassProc(edit, message, wparam, lparam);
}


LRESULT CALLBACK slider_proc(HWND slider, UINT message, WPARAM wparam, LPARAM lparam,
                             UINT_PTR, DWORD_PTR data) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(slider, &paint);
        auto* gui = state(GetParent(slider));
        if (gui) paint_parent_layer(slider, dc, *gui);

        RECT channel{};
        RECT native_thumb{};
        SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
        SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&native_thumb));
        const UINT dpi = GetDpiForWindow(slider);
        const auto scale = [dpi](int value) {
            return std::max(1, MulDiv(value, static_cast<int>(dpi), 96));
        };
        const int center_y = (channel.top + channel.bottom) / 2;
        const int thumb_x = (native_thumb.left + native_thumb.right) / 2;
        const int half_track = scale(2);
        const bool enabled = IsWindowEnabled(slider) != FALSE;
        const COLORREF inactive_color = enabled ? ui::Theme::track_inactive
                                                : ui::Theme::disabled_surface;
        RECT inactive{channel.left, center_y - half_track,
                      channel.right, center_y + half_track};
        RECT active{channel.left, center_y - half_track,
                    thumb_x, center_y + half_track};
        ui::draw_panel(dc, inactive, inactive_color, inactive_color, scale(4));
        if (active.right > active.left) {
            const COLORREF active_color = enabled ? ui::Theme::primary
                                                  : ui::Theme::disabled_text;
            ui::draw_panel(dc, active, active_color, active_color, scale(4));
        }
        const bool focused = GetFocus() == slider;
        const bool hovered = data != 0;
        const int halo_radius = scale(8);
        const int thumb_radius = scale(6);
        if (enabled && (focused || hovered)) {
            HBRUSH halo = CreateSolidBrush(ui::Theme::primary_soft);
            const auto old_halo = SelectObject(dc, halo);
            const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, thumb_x - halo_radius, center_y - halo_radius,
                    thumb_x + halo_radius + 1, center_y + halo_radius + 1);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_halo);
            DeleteObject(halo);
        }
        HBRUSH thumb = CreateSolidBrush(enabled ? ui::Theme::primary
                                                : ui::Theme::disabled_text);
        const auto old_thumb = SelectObject(dc, thumb);
        const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, thumb_x - thumb_radius, center_y - thumb_radius,
                thumb_x + thumb_radius + 1, center_y + thumb_radius + 1);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_thumb);
        DeleteObject(thumb);
        EndPaint(slider, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSEMOVE) {
        if (!data) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, slider, 0};
            TrackMouseEvent(&tracking);
            SetWindowSubclass(slider, slider_proc, 1, 1);
        }
        const LRESULT result = DefSubclassProc(slider, message, wparam, lparam);
        InvalidateRect(slider, nullptr, FALSE);
        return result;
    }
    if (message == WM_MOUSELEAVE) {
        SetWindowSubclass(slider, slider_proc, 1, 0);
        InvalidateRect(slider, nullptr, FALSE);
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE ||
        message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS) {
        const LRESULT result = DefSubclassProc(slider, message, wparam, lparam);
        InvalidateRect(slider, nullptr, FALSE);
        return result;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(slider, slider_proc, 1);
    return DefSubclassProc(slider, message, wparam, lparam);
}


void update_value_labels(GuiState& gui) {
    gui.syncing_controls = true;
    set_edit(gui.gamma_value, slider_value(gui.gamma_slider) / 100.0);
    set_edit(gui.brightness_value, slider_value(gui.brightness_slider) / 100.0);
    set_edit(gui.contrast_value, slider_value(gui.contrast_slider) / 100.0);
    set_edit(gui.r_gain_value, slider_value(gui.r_gain_slider) / 100.0);
    set_edit(gui.g_gain_value, slider_value(gui.g_gain_slider) / 100.0);
    set_edit(gui.b_gain_value, slider_value(gui.b_gain_slider) / 100.0);
    gui.syncing_controls = false;
}


void set_adjustment_enabled(GuiState& gui, bool enabled) {
    const std::array<HWND, 14> controls{
        gui.gamma_slider, gui.brightness_slider, gui.contrast_slider,
        gui.r_gain_slider, gui.g_gain_slider, gui.b_gain_slider,
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
        gui.reset_button, gui.before_after_button,
    };
    for (HWND control : controls) EnableWindow(control, enabled ? TRUE : FALSE);
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button,
                 enabled && gui.session.dirty && gui.live_preview ? TRUE : FALSE);
}


void set_params_to_controls(GuiState& gui, const GammaParams& params) {
    gui.syncing_controls = true;
    set_slider(gui.gamma_slider, static_cast<int>(params.gamma * 100.0));
    set_slider(gui.brightness_slider, static_cast<int>(params.brightness * 100.0));
    set_slider(gui.contrast_slider, static_cast<int>(params.contrast * 100.0));
    set_slider(gui.r_gain_slider, static_cast<int>(params.r_gain * 100.0));
    set_slider(gui.g_gain_slider, static_cast<int>(params.g_gain * 100.0));
    set_slider(gui.b_gain_slider, static_cast<int>(params.b_gain * 100.0));
    gui.syncing_controls = false;
    update_value_labels(gui);
    invalidate_preview_curve(gui);
}


HWND make_control(DWORD style, LPCWSTR class_name, LPCWSTR text, HWND parent,
                  int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, class_name, text, style, x, y, width, height,
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    if (!control) {
        log_message(LogLevel::error, L"Could not create control " + std::to_wstring(id) +
                                         L" (Win32 error " +
                                         std::to_wstring(GetLastError()) + L")");
    }
    return control;
}

HWND make_control_ex(DWORD ex_style, DWORD style, LPCWSTR class_name, LPCWSTR text,
                     HWND parent, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(ex_style, class_name, text, style, x, y, width, height,
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    if (!control) {
        log_message(LogLevel::error, L"Could not create control " + std::to_wstring(id) +
                                         L" (Win32 error " +
                                         std::to_wstring(GetLastError()) + L")");
    }
    return control;
}


void create_controls(GuiState& gui) {
    const UINT dpi = GetDpiForWindow(gui.window);
    recreate_fonts(gui, dpi);
    gui.background_brush = CreateSolidBrush(ui::Theme::main_surface);
    gui.card_brush = CreateSolidBrush(ui::Theme::panel_surface);
    gui.sidebar_brush = CreateSolidBrush(ui::Theme::sidebar);
    gui.profile_edit_brush = CreateSolidBrush(ui::Theme::sidebar_selected);

    gui.title = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Gamma Changer",
                             gui.window, kTitleLabel, 0, 0, 0, 0);
    gui.subtitle = make_control(WS_CHILD | WS_VISIBLE, L"STATIC",
                                L"Display calibration utility",
                                gui.window, kSubtitleLabel, 0, 0, 0, 0);
    gui.display_caption = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"DISPLAY",
                                       gui.window, kDisplayCaption, 0, 0, 0, 0);
    gui.preset_caption = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"PROFILES",
                                      gui.window, kPresetCaption, 0, 0, 0, 0);
    gui.main_heading = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Display Calibration",
                                    gui.window, kMainHeading, 0, 0, 0, 0);
    gui.main_subtitle = make_control(WS_CHILD | WS_VISIBLE, L"STATIC",
                                     L"Adjust the selected display with live, reversible feedback.",
                                     gui.window, kMainSubtitle, 0, 0, 0, 0);
    gui.tone_heading = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"BASIC",
                                    gui.window, kToneHeading, 0, 0, 0, 0);
    gui.color_heading = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"COLOR",
                                     gui.window, kColorHeading, 0, 0, 0, 0);
    gui.preview_heading = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Tone Response",
                                       gui.window, kPreviewHeading, 0, 0, 0, 0);
    gui.preview_caption = make_control(WS_CHILD | WS_VISIBLE | SS_RIGHT, L"STATIC",
                                        L"- Reference      -- Adjusted", gui.window, kPreviewCaption,
                                       0, 0, 0, 0);
    gui.tone_caption = make_control(WS_CHILD | WS_VISIBLE, L"STATIC",
                                    L"Luminance and tonal response", gui.window, kToneCaption,
                                    0, 0, 0, 0);
    gui.color_caption = make_control(WS_CHILD | WS_VISIBLE, L"STATIC",
                                     L"Individual RGB channel gain", gui.window, kColorCaption,
                                     0, 0, 0, 0);
    gui.display_status = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Detecting displays...",
                                      gui.window, kDisplayStatus, 0, 0, 0, 0);
    gui.display_combo = make_control(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                                     WC_COMBOBOXW, nullptr, gui.window, kDisplayCombo,
                                     0, 0, 0, 0);
    SendMessageW(gui.display_combo, CB_SETMINVISIBLE, 8, 0);
    gui.refresh_button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                       L"BUTTON", L"Refresh displays", gui.window, kRefreshButton,
                                       0, 0, 0, 0);

    gui.preset_save = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   L"BUTTON", L"+  New profile", gui.window,
                                   kPresetSave, 0, 0, 0, 0);
    gui.preset_delete = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     L"BUTTON", L"Delete profile", gui.window,
                                      kPresetDelete, 0, 0, 0, 0);
    gui.profile_rename = make_control(WS_CHILD | ES_MULTILINE | ES_AUTOHSCROLL |
                                          ES_NOHIDESEL | WS_TABSTOP | WS_CLIPSIBLINGS,
                                       L"EDIT", L"", gui.window, kProfileRename,
                                       0, 0, 0, 0);
    SendMessageW(gui.profile_rename, EM_SETLIMITTEXT, 48, 0);
    SetWindowSubclass(gui.profile_rename, profile_rename_proc, 1, 0);
    gui.startup_button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      L"BUTTON", L"Start with Windows", gui.window,
                                      kStartupToggle, 0, 0, 0, 0);

    auto make_slider = [&](int id) {
        return make_control(WS_CHILD | WS_VISIBLE | TBS_NOTICKS | WS_TABSTOP,
                            TRACKBAR_CLASSW, nullptr, gui.window, id, 0, 0, 0, 0);
    };
    auto make_value = [&](int id) {
        return make_control_ex(0, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
                                   ES_RIGHT | WS_TABSTOP,
                               L"EDIT", L"1.00", gui.window, id, 0, 0, 0, 0);
    };

    gui.gamma_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Gamma", gui.window, 0,
                                   0, 0, 0, 0);
    gui.gamma_slider = make_slider(kGammaSlider);
    gui.gamma_value = make_value(kGammaValue);
    gui.brightness_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Brightness", gui.window, 0,
                                        0, 0, 0, 0);
    gui.brightness_slider = make_slider(kBrightnessSlider);
    gui.brightness_value = make_value(kBrightnessValue);
    gui.contrast_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Contrast", gui.window, 0,
                                      0, 0, 0, 0);
    gui.contrast_slider = make_slider(kContrastSlider);
    gui.contrast_value = make_value(kContrastValue);
    gui.r_gain_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Red gain", gui.window, 0,
                                    0, 0, 0, 0);
    gui.r_gain_slider = make_slider(kRGainSlider);
    gui.r_gain_value = make_value(kRGainValue);
    gui.g_gain_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Green gain", gui.window, 0,
                                    0, 0, 0, 0);
    gui.g_gain_slider = make_slider(kGGainSlider);
    gui.g_gain_value = make_value(kGGainValue);
    gui.b_gain_label = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Blue gain", gui.window, 0,
                                    0, 0, 0, 0);
    gui.b_gain_slider = make_slider(kBGainSlider);
    gui.b_gain_value = make_value(kBGainValue);

    SendMessageW(gui.gamma_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::gamma.minimum * 100),
                          static_cast<int>(calibration_ranges::gamma.maximum * 100)));
    SendMessageW(gui.brightness_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::brightness.minimum * 100),
                          static_cast<int>(calibration_ranges::brightness.maximum * 100)));
    SendMessageW(gui.contrast_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::contrast.minimum * 100),
                          static_cast<int>(calibration_ranges::contrast.maximum * 100)));
    SendMessageW(gui.r_gain_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::gain.minimum * 100),
                          static_cast<int>(calibration_ranges::gain.maximum * 100)));
    SendMessageW(gui.g_gain_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::gain.minimum * 100),
                          static_cast<int>(calibration_ranges::gain.maximum * 100)));
    SendMessageW(gui.b_gain_slider, TBM_SETRANGE, TRUE,
                 MAKELONG(static_cast<int>(calibration_ranges::gain.minimum * 100),
                          static_cast<int>(calibration_ranges::gain.maximum * 100)));

    if (FAILED(SetWindowTheme(gui.display_combo, L"Explorer", nullptr))) {
        log_message(LogLevel::warning, L"Could not apply the Explorer theme to the display list");
    }
    const std::array<HWND, 12> themed_controls{
        gui.gamma_slider, gui.brightness_slider, gui.contrast_slider,
        gui.r_gain_slider, gui.g_gain_slider, gui.b_gain_slider,
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
    };
    for (HWND control : themed_controls) {
        if (FAILED(SetWindowTheme(control, L"Explorer", nullptr))) {
            log_message(LogLevel::warning, L"Could not apply the Explorer theme to a control");
        }
    }
    // The inline rename editor is intentionally dark and lives in the custom
    // Sidebar row host. Disable Explorer theming so Windows cannot reintroduce
    // a light inset or border on a future theme repaint.
    if (FAILED(SetWindowTheme(gui.profile_rename, L"", L""))) {
        log_message(LogLevel::warning,
                    L"Could not disable native theming for the Profile rename editor");
    }

    gui.apply_button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     L"BUTTON", L"Saved", gui.window, kApplyButton,
                                    0, 0, 0, 0);
    gui.reset_button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     L"BUTTON", L"Restore defaults", gui.window, kResetButton,
                                    0, 0, 0, 0);
    gui.before_after_button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                            L"BUTTON", L"Hold original", gui.window,
                                            kBeforeAfter, 0, 0, 0, 0);
    SetWindowSubclass(gui.before_after_button, compare_proc, 1, 0);
    gui.status = make_control(WS_CHILD | WS_VISIBLE, L"STATIC", L"Ready",
                              gui.window, kStatusLabel, 0, 0, 0, 0);
    EnableWindow(gui.apply_button, FALSE);

    apply_font(gui.title, gui.title_font);
    apply_font(gui.subtitle, gui.normal_font);
    apply_font(gui.display_caption, gui.caption_font);
    apply_font(gui.preset_caption, gui.caption_font);
    apply_font(gui.main_heading, gui.heading_font);
    apply_font(gui.main_subtitle, gui.normal_font);
    apply_font(gui.tone_heading, gui.section_font);
    apply_font(gui.color_heading, gui.section_font);
    apply_font(gui.preview_heading, gui.panel_font);
    apply_font(gui.preview_caption, gui.small_font);
    apply_font(gui.tone_caption, gui.small_font);
    apply_font(gui.color_caption, gui.small_font);
    apply_font(gui.display_status, gui.small_font);
    apply_font(gui.gamma_label, gui.normal_font);
    apply_font(gui.brightness_label, gui.normal_font);
    apply_font(gui.contrast_label, gui.normal_font);
    apply_font(gui.r_gain_label, gui.normal_font);
    apply_font(gui.g_gain_label, gui.normal_font);
    apply_font(gui.b_gain_label, gui.normal_font);
    apply_font(gui.display_combo, gui.normal_font);
    apply_font(gui.refresh_button, gui.normal_font);
    for (auto button : gui.preset_buttons) apply_font(button, gui.normal_font);
    apply_font(gui.preset_save, gui.normal_font);
    apply_font(gui.preset_delete, gui.normal_font);
    apply_font(gui.profile_rename, gui.normal_font);
    apply_font(gui.startup_button, gui.normal_font);
    apply_font(gui.apply_button, gui.normal_font);
    apply_font(gui.reset_button, gui.normal_font);
    apply_font(gui.before_after_button, gui.normal_font);
    apply_font(gui.status, gui.normal_font);
    apply_font(gui.gamma_value, gui.normal_font);
    apply_font(gui.brightness_value, gui.normal_font);
    apply_font(gui.contrast_value, gui.normal_font);
    apply_font(gui.r_gain_value, gui.normal_font);
    apply_font(gui.g_gain_value, gui.normal_font);
    apply_font(gui.b_gain_value, gui.normal_font);

    const std::array<HWND, 6> numeric_edits{
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
    };
    for (HWND edit : numeric_edits) {
        SetWindowSubclass(edit, numeric_edit_proc, 1, 0);
    }
    const std::array<HWND, 6> sliders{
        gui.gamma_slider, gui.brightness_slider, gui.contrast_slider,
        gui.r_gain_slider, gui.g_gain_slider, gui.b_gain_slider,
    };
    for (HWND slider : sliders) SetWindowSubclass(slider, slider_proc, 1, 0);

    RECT client{};
    GetClientRect(gui.window, &client);
    layout_controls(gui, client.right - client.left, client.bottom - client.top);
}

}  // namespace gamma_changer
