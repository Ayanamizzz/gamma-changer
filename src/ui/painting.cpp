#include "gui_internal.h"

namespace gamma_changer {

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

void draw_owner_button(const DRAWITEMSTRUCT& item, const GuiState& gui) {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const auto draw_focus_frame = [&](COLORREF color) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -2, -2);
        HPEN focus_pen = CreatePen(PS_SOLID, 2, color);
        if (!focus_pen) return;
        const auto old_focus_pen = SelectObject(item.hDC, focus_pen);
        const auto old_focus_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        RoundRect(item.hDC, focus.left, focus.top, focus.right, focus.bottom,
                  ui::Metrics::control_radius, ui::Metrics::control_radius);
        SelectObject(item.hDC, old_focus_brush);
        SelectObject(item.hDC, old_focus_pen);
        DeleteObject(focus_pen);
    };

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
        if (focused) draw_focus_frame(ui::Theme::primary_soft);
        if (old_font) SelectObject(item.hDC, old_font);
        return;
    }

    const bool profile_item =
        item.CtlID >= kProfileIdBase &&
        static_cast<std::size_t>(item.CtlID - kProfileIdBase) < gui.profiles.size();
    const std::size_t profile_index =
        profile_item ? static_cast<std::size_t>(item.CtlID - kProfileIdBase) : 0;
    const UINT profile_dpi = profile_item ? GetDpiForWindow(item.hwndItem) : 96;
    const auto profile_scale = [profile_dpi](int value) {
        return MulDiv(value, static_cast<int>(profile_dpi), 96);
    };

    COLORREF fill = ui::Theme::primary;
    COLORREF text = ui::Theme::control_surface;
    COLORREF outline = fill;
    if (profile_item) {
        if (gui.active_profile_linked && profile_index == gui.active_preset) {
            fill = ui::Theme::sidebar_selected;
            text = ui::Theme::control_surface;
            outline = ui::Theme::sidebar_selected;
        } else {
            fill = ui::Theme::sidebar;
            text = ui::Theme::sidebar_text;
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
                                 item.CtlID == kPresetDelete || profile_item;
    if (sidebar_button) {
        paint_parent_layer(item.hwndItem, item.hDC, const_cast<GuiState&>(gui));
    }

    const bool selected_profile = profile_item && gui.active_profile_linked &&
                                  profile_index == gui.active_preset;
    const bool editing_profile = profile_item &&
                                 profile_index == gui.renaming_preset;
    const bool hovered_profile = profile_item && gui.hovered_profile == item.hwndItem;
    const bool ghost_action = item.CtlID == kRefreshButton || item.CtlID == kPresetDelete;
    const int profile_radius = profile_scale(ui::Metrics::control_radius);
    if (editing_profile) {
        // The owner-drawn row remains the visual host while the native EDIT
        // overlays only its text area. A solid editing surface also matches the
        // EDIT brush, avoiding a rectangular light island in the Sidebar.
        ui::draw_panel(item.hDC, item.rcItem, ui::Theme::sidebar_selected,
                       ui::Theme::primary, profile_radius);
    } else if (selected_profile) {
        ui::draw_layered_panel(item.hDC, item.rcItem, ui::Theme::sidebar_selected, 142,
                               CLR_INVALID, profile_radius);
    } else if (hovered_profile) {
        ui::draw_layered_panel(item.hDC, item.rcItem, ui::Theme::sidebar_selected, 72,
                               CLR_INVALID, profile_radius);
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

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = font ? SelectObject(item.hDC, font) : nullptr;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    const UINT alignment = profile_item
                               ? DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
                               : DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    RECT label_rect = item.rcItem;
    if (profile_item) {
        label_rect.left += profile_scale(ui::Metrics::profile_text_inset);
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
    } else if (!editing_profile) {
        DrawTextW(item.hDC, label, -1, &label_rect, alignment);
    }
    if (profile_item && (item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -profile_scale(3), -profile_scale(2));
        HPEN focus_pen = CreatePen(PS_SOLID, 1, ui::Theme::sidebar_secondary);
        const auto old_focus_pen = SelectObject(item.hDC, focus_pen);
        const auto old_focus_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        RoundRect(item.hDC, focus.left, focus.top, focus.right, focus.bottom,
                  profile_radius, profile_radius);
        SelectObject(item.hDC, old_focus_brush);
        SelectObject(item.hDC, old_focus_pen);
        DeleteObject(focus_pen);
    }
    if (profile_item && gui.active_profile_linked &&
        profile_index == gui.active_preset) {
        HBRUSH indicator = CreateSolidBrush(ui::Theme::primary);
        HGDIOBJ old_indicator = SelectObject(item.hDC, indicator);
        RECT accent{item.rcItem.left + profile_scale(3),
                    item.rcItem.top + profile_scale(6),
                    item.rcItem.left + profile_scale(6),
                    item.rcItem.bottom - profile_scale(6)};
        FillRect(item.hDC, &accent, indicator);
        SelectObject(item.hDC, old_indicator);
        DeleteObject(indicator);
    }
    if (!profile_item && focused) {
        const COLORREF focus_color = item.CtlID == kApplyButton
                                         ? ui::Theme::control_surface
                                         : (sidebar_button ? ui::Theme::primary_soft
                                                           : ui::Theme::primary);
        draw_focus_frame(focus_color);
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

    if (gui.profiles.size() > kProfileVisibleRows) {
        const int row_height = scale(ui::Metrics::profile_row_height);
        const int row_stride = row_height + scale(ui::Metrics::profile_row_gap);
        const int first_y = scale(252) + scale(18) +
                            scale(ui::Metrics::profile_title_gap);
        const int list_bottom = first_y + kProfileVisibleRows * row_height +
                                (kProfileVisibleRows - 1) * scale(ui::Metrics::profile_row_gap);
        const int track_width = scale(4);
        const int track_x = sidebar_width - scale(12);
        RECT track{track_x, first_y + 2, track_x + track_width, list_bottom - 2};
        ui::draw_panel(hdc, track, ui::Theme::track_inactive,
                       ui::Theme::track_inactive, 2);
        const int visible_height = list_bottom - first_y;
        const int total_height = static_cast<int>(gui.profiles.size()) * row_stride;
        const int max_scroll = std::max(1, total_height - visible_height);
        const int thumb_height = std::max(scale(18),
            static_cast<int>(static_cast<long long>(visible_height) * visible_height /
                             std::max(1, total_height)));
        const int thumb_travel = std::max(1, visible_height - thumb_height);
        const int thumb_y = first_y + static_cast<int>(
            static_cast<long long>(gui.profile_scroll_offset) * thumb_travel / max_scroll);
        RECT thumb{track_x, thumb_y, track_x + track_width, thumb_y + thumb_height};
        ui::draw_panel(hdc, thumb, ui::Theme::primary, ui::Theme::primary, 2);
    }

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

}  // namespace gamma_changer
