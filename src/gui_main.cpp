#include "app_version.h"
#include "calibration_controller.h"
#include "display_manager.h"
#include "gamma_lut.h"
#include "instance_manager.h"
#include "logger.h"
#include "profile_store.h"
#include "profile_manager.h"
#include "startup_manager.h"
#include "resource.h"
#include "ui_rendering.h"
#include "ui_theme.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

using namespace gamma_changer;

#if defined(_MSC_VER)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

constexpr wchar_t kWindowClass[] = L"GammaChangerCppWindow";
constexpr UINT kPreviewTimer = 1;
constexpr UINT kDisplayRefreshTimer = 2;
constexpr UINT kAutoSaveTimer = 3;
constexpr UINT kRecoveryRetryTimer = 4;
constexpr UINT kAutoSaveDelayMs = 700;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kProfileRenameMessage = WM_APP + 2;
constexpr UINT kProfileContextMessage = WM_APP + 3;
constexpr UINT kProfileCommitRenameMessage = WM_APP + 4;
constexpr UINT kUndoProfileSwitchMessage = WM_APP + 5;
constexpr UINT kTrayId = 1;
constexpr UINT kTrayShowCommand = 1;
constexpr UINT kTrayExitCommand = 2;
constexpr UINT kTrayProfileCommandBase = 100;

enum ControlId : int {
    kTitleLabel = 100,
    kSubtitleLabel = 101,
    kDisplayCombo = 102,
    kRefreshButton = 103,
    kGammaSlider = 104,
    kBrightnessSlider = 105,
    kContrastSlider = 106,
    kRGainSlider = 107,
    kGGainSlider = 108,
    kBGainSlider = 109,
    kGammaValue = 110,
    kBrightnessValue = 111,
    kContrastValue = 112,
    kRGainValue = 113,
    kGGainValue = 114,
    kBGainValue = 115,
    kApplyButton = 116,
    kResetButton = 117,
    kStatusLabel = 118,
    kPresetSlot1 = 119,
    kPresetSlot2 = 120,
    kPresetSlot3 = 121,
    kPresetSlot4 = 122,
    kPresetSave = 124,
    kPresetDelete = 126,
    kDisplayCaption = 127,
    kPresetCaption = 128,
    kMainHeading = 129,
    kMainSubtitle = 130,
    kToneHeading = 131,
    kColorHeading = 132,
    kStartupToggle = 133,
    kPreviewHeading = 134,
    kPreviewCaption = 135,
    kToneCaption = 136,
    kColorCaption = 137,
    kDisplayStatus = 138,
    kProfileRename = 139,
    kBeforeAfter = 140,
};

enum class StatusTone {
    idle,
    success,
    warning,
    error,
};

struct GuiState {
    HWND window = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND display_combo = nullptr;
    HWND refresh_button = nullptr;
    HWND display_caption = nullptr;
    HWND preset_caption = nullptr;
    HWND main_heading = nullptr;
    HWND main_subtitle = nullptr;
    HWND tone_heading = nullptr;
    HWND color_heading = nullptr;
    HWND preview_heading = nullptr;
    HWND preview_caption = nullptr;
    HWND tone_caption = nullptr;
    HWND color_caption = nullptr;
    HWND display_status = nullptr;
    HWND startup_button = nullptr;
    HWND gamma_label = nullptr;
    HWND brightness_label = nullptr;
    HWND contrast_label = nullptr;
    HWND r_gain_label = nullptr;
    HWND g_gain_label = nullptr;
    HWND b_gain_label = nullptr;
    HWND gamma_slider = nullptr;
    HWND brightness_slider = nullptr;
    HWND contrast_slider = nullptr;
    HWND r_gain_slider = nullptr;
    HWND g_gain_slider = nullptr;
    HWND b_gain_slider = nullptr;
    HWND gamma_value = nullptr;
    HWND brightness_value = nullptr;
    HWND contrast_value = nullptr;
    HWND r_gain_value = nullptr;
    HWND g_gain_value = nullptr;
    HWND b_gain_value = nullptr;
    HWND apply_button = nullptr;
    HWND reset_button = nullptr;
    HWND status = nullptr;
    std::array<HWND, kPresetCount> preset_buttons{};
    HWND preset_save = nullptr;
    HWND preset_delete = nullptr;
    HWND profile_rename = nullptr;
    HWND before_after_button = nullptr;
    HFONT normal_font = nullptr;
    HFONT title_font = nullptr;
    HFONT heading_font = nullptr;
    HFONT panel_font = nullptr;
    HFONT section_font = nullptr;
    HFONT caption_font = nullptr;
    HFONT small_font = nullptr;
    HBRUSH background_brush = nullptr;
    HBRUSH card_brush = nullptr;
    HBRUSH sidebar_brush = nullptr;
    HDC paint_buffer_dc = nullptr;
    HBITMAP paint_buffer_bitmap = nullptr;
    HGDIOBJ paint_buffer_original = nullptr;
    int paint_buffer_width = 0;
    int paint_buffer_height = 0;
    ui::BackgroundRenderer background;
    bool syncing_controls = false;
    bool live_preview = true;
    bool startup_enabled = false;
    bool startup_launch = false;
    bool dirty = false;
    HWND hovered_profile = nullptr;
    HWND hovered_numeric = nullptr;
    bool profile_keyboard_focus = false;
    StatusTone status_tone = StatusTone::success;
    std::vector<DisplayInfo> displays;
    int active_display_index = -1;
    std::array<PresetSlot, kPresetCount> presets{};
    std::size_t active_preset = 0;
    std::size_t renaming_preset = kPresetCount;
    bool profile_switch_undo_available = false;
    std::size_t undo_preset = 0;
    GammaParams undo_params{};
    PresetSlot undo_slot{};
    CalibrationSettings committed_settings{};
    ProfileStore store;
    CalibrationController controller{store};
    ProfileManager profile_manager{store};
    bool reapply_after_display_refresh = false;
    int recovery_retry_attempt = 0;
    std::unordered_map<std::wstring, CalibrationSettings> pending_adjustments;
    bool comparing_original = false;
    bool profile_store_available = true;
};

GuiState* state(HWND window) {
    return reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

GammaParams params_from_sliders(const GuiState& gui);
void begin_profile_rename(GuiState& gui, std::size_t index);
void layout_controls(GuiState& gui, int width, int height);
bool cancel_active_preview(GuiState& gui);
void preview_selected(GuiState& gui);
void set_status(GuiState& gui, const std::wstring& text, StatusTone tone);
const DisplayInfo* selected_display(const GuiState& gui);

void invalidate_control_background(GuiState& gui, HWND control, int padding = 2) {
    if (!gui.window || !control) return;
    RECT rect{};
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, gui.window, reinterpret_cast<POINT*>(&rect), 2);
    InflateRect(&rect, padding, padding);
    RedrawWindow(gui.window, &rect, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void invalidate_preview_curve(GuiState& gui) {
    if (!gui.window) return;
    RECT client{};
    GetClientRect(gui.window, &client);
    const UINT dpi = GetDpiForWindow(gui.window);
    const auto scale = [dpi](int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const int sidebar_width = scale(ui::Metrics::sidebar_width);
    RECT curve{sidebar_width + scale(32), scale(130), client.right - scale(32), scale(274)};
    InvalidateRect(gui.window, &curve, FALSE);
}

void invalidate_status_area(GuiState& gui) {
    if (!gui.window) return;
    invalidate_control_background(gui, gui.status, 3);
    RECT client{};
    GetClientRect(gui.window, &client);
    const UINT dpi = GetDpiForWindow(gui.window);
    const auto scale = [dpi](int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const int main_x = scale(ui::Metrics::sidebar_width) + scale(32);
    RECT indicator{main_x, client.bottom - scale(58), main_x + scale(14),
                   client.bottom - scale(39)};
    InvalidateRect(gui.window, &indicator, FALSE);
}

LRESULT CALLBACK profile_rename_proc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(edit));
    if (message == WM_KEYDOWN && wparam == VK_RETURN && gui) {
        PostMessageW(gui->window, kProfileCommitRenameMessage, TRUE, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && gui) {
        PostMessageW(gui->window, kProfileCommitRenameMessage, FALSE, 0);
        return 0;
    }
    if (message == WM_KILLFOCUS && gui && IsWindowVisible(edit)) {
        PostMessageW(gui->window, kProfileCommitRenameMessage, TRUE, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, profile_rename_proc, 1);
    return DefSubclassProc(edit, message, wparam, lparam);
}

LRESULT CALLBACK compare_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR, DWORD_PTR) {
    auto* gui = state(GetParent(button));
    if (message == WM_LBUTTONDOWN && gui && gui->live_preview && gui->dirty) {
        if (cancel_active_preview(*gui)) {
            gui->comparing_original = true;
            SetCapture(button);
            set_status(*gui, L"Showing the committed result", StatusTone::idle);
            InvalidateRect(button, nullptr, FALSE);
        }
    }
    if ((message == WM_LBUTTONUP || message == WM_CAPTURECHANGED) && gui &&
        gui->comparing_original) {
        gui->comparing_original = false;
        if (GetCapture() == button) ReleaseCapture();
        preview_selected(*gui);
        InvalidateRect(button, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(button, compare_proc, 1);
    return DefSubclassProc(button, message, wparam, lparam);
}

void set_status(GuiState& gui, const std::wstring& text,
                StatusTone tone = StatusTone::idle) {
    gui.status_tone = tone;
    SetWindowTextW(gui.status, text.c_str());
    invalidate_status_area(gui);
    if (tone == StatusTone::error) log_message(LogLevel::error, text);
}

void mark_changed(GuiState& gui) {
    gui.dirty = !settings_equal(params_from_sliders(gui), gui.committed_settings);
    EnableWindow(gui.before_after_button, gui.dirty && gui.live_preview ? TRUE : FALSE);
    if (gui.dirty && gui.profile_store_available) {
        SetWindowTextW(gui.apply_button, L"Saving...");
        EnableWindow(gui.apply_button, FALSE);
        SetTimer(gui.window, kAutoSaveTimer, kAutoSaveDelayMs, nullptr);
    } else {
        KillTimer(gui.window, kAutoSaveTimer);
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
    }
    if (gui.profile_switch_undo_available) {
        set_status(gui, L"Profile switched  |  Ctrl+Z restores previous adjustments",
                   StatusTone::success);
    } else {
        set_status(gui,
                   gui.dirty
                       ? (gui.live_preview ? L"Previewing changes live" : L"Changes ready to apply")
                       : L"Ready",
                   gui.dirty && gui.live_preview ? StatusTone::success : StatusTone::idle);
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

void set_slider(HWND slider, int value) {
    SendMessageW(slider, TBM_SETPOS, TRUE, value);
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
    value = std::clamp(value, minimum, maximum);
    gui.syncing_controls = true;
    set_slider(slider, static_cast<int>(value * 100.0));
    gui.syncing_controls = false;
    mark_changed(gui);
}

void normalize_edit(GuiState& gui, HWND edit, HWND slider, double minimum, double maximum) {
    double value = 0.0;
    if (!read_edit(edit, value) || !std::isfinite(value)) {
        gui.syncing_controls = true;
        set_edit(edit, slider_value(slider) / 100.0);
        gui.syncing_controls = false;
        return;
    }
    value = std::clamp(value, minimum, maximum);
    gui.syncing_controls = true;
    set_slider(slider, static_cast<int>(value * 100.0));
    set_edit(edit, value);
    gui.syncing_controls = false;
}

void normalize_all_edits(GuiState& gui) {
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
            set_slider(slider, static_cast<int>(value * 100.0));
            set_edit(edit, value);
            gui->syncing_controls = false;
            mark_changed(*gui);
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, numeric_edit_proc, 1);
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
    if ((message == WM_LBUTTONDBLCLK || (message == WM_KEYDOWN && wparam == VK_F2)) && gui) {
        const int index = GetDlgCtrlID(item) - kPresetSlot1;
        if (index >= 0 && index < static_cast<int>(kPresetCount)) {
            PostMessageW(gui->window, kProfileRenameMessage, static_cast<WPARAM>(index), 0);
            return 0;
        }
    }
    if (message == WM_CONTEXTMENU && gui) {
        const int index = GetDlgCtrlID(item) - kPresetSlot1;
        if (index >= 0 && index < static_cast<int>(kPresetCount)) {
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

void paint_parent_layer(HWND control, HDC dc, GuiState& gui) {
    RECT control_rect{};
    GetWindowRect(control, &control_rect);
    MapWindowPoints(nullptr, gui.window, reinterpret_cast<POINT*>(&control_rect), 2);
    RECT client{};
    GetClientRect(gui.window, &client);
    const UINT dpi = GetDpiForWindow(gui.window);
    const int sidebar_width = MulDiv(ui::Metrics::sidebar_width, static_cast<int>(dpi), 96);
    const int saved = SaveDC(dc);
    SetViewportOrgEx(dc, -control_rect.left, -control_rect.top, nullptr);
    gui.background.draw(dc, client, sidebar_width);
    RestoreDC(dc, saved);
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
                 enabled && gui.dirty && gui.live_preview ? TRUE : FALSE);
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

void refresh_preset_buttons(GuiState& gui) {
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const std::wstring text = gui.presets[i].occupied && !gui.presets[i].name.empty()
                                      ? gui.presets[i].name
                                      : L"Empty profile";
        SetWindowTextW(gui.preset_buttons[i], text.c_str());
        invalidate_control_background(gui, gui.preset_buttons[i], 0);
    }
    const bool can_create = std::any_of(gui.presets.begin(), gui.presets.end(),
                                        [](const PresetSlot& slot) { return !slot.occupied; });
    const bool can_delete = gui.profile_store_available && gui.active_preset < kPresetCount &&
                            gui.presets[gui.active_preset].occupied;
    EnableWindow(gui.preset_save,
                 gui.profile_store_available && can_create ? TRUE : FALSE);
    EnableWindow(gui.preset_delete, can_delete ? TRUE : FALSE);
}

bool persist_presets(GuiState& gui);
bool save_active_preset(GuiState& gui);
bool cancel_active_preview(GuiState& gui);

void begin_profile_rename(GuiState& gui, std::size_t index) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return;
    }
    if (index >= kPresetCount || !gui.presets[index].occupied) {
        set_status(gui, L"Create this profile before renaming it", StatusTone::warning);
        return;
    }
    gui.renaming_preset = index;
    SetWindowTextW(gui.profile_rename, gui.presets[index].name.c_str());
    RECT client{};
    GetClientRect(gui.window, &client);
    layout_controls(gui, client.right, client.bottom);
    ShowWindow(gui.profile_rename, SW_SHOW);
    SetFocus(gui.profile_rename);
    SendMessageW(gui.profile_rename, EM_SETSEL, 0, -1);
}

void finish_profile_rename(GuiState& gui, bool commit) {
    if (gui.renaming_preset >= kPresetCount) return;
    const std::size_t index = gui.renaming_preset;
    gui.renaming_preset = kPresetCount;
    ShowWindow(gui.profile_rename, SW_HIDE);
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
    const std::wstring previous = gui.presets[index].name;
    gui.presets[index].name = name;
    if (!persist_presets(gui)) gui.presets[index].name = previous;
    else set_status(gui, name + L" renamed", StatusTone::success);
    refresh_preset_buttons(gui);
    SetFocus(gui.preset_buttons[index]);
}

void duplicate_profile(GuiState& gui, std::size_t source_index) {
    if (source_index >= kPresetCount || !gui.presets[source_index].occupied) {
        set_status(gui, L"Only saved profiles can be duplicated", StatusTone::warning);
        return;
    }
    const auto empty = std::find_if(gui.presets.begin(), gui.presets.end(),
                                    [](const PresetSlot& slot) { return !slot.occupied; });
    if (empty == gui.presets.end()) {
        set_status(gui, L"Remove a profile before duplicating another", StatusTone::warning);
        return;
    }
    const auto previous = *empty;
    *empty = gui.presets[source_index];
    empty->name += L" Copy";
    if (empty->name.size() > 48) empty->name.resize(48);
    if (!persist_presets(gui)) *empty = previous;
    else set_status(gui, empty->name + L" created", StatusTone::success);
    refresh_preset_buttons(gui);
}

void create_profile_from_current(GuiState& gui) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return;
    }
    const auto empty = std::find_if(gui.presets.begin(), gui.presets.end(),
                                    [](const PresetSlot& slot) { return !slot.occupied; });
    if (empty == gui.presets.end()) {
        set_status(gui, L"Remove a profile before creating another", StatusTone::warning);
        return;
    }
    const std::size_t index = static_cast<std::size_t>(empty - gui.presets.begin());
    const PresetSlot previous = *empty;
    gui.active_preset = index;
    auto& slot = gui.presets[index];
    slot.occupied = true;
    slot.name = L"Custom " + std::to_wstring(index + 1);
    slot.params = params_from_sliders(gui);
    if (persist_presets(gui)) {
        refresh_preset_buttons(gui);
        set_status(gui, slot.name + L" created", StatusTone::success);
    } else {
        *empty = previous;
    }
}

bool persist_presets(GuiState& gui) {
    if (!gui.profile_store_available) {
        set_status(gui, L"Profiles are read-only until the damaged profile file is repaired",
                   StatusTone::error);
        return false;
    }
    std::wstring error;
    if (!gui.profile_manager.update_legacy_slots(gui.presets, error)) {
        set_status(gui, L"Preset save failed: " + error, StatusTone::error);
        return false;
    }
    return true;
}

bool apply_selected(GuiState& gui);
bool save_and_apply_current(GuiState& gui, bool automatic);

void select_preset(GuiState& gui, std::size_t index) {
    if (index >= kPresetCount) return;
    if (index == gui.active_preset) return;
    KillTimer(gui.window, kAutoSaveTimer);
    if (gui.dirty && !save_and_apply_current(gui, true)) return;
    if (!cancel_active_preview(gui)) return;
    gui.profile_switch_undo_available = true;
    gui.undo_preset = gui.active_preset;
    gui.undo_params = params_from_sliders(gui);
    gui.undo_slot = gui.presets[gui.active_preset];
    gui.active_preset = index;
    const GammaParams next_params = gui.presets[index].occupied
                                        ? gui.presets[index].params
                                        : default_params();
    set_params_to_controls(gui, next_params);
    const auto* display = selected_display(gui);
    std::wstring error;
    if (!display || !gui.controller.apply_and_save(*display, next_params, error)) {
        gui.active_preset = gui.undo_preset;
        gui.profile_switch_undo_available = false;
        set_params_to_controls(gui, gui.undo_params);
        refresh_preset_buttons(gui);
        set_status(gui, display ? L"Profile switch failed: " + error : L"No display selected",
                   StatusTone::error);
        return;
    }
    gui.committed_settings = next_params;
    gui.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, gui.presets[index].occupied
                        ? gui.presets[index].name + L" applied  |  Ctrl+Z to go back"
                        : L"Empty profile selected  |  Adjust a value to create it",
               StatusTone::success);
}

void undo_profile_switch(GuiState& gui) {
    if (!gui.profile_switch_undo_available) {
        set_status(gui, L"Nothing to restore", StatusTone::idle);
        return;
    }
    if (!cancel_active_preview(gui)) return;
    const std::size_t preset = gui.undo_preset;
    const GammaParams params = gui.undo_params;
    const PresetSlot slot = gui.undo_slot;
    gui.profile_switch_undo_available = false;
    gui.active_preset = preset;
    const PresetSlot current_slot = gui.presets[preset];
    gui.presets[preset] = slot;
    set_params_to_controls(gui, params);
    const auto* display = selected_display(gui);
    std::wstring error;
    if (!display || !gui.controller.apply_and_save(*display, params, error)) {
        gui.presets[preset] = current_slot;
        set_status(gui, display ? L"Could not restore previous adjustments: " + error
                                : L"No display selected",
                   StatusTone::error);
        refresh_preset_buttons(gui);
        return;
    }
    if (!persist_presets(gui)) {
        gui.presets[preset] = current_slot;
        if (display) {
            const CommitResult rollback = gui.controller.commit(*display, gui.committed_settings);
            if (!rollback.succeeded()) {
                set_status(gui, L"Could not roll back the display after profile save failed: " +
                                rollback.error, StatusTone::error);
            }
        }
        refresh_preset_buttons(gui);
        return;
    }
    gui.committed_settings = params;
    gui.dirty = false;
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, L"Previous profile adjustments restored", StatusTone::success);
}

bool save_active_preset(GuiState& gui) {
    if (gui.active_preset >= kPresetCount) return false;
    const PresetSlot previous = gui.presets[gui.active_preset];
    gui.presets[gui.active_preset].occupied = true;
    if (gui.presets[gui.active_preset].name.empty()) {
        gui.presets[gui.active_preset].name = L"Custom " +
                                              std::to_wstring(gui.active_preset + 1);
    }
    gui.presets[gui.active_preset].params = params_from_sliders(gui);
    if (persist_presets(gui)) {
        gui.profile_switch_undo_available = false;
        refresh_preset_buttons(gui);
        set_status(gui, gui.presets[gui.active_preset].name + L" saved", StatusTone::success);
        return true;
    }
    gui.presets[gui.active_preset] = previous;
    refresh_preset_buttons(gui);
    return false;
}

bool save_and_apply_current(GuiState& gui, bool automatic) {
    if (!gui.dirty) return true;
    const PresetSlot previous = gui.presets[gui.active_preset];
    const CalibrationSettings previous_settings = gui.committed_settings;
    auto& slot = gui.presets[gui.active_preset];
    slot.occupied = true;
    if (slot.name.empty()) {
        slot.name = L"Custom " + std::to_wstring(gui.active_preset + 1);
    }
    slot.params = params_from_sliders(gui);
    const auto* display = selected_display(gui);
    if (!display) {
        slot = previous;
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }
    const CommitResult commit = gui.controller.commit(*display, slot.params);
    if (!commit.succeeded()) {
        slot = previous;
        refresh_preset_buttons(gui);
        set_status(gui, commit.error, StatusTone::error);
        SetWindowTextW(gui.apply_button, L"Retry save");
        EnableWindow(gui.apply_button, TRUE);
        return false;
    }
    if (!persist_presets(gui)) {
        slot = previous;
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
    gui.committed_settings = slot.params;
    gui.profile_switch_undo_available = false;
    gui.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    set_status(gui, automatic ? L"Saved automatically" : L"Saved", StatusTone::success);
    return true;
}

std::wstring display_storage_id(const DisplayInfo& display) {
    return display.stable_id.empty() ? display.device_name : display.stable_id;
}

bool preserve_pending_adjustment(GuiState& gui) {
    if (!gui.dirty) return true;
    const DisplayInfo* display = selected_display(gui);
    if (!display) return false;
    KillTimer(gui.window, kPreviewTimer);
    KillTimer(gui.window, kAutoSaveTimer);
    const CalibrationSettings settings = params_from_sliders(gui);
    const PresetSlot previous = gui.presets[gui.active_preset];
    auto& slot = gui.presets[gui.active_preset];
    slot.occupied = true;
    if (slot.name.empty()) slot.name = L"Custom " + std::to_wstring(gui.active_preset + 1);
    slot.params = settings;
    std::wstring error;
    if (!gui.controller.save_for_offline_display(*display, settings, error)) {
        slot = previous;
        set_status(gui, L"Could not preserve pending adjustments: " + error,
                   StatusTone::error);
        return false;
    }
    if (!persist_presets(gui)) {
        slot = previous;
        std::wstring rollback_error;
        gui.controller.save_for_offline_display(*display, gui.committed_settings,
                                                rollback_error);
        return false;
    }
    gui.pending_adjustments[display_storage_id(*display)] = settings;
    gui.committed_settings = settings;
    gui.dirty = false;
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
    return !gui.dirty || save_and_apply_current(gui, true);
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

void delete_preset(GuiState& gui, std::size_t index) {
    if (index >= kPresetCount || !gui.presets[index].occupied) {
        set_status(gui, L"The selected default is already empty", StatusTone::warning);
        return;
    }
    int choice = IDCANCEL;
    TaskDialog(gui.window, nullptr, L"Clear selected preset?",
               L"The saved values in this slot will be removed.",
               L"Your current display adjustments will not change.",
               TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, TD_WARNING_ICON, &choice);
    if (choice != IDYES) return;
    const PresetSlot previous = gui.presets[index];
    gui.presets[index] = PresetSlot{};
    if (!persist_presets(gui)) {
        gui.presets[index] = previous;
        refresh_preset_buttons(gui);
        return;
    }
    refresh_preset_buttons(gui);
    set_status(gui, L"Default " + std::to_wstring(index + 1) + L" cleared",
               StatusTone::success);
}

void delete_active_preset(GuiState& gui) {
    delete_preset(gui, gui.active_preset);
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
    gui.active_display_index = index;
    gui.committed_settings = gui.controller.load_settings(gui.displays[index]);
    set_params_to_controls(gui, gui.committed_settings);
    gui.dirty = false;
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

bool apply_selected(GuiState& gui) {
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }

    const GammaParams params = params_from_sliders(gui);
    std::wstring error;
    if (!gui.controller.apply_and_save(*display, params, error)) {
        set_status(gui, error, StatusTone::error);
        return false;
    }
    gui.committed_settings = params;
    gui.profile_switch_undo_available = false;
    gui.dirty = false;
    KillTimer(gui.window, kAutoSaveTimer);
    SetWindowTextW(gui.apply_button, L"Saved");
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    set_status(gui, L"Applied successfully", StatusTone::success);
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
    if (!gui.profile_switch_undo_available) {
        set_status(gui, L"Previewing changes live", StatusTone::success);
    }
}

void reset_selected(GuiState& gui) {
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return;
    }

    if (!cancel_active_preview(gui)) return;
    gui.profile_switch_undo_available = true;
    gui.undo_preset = gui.active_preset;
    gui.undo_params = params_from_sliders(gui);
    gui.undo_slot = gui.presets[gui.active_preset];
    KillTimer(gui.window, kAutoSaveTimer);
    set_params_to_controls(gui, default_params());
    gui.dirty = !settings_equal(default_params(), gui.committed_settings);
    if (!gui.dirty) {
        SetWindowTextW(gui.apply_button, L"Saved");
        EnableWindow(gui.apply_button, FALSE);
        set_status(gui, L"Default settings are already active", StatusTone::idle);
        return;
    }
    SetWindowTextW(gui.apply_button, L"Saving...");
    EnableWindow(gui.apply_button, FALSE);
    if (save_and_apply_current(gui, true)) {
        gui.profile_switch_undo_available = true;
        set_status(gui, L"Default settings restored  |  Ctrl+Z to undo",
                   StatusTone::success);
    }
}

void refresh_displays(GuiState& gui) {
    preserve_pending_adjustment(gui);
    const bool preview_restored = cancel_active_preview(gui);
    if (!preview_restored) gui.controller.abandon_preview();
    std::wstring previous_device;
    std::wstring previous_stable_id;
    if (const auto* display = selected_display(gui)) {
        previous_device = display->device_name;
        previous_stable_id = display->stable_id;
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
        load_selected_profile(gui);
        const std::wstring selected_id = display_storage_id(gui.displays[selected]);
        const auto pending = gui.pending_adjustments.find(selected_id);
        if (pending != gui.pending_adjustments.end()) {
            std::wstring error;
            if (gui.controller.reapply_committed(gui.displays[selected], pending->second, error)) {
                gui.committed_settings = pending->second;
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
            set_status(gui,
                       preview_restored ? L"Ready" : L"Display changed; preview was discarded",
                       preview_restored ? StatusTone::success : StatusTone::warning);
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

void add_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"Gamma Changer");
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        log_message(LogLevel::warning, L"Could not add the system tray icon");
        return;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void remove_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void show_main_window(HWND window) {
    if (!IsWindow(window)) return;
    ShowWindow(window, SW_RESTORE);
    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
}

bool apply_profile_from_tray(GuiState& gui, std::size_t index) {
    if (index >= kPresetCount || !gui.presets[index].occupied) return false;
    if (gui.dirty) {
        KillTimer(gui.window, kAutoSaveTimer);
        if (!save_and_apply_current(gui, true)) {
            show_main_window(gui.window);
            return false;
        }
    }
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }

    std::wstring error;
    if (!gui.controller.apply_and_save(*display, gui.presets[index].params, error)) {
        set_status(gui, L"Tray profile apply failed: " + error, StatusTone::error);
        return false;
    }
    gui.active_preset = index;
    gui.committed_settings = gui.presets[index].params;
    set_params_to_controls(gui, gui.committed_settings);
    gui.dirty = false;
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, gui.presets[index].name + L" applied", StatusTone::success);
    log_message(LogLevel::info, L"Tray profile applied: " + gui.presets[index].name);
    return true;
}

void show_tray_menu(HWND window) {
    auto* gui = state(window);
    HMENU menu = CreatePopupMenu();
    HMENU profiles = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayShowCommand, L"Show Window");
    if (gui) {
        for (std::size_t i = 0; i < kPresetCount; ++i) {
            if (!gui->presets[i].occupied) continue;
            std::wstring name = gui->presets[i].name;
            std::size_t ampersand = 0;
            while ((ampersand = name.find(L'&', ampersand)) != std::wstring::npos) {
                name.insert(ampersand, 1, L'&');
                ampersand += 2;
            }
            const UINT flags = MF_STRING | (i == gui->active_preset ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(profiles, flags, kTrayProfileCommandBase + static_cast<UINT>(i),
                        name.c_str());
        }
    }
    if (GetMenuItemCount(profiles) == 0) {
        AppendMenuW(profiles, MF_STRING | MF_GRAYED, 0, L"No saved profiles");
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), L"Profiles");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenuEx(menu,
                                          TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                          point.x, point.y, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kTrayShowCommand) {
        show_main_window(window);
    } else if (command == kTrayExitCommand) {
        SendMessageW(window, WM_CLOSE, 0, 0);
    } else if (gui && command >= kTrayProfileCommandBase &&
               command < kTrayProfileCommandBase + kPresetCount) {
        apply_profile_from_tray(*gui,
                                static_cast<std::size_t>(command - kTrayProfileCommandBase));
    }
}

HWND make_control(DWORD style, LPCWSTR class_name, LPCWSTR text, HWND parent,
                  int id, int x, int y, int width, int height) {
    return CreateWindowExW(0, class_name, text, style, x, y, width, height,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND make_control_ex(DWORD ex_style, DWORD style, LPCWSTR class_name, LPCWSTR text,
                     HWND parent, int id, int x, int y, int width, int height) {
    return CreateWindowExW(ex_style, class_name, text, style, x, y, width, height,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void layout_controls(GuiState& gui, int width, int height) {
    const UINT dpi = gui.window ? GetDpiForWindow(gui.window) : 96;
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int sidebar_width = scale(ui::Metrics::sidebar_width);
    const int side_margin = scale(24);
    const int side_width = sidebar_width - side_margin * 2;
    const int main_x = sidebar_width + scale(32);
    const int main_right = width - scale(36);
    const int main_width = main_right - main_x;

    MoveWindow(gui.title, side_margin, scale(24), side_width, scale(34), TRUE);
    MoveWindow(gui.subtitle, side_margin, scale(58), side_width, scale(34), TRUE);
    MoveWindow(gui.display_caption, side_margin, scale(112), side_width - scale(76), scale(18), TRUE);
    MoveWindow(gui.refresh_button, sidebar_width - side_margin - scale(28), scale(106), scale(28), scale(28), TRUE);
    // The full height is required by Win32 so the opened list can show every monitor.
    MoveWindow(gui.display_combo, side_margin, scale(139), side_width, scale(196), TRUE);
    SendMessageW(gui.display_combo, CB_SETDROPPEDWIDTH,
                 static_cast<WPARAM>(std::max(side_width, scale(320))), 0);
    MoveWindow(gui.display_status, side_margin, scale(181), side_width, scale(50), TRUE);

    const int profile_section_x = scale(ui::Metrics::profile_section_inset);
    const int profile_content_x = scale(ui::Metrics::profile_content_inset);
    const int profile_content_width = sidebar_width - profile_content_x - profile_section_x;
    const int profile_title_y = scale(252);
    const int profile_title_height = scale(18);
    const int profile_first_y = profile_title_y + profile_title_height +
                                scale(ui::Metrics::profile_title_gap);
    const int profile_row_height = scale(ui::Metrics::profile_row_height);
    const int profile_row_stride = profile_row_height + scale(ui::Metrics::profile_row_gap);
    MoveWindow(gui.preset_caption, profile_section_x, profile_title_y,
               sidebar_width - profile_section_x * 2, profile_title_height, TRUE);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        MoveWindow(gui.preset_buttons[i], profile_content_x,
                   profile_first_y + static_cast<int>(i) * profile_row_stride,
                   profile_content_width, profile_row_height, TRUE);
    }
    if (gui.profile_rename && gui.renaming_preset < kPresetCount) {
        MoveWindow(gui.profile_rename, profile_content_x + scale(10),
                   profile_first_y + static_cast<int>(gui.renaming_preset) * profile_row_stride + scale(3),
                   profile_content_width - scale(16), profile_row_height - scale(6), TRUE);
    }
    const int profile_list_bottom = profile_first_y +
        static_cast<int>(kPresetCount) * profile_row_height +
        static_cast<int>(kPresetCount - 1) * scale(ui::Metrics::profile_row_gap);
    const int profile_save_y = profile_list_bottom + scale(ui::Metrics::profile_action_gap);
    const int delete_width = scale(42);
    const int action_gap = scale(8);
    MoveWindow(gui.preset_save, profile_content_x, profile_save_y,
               profile_content_width - delete_width - action_gap, scale(32), TRUE);
    MoveWindow(gui.preset_delete,
               profile_content_x + profile_content_width - delete_width, profile_save_y,
               delete_width, scale(32), TRUE);
    MoveWindow(gui.startup_button, side_margin, height - scale(62), side_width,
               scale(36), TRUE);

    MoveWindow(gui.main_heading, main_x, scale(24), main_width, scale(34), TRUE);
    MoveWindow(gui.main_subtitle, main_x, scale(58), main_width, scale(24), TRUE);
    MoveWindow(gui.preview_heading, main_x + scale(18), scale(101), scale(220), scale(25), TRUE);
    MoveWindow(gui.preview_caption, main_right - scale(210), scale(104), scale(190), scale(20), TRUE);

    const int controls_x = main_x + scale(16);
    const int controls_width = main_width - scale(32);
    const int value_width = scale(ui::Metrics::numeric_width);
    const int label_width = scale(112);
    const int slider_x = controls_x + label_width;
    const int slider_width = std::max(scale(250), controls_width - label_width - value_width - scale(16));
    const int value_x = slider_x + slider_width + scale(16);

    MoveWindow(gui.tone_heading, controls_x, scale(292), controls_width, scale(24), TRUE);
    MoveWindow(gui.tone_caption, controls_x, scale(318), controls_width, scale(20), TRUE);
    MoveWindow(gui.color_heading, controls_x, scale(478), controls_width, scale(22), TRUE);
    MoveWindow(gui.color_caption, controls_x, scale(501), controls_width, scale(18), TRUE);

    auto place_row = [&](HWND label, HWND slider, HWND value, int y) {
        MoveWindow(label, controls_x, scale(y + 5), label_width - scale(8), scale(22), TRUE);
        MoveWindow(slider, slider_x, scale(y), slider_width, scale(30), TRUE);
        MoveWindow(value, value_x, scale(y), value_width, scale(28), TRUE);
        SetWindowRgn(value, CreateRoundRectRgn(0, 0, value_width + 1, scale(28) + 1,
                                               scale(ui::Metrics::control_radius),
                                               scale(ui::Metrics::control_radius)), TRUE);
    };
    place_row(gui.brightness_label, gui.brightness_slider, gui.brightness_value, 346);
    place_row(gui.contrast_label, gui.contrast_slider, gui.contrast_value, 390);
    place_row(gui.gamma_label, gui.gamma_slider, gui.gamma_value, 434);
    place_row(gui.r_gain_label, gui.r_gain_slider, gui.r_gain_value, 530);
    place_row(gui.g_gain_label, gui.g_gain_slider, gui.g_gain_value, 574);
    place_row(gui.b_gain_label, gui.b_gain_slider, gui.b_gain_value, 618);

    const int footer_y = height - scale(58);
    MoveWindow(gui.status, main_x + scale(18), footer_y + scale(9),
               std::max(scale(160), main_width - scale(300)), scale(26), TRUE);
    MoveWindow(gui.reset_button, main_right - scale(254), footer_y + scale(2), scale(104), scale(36), TRUE);
    MoveWindow(gui.before_after_button, main_right - scale(378), footer_y + scale(2),
               scale(112), scale(36), TRUE);
    MoveWindow(gui.apply_button, main_right - scale(138), footer_y + scale(2), scale(138), scale(36), TRUE);
}

void apply_font(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void recreate_fonts(GuiState& gui, UINT dpi) {
    const HFONT old_normal = gui.normal_font;
    const HFONT old_title = gui.title_font;
    const HFONT old_heading = gui.heading_font;
    const HFONT old_panel = gui.panel_font;
    const HFONT old_section = gui.section_font;
    const HFONT old_caption = gui.caption_font;
    const HFONT old_small = gui.small_font;

    gui.normal_font = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0,
                                  FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.title_font = CreateFontW(-MulDiv(19, static_cast<int>(dpi), 72), 0, 0, 0,
                                 FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.heading_font = CreateFontW(-MulDiv(15, static_cast<int>(dpi), 72), 0, 0, 0,
                                   FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.panel_font = CreateFontW(-MulDiv(13, static_cast<int>(dpi), 72), 0, 0, 0,
                                 FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.section_font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0,
                                   FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.caption_font = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0,
                                   FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gui.small_font = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0,
                                 FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    apply_font(gui.title, gui.title_font);
    const std::array<HWND, 17> normal_controls{
        gui.subtitle, gui.display_combo, gui.refresh_button,
        gui.gamma_label, gui.brightness_label, gui.contrast_label,
        gui.r_gain_label, gui.g_gain_label, gui.b_gain_label,
        gui.preset_save, gui.preset_delete, gui.startup_button,
        gui.apply_button, gui.reset_button, gui.status,
        gui.gamma_value, gui.brightness_value,
    };
    for (HWND control : normal_controls) apply_font(control, gui.normal_font);
    apply_font(gui.contrast_value, gui.normal_font);
    apply_font(gui.r_gain_value, gui.normal_font);
    apply_font(gui.g_gain_value, gui.normal_font);
    apply_font(gui.b_gain_value, gui.normal_font);
    apply_font(gui.before_after_button, gui.normal_font);
    apply_font(gui.profile_rename, gui.normal_font);
    for (HWND button : gui.preset_buttons) apply_font(button, gui.normal_font);
    apply_font(gui.main_heading, gui.heading_font);
    apply_font(gui.preview_heading, gui.panel_font);
    apply_font(gui.tone_heading, gui.section_font);
    apply_font(gui.color_heading, gui.section_font);
    apply_font(gui.display_caption, gui.caption_font);
    apply_font(gui.preset_caption, gui.caption_font);
    apply_font(gui.main_subtitle, gui.normal_font);
    apply_font(gui.preview_caption, gui.small_font);
    apply_font(gui.tone_caption, gui.small_font);
    apply_font(gui.color_caption, gui.small_font);
    apply_font(gui.display_status, gui.small_font);

    if (old_normal) DeleteObject(old_normal);
    if (old_title) DeleteObject(old_title);
    if (old_heading) DeleteObject(old_heading);
    if (old_panel) DeleteObject(old_panel);
    if (old_section) DeleteObject(old_section);
    if (old_caption) DeleteObject(old_caption);
    if (old_small) DeleteObject(old_small);
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

    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const std::wstring name = L"Default " + std::to_wstring(i + 1);
        gui.preset_buttons[i] = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             L"BUTTON", name.c_str(), gui.window,
                                             kPresetSlot1 + static_cast<int>(i), 0, 0, 0, 0);
    }
    for (HWND button : gui.preset_buttons) SetWindowSubclass(button, profile_proc, 1, 0);
    gui.preset_save = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   L"BUTTON", L"+  New profile", gui.window,
                                   kPresetSave, 0, 0, 0, 0);
    gui.preset_delete = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     L"BUTTON", L"Delete profile", gui.window,
                                      kPresetDelete, 0, 0, 0, 0);
    gui.profile_rename = make_control(WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                      L"EDIT", L"", gui.window, kProfileRename,
                                      0, 0, 0, 0);
    SendMessageW(gui.profile_rename, EM_SETLIMITTEXT, 48, 0);
    SetWindowSubclass(gui.profile_rename, profile_rename_proc, 1, 0);
    gui.startup_button = make_control(WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
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

    SetWindowTheme(gui.display_combo, L"Explorer", nullptr);
    const std::array<HWND, 12> themed_controls{
        gui.gamma_slider, gui.brightness_slider, gui.contrast_slider,
        gui.r_gain_slider, gui.g_gain_slider, gui.b_gain_slider,
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
    };
    for (HWND control : themed_controls) SetWindowTheme(control, L"Explorer", nullptr);

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

void draw_owner_button(const DRAWITEMSTRUCT& item, const GuiState& gui) {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;

    if (item.CtlID == kStartupToggle) {
        paint_parent_layer(item.hwndItem, item.hDC, const_cast<GuiState&>(gui));
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
        const auto old_font = font ? SelectObject(item.hDC, font) : nullptr;
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, ui::Theme::sidebar_text);
        RECT label_rect = item.rcItem;
        label_rect.right -= 58;
        DrawTextW(item.hDC, L"Start with Windows", -1, &label_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int height = item.rcItem.bottom - item.rcItem.top;
        RECT toggle{item.rcItem.right - 48, item.rcItem.top + (height - 22) / 2,
                    item.rcItem.right - 4, item.rcItem.top + (height - 22) / 2 + 22};
        const COLORREF toggle_color = gui.startup_enabled ? ui::Theme::success
                                                          : ui::Theme::sidebar_muted;
        HBRUSH track = CreateSolidBrush(toggle_color);
        HPEN track_pen = CreatePen(PS_SOLID, 1, toggle_color);
        const auto old_brush = SelectObject(item.hDC, track);
        const auto old_pen = SelectObject(item.hDC, track_pen);
        RoundRect(item.hDC, toggle.left, toggle.top, toggle.right, toggle.bottom, 22, 22);
        SelectObject(item.hDC, old_pen);
        SelectObject(item.hDC, old_brush);
        DeleteObject(track_pen);
        DeleteObject(track);

        const int thumb_left = gui.startup_enabled ? toggle.right - 19 : toggle.left + 4;
        HBRUSH thumb = CreateSolidBrush(ui::Theme::control_surface);
        const auto prior_brush = SelectObject(item.hDC, thumb);
        const auto prior_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        Ellipse(item.hDC, thumb_left, toggle.top + 4, thumb_left + 14, toggle.top + 18);
        SelectObject(item.hDC, prior_pen);
        SelectObject(item.hDC, prior_brush);
        DeleteObject(thumb);
        if (old_font) SelectObject(item.hDC, old_font);
        return;
    }

    COLORREF fill = ui::Theme::primary;
    COLORREF text = ui::Theme::control_surface;
    COLORREF outline = fill;
    if (item.CtlID >= kPresetSlot1 && item.CtlID <= kPresetSlot4) {
        const std::size_t index = static_cast<std::size_t>(item.CtlID - kPresetSlot1);
        if (index == gui.active_preset) {
            fill = ui::Theme::sidebar_selected;
            text = ui::Theme::control_surface;
            outline = ui::Theme::sidebar_selected;
        } else {
            fill = ui::Theme::sidebar;
            text = gui.presets[index].occupied ? ui::Theme::sidebar_text
                                               : ui::Theme::sidebar_secondary;
            outline = ui::Theme::sidebar;
        }
    } else if (item.CtlID == kResetButton) {
        fill = ui::Theme::footer_surface;
        text = ui::Theme::text_secondary;
        outline = ui::Theme::footer_surface;
    } else if (item.CtlID == kBeforeAfter) {
        fill = ui::Theme::footer_surface;
        text = ui::Theme::text_secondary;
        outline = ui::Theme::border;
    } else if (item.CtlID == kPresetDelete) {
        fill = ui::Theme::sidebar;
        text = ui::Theme::sidebar_secondary;
        outline = ui::Theme::sidebar_selected;
    } else if (item.CtlID == kRefreshButton) {
        fill = ui::Theme::sidebar;
        text = ui::Theme::sidebar_secondary;
        outline = ui::Theme::sidebar;
    } else if (item.CtlID == kPresetSave) {
        fill = ui::Theme::sidebar_action;
        text = ui::Theme::sidebar_text;
        outline = ui::Theme::sidebar_action_border;
    }
    if (disabled) {
        fill = ui::Theme::disabled_surface;
        text = ui::Theme::disabled_text;
        outline = ui::Theme::disabled_surface;
    } else if (pressed) {
        fill = ui::Theme::primary_pressed;
        text = ui::Theme::control_surface;
        outline = fill;
    }

    const bool sidebar_button = item.CtlID == kRefreshButton || item.CtlID == kPresetSave ||
                                 item.CtlID == kPresetDelete ||
                                 (item.CtlID >= kPresetSlot1 && item.CtlID <= kPresetSlot4);
    if (sidebar_button) {
        paint_parent_layer(item.hwndItem, item.hDC, const_cast<GuiState&>(gui));
    }

    const bool profile_item = item.CtlID >= kPresetSlot1 && item.CtlID <= kPresetSlot4;
    const bool selected_profile = profile_item &&
        static_cast<std::size_t>(item.CtlID - kPresetSlot1) == gui.active_preset;
    const bool hovered_profile = profile_item && gui.hovered_profile == item.hwndItem;
    const bool ghost_action = item.CtlID == kRefreshButton || item.CtlID == kPresetDelete;
    if (selected_profile) {
        ui::draw_layered_panel(item.hDC, item.rcItem, ui::Theme::sidebar_selected, 142,
                               CLR_INVALID, ui::Metrics::control_radius);
    } else if (hovered_profile) {
        ui::draw_layered_panel(item.hDC, item.rcItem, ui::Theme::sidebar_selected, 72,
                               CLR_INVALID, ui::Metrics::control_radius);
    } else if (!profile_item && !ghost_action) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, outline);
        HGDIOBJ old_brush = SelectObject(item.hDC, brush);
        HGDIOBJ old_pen = SelectObject(item.hDC, pen);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right,
                  item.rcItem.bottom, ui::Metrics::radius, ui::Metrics::radius);
        SelectObject(item.hDC, old_pen);
        SelectObject(item.hDC, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    wchar_t label[64]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = font ? SelectObject(item.hDC, font) : nullptr;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    const UINT alignment = profile_item
                               ? DT_LEFT | DT_VCENTER | DT_SINGLELINE
                               : DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    RECT label_rect = item.rcItem;
    if (profile_item) {
        label_rect.left += ui::Metrics::profile_text_inset;
    }
    if (item.CtlID == kRefreshButton) {
        const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        const int cx = (item.rcItem.left + item.rcItem.right) / 2;
        HPEN icon_pen = CreatePen(PS_SOLID, 2, ui::Theme::sidebar_secondary);
        const auto old_icon_pen = SelectObject(item.hDC, icon_pen);
        Arc(item.hDC, cx - 7, cy - 7, cx + 7, cy + 7,
            cx + 7, cy - 1, cx - 3, cy - 7);
        MoveToEx(item.hDC, cx + 6, cy - 5, nullptr);
        LineTo(item.hDC, cx + 7, cy - 1);
        LineTo(item.hDC, cx + 3, cy - 1);
        SelectObject(item.hDC, old_icon_pen);
        DeleteObject(icon_pen);
    } else if (item.CtlID == kPresetDelete) {
        const int cx = (item.rcItem.left + item.rcItem.right) / 2;
        const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        HPEN icon_pen = CreatePen(PS_SOLID, 1, text);
        const auto old_icon_pen = SelectObject(item.hDC, icon_pen);
        const auto old_icon_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        Rectangle(item.hDC, cx - 5, cy - 5, cx + 6, cy + 8);
        MoveToEx(item.hDC, cx - 7, cy - 8, nullptr);
        LineTo(item.hDC, cx + 8, cy - 8);
        MoveToEx(item.hDC, cx - 2, cy - 10, nullptr);
        LineTo(item.hDC, cx + 3, cy - 10);
        SelectObject(item.hDC, old_icon_brush);
        SelectObject(item.hDC, old_icon_pen);
        DeleteObject(icon_pen);
    } else {
        DrawTextW(item.hDC, label, -1, &label_rect, alignment);
    }
    if (profile_item && gui.profile_keyboard_focus &&
        (item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -2);
        HPEN focus_pen = CreatePen(PS_SOLID, 1, ui::Theme::sidebar_secondary);
        const auto old_focus_pen = SelectObject(item.hDC, focus_pen);
        const auto old_focus_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        RoundRect(item.hDC, focus.left, focus.top, focus.right, focus.bottom,
                  ui::Metrics::control_radius, ui::Metrics::control_radius);
        SelectObject(item.hDC, old_focus_brush);
        SelectObject(item.hDC, old_focus_pen);
        DeleteObject(focus_pen);
    }
    if (item.CtlID >= kPresetSlot1 && item.CtlID <= kPresetSlot4 &&
        static_cast<std::size_t>(item.CtlID - kPresetSlot1) == gui.active_preset) {
        HBRUSH indicator = CreateSolidBrush(ui::Theme::primary);
        HGDIOBJ old_indicator = SelectObject(item.hDC, indicator);
        RECT accent{item.rcItem.left + 3, item.rcItem.top + 6,
                    item.rcItem.left + 6, item.rcItem.bottom - 6};
        FillRect(item.hDC, &accent, indicator);
        SelectObject(item.hDC, old_indicator);
        DeleteObject(indicator);
    }
    if (old_font) SelectObject(item.hDC, old_font);
}

void draw_preview(HDC dc, const GuiState& gui, const RECT& rect) {
    ui::draw_layered_panel(dc, rect, ui::Theme::panel_surface, 196,
                           ui::Theme::border_subtle, ui::Metrics::panel_radius);
    RECT graph{rect.left + 20, rect.top + 48, rect.right - 20, rect.bottom - 16};
    HPEN grid = CreatePen(PS_SOLID, 1, ui::Theme::grid);
    auto old_pen = SelectObject(dc, grid);
    for (int i = 1; i < 4; ++i) {
        const int x = graph.left + (graph.right - graph.left) * i / 4;
        const int y = graph.top + (graph.bottom - graph.top) * i / 4;
        MoveToEx(dc, x, graph.top, nullptr); LineTo(dc, x, graph.bottom);
        MoveToEx(dc, graph.left, y, nullptr); LineTo(dc, graph.right, y);
    }
    DeleteObject(SelectObject(dc, old_pen));

    HPEN reference = CreatePen(PS_SOLID, 2, ui::Theme::reference_curve);
    auto old_reference = SelectObject(dc, reference);
    MoveToEx(dc, graph.left, graph.bottom, nullptr);
    LineTo(dc, graph.right, graph.top);
    SelectObject(dc, old_reference);
    DeleteObject(reference);

    const GammaRamp ramp = gui.controller.generate_preview(params_from_sliders(gui));
    auto draw_curve = [&](COLORREF color, std::size_t channel, int width) {
        HPEN curve = CreatePen(PS_SOLID, width, color);
        auto prior = SelectObject(dc, curve);
        for (int i = 0; i <= 96; ++i) {
            const double input = i / 96.0;
            const std::size_t sample = static_cast<std::size_t>(input * (kRampSize - 1));
            const double output = ramp.channel[channel][sample] / 65535.0;
            const int x = graph.left + static_cast<int>((graph.right - graph.left) * input);
            const int y = graph.bottom - static_cast<int>((graph.bottom - graph.top) * output);
            if (i == 0) MoveToEx(dc, x, y, nullptr); else LineTo(dc, x, y);
        }
        SelectObject(dc, prior);
        DeleteObject(curve);
    };
    const bool neutral = params_from_sliders(gui).r_gain == params_from_sliders(gui).g_gain &&
                         params_from_sliders(gui).g_gain == params_from_sliders(gui).b_gain;
    if (neutral) {
        draw_curve(ui::Theme::primary, 0, 3);
    } else {
        draw_curve(RGB(239, 68, 68), 0, 2);
        draw_curve(RGB(34, 197, 94), 1, 2);
        draw_curve(RGB(59, 130, 246), 2, 2);
    }
}

void render_background(HDC hdc, HWND window, GuiState& gui) {
    RECT client{};
    GetClientRect(window, &client);

    const UINT dpi = GetDpiForWindow(window);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int sidebar_width = scale(ui::Metrics::sidebar_width);
    gui.background.draw(hdc, client, sidebar_width);

    const int main_x = sidebar_width + scale(32);
    const int main_right = client.right - scale(32);
    RECT footer_layer{sidebar_width, client.bottom - scale(78), client.right, client.bottom};
    ui::fill_layered_surface(hdc, footer_layer, ui::Theme::footer_surface, 232);
    RECT preview{main_x, scale(88), main_right, scale(274)};
    draw_preview(hdc, gui, preview);

    const std::array<HWND, 6> numeric_edits{
        gui.gamma_value, gui.brightness_value, gui.contrast_value,
        gui.r_gain_value, gui.g_gain_value, gui.b_gain_value,
    };
    for (HWND edit : numeric_edits) {
        RECT input{};
        GetWindowRect(edit, &input);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&input), 2);
        InflateRect(&input, 1, 1);
        const bool focused = GetFocus() == edit;
        const bool hovered = gui.hovered_numeric == edit;
        ui::draw_layered_panel(hdc, input, ui::Theme::control_surface,
                               hovered || focused ? 255 : 242,
                               focused ? ui::Theme::primary : ui::Theme::input_border,
                               scale(ui::Metrics::control_radius));
    }

    HPEN section_divider = CreatePen(PS_SOLID, 1, ui::Theme::border_subtle);
    auto old_section_pen = SelectObject(hdc, section_divider);
    MoveToEx(hdc, main_x, scale(468), nullptr);
    LineTo(hdc, main_right, scale(468));
    SelectObject(hdc, old_section_pen);
    DeleteObject(section_divider);

    COLORREF status_color = ui::Theme::idle;
    switch (gui.status_tone) {
    case StatusTone::success: status_color = ui::Theme::success; break;
    case StatusTone::warning: status_color = ui::Theme::warning; break;
    case StatusTone::error: status_color = ui::Theme::error; break;
    case StatusTone::idle: break;
    }
    HBRUSH status_brush = CreateSolidBrush(status_color);
    HGDIOBJ old_status_brush = SelectObject(hdc, status_brush);
    Ellipse(hdc, main_x + scale(2), client.bottom - scale(53),
            main_x + scale(10), client.bottom - scale(45));
    SelectObject(hdc, old_status_brush);
    DeleteObject(status_brush);

    ui::draw_separator(hdc, sidebar_width, client.bottom - scale(78),
                       client.right, ui::Theme::border);
}

bool ensure_paint_buffer(GuiState& gui, HDC reference, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!gui.paint_buffer_dc) {
        gui.paint_buffer_dc = CreateCompatibleDC(reference);
        if (!gui.paint_buffer_dc) return false;
    }
    if (gui.paint_buffer_bitmap && gui.paint_buffer_width == width &&
        gui.paint_buffer_height == height) {
        return true;
    }

    HBITMAP replacement = CreateCompatibleBitmap(reference, width, height);
    if (!replacement) return false;
    HGDIOBJ previous = SelectObject(gui.paint_buffer_dc, replacement);
    if (!gui.paint_buffer_original) gui.paint_buffer_original = previous;
    if (gui.paint_buffer_bitmap) DeleteObject(gui.paint_buffer_bitmap);
    gui.paint_buffer_bitmap = replacement;
    gui.paint_buffer_width = width;
    gui.paint_buffer_height = height;
    return true;
}

void paint_background(HWND window, GuiState& gui) {
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (ensure_paint_buffer(gui, hdc, width, height)) {
        const int saved = SaveDC(gui.paint_buffer_dc);
        IntersectClipRect(gui.paint_buffer_dc, paint.rcPaint.left, paint.rcPaint.top,
                          paint.rcPaint.right, paint.rcPaint.bottom);
        render_background(gui.paint_buffer_dc, window, gui);
        RestoreDC(gui.paint_buffer_dc, saved);
        BitBlt(hdc, paint.rcPaint.left, paint.rcPaint.top,
               paint.rcPaint.right - paint.rcPaint.left,
               paint.rcPaint.bottom - paint.rcPaint.top,
               gui.paint_buffer_dc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    } else {
        render_background(hdc, window, gui);
    }
    EndPaint(window, &paint);
}

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
            if (index >= kPresetCount) return 0;
            HMENU menu = CreatePopupMenu();
            const bool occupied = gui->presets[index].occupied;
            AppendMenuW(menu, MF_STRING | (occupied ? MF_ENABLED : MF_GRAYED), 1, L"Rename");
            AppendMenuW(menu, MF_STRING | (occupied ? MF_ENABLED : MF_GRAYED), 2, L"Duplicate");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (occupied ? MF_ENABLED : MF_GRAYED), 3, L"Delete");
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                x, y, 0, window, nullptr);
            DestroyMenu(menu);
            if (command == 1) begin_profile_rename(*gui, index);
            else if (command == 2) duplicate_profile(*gui, index);
            else if (command == 3) delete_preset(*gui, index);
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
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
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
            set_status(*current, L"Profile migration failed: " + profile_error,
                       StatusTone::error);
        }
        current->presets = current->profile_manager.legacy_slots();
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
            EnableWindow(current->apply_button, FALSE);
            set_status(*current, L"Profile file is damaged; saving is disabled",
                       StatusTone::error);
        }
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
                gui->controller.reapply_committed(*display, gui->committed_settings,
                                                  rollback_error);
            }
        }
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
                (item->CtlID >= kPresetSlot1 && item->CtlID <= kPresetSlot4)) {
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
            } else if (control == gui->display_status) {
                SetTextColor(dc, ui::Theme::sidebar_secondary);
            } else if (control == gui->subtitle || control == gui->main_subtitle ||
                       control == gui->status || control == gui->preview_caption ||
                       control == gui->tone_caption || control == gui->color_caption) {
                SetTextColor(dc, ui::Theme::text_secondary);
            } else {
                SetTextColor(dc, ui::Theme::text);
            }
            const bool trackbar = control == gui->gamma_slider ||
                                  control == gui->brightness_slider ||
                                  control == gui->contrast_slider ||
                                  control == gui->r_gain_slider ||
                                  control == gui->g_gain_slider ||
                                  control == gui->b_gain_slider;
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
        if (!gui->syncing_controls && HIWORD(wparam) == EN_CHANGE) {
            switch (LOWORD(wparam)) {
            case kGammaValue: edit_changed(*gui, gui->gamma_value, gui->gamma_slider, calibration_ranges::gamma.minimum, calibration_ranges::gamma.maximum); return 0;
            case kBrightnessValue: edit_changed(*gui, gui->brightness_value, gui->brightness_slider, calibration_ranges::brightness.minimum, calibration_ranges::brightness.maximum); return 0;
            case kContrastValue: edit_changed(*gui, gui->contrast_value, gui->contrast_slider, calibration_ranges::contrast.minimum, calibration_ranges::contrast.maximum); return 0;
            case kRGainValue: edit_changed(*gui, gui->r_gain_value, gui->r_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            case kGGainValue: edit_changed(*gui, gui->g_gain_value, gui->g_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            case kBGainValue: edit_changed(*gui, gui->b_gain_value, gui->b_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            default: break;
            }
        }
        if (!gui->syncing_controls && HIWORD(wparam) == EN_KILLFOCUS) {
            switch (LOWORD(wparam)) {
            case kGammaValue: normalize_edit(*gui, gui->gamma_value, gui->gamma_slider, calibration_ranges::gamma.minimum, calibration_ranges::gamma.maximum); return 0;
            case kBrightnessValue: normalize_edit(*gui, gui->brightness_value, gui->brightness_slider, calibration_ranges::brightness.minimum, calibration_ranges::brightness.maximum); return 0;
            case kContrastValue: normalize_edit(*gui, gui->contrast_value, gui->contrast_slider, calibration_ranges::contrast.minimum, calibration_ranges::contrast.maximum); return 0;
            case kRGainValue: normalize_edit(*gui, gui->r_gain_value, gui->r_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            case kGGainValue: normalize_edit(*gui, gui->g_gain_value, gui->g_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            case kBGainValue: normalize_edit(*gui, gui->b_gain_value, gui->b_gain_slider, calibration_ranges::gain.minimum, calibration_ranges::gain.maximum); return 0;
            default: break;
            }
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= kPresetSlot1 &&
            LOWORD(wparam) <= kPresetSlot4) {
            select_preset(*gui, static_cast<std::size_t>(LOWORD(wparam) - kPresetSlot1));
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
        if (LOWORD(wparam) == kDisplayCombo && HIWORD(wparam) == CBN_SELCHANGE) {
            KillTimer(window, kPreviewTimer);
            KillTimer(window, kAutoSaveTimer);
            if (gui->dirty && !save_and_apply_current(*gui, true)) {
                select_display_item(*gui, gui->active_display_index);
                return 0;
            }
            load_selected_profile(*gui);
            const int index = selected_display_index(*gui);
            if (index >= 0 && index < static_cast<int>(gui->displays.size())) {
                const std::wstring metadata = display_metadata(gui->displays[index]);
                SetWindowTextW(gui->display_status, metadata.c_str());
                set_status(*gui, L"Ready", StatusTone::success);
            }
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
        if (wparam == kPreviewTimer && gui) {
            KillTimer(window, kPreviewTimer);
            if (gui->live_preview) preview_selected(*gui);
        } else if (wparam == kDisplayRefreshTimer && gui) {
            KillTimer(window, kDisplayRefreshTimer);
            refresh_displays(*gui);
            if (gui->reapply_after_display_refresh) {
                gui->reapply_after_display_refresh = false;
                schedule_recovery_retry(*gui);
            }
        } else if (wparam == kAutoSaveTimer && gui) {
            KillTimer(window, kAutoSaveTimer);
            save_and_apply_current(*gui, true);
        } else if (wparam == kRecoveryRetryTimer && gui) {
            run_recovery_retry(*gui);
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
        log_message(LogLevel::info, L"Final adjustments saved; destroying main window");
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
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
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
    const bool startup_launch = command_line && wcsstr(command_line, L"--startup") != nullptr;
    SingleInstanceLock instance_lock;
    if (!instance_lock.is_primary()) {
        if (!startup_launch) activate_existing_window(kWindowClass);
        return 0;
    }

    // Prevent Windows from bitmap-scaling the whole window on 125%/150% displays.
    // Per-monitor v2 keeps text and common controls rendered at native resolution.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&window_class);

    GuiState gui;
    gui.startup_launch = startup_launch;
    const UINT initial_dpi = GetDpiForSystem();
    const int initial_width = MulDiv(1120, static_cast<int>(initial_dpi), 96);
    const int initial_height = MulDiv(800, static_cast<int>(initial_dpi), 96);
    HWND window = CreateWindowExW(0, kWindowClass, kApplicationTitle,
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                      WS_THICKFRAME | WS_MAXIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, initial_width, initial_height,
                                  nullptr, nullptr, instance, &gui);
    if (!window) {
        if (SUCCEEDED(com_result)) CoUninitialize();
        return 1;
    }

    if (gui.startup_launch) {
        ShowWindow(window, SW_HIDE);
    } else {
        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        const bool undo_shortcut = message.message == WM_KEYDOWN &&
                                   message.wParam == L'Z' &&
                                   (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const HWND focus = GetFocus();
        const bool editing_text = focus == gui.profile_rename || focus == gui.gamma_value ||
                                  focus == gui.brightness_value || focus == gui.contrast_value ||
                                  focus == gui.r_gain_value || focus == gui.g_gain_value ||
                                  focus == gui.b_gain_value;
        if (undo_shortcut && !editing_text) {
            SendMessageW(window, kUndoProfileSwitchMessage, 0, 0);
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com_result)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
