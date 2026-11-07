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

void draw_card(HDC dc, const RECT& card, const Scale& s) {
    // Two-step soft shadow, then a near-opaque white card with a hairline.
    RECT shadow = card;
    OffsetRect(&shadow, 0, s(2));
    ui::aa_fill_round_rect(dc, shadow, static_cast<float>(s(Metrics::card_radius)), RGB(40, 20, 60), 10);
    OffsetRect(&shadow, 0, -s(1));
    InflateRect(&shadow, 1, 1);
    ui::aa_fill_round_rect(dc, shadow, static_cast<float>(s(Metrics::card_radius)), RGB(40, 20, 60), 8);
    ui::aa_fill_round_rect(dc, card, static_cast<float>(s(Metrics::card_radius)), Theme::card, 238);
    ui::aa_stroke_round_rect(dc, card, static_cast<float>(s(Metrics::card_radius)),
                             Theme::card_border, 1.0f, 200);
}

void draw_curve_card(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    draw_card(dc, g.curve_card, s);
    const RECT& plot = g.curve_plot;
    if (plot.right - plot.left < s(40) || plot.bottom - plot.top < s(40)) return;

    for (int i = 1; i < 4; ++i) {
        const float x = plot.left + (plot.right - plot.left) * i / 4.0f;
        const float y = plot.top + (plot.bottom - plot.top) * i / 4.0f;
        ui::aa_line(dc, x, static_cast<float>(plot.top), x, static_cast<float>(plot.bottom),
                    Theme::grid, 1.0f);
        ui::aa_line(dc, static_cast<float>(plot.left), y, static_cast<float>(plot.right), y,
                    Theme::grid, 1.0f);
    }
    ui::aa_stroke_round_rect(dc, plot, static_cast<float>(s(4)), Theme::grid, 1.0f);
    ui::aa_line(dc, static_cast<float>(plot.left), static_cast<float>(plot.bottom),
                static_cast<float>(plot.right), static_cast<float>(plot.top),
                Theme::reference_curve, 1.4f, 255, true);

    const CalibrationSettings settings = params_from_sliders(gui);
    const GammaRamp ramp = gui.controller.generate_preview(settings);
    const auto curve_points = [&](std::size_t channel) {
        std::vector<ui::CurvePoint> points;
        constexpr int kSamples = 128;
        points.reserve(kSamples + 1);
        for (int i = 0; i <= kSamples; ++i) {
            const double input = i / static_cast<double>(kSamples);
            const auto sample = static_cast<std::size_t>(input * (kRampSize - 1) + 0.5);
            const double output = ramp.channel[channel][sample] / 65535.0;
            points.push_back({static_cast<float>(plot.left + (plot.right - plot.left) * input),
                              static_cast<float>(plot.bottom - (plot.bottom - plot.top) * output)});
        }
        return points;
    };
    const bool neutral = settings.r_gain == settings.g_gain && settings.g_gain == settings.b_gain;
    if (neutral) {
        const auto points = curve_points(0);
        ui::aa_curve(dc, points.data(), static_cast<int>(points.size()), Theme::primary, 2.4f,
                     plot.bottom, 26);
    } else {
        const COLORREF colors[3] = {Theme::curve_red, Theme::curve_green, Theme::curve_blue};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto points = curve_points(channel);
            ui::aa_curve(dc, points.data(), static_cast<int>(points.size()), colors[channel], 2.0f);
        }
    }

    // Legend, right-aligned on the title row.
    struct Entry {
        COLORREF color;
        const wchar_t* label;
        bool dashed;
    };
    std::vector<Entry> legend{{Theme::reference_curve, L"Linear", true}};
    if (neutral) {
        legend.push_back({Theme::primary, L"Output", false});
    } else {
        legend.push_back({Theme::curve_red, L"R", false});
        legend.push_back({Theme::curve_green, L"G", false});
        legend.push_back({Theme::curve_blue, L"B", false});
    }
    int x = g.curve_card.right - s(20);
    const int y = g.curve_card.top + s(28);
    for (auto entry = legend.rbegin(); entry != legend.rend(); ++entry) {
        const int width = text_width(dc, gui.small_font, entry->label);
        x -= width;
        draw_text(dc, gui.small_font, Theme::text_secondary, entry->label,
                  RECT{x, y - s(10), x + width, y + s(10)}, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x -= s(6) + s(16);
        ui::aa_line(dc, static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(x + s(16)), static_cast<float>(y), entry->color, 2.0f, 255,
                    entry->dashed);
        x -= s(14);
    }
}

void draw_sidebar_layer(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    const int P = g.padding;
    // Section dividers.
    ui::aa_line(dc, static_cast<float>(P), static_cast<float>(g.apps_top - s(12)),
                static_cast<float>(g.sidebar_width - P), static_cast<float>(g.apps_top - s(12)),
                Theme::sidebar_divider, 1.0f);
    ui::aa_line(dc, static_cast<float>(P), static_cast<float>(g.sidebar_divider_y),
                static_cast<float>(g.sidebar_width - P), static_cast<float>(g.sidebar_divider_y),
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
        const int x = g.sidebar_width - s(6);
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

void draw_footer(HDC dc, const GuiState& gui, const UiGeometry& g) {
    const Scale& s = g.scale;
    RECT footer{g.sidebar_width, g.footer_top, g.width, g.height};
    ui::fill_layered_surface(dc, footer, Theme::card, 150);
    ui::aa_line(dc, static_cast<float>(g.sidebar_width), static_cast<float>(g.footer_top),
                static_cast<float>(g.width), static_cast<float>(g.footer_top), Theme::divider, 1.0f);

    COLORREF color = Theme::idle;
    switch (gui.status_tone) {
    case StatusTone::success: color = Theme::success; break;
    case StatusTone::warning: color = Theme::warning; break;
    case StatusTone::error: color = Theme::error; break;
    case StatusTone::idle: break;
    }
    const float cy = g.footer_top + s(Metrics::footer_height) / 2.0f;
    const float cx = g.main_left + static_cast<float>(s(7));
    ui::aa_fill_ellipse(dc, cx, cy, static_cast<float>(s(7)), color, 40);
    ui::aa_fill_ellipse(dc, cx, cy, static_cast<float>(s(4)), color);
}

void render_scene(HDC dc, GuiState& gui, const UiGeometry& g) {
    const RECT client{0, 0, g.width, g.height};
    gui.background.draw(dc, client, g.sidebar_width);
    draw_sidebar_layer(dc, gui, g);
    draw_curve_card(dc, gui, g);
    draw_card(dc, g.tone_card, g.scale);
    draw_card(dc, g.color_card, g.scale);
    draw_value_boxes(dc, gui, g);
    draw_footer(dc, gui, g);
}

// ---- Owner-drawn controls ---------------------------------------------------

void draw_toggle(HDC dc, const RECT& rect, bool on, bool paused, bool enabled, const Scale& s) {
    const int height = s(20);
    const int width = s(36);
    const RECT track{rect.right - width, rect.top + (rect.bottom - rect.top - height) / 2,
                     rect.right, rect.top + (rect.bottom - rect.top + height) / 2};
    const COLORREF color = !enabled ? Theme::toggle_off
                         : paused   ? Theme::warning
                         : on       ? Theme::primary
                                    : Theme::toggle_off;
    ui::aa_fill_round_rect(dc, track, height / 2.0f, color);
    const float radius = height / 2.0f - s(3);
    const float cx = (on || paused) ? track.right - height / 2.0f : track.left + height / 2.0f;
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
    const RECT card = ui_geometry(gui).curve_card;
    RedrawWindow(gui.window, &card, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void invalidate_status_area(GuiState& gui) {
    if (!gui.window) return;
    const UiGeometry g = ui_geometry(gui);
    RECT status{g.main_left, g.footer_top + 1, g.main_left + g.scale(60), g.height};
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
    const float radius = static_cast<float>(s(Metrics::control_radius));
    HDC dc = item.hDC;
    paint_parent_layer(item.hwndItem, dc, gui);

    wchar_t buffer[128]{};
    GetWindowTextW(item.hwndItem, buffer, static_cast<int>(std::size(buffer)));
    const std::wstring label = buffer;
    const UINT centered = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

    if (item.CtlID >= static_cast<UINT>(kProfileIdBase) &&
        item.CtlID - kProfileIdBase < gui.profiles.size()) {
        draw_profile_row(item, gui, item.CtlID - kProfileIdBase, s);
        return;
    }

    switch (item.CtlID) {
    case kStartupToggle:
    case kAutoSwitchToggle: {
        const bool automatic = item.CtlID == kAutoSwitchToggle;
        const bool on = automatic ? gui.app_profile_config.enabled : gui.startup_enabled;
        const bool paused = automatic && on && gui.auto_switch_paused;
        RECT text = item.rcItem;
        text.right -= s(48);
        draw_text(dc, gui.normal_font, disabled ? Theme::sidebar_muted : Theme::sidebar_text,
                  label, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_toggle(dc, item.rcItem, on, paused, !disabled, s);
        return;
    }
    case kRefreshButton:
    case kPresetSave: {
        const int cx = (item.rcItem.left + item.rcItem.right) / 2;
        const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        if (pressed) {
            ui::aa_fill_ellipse(dc, static_cast<float>(cx), static_cast<float>(cy),
                                static_cast<float>(s(13)), Theme::sidebar_selected, 30);
        }
        const COLORREF color = disabled ? Theme::sidebar_divider : Theme::sidebar_secondary;
        if (item.CtlID == kRefreshButton) ui::draw_refresh_icon(dc, cx, cy, s(6), color);
        else ui::draw_plus_icon(dc, cx, cy, s(6), color);
        return;
    }
    case kAppRules: {
        ui::aa_fill_round_rect(dc, item.rcItem, radius,
                               pressed ? Theme::sidebar_field_border : Theme::sidebar_field);
        ui::aa_stroke_round_rect(dc, item.rcItem, radius,
                                 focused ? Theme::sidebar_secondary : Theme::sidebar_field_border);
        draw_text(dc, gui.normal_font, disabled ? Theme::sidebar_muted : Theme::sidebar_text,
                  label, item.rcItem, centered);
        return;
    }
    case kApplyButton: {
        const COLORREF fill = disabled ? Theme::primary_soft
                            : pressed  ? Theme::primary_pressed
                                       : Theme::primary;
        ui::aa_fill_round_rect(dc, item.rcItem, radius, fill);
        draw_text(dc, gui.section_font, disabled ? Theme::primary : RGB(255, 255, 255), label,
                  item.rcItem, centered);
        return;
    }
    case kBeforeAfter: {
        ui::aa_fill_round_rect(dc, item.rcItem, radius,
                               pressed ? Theme::primary_soft : Theme::secondary_surface,
                               disabled ? 120 : 255);
        ui::aa_stroke_round_rect(dc, item.rcItem, radius,
                                 disabled ? Theme::divider
                                 : (pressed || gui.session.comparing_original) ? Theme::primary
                                                                               : Theme::card_border);
        draw_text(dc, gui.section_font, disabled ? Theme::disabled_text : Theme::text, label,
                  item.rcItem, centered);
        return;
    }
    case kResetButton: {
        if (pressed) ui::aa_fill_round_rect(dc, item.rcItem, radius, Theme::divider);
        draw_text(dc, gui.section_font, disabled ? Theme::disabled_text : Theme::text_secondary,
                  label, item.rcItem, centered);
        return;
    }
    default:
        ui::aa_fill_round_rect(dc, item.rcItem, radius, Theme::primary);
        draw_text(dc, gui.normal_font, RGB(255, 255, 255), label, item.rcItem, centered);
        return;
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
