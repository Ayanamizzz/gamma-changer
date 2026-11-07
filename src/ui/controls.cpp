#include "gui_internal.h"

namespace gamma_changer {

LRESULT CALLBACK compare_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(button));
    if (message == WM_LBUTTONDOWN && gui && gui->live_preview && gui->session.dirty) {
        if (cancel_active_preview(*gui)) {
            gui->session.comparing_original = true;
            SetCapture(button);
            set_status(*gui, L"Showing the committed result", StatusTone::idle);
            InvalidateRect(button, nullptr, FALSE);
        }
    }
    if ((message == WM_LBUTTONUP || message == WM_CAPTURECHANGED) && gui &&
        gui->session.comparing_original) {
        gui->session.comparing_original = false;
        if (GetCapture() == button) ReleaseCapture();
        preview_selected(*gui);
        InvalidateRect(button, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(button, compare_proc, 1);
    return DefSubclassProc(button, message, wparam, lparam);
}


AdjustmentBindings adjustment_bindings(const GuiState& gui) {
    return {{
        {gui.gamma_value, gui.gamma_slider, calibration_ranges::gamma, &CalibrationSettings::gamma},
        {gui.brightness_value, gui.brightness_slider, calibration_ranges::brightness,
         &CalibrationSettings::brightness},
        {gui.contrast_value, gui.contrast_slider, calibration_ranges::contrast,
         &CalibrationSettings::contrast},
        {gui.r_gain_value, gui.r_gain_slider, calibration_ranges::gain, &CalibrationSettings::r_gain},
        {gui.g_gain_value, gui.g_gain_slider, calibration_ranges::gain, &CalibrationSettings::g_gain},
        {gui.b_gain_value, gui.b_gain_slider, calibration_ranges::gain, &CalibrationSettings::b_gain},
    }};
}

const AdjustmentBinding* find_adjustment(const AdjustmentBindings& bindings, HWND edit) {
    for (const auto& binding : bindings) {
        if (binding.edit == edit) return &binding;
    }
    return nullptr;
}

namespace {

void set_slider(HWND slider, double value) {
    SendMessageW(slider, TBM_SETPOS, TRUE, setting_to_steps(value));
}

double slider_setting(HWND slider) {
    return steps_to_setting(static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
}

void set_edit(HWND edit, double value) {
    wchar_t text[64]{};
    swprintf_s(text, L"%.2f", value);
    SetWindowTextW(edit, text);
}

bool read_edit(HWND edit, double& value) {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const double parsed = wcstod(text, &end);
    if (end == text || *end != L'\0' || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

}  // namespace

GammaParams params_from_sliders(const GuiState& gui) {
    GammaParams params;
    for (const auto& binding : adjustment_bindings(gui)) {
        params.*binding.field = slider_setting(binding.slider);
    }
    return params;
}

void edit_changed(GuiState& gui, const AdjustmentBinding& binding) {
    double value = 0.0;
    if (!read_edit(binding.edit, value)) return;
    value = std::clamp(value, binding.range.minimum, binding.range.maximum);
    gui.syncing_controls = true;
    set_slider(binding.slider, value);
    gui.syncing_controls = false;
    mark_changed(gui);
}

void normalize_edit(GuiState& gui, const AdjustmentBinding& binding) {
    double value = 0.0;
    gui.syncing_controls = true;
    if (!read_edit(binding.edit, value)) {
        set_edit(binding.edit, slider_setting(binding.slider));
    } else {
        value = std::clamp(value, binding.range.minimum, binding.range.maximum);
        set_slider(binding.slider, value);
        set_edit(binding.edit, value);
    }
    gui.syncing_controls = false;
}

void normalize_all_edits(GuiState& gui) {
    for (const auto& binding : adjustment_bindings(gui)) normalize_edit(gui, binding);
    mark_changed(gui);
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
    const AdjustmentBindings bindings =
        gui ? adjustment_bindings(*gui) : AdjustmentBindings{};
    const AdjustmentBinding* binding = gui ? find_adjustment(bindings, edit) : nullptr;
    if (message == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN) && binding) {
        double value = 0.0;
        if (!read_edit(edit, value)) value = binding->range.default_value;
        value = std::clamp(value + (wparam == VK_UP ? 0.01 : -0.01), binding->range.minimum,
                           binding->range.maximum);
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
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && binding) {
        gui->syncing_controls = true;
        set_edit(edit, slider_setting(binding->slider));
        gui->syncing_controls = false;
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK && binding) {
        const double value = binding->range.default_value;
        gui->syncing_controls = true;
        set_slider(binding->slider, value);
        set_edit(edit, value);
        gui->syncing_controls = false;
        mark_changed(*gui);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, numeric_edit_proc, 1);
    return DefSubclassProc(edit, message, wparam, lparam);
}


LRESULT CALLBACK slider_proc(HWND slider, UINT message, WPARAM wparam, LPARAM lparam,
                             UINT_PTR, DWORD_PTR data) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(slider, &paint);
        RECT client{};
        GetClientRect(slider, &client);
        auto* gui = state(GetParent(slider));
        if (gui) paint_parent_layer(slider, dc, *gui);

        const int inset = 7;
        const int center_y = (client.bottom - client.top) / 2;
        const int minimum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMIN, 0, 0));
        const int maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
        const int position = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        const int track_width = std::max(1, static_cast<int>(client.right) - inset * 2);
        const int thumb_x = inset + MulDiv(position - minimum, track_width,
                                           std::max(1, maximum - minimum));
        RECT inactive{inset, center_y - 2, client.right - inset, center_y + 2};
        RECT active{inset, center_y - 2, thumb_x, center_y + 2};
        ui::draw_panel(dc, inactive, ui::Theme::track_inactive,
                       ui::Theme::track_inactive, ui::Metrics::space_1);
        if (active.right > active.left) {
            ui::draw_panel(dc, active, ui::Theme::primary, ui::Theme::primary, 4);
        }
        const bool focused = GetFocus() == slider;
        const bool hovered = data != 0;
        if (focused || hovered) {
            HBRUSH halo = CreateSolidBrush(ui::Theme::primary_soft);
            const auto old_halo = SelectObject(dc, halo);
            const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, thumb_x - 8, center_y - 8, thumb_x + 9, center_y + 9);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_halo);
            DeleteObject(halo);
        }
        HBRUSH thumb = CreateSolidBrush(ui::Theme::primary);
        const auto old_thumb = SelectObject(dc, thumb);
        const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, thumb_x - 6, center_y - 6, thumb_x + 7, center_y + 7);
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
    for (const auto& binding : adjustment_bindings(gui)) {
        set_edit(binding.edit, slider_setting(binding.slider));
    }
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
    for (const auto& binding : adjustment_bindings(gui)) {
        set_slider(binding.slider, params.*binding.field);
    }
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
    gui.refresh_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       L"BUTTON", L"", gui.window, kRefreshButton,
                                       0, 0, 0, 0);

    gui.preset_save = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   L"BUTTON", L"+  New profile", gui.window,
                                   kPresetSave, 0, 0, 0, 0);
    gui.preset_delete = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     L"BUTTON", L"Delete profile", gui.window,
                                      kPresetDelete, 0, 0, 0, 0);
    gui.profile_rename = make_control(WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS,
                                      L"EDIT", L"", gui.window, kProfileRename,
                                      0, 0, 0, 0);
    SendMessageW(gui.profile_rename, EM_SETLIMITTEXT, 48, 0);
    SetWindowSubclass(gui.profile_rename, profile_rename_proc, 1, 0);
    gui.startup_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      L"BUTTON", L"Start with Windows", gui.window,
                                      kStartupToggle, 0, 0, 0, 0);
    gui.bind_application_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                               L"BUTTON", L"Bind app...", gui.window,
                                               kBindApplication, 0, 0, 0, 0);
    gui.app_rules_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                        L"BUTTON", L"App rules...", gui.window,
                                        kAppRules, 0, 0, 0, 0);
    gui.auto_switch_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                          L"BUTTON", L"Auto-switch: Off", gui.window,
                                          kAutoSwitchToggle, 0, 0, 0, 0);
    gui.auto_switch_status = make_control(WS_CHILD | WS_VISIBLE, L"STATIC",
                                          L"Bind an app to follow its profile", gui.window,
                                          kAutoSwitchStatus, 0, 0, 0, 0);

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

    for (const auto& binding : adjustment_bindings(gui)) {
        SendMessageW(binding.slider, TBM_SETRANGEMIN, FALSE,
                     setting_to_steps(binding.range.minimum));
        SendMessageW(binding.slider, TBM_SETRANGEMAX, TRUE,
                     setting_to_steps(binding.range.maximum));
    }

    if (FAILED(SetWindowTheme(gui.display_combo, L"Explorer", nullptr))) {
        log_message(LogLevel::warning, L"Could not apply the Explorer theme to the display list");
    }
    const std::array<HWND, 13> themed_controls{
        gui.gamma_slider, gui.brightness_slider, gui.contrast_slider,
        gui.r_gain_slider, gui.g_gain_slider, gui.b_gain_slider,
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
        gui.profile_rename,
    };
    for (HWND control : themed_controls) {
        if (FAILED(SetWindowTheme(control, L"Explorer", nullptr))) {
            log_message(LogLevel::warning, L"Could not apply the Explorer theme to a control");
        }
    }

    gui.apply_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     L"BUTTON", L"Saved", gui.window, kApplyButton,
                                    0, 0, 0, 0);
    gui.reset_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     L"BUTTON", L"Restore defaults", gui.window, kResetButton,
                                    0, 0, 0, 0);
    gui.before_after_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
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
    apply_font(gui.bind_application_button, gui.normal_font);
    apply_font(gui.app_rules_button, gui.normal_font);
    apply_font(gui.auto_switch_button, gui.normal_font);
    apply_font(gui.auto_switch_status, gui.small_font);
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
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(6, 6));
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
