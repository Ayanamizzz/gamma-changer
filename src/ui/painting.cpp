#include "gui_internal.h"

namespace gamma_changer {
namespace {

using ui::Metrics;
using ui::Theme;

void draw_text(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT rect,
               UINT format) {
    const auto old_font = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    if (old_font) SelectObject(dc, old_font);
}

int text_width(HDC dc, HFONT font, const std::wstring& text) {
    const auto old_font = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old_font) SelectObject(dc, old_font);
    return size.cx;
}

// The editor card is the one raised surface: soft two-step shadow, near-opaque
// white, hairline border.
void draw_raised_card(HDC dc, const RECT& card, const Scale& s) {
    const float radius = static_cast<float>(s(Metrics::card_radius));
    RECT shadow = card;
    OffsetRect(&shadow, 0, s(2));
    ui::aa_fill_round_rect(dc, shadow, radius, RGB(40, 20, 60), 10);
    OffsetRect(&shadow, 0, -s(1));
    InflateRect(&shadow, 1, 1);
    ui::aa_fill_round_rect(dc, shadow, radius, RGB(40, 20, 60), 8);
    ui::aa_fill_round_rect(dc, card, radius, Theme::card, 240);
    ui::aa_stroke_round_rect(dc, card, radius, Theme::card_border, 1.0f, 200);
}

// The apps card sits lower: translucent, no shadow, so the wallpaper reads
// through and the editor stays the focus.
void draw_flat_card(HDC dc, const RECT& card, const Scale& s) {
    const float radius = static_cast<float>(s(Metrics::card_radius));
    ui::aa_fill_round_rect(dc, card, radius, Theme::card, 176);
    ui::aa_stroke_round_rect(dc, card, radius, Theme::card_border, 1.0f, 170);
}

void draw_editor_card(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    draw_raised_card(dc, g.editor_card, s);
    ui::aa_line(dc, static_cast<float>(g.controls_left), static_cast<float>(g.group_divider_y),
                static_cast<float>(g.controls_right), static_cast<float>(g.group_divider_y),
                Theme::divider, 1.0f);

    const RECT& plot = g.curve_plot;
    if (plot.right - plot.left < s(60)) return;
    const float radius = static_cast<float>(s(6));
    ui::aa_fill_round_rect(dc, plot, radius, Theme::plot_surface);
    for (int i = 1; i < 4; ++i) {
        const float x = plot.left + (plot.right - plot.left) * i / 4.0f;
        const float y = plot.top + (plot.bottom - plot.top) * i / 4.0f;
        ui::aa_line(dc, x, static_cast<float>(plot.top), x, static_cast<float>(plot.bottom),
                    Theme::grid, 1.0f);
        ui::aa_line(dc, static_cast<float>(plot.left), y, static_cast<float>(plot.right), y,
                    Theme::grid, 1.0f);
    }
    // Axis names sit in the two corners a tone curve rarely reaches.
    const RECT output_label{plot.left + s(10), plot.top + s(8), plot.right, plot.top + s(26)};
    const RECT input_label{plot.left, plot.bottom - s(26), plot.right - s(10), plot.bottom - s(8)};
    draw_text(dc, gui.small_font, Theme::text_muted, L"Output", output_label,
              DT_LEFT | DT_TOP | DT_SINGLELINE);
    draw_text(dc, gui.small_font, Theme::text_muted, L"Input", input_label,
              DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
    // Reference: the unadjusted response.
    ui::aa_line(dc, static_cast<float>(plot.left), static_cast<float>(plot.bottom),
                static_cast<float>(plot.right), static_cast<float>(plot.top),
                Theme::reference_curve, 1.4f, 255, true);

    const CalibrationSettings settings = gui.session.working;
    const GammaRamp ramp = gui.controller.generate_preview(settings);
    // Plot every LUT entry at its own input position. Resampling to a
    // different grid snaps each point to a neighbouring entry, and the sign of
    // that snap flips mid-way, which drew a false kink at input 0.5.
    const auto curve_points = [&](std::size_t channel) {
        std::vector<ui::CurvePoint> points;
        points.reserve(kRampSize);
        for (std::size_t i = 0; i < kRampSize; ++i) {
            const double input = static_cast<double>(i) / static_cast<double>(kRampSize - 1);
            const double output = ramp.channel[channel][i] / 65535.0;
            points.push_back({static_cast<float>(plot.left + (plot.right - plot.left) * input),
                              static_cast<float>(plot.bottom - (plot.bottom - plot.top) * output)});
        }
        return points;
    };
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, plot.left, plot.top, plot.right, plot.bottom);
    const bool neutral = settings.r_gain == settings.g_gain && settings.g_gain == settings.b_gain;
    if (neutral) {
        const auto points = curve_points(0);
        ui::aa_curve(dc, points.data(), static_cast<int>(points.size()), Theme::primary, 2.6f,
                     plot.bottom, 24);
    } else {
        const COLORREF colors[3] = {Theme::curve_red, Theme::curve_green, Theme::curve_blue};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto points = curve_points(channel);
            ui::aa_curve(dc, points.data(), static_cast<int>(points.size()), colors[channel], 2.2f);
        }
    }
    RestoreDC(dc, saved);
    ui::aa_stroke_round_rect(dc, plot, radius, Theme::card_border, 1.0f);
}

void draw_sidebar_layer(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    const int P = g.padding;
    ui::aa_line(dc, static_cast<float>(P), static_cast<float>(g.automation_top),
                static_cast<float>(g.sidebar_width - P), static_cast<float>(g.automation_top),
                Theme::sidebar_divider, 1.0f);

    // Inline rename field.
    if (gui.renaming_preset < gui.profiles.size()) {
        const RECT row = profile_row_rect(g, gui.renaming_preset, gui.profile_scroll_offset);
        ui::aa_fill_round_rect(dc, row, static_cast<float>(s(Metrics::control_radius)),
                               Theme::sidebar_field);
        ui::aa_stroke_round_rect(dc, row, static_cast<float>(s(Metrics::control_radius)),
                                 Theme::primary, 1.5f);
    }

    // Overlay scrollbar when the list overflows.
    const int visible = g.profile_list.bottom - g.profile_list.top;
    const int total = static_cast<int>(gui.profiles.size()) * g.profile_row_stride;
    if (total > visible + s(Metrics::profile_row_gap)) {
        const int max_scroll = std::max(1, total - visible);
        const int thumb_height = std::max(s(24), visible * visible / std::max(1, total));
        const int travel = std::max(1, visible - thumb_height);
        const int thumb_top =
            g.profile_list.top +
            static_cast<int>(static_cast<long long>(gui.profile_scroll_offset) * travel / max_scroll);
        const int x = g.sidebar_width - s(5);
        RECT thumb{x - s(3), thumb_top, x, thumb_top + thumb_height};
        ui::aa_fill_round_rect(dc, thumb, static_cast<float>(s(2)), Theme::sidebar_text, 70);
    }
}

void draw_value_boxes(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    const AdjustmentBindings bindings = adjustment_bindings(gui);
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const RECT& box = g.rows[i].value_box;
        const bool focused = GetFocus() == bindings[i].edit;
        const bool hovered = gui.hovered_numeric == bindings[i].edit;
        const bool enabled = IsWindowEnabled(bindings[i].edit) != FALSE;
        const float radius = static_cast<float>(s(Metrics::control_radius));
        ui::aa_fill_round_rect(dc, box, radius, Theme::control_surface);
        ui::aa_stroke_round_rect(dc, box, radius,
                                 focused   ? Theme::primary
                                 : hovered ? Theme::text_muted
                                           : Theme::input_border,
                                 focused ? 1.6f : 1.0f, enabled ? 255 : 140);
    }
}

void draw_status_dot(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    COLORREF color = Theme::idle;
    switch (gui.status_tone) {
    case StatusTone::success: color = Theme::success; break;
    case StatusTone::warning: color = Theme::warning; break;
    case StatusTone::error: color = Theme::error; break;
    case StatusTone::idle: break;
    }
    const float cy = g.status_top + s(Metrics::status_height) / 2.0f;
    const float cx = g.main_left + static_cast<float>(s(7));
    ui::aa_fill_ellipse(dc, cx, cy, static_cast<float>(s(7)), color, 40);
    ui::aa_fill_ellipse(dc, cx, cy, static_cast<float>(s(4)), color);
}

void render_scene(HDC dc, GuiState& gui, const UiGeometry& g) {
    const RECT client{0, 0, g.width, g.height};
    gui.background.draw(dc, client, g.sidebar_width);
    draw_sidebar_layer(dc, gui, g);
    draw_editor_card(dc, gui, g);
    draw_value_boxes(dc, gui, g);
    draw_flat_card(dc, g.apps_card, g.scale);
    draw_status_dot(dc, gui, g);
}

// ---- Owner-drawn controls ---------------------------------------------------

void draw_toggle(HDC dc, const RECT& rect, bool on, bool enabled, const Scale& s) {
    const int height = s(20);
    const int width = s(36);
    const RECT track{rect.right - width, rect.top + (rect.bottom - rect.top - height) / 2,
                     rect.right, rect.top + (rect.bottom - rect.top + height) / 2};
    ui::aa_fill_round_rect(dc, track, height / 2.0f,
                           enabled && on ? Theme::primary : Theme::toggle_off);
    const float radius = height / 2.0f - s(3);
    const float cx = on ? track.right - height / 2.0f : track.left + height / 2.0f;
    ui::aa_fill_ellipse(dc, cx, (track.top + track.bottom) / 2.0f, radius, RGB(255, 255, 255));
}

void draw_profile_row(const DRAWITEMSTRUCT& item, const GuiState& gui, std::size_t index,
                      const Scale& s) {
    const bool selected = index == gui.active_preset;
    const bool hovered = gui.hovered_profile == item.hwndItem;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const float radius = static_cast<float>(s(Metrics::control_radius));
    if (selected || hovered || pressed) {
        ui::aa_fill_round_rect(item.hDC, item.rcItem, radius, Theme::sidebar_selected,
                               selected ? 34 : pressed ? 26 : 16);
    }
    if (selected) {
        const int mid = (item.rcItem.top + item.rcItem.bottom) / 2;
        RECT accent{item.rcItem.left + s(2), mid - s(8), item.rcItem.left + s(5), mid + s(8)};
        ui::aa_fill_round_rect(item.hDC, accent, 1.5f, Theme::primary);
    }
    RECT label = item.rcItem;
    label.left += s(Metrics::profile_text_inset);
    label.right -= s(10);
    // Which apps use the profile, right-aligned and quiet.
    if (index < gui.profile_notes.size() && !gui.profile_notes[index].empty()) {
        const std::wstring& note = gui.profile_notes[index];
        const int note_width = text_width(item.hDC, gui.small_font, note);
        RECT note_rect = label;
        note_rect.left = std::max(label.left + s(40), label.right - note_width);
        draw_text(item.hDC, gui.small_font,
                  gui.apps.following ? Theme::sidebar_secondary : Theme::sidebar_muted, note,
                  note_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        label.right = note_rect.left - s(8);
    }
    const std::wstring name = gui.profiles[index].name.empty() ? L"Unnamed profile"
                                                               : gui.profiles[index].name;
    draw_text(item.hDC, selected ? gui.section_font : gui.normal_font,
              selected ? Theme::sidebar_text : Theme::sidebar_secondary, name, label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (gui.profile_keyboard_focus && (item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -1, -1);
        ui::aa_stroke_round_rect(item.hDC, focus, radius, Theme::sidebar_secondary, 1.0f, 160);
    }
}

void draw_cross(HDC dc, float cx, float cy, float half, COLORREF color) {
    ui::aa_line(dc, cx - half, cy - half, cx + half, cy + half, color, 1.5f);
    ui::aa_line(dc, cx - half, cy + half, cx + half, cy - half, color, 1.5f);
}

// Pill for one assigned app: icon, file name, and a remove cross.
void draw_app_chip(const DRAWITEMSTRUCT& item, GuiState& gui, const AppChip& chip,
                   const Scale& s, bool hovered, bool focused) {
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const float radius = (rect.bottom - rect.top) / 2.0f;
    const bool live = gui.apps.following && chip.enabled;
    ui::aa_fill_round_rect(dc, rect, radius,
                           !live ? Theme::chip_off : hovered ? Theme::chip_hover : Theme::chip);
    if (focused) ui::aa_stroke_round_rect(dc, rect, radius, Theme::primary, 1.5f);

    const int icon_size = s(16);
    const int icon_left = rect.left + s(8);
    const int icon_top = rect.top + (rect.bottom - rect.top - icon_size) / 2;
    if (HICON icon = app_icon(gui, chip.path)) {
        DrawIconEx(dc, icon_left, icon_top, icon, icon_size, icon_size, 0, nullptr, DI_NORMAL);
    } else {
        // No readable icon (file missing or protected): a lettered tile.
        const RECT tile{icon_left, icon_top, icon_left + icon_size, icon_top + icon_size};
        ui::aa_fill_round_rect(dc, tile, static_cast<float>(s(4)),
                               live ? Theme::primary : Theme::idle);
        const std::wstring initial =
            chip.label.empty() ? L"?" : std::wstring(1, static_cast<wchar_t>(towupper(chip.label[0])));
        draw_text(dc, gui.small_font, RGB(255, 255, 255), initial, tile,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const RECT label{icon_left + icon_size + s(7), rect.top, rect.right - s(26), rect.bottom};
    draw_text(dc, gui.normal_font, live ? Theme::text : Theme::text_muted, chip.label, label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_cross(dc, static_cast<float>(rect.right - s(15)), (rect.top + rect.bottom) / 2.0f,
               static_cast<float>(s(7)) / 2.0f,
               hovered ? Theme::text : Theme::text_muted);
}

// Outlined pill that adds something: a plus and a label.
void draw_action_chip(HDC dc, const RECT& rect, const GuiState& gui, const std::wstring& label,
                      const Scale& s, bool plus, bool hovered, bool pressed, bool focused,
                      bool disabled = false) {
    const float radius = (rect.bottom - rect.top) / 2.0f;
    ui::aa_fill_round_rect(dc, rect, radius, Theme::control_surface,
                           disabled ? 110 : pressed ? 255 : hovered ? 236 : 170);
    ui::aa_stroke_round_rect(dc, rect, radius,
                             focused || hovered ? Theme::primary : Theme::input_border,
                             focused ? 1.5f : 1.0f);
    RECT text = rect;
    if (plus) {
        ui::draw_plus_icon(dc, rect.left + s(16), (rect.top + rect.bottom) / 2, s(4),
                           disabled ? Theme::disabled_text : Theme::primary);
        text.left += s(27);
        text.right -= s(12);
        draw_text(dc, gui.normal_font, disabled ? Theme::disabled_text : Theme::text, label, text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        draw_text(dc, gui.normal_font, Theme::text_secondary, label, text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

}  // namespace

void invalidate_control_background(GuiState& gui, HWND control, int padding) {
    if (!gui.window || !control) return;
    RECT rect{};
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, gui.window, reinterpret_cast<POINT*>(&rect), 2);
    InflateRect(&rect, padding, padding);
    RedrawWindow(gui.window, &rect, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void invalidate_preview_curve(GuiState& gui) {
    if (!gui.window) return;
    RECT plot = ui_geometry(gui).curve_plot;
    InflateRect(&plot, 2, 2);
    RedrawWindow(gui.window, &plot, nullptr, RDW_INVALIDATE);
}

void invalidate_status_area(GuiState& gui) {
    if (!gui.window) return;
    const UiGeometry g = ui_geometry(gui);
    RECT status{g.main_left, g.status_top, g.main_left + g.scale(20), g.height};
    RedrawWindow(gui.window, &status, nullptr, RDW_INVALIDATE);
    invalidate_control_background(gui, gui.status, 3);
}

void paint_parent_layer(HWND control, HDC dc, GuiState& gui) {
    RECT control_rect{};
    GetWindowRect(control, &control_rect);
    MapWindowPoints(nullptr, gui.window, reinterpret_cast<POINT*>(&control_rect), 2);
    const int saved = SaveDC(dc);
    SetViewportOrgEx(dc, -control_rect.left, -control_rect.top, nullptr);
    IntersectClipRect(dc, control_rect.left, control_rect.top, control_rect.right,
                      control_rect.bottom);
    render_scene(dc, gui, ui_geometry(gui));
    RestoreDC(dc, saved);
}

void draw_owner_button(const DRAWITEMSTRUCT& item, const GuiState& const_gui) {
    auto& gui = const_cast<GuiState&>(const_gui);
    const Scale s{GetDpiForWindow(gui.window)};
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0;
    const bool hovered = gui.hovered_control == item.hwndItem && !disabled;
    const float radius = static_cast<float>(s(Metrics::control_radius));
    HDC dc = item.hDC;
    paint_parent_layer(item.hwndItem, dc, gui);

    wchar_t buffer[128]{};
    GetWindowTextW(item.hwndItem, buffer, static_cast<int>(std::size(buffer)));
    const std::wstring label = buffer;
    const UINT centered = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

    if (item.CtlID >= static_cast<UINT>(kAppChipIdBase)) {
        const std::size_t index = item.CtlID - kAppChipIdBase;
        if (index < gui.apps.chips.size()) {
            draw_app_chip(item, gui, gui.apps.chips[index], s, hovered, focused);
        }
        return;
    }
    if (item.CtlID >= static_cast<UINT>(kProfileIdBase)) {
        const std::size_t index = item.CtlID - kProfileIdBase;
        if (index < gui.profiles.size()) draw_profile_row(item, gui, index, s);
        return;
    }

    switch (item.CtlID) {
    case kStartupToggle:
    case kAutoSwitchToggle: {
        const bool on = item.CtlID == kAutoSwitchToggle ? gui.app_profile_config.enabled
                                                        : gui.startup_enabled;
        RECT text = item.rcItem;
        text.right -= s(48);
        draw_text(dc, gui.normal_font, disabled ? Theme::sidebar_muted : Theme::sidebar_text,
                  label, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_toggle(dc, item.rcItem, on, !disabled, s);
        if (focused) {
            ui::aa_stroke_round_rect(dc, item.rcItem, radius, Theme::sidebar_secondary, 1.0f, 120);
        }
        return;
    }
    case kRefreshButton:
    case kPresetSave: {
        const int cx = (item.rcItem.left + item.rcItem.right) / 2;
        const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        if (pressed || hovered || focused) {
            ui::aa_fill_ellipse(dc, static_cast<float>(cx), static_cast<float>(cy),
                                static_cast<float>(s(13)), Theme::sidebar_selected,
                                pressed ? 36 : 20);
        }
        const COLORREF color = disabled ? Theme::sidebar_divider
                             : hovered  ? Theme::sidebar_text
                                        : Theme::sidebar_secondary;
        if (item.CtlID == kRefreshButton) ui::draw_refresh_icon(dc, cx, cy, s(6), color);
        else ui::draw_plus_icon(dc, cx, cy, s(6), color);
        return;
    }
    case kProfileMenu: {
        if (pressed || hovered || focused) {
            ui::aa_fill_round_rect(dc, item.rcItem, radius, Theme::control_surface,
                                   pressed ? 255 : 200);
            ui::aa_stroke_round_rect(dc, item.rcItem, radius,
                                     focused ? Theme::primary : Theme::card_border);
        }
        const float cx = (item.rcItem.left + item.rcItem.right) / 2.0f;
        const float cy = (item.rcItem.top + item.rcItem.bottom) / 2.0f;
        for (int dot = -1; dot <= 1; ++dot) {
            ui::aa_fill_ellipse(dc, cx + dot * static_cast<float>(s(6)), cy,
                                static_cast<float>(s(3)) / 2.0f,
                                disabled ? Theme::disabled_text : Theme::text_secondary);
        }
        return;
    }
    case kBeforeAfter: {
        const bool comparing = gui.session.comparing_original;
        ui::aa_fill_round_rect(dc, item.rcItem, radius,
                               comparing ? Theme::primary_soft : Theme::control_surface,
                               disabled ? 120 : hovered || comparing ? 255 : 220);
        ui::aa_stroke_round_rect(dc, item.rcItem, radius,
                                 disabled ? Theme::divider
                                 : comparing || focused || hovered ? Theme::primary
                                                                   : Theme::card_border,
                                 focused ? 1.5f : 1.0f);
        draw_text(dc, gui.section_font, disabled ? Theme::disabled_text : Theme::text, label,
                  item.rcItem, centered);
        return;
    }
    case kResetButton: {
        if (pressed || hovered || focused) {
            ui::aa_fill_round_rect(dc, item.rcItem, radius, Theme::control_surface,
                                   pressed ? 255 : 200);
            ui::aa_stroke_round_rect(dc, item.rcItem, radius,
                                     focused ? Theme::primary : Theme::card_border);
        }
        draw_text(dc, gui.section_font, disabled ? Theme::disabled_text : Theme::text_secondary,
                  label, item.rcItem, centered);
        return;
    }
    case kAddApp:
        draw_action_chip(dc, item.rcItem, gui, label, s, true, hovered, pressed, focused,
                         disabled);
        return;
    case kAppOverflow:
        draw_action_chip(dc, item.rcItem, gui, label, s, false, hovered, pressed, focused);
        return;
    case kOtherApps: {
        if (!gui.apps.holds_other) {
            draw_action_chip(dc, item.rcItem, gui, label, s, true, hovered, pressed, focused);
            return;
        }
        // Held by this profile: a filled accent pill, the one fixed chip.
        const float pill = (item.rcItem.bottom - item.rcItem.top) / 2.0f;
        ui::aa_fill_round_rect(dc, item.rcItem, pill, Theme::primary);
        if (focused) ui::aa_stroke_round_rect(dc, item.rcItem, pill, Theme::text, 1.5f);
        draw_text(dc, gui.normal_font, RGB(255, 255, 255), label, item.rcItem, centered);
        return;
    }
    case kApplyButton:
    default: {
        ui::aa_fill_round_rect(dc, item.rcItem, radius,
                               pressed ? Theme::primary_pressed
                               : hovered ? Theme::primary_hover
                                         : Theme::primary);
        draw_text(dc, gui.section_font, RGB(255, 255, 255), label, item.rcItem, centered);
        return;
    }
    }
}

void paint_background(HWND window, GuiState& gui) {
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const UiGeometry g = ui_geometry(gui, width, height);
    if (width > 0 && height > 0) {
        if (!gui.paint_buffer_dc) gui.paint_buffer_dc = CreateCompatibleDC(hdc);
        if (gui.paint_buffer_dc && (!gui.paint_buffer_bitmap || gui.paint_buffer_width != width ||
                                    gui.paint_buffer_height != height)) {
            HBITMAP replacement = CreateCompatibleBitmap(hdc, width, height);
            if (replacement) {
                HGDIOBJ previous = SelectObject(gui.paint_buffer_dc, replacement);
                if (!gui.paint_buffer_original) gui.paint_buffer_original = previous;
                if (gui.paint_buffer_bitmap) DeleteObject(gui.paint_buffer_bitmap);
                gui.paint_buffer_bitmap = replacement;
                gui.paint_buffer_width = width;
                gui.paint_buffer_height = height;
            }
        }
    }
    if (gui.paint_buffer_dc && gui.paint_buffer_bitmap && gui.paint_buffer_width == width &&
        gui.paint_buffer_height == height) {
        const int saved = SaveDC(gui.paint_buffer_dc);
        IntersectClipRect(gui.paint_buffer_dc, paint.rcPaint.left, paint.rcPaint.top,
                          paint.rcPaint.right, paint.rcPaint.bottom);
        render_scene(gui.paint_buffer_dc, gui, g);
        RestoreDC(gui.paint_buffer_dc, saved);
        BitBlt(hdc, paint.rcPaint.left, paint.rcPaint.top,
               paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
               gui.paint_buffer_dc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    } else {
        render_scene(hdc, gui, g);
    }
    EndPaint(window, &paint);
}

}  // namespace gamma_changer
