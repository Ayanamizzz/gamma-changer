#include "gui_internal.h"

namespace gamma_changer {

// "Hold to compare": while the button is held the display shows the neutral
// response, so the profile can be judged against no adjustment at all.
LRESULT CALLBACK compare_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(button));
    if (message == WM_LBUTTONDOWN && gui && !gui->session.comparing_original) {
        if (const auto* display = selected_display(*gui)) {
            std::wstring error;
            if (gui->controller.preview(*display, default_params(), error)) {
                // Only now pause a pending edit; releasing the button resumes
                // it. A failed preview leaves its timers running untouched.
                KillTimer(gui->window, kPreviewTimer);
                KillTimer(gui->window, kAutoSaveTimer);
                gui->session.comparing_original = true;
                SetCapture(button);
                set_status(*gui, L"Showing the display without this profile", StatusTone::idle);
            } else {
                set_status(*gui, error, StatusTone::error);
            }
            InvalidateRect(button, nullptr, FALSE);
        }
    }
    // The button control takes the capture again for its own pressed state;
    // only capture moving to a different window ends the comparison.
    const bool released = message == WM_LBUTTONUP ||
                          (message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(lparam) != button);
    if (released && gui && gui->session.comparing_original) {
        gui->session.comparing_original = false;
        if (GetCapture() == button) ReleaseCapture();
        if (gui->session.dirty) {
            // Back to the unsaved edit; it saves on the usual delay.
            preview_selected(*gui);
            SetTimer(gui->window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
        } else if (cancel_active_preview(*gui)) {
            set_status(*gui, L"Ready", StatusTone::success);
        }
        InvalidateRect(button, nullptr, FALSE);
    }
    return hover_button_proc(button, message, wparam, lparam, 1, 0);
}

LRESULT CALLBACK hover_button_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                                   UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(button));
    if (message == WM_MOUSEMOVE && gui && gui->hovered_control != button) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, button, 0};
        TrackMouseEvent(&tracking);
        const HWND previous = gui->hovered_control;
        gui->hovered_control = button;
        if (previous) InvalidateRect(previous, nullptr, FALSE);
        InvalidateRect(button, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE && gui) {
        if (gui->hovered_control == button) gui->hovered_control = nullptr;
        InvalidateRect(button, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_DELETE && gui) {
        const int index = GetDlgCtrlID(button) - kAppChipIdBase;
        if (index >= 0 && index < static_cast<int>(gui->apps.chips.size())) {
            const std::wstring path = gui->apps.chips[static_cast<std::size_t>(index)].path;
            unassign_app(*gui, path);
            SetFocus(gui->add_app_button);
            return 0;
        }
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) InvalidateRect(button, nullptr, FALSE);
    if (message == WM_NCDESTROY) {
        if (gui && gui->hovered_control == button) gui->hovered_control = nullptr;
        RemoveWindowSubclass(button, hover_button_proc, 1);
        RemoveWindowSubclass(button, compare_proc, 1);
    }
    return DefSubclassProc(button, message, wparam, lparam);
}


AdjustmentBindings adjustment_bindings(const GuiState& gui) {
    return {{
        {gui.gamma_label, gui.gamma_value, gui.gamma_slider, calibration_ranges::gamma,
         &CalibrationSettings::gamma},
        {gui.brightness_label, gui.brightness_value, gui.brightness_slider,
         calibration_ranges::brightness, &CalibrationSettings::brightness},
        {gui.contrast_label, gui.contrast_value, gui.contrast_slider, calibration_ranges::contrast,
         &CalibrationSettings::contrast},
        {gui.r_gain_label, gui.r_gain_value, gui.r_gain_slider, calibration_ranges::gain,
         &CalibrationSettings::r_gain},
        {gui.g_gain_label, gui.g_gain_value, gui.g_gain_slider, calibration_ranges::gain,
         &CalibrationSettings::g_gain},
        {gui.b_gain_label, gui.b_gain_value, gui.b_gain_slider, calibration_ranges::gain,
         &CalibrationSettings::b_gain},
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
    if (!gui.gamma_slider) return;  // headless GuiState (tests, tools): no edits to read
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
            invalidate_control_background(*gui, edit, Scale{GetDpiForWindow(edit)}(12));
        }
    }
    if (message == WM_MOUSELEAVE) {
        SetWindowSubclass(edit, numeric_edit_proc, 1, 0);
        if (gui) {
            gui->hovered_numeric = nullptr;
            invalidate_control_background(*gui, edit, Scale{GetDpiForWindow(edit)}(12));
        }
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        if (gui) invalidate_control_background(*gui, edit, Scale{GetDpiForWindow(edit)}(12));
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
        HDC window_dc = BeginPaint(slider, &paint);
        RECT client{};
        GetClientRect(slider, &client);
        // Double-buffered: parent scene + track + thumb, then one blit.
        HDC dc = CreateCompatibleDC(window_dc);
        HBITMAP buffer = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
        const auto old_buffer = SelectObject(dc, buffer);
        auto* gui = state(GetParent(slider));
        if (gui) paint_parent_layer(slider, dc, *gui);

        const Scale s{GetDpiForWindow(slider)};
        const int inset = s(9);
        const float center_y = client.bottom / 2.0f;
        const int minimum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMIN, 0, 0));
        const int maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
        const int position = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        const int track_width = std::max(1, static_cast<int>(client.right) - inset * 2);
        const auto x_of = [&](int steps) {
            return inset + static_cast<float>(steps - minimum) * track_width /
                               static_cast<float>(std::max(1, maximum - minimum));
        };
        // The fill runs from the neutral value to the thumb, so an untouched
        // adjustment shows an empty track and a changed one shows by how much.
        int neutral = minimum;
        COLORREF accent = ui::Theme::primary;
        if (gui) {
            const AdjustmentBindings bindings = adjustment_bindings(*gui);
            const COLORREF channel[3] = {ui::Theme::curve_red, ui::Theme::curve_green,
                                         ui::Theme::curve_blue};
            for (std::size_t i = 0; i < bindings.size(); ++i) {
                if (bindings[i].slider != slider) continue;
                neutral = setting_to_steps(bindings[i].range.default_value);
                if (i >= 3) accent = channel[i - 3];
            }
        }
        const float thumb_x = x_of(position);
        const float neutral_x = x_of(neutral);
        const bool enabled = IsWindowEnabled(slider) != FALSE;
        const int half = s(2);
        const int track_top = static_cast<int>(center_y) - half;
        const RECT inactive{inset, track_top, client.right - inset, track_top + half * 2};
        ui::aa_fill_round_rect(dc, inactive, static_cast<float>(half), ui::Theme::track_inactive);
        if (enabled && position != neutral) {
            const RECT active{static_cast<int>(std::min(thumb_x, neutral_x)), track_top,
                              static_cast<int>(std::max(thumb_x, neutral_x)), track_top + half * 2};
            ui::aa_fill_round_rect(dc, active, static_cast<float>(half), accent);
        }
        // Neutral marker: where a double-click on the value returns to.
        ui::aa_line(dc, neutral_x, center_y - s(5), neutral_x, center_y + s(5),
                    ui::Theme::reference_curve, 1.5f);
        const bool focused = GetFocus() == slider;
        const bool hovered = data != 0;
        const float radius = static_cast<float>(s(8));
        if (enabled && (focused || hovered)) {
            ui::aa_fill_ellipse(dc, thumb_x, center_y, radius + s(5), accent, 34);
        }
        ui::aa_fill_ellipse(dc, thumb_x, center_y + 1.0f, radius, RGB(40, 20, 60), 30);
        ui::aa_fill_ellipse(dc, thumb_x, center_y, radius, RGB(255, 255, 255));
        ui::aa_stroke_ellipse(dc, thumb_x, center_y, radius - 1.0f,
                              enabled ? accent : ui::Theme::disabled_text, 2.0f);

        BitBlt(window_dc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_buffer);
        DeleteObject(buffer);
        DeleteDC(dc);
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
    for (const auto& binding : adjustment_bindings(gui)) {
        EnableWindow(binding.slider, enabled ? TRUE : FALSE);
        EnableWindow(binding.edit, enabled ? TRUE : FALSE);
    }
    EnableWindow(gui.reset_button, enabled ? TRUE : FALSE);
    EnableWindow(gui.before_after_button, enabled ? TRUE : FALSE);
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


// Paints the closed display selector as a sidebar field (the native white
// combo box clashes with the dark sidebar). The drop-down list stays native.
LRESULT CALLBACK display_combo_proc(HWND combo, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(combo));
    if (message == WM_PAINT && gui) {
        PAINTSTRUCT paint{};
        HDC window_dc = BeginPaint(combo, &paint);
        RECT client{};
        GetClientRect(combo, &client);
        HDC dc = CreateCompatibleDC(window_dc);
        HBITMAP buffer = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
        const auto old_buffer = SelectObject(dc, buffer);
        paint_parent_layer(combo, dc, *gui);

        const Scale s{GetDpiForWindow(combo)};
        const float radius = static_cast<float>(s(ui::Metrics::control_radius));
        const bool enabled = IsWindowEnabled(combo) != FALSE;
        const bool focused = GetFocus() == combo ||
                             SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
        ui::aa_fill_round_rect(dc, client, radius, ui::Theme::sidebar_field, enabled ? 255 : 140);
        ui::aa_stroke_round_rect(dc, client, radius,
                                 focused ? ui::Theme::primary : ui::Theme::sidebar_field_border,
                                 focused ? 1.5f : 1.0f);
        std::wstring text;
        const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR) {
            const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, selected, 0);
            if (length > 0) {
                text.resize(static_cast<std::size_t>(length) + 1);
                SendMessageW(combo, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(text.data()));
                text.resize(static_cast<std::size_t>(length));
            }
        } else if (!enabled) {
            text = L"No display";
        }
        RECT label{client.left + s(12), client.top, client.right - s(32), client.bottom};
        const auto old_font = SelectObject(dc, gui->normal_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? ui::Theme::sidebar_text : ui::Theme::sidebar_muted);
        DrawTextW(dc, text.c_str(), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old_font);
        ui::draw_chevron_down(dc, client.right - s(16), client.bottom / 2, s(4),
                              ui::Theme::sidebar_secondary);

        BitBlt(window_dc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_buffer);
        DeleteObject(buffer);
        DeleteDC(dc);
        EndPaint(combo, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == CB_SETCURSEL) {
        const LRESULT result = DefSubclassProc(combo, message, wparam, lparam);
        InvalidateRect(combo, nullptr, FALSE);
        return result;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(combo, display_combo_proc, 1);
    return DefSubclassProc(combo, message, wparam, lparam);
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
    gui.background_brush = CreateSolidBrush(ui::Theme::card);
    gui.card_brush = CreateSolidBrush(ui::Theme::control_surface);
    gui.sidebar_brush = CreateSolidBrush(ui::Theme::sidebar_field);

    const auto label = [&](const wchar_t* text, int id, DWORD extra = 0) {
        return make_control(WS_CHILD | WS_VISIBLE | SS_NOPREFIX | extra, L"STATIC", text,
                            gui.window, id, 0, 0, 0, 0);
    };
    const auto owner_button = [&](const wchar_t* text, int id, bool hover = false) {
        HWND button = make_control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, L"BUTTON",
                                   text, gui.window, id, 0, 0, 0, 0);
        if (button && hover) SetWindowSubclass(button, hover_button_proc, 1, 0);
        return button;
    };

    // Sidebar: display, profiles, switches. Tab order follows creation order.
    gui.display_caption = label(L"Display", kDisplayCaption);
    gui.refresh_button = owner_button(L"Refresh displays", kRefreshButton, true);
    gui.display_combo = make_control(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                                     WC_COMBOBOXW, nullptr, gui.window, kDisplayCombo,
                                     0, 0, 0, 0);
    SendMessageW(gui.display_combo, CB_SETMINVISIBLE, 8, 0);
    SetWindowSubclass(gui.display_combo, display_combo_proc, 1, 0);
    gui.display_status = label(L"Detecting displays...", kDisplayStatus, SS_ENDELLIPSIS);
    gui.preset_caption = label(L"Profiles", kPresetCaption);
    gui.preset_save = owner_button(L"New profile", kPresetSave, true);
    gui.profile_rename = make_control(WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS,
                                      L"EDIT", L"", gui.window, kProfileRename, 0, 0, 0, 0);
    SendMessageW(gui.profile_rename, EM_SETLIMITTEXT, 48, 0);
    SetWindowSubclass(gui.profile_rename, profile_rename_proc, 1, 0);
    gui.auto_switch_button = owner_button(L"Follow apps", kAutoSwitchToggle);
    gui.auto_switch_status = label(L"", kAutoSwitchStatus, SS_ENDELLIPSIS);
    gui.startup_button = owner_button(L"Start with Windows", kStartupToggle);

    // Work area: the selected profile.
    gui.main_heading = label(L"", kMainHeading, SS_ENDELLIPSIS);
    gui.main_subtitle = label(L"", kMainSubtitle, SS_ENDELLIPSIS);
    gui.before_after_button = owner_button(L"Hold to compare", kBeforeAfter);
    SetWindowSubclass(gui.before_after_button, compare_proc, 1, 0);
    gui.reset_button = owner_button(L"Reset", kResetButton, true);
    gui.profile_menu_button = owner_button(L"Profile actions", kProfileMenu, true);

    const auto make_slider = [&](int id) {
        return make_control(WS_CHILD | WS_VISIBLE | TBS_NOTICKS | WS_TABSTOP, TRACKBAR_CLASSW,
                            nullptr, gui.window, id, 0, 0, 0, 0);
    };
    const auto make_value = [&](int id) {
        return make_control_ex(0, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                               L"EDIT", L"1.00", gui.window, id, 0, 0, 0, 0);
    };
    // Created in reading order (brightness, contrast, gamma, red, green, blue)
    // so Tab moves down the card.
    gui.brightness_label = label(L"Brightness", 0);
    gui.brightness_slider = make_slider(kBrightnessSlider);
    gui.brightness_value = make_value(kBrightnessValue);
    gui.contrast_label = label(L"Contrast", 0);
    gui.contrast_slider = make_slider(kContrastSlider);
    gui.contrast_value = make_value(kContrastValue);
    gui.gamma_label = label(L"Gamma", 0);
    gui.gamma_slider = make_slider(kGammaSlider);
    gui.gamma_value = make_value(kGammaValue);
    gui.r_gain_label = label(L"Red", 0);
    gui.r_gain_slider = make_slider(kRGainSlider);
    gui.r_gain_value = make_value(kRGainValue);
    gui.g_gain_label = label(L"Green", 0);
    gui.g_gain_slider = make_slider(kGGainSlider);
    gui.g_gain_value = make_value(kGGainValue);
    gui.b_gain_label = label(L"Blue", 0);
    gui.b_gain_slider = make_slider(kBGainSlider);
    gui.b_gain_value = make_value(kBGainValue);

    for (const auto& binding : adjustment_bindings(gui)) {
        SendMessageW(binding.slider, TBM_SETRANGEMIN, FALSE,
                     setting_to_steps(binding.range.minimum));
        SendMessageW(binding.slider, TBM_SETRANGEMAX, TRUE,
                     setting_to_steps(binding.range.maximum));
        SetWindowSubclass(binding.slider, slider_proc, 1, 0);
        SendMessageW(binding.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SetWindowSubclass(binding.edit, numeric_edit_proc, 1, 0);
    }

    // Apps card: chips are created on demand by ensure_app_chip_buttons().
    gui.apps_heading = label(L"Apps that switch to this profile", kAppsHeading, SS_ENDELLIPSIS);
    gui.apps_note = label(L"", kAppsNote, SS_RIGHT | SS_ENDELLIPSIS);
    gui.other_apps_button = owner_button(L"All other apps", kOtherApps, true);
    gui.app_overflow_button = owner_button(L"", kAppOverflow, true);
    gui.add_app_button = owner_button(L"Add app", kAddApp, true);

    // Status line
    gui.status = label(L"Ready", kStatusLabel, SS_ENDELLIPSIS);
    gui.apply_button = owner_button(L"Retry save", kApplyButton, true);
    ShowWindow(gui.apply_button, SW_HIDE);

    recreate_fonts(gui, GetDpiForWindow(gui.window));
    RECT client{};
    GetClientRect(gui.window, &client);
    layout_controls(gui, client.right - client.left, client.bottom - client.top);
}

}  // namespace gamma_changer
