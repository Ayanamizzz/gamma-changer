#include "gui_internal.h"

namespace gamma_changer {
namespace {

using ui::Metrics;

RECT make_rect(int left, int top, int width, int height) {
    return RECT{left, top, left + width, top + height};
}

void place(HWND control, const RECT& rect) {
    if (control) {
        MoveWindow(control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   TRUE);
    }
}

// Visual slot (top to bottom) for each adjustment, in adjustment_bindings()
// order: gamma, brightness, contrast, red, green, blue. Tone reads top-down as
// brightness, contrast, gamma; the colour gains follow below a hairline.
constexpr int kAdjustmentSlot[6] = {2, 0, 1, 3, 4, 5};

// Lays `widths` out left to right, wrapping at `right`. Returns the row count.
int flow_chips(const std::vector<int>& widths, std::vector<RECT>& rects, int left, int right,
               int top, int height, int gap) {
    rects.assign(widths.size(), RECT{});
    int x = left;
    int row = 0;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        if (x > left && x + widths[i] > right) {
            x = left;
            ++row;
        }
        const int y = top + row * (height + gap);
        rects[i] = RECT{x, y, x + widths[i], y + height};
        x += widths[i] + gap;
    }
    return widths.empty() ? 1 : row + 1;
}

// Chips of the Apps card in reading order: the "All other apps" chip leads
// when this profile holds it, then the apps, "+N more", "Add app", and the
// offer to take over the other apps last. Apps fold into "+N more" until the
// card needs at most Metrics::chip_rows_max rows.
int place_app_chips(const AppChipModel& model, UiGeometry& g, int left, int right, int top) {
    const Scale& s = g.scale;
    const int height = s(Metrics::chip_height);
    const int gap = s(Metrics::chip_gap);
    const std::size_t count = model.chips.size();
    for (std::size_t visible = count;; --visible) {
        std::vector<int> widths;
        std::vector<RECT*> targets;
        std::vector<RECT> chip_rects(count);
        RECT other{};
        RECT overflow{};
        RECT add{};
        if (model.show_other && model.holds_other) {
            widths.push_back(model.other_width);
            targets.push_back(&other);
        }
        for (std::size_t i = 0; i < visible; ++i) {
            widths.push_back(model.chips[i].width);
            targets.push_back(&chip_rects[i]);
        }
        if (visible < count) {
            widths.push_back(model.overflow_width);
            targets.push_back(&overflow);
        }
        widths.push_back(model.add_width);
        targets.push_back(&add);
        if (model.show_other && !model.holds_other) {
            widths.push_back(model.other_width);
            targets.push_back(&other);
        }
        std::vector<RECT> rects;
        const int rows = flow_chips(widths, rects, left, right, top, height, gap);
        if (rows > Metrics::chip_rows_max && visible > 0) continue;
        for (std::size_t i = 0; i < rects.size(); ++i) *targets[i] = rects[i];
        g.chip_rects = std::move(chip_rects);
        g.other_apps = other;
        g.overflow = overflow;
        g.add_app = add;
        g.hidden_chips = static_cast<int>(count - visible);
        return std::min(rows, static_cast<int>(Metrics::chip_rows_max));
    }
}

bool font_face_available(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(query.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(
        dc, &query,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found_flag) -> int {
            *reinterpret_cast<bool*>(found_flag) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

const wchar_t* display_face() {
    static const wchar_t* face =
        font_face_available(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display"
                                                          : L"Segoe UI";
    return face;
}

const wchar_t* text_face() {
    static const wchar_t* face =
        font_face_available(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    return face;
}

HFONT make_font(UINT dpi, int points_x10, int weight, const wchar_t* face) {
    return CreateFontW(-MulDiv(points_x10, static_cast<int>(dpi), 720), 0, 0, 0, weight, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

int font_height(HFONT font) {
    HDC dc = GetDC(nullptr);
    const auto old = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return metrics.tmHeight;
}

}  // namespace

UiGeometry ui_geometry(const GuiState& gui, int width, int height) {
    UiGeometry g;
    g.scale = Scale{gui.window ? GetDpiForWindow(gui.window) : 96u};
    const Scale& s = g.scale;
    g.width = width;
    g.height = height;
    g.sidebar_width = s(Metrics::sidebar_width);
    g.padding = s(Metrics::sidebar_padding);

    // Sidebar: display, flexible profile list, bottom-anchored switches.
    g.automation_top = height - s(126);
    g.profile_row_height = s(Metrics::profile_row_height);
    g.profile_row_stride = g.profile_row_height + s(Metrics::profile_row_gap);
    const int list_top = s(142);
    g.profile_list = RECT{s(Metrics::profile_row_inset), list_top,
                          g.sidebar_width - s(Metrics::profile_row_inset),
                          std::max(list_top + g.profile_row_height, g.automation_top - s(12))};

    // Work area, top to bottom: profile header, editor card (flexible), apps
    // card (as tall as its chips need), status line.
    g.main_left = g.sidebar_width + s(Metrics::main_gap);
    g.main_right = std::max(g.main_left + s(420), width - s(Metrics::main_gap));
    g.status_top = height - s(Metrics::status_height);

    const int action_top = s(22);
    const int action_height = s(32);
    g.menu_button = RECT{g.main_right - s(36), action_top, g.main_right, action_top + action_height};
    g.reset_button = RECT{g.menu_button.left - s(8) - s(68), action_top, g.menu_button.left - s(8),
                          action_top + action_height};
    g.compare_button = RECT{g.reset_button.left - s(8) - s(138), action_top,
                            g.reset_button.left - s(8), action_top + action_height};
    g.header_title = RECT{g.main_left, s(14), g.compare_button.left - s(16), s(50)};
    g.header_subtitle = RECT{g.main_left, s(50), g.compare_button.left - s(16), s(72)};

    const int apps_inset = s(18);
    const int chips_top_offset = s(14) + s(22) + s(10);
    // Chip rects are placed relative to y = 0 first, then moved under the card top.
    const int chip_rows = place_app_chips(gui.apps, g, g.main_left + apps_inset,
                                          g.main_right - apps_inset, 0);
    const int apps_height = chips_top_offset + chip_rows * s(Metrics::chip_height) +
                            (chip_rows - 1) * s(Metrics::chip_gap) + s(16);
    const int apps_bottom = g.status_top - s(4);
    const int editor_top = s(Metrics::header_height);
    const int editor_min = s(Metrics::card_padding) * 2 + 6 * s(Metrics::adjustment_row_min) +
                           s(Metrics::adjustment_group_gap);
    const int apps_top = std::max(editor_top + editor_min + s(Metrics::card_gap),
                                  apps_bottom - apps_height);
    g.apps_card = RECT{g.main_left, apps_top, g.main_right, apps_top + apps_height};
    g.apps_heading = RECT{g.main_left + apps_inset, apps_top + s(14), g.main_right - apps_inset,
                          apps_top + s(14) + s(22)};
    const auto move_down = [&](RECT& rect) {
        if (rect.right > rect.left) OffsetRect(&rect, 0, apps_top + chips_top_offset);
    };
    for (RECT& rect : g.chip_rects) move_down(rect);
    move_down(g.add_app);
    move_down(g.other_apps);
    move_down(g.overflow);

    g.editor_card = RECT{g.main_left, editor_top, g.main_right, apps_top - s(Metrics::card_gap)};
    const RECT inner{g.editor_card.left + s(Metrics::card_padding),
                     g.editor_card.top + s(Metrics::card_padding),
                     g.editor_card.right - s(Metrics::card_padding),
                     g.editor_card.bottom - s(Metrics::card_padding)};
    const int inner_width = inner.right - inner.left;
    const int inner_height = inner.bottom - inner.top;
    // A tone curve maps input to output on equal axes, so the plot is square.
    // It takes up to half the card but always leaves the sliders a usable run.
    const int side = std::max(
        s(80), std::min({inner_height, inner_width / 2, inner_width - s(28) - s(310)}));
    const int plot_top = inner.top + (inner_height - side) / 2;
    g.curve_plot = RECT{inner.left, plot_top, inner.left + side, plot_top + side};

    g.controls_left = g.curve_plot.right + s(28);
    g.controls_right = inner.right;
    const int group_gap = s(Metrics::adjustment_group_gap);
    const int stride = std::clamp((inner_height - group_gap) / 6, s(Metrics::adjustment_row_min),
                                  s(Metrics::adjustment_row_max));
    const int block_top = inner.top + (inner_height - (6 * stride + group_gap)) / 2;
    g.group_divider_y = block_top + 3 * stride + group_gap / 2;
    const int box_height = s(Metrics::value_box_height);
    const int text_height = gui.input_text_height > 0 ? gui.input_text_height : s(18);
    for (std::size_t index = 0; index < g.rows.size(); ++index) {
        const int slot = kAdjustmentSlot[index];
        const int top = block_top + slot * stride + (slot >= 3 ? group_gap : 0) +
                        (stride - box_height) / 2;
        auto& row = g.rows[index];
        row.label = make_rect(g.controls_left, top + (box_height - s(22)) / 2,
                              s(Metrics::adjustment_label_width), s(22));
        row.value_box = make_rect(g.controls_right - s(Metrics::value_box_width), top,
                                  s(Metrics::value_box_width), box_height);
        const int slider_left = row.label.right + s(4);
        row.slider = RECT{slider_left, top,
                          std::max<LONG>(slider_left + s(60), row.value_box.left - s(14)),
                          top + box_height};
        const int edit_top = row.value_box.top + (box_height - text_height) / 2;
        row.value_edit = RECT{row.value_box.left + s(8), edit_top, row.value_box.right - s(10),
                              edit_top + text_height};
    }
    return g;
}

UiGeometry ui_geometry(const GuiState& gui) {
    RECT client{};
    if (gui.window) GetClientRect(gui.window, &client);
    return ui_geometry(gui, client.right - client.left, client.bottom - client.top);
}

RECT profile_row_rect(const UiGeometry& g, std::size_t index, int scroll_offset) {
    const int top =
        g.profile_list.top + static_cast<int>(index) * g.profile_row_stride - scroll_offset;
    return RECT{g.profile_list.left, top, g.profile_list.right, top + g.profile_row_height};
}

void layout_controls(GuiState& gui, int width, int height) {
    const UiGeometry g = ui_geometry(gui, width, height);
    const Scale& s = g.scale;
    const int P = g.padding;
    const int SW = g.sidebar_width;
    const int content = SW - 2 * P;
    const int icon = s(28);

    // ---- Sidebar: display
    place(gui.display_caption, make_rect(P, s(20), content - icon, s(18)));
    place(gui.refresh_button, make_rect(SW - P - icon + s(6), s(15), icon, icon));
    // The window height includes the drop-down list; the field height is set
    // separately with CB_SETITEMHEIGHT(-1).
    place(gui.display_combo, make_rect(P, s(44), content, s(260)));
    SendMessageW(gui.display_combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 s(Metrics::control_height) - 6);
    SendMessageW(gui.display_combo, CB_SETDROPPEDWIDTH,
                 static_cast<WPARAM>(std::max(content, s(320))), 0);
    place(gui.display_status, make_rect(P, s(83), content, s(18)));

    // ---- Sidebar: profiles
    place(gui.preset_caption, make_rect(P, s(118), content - icon, s(18)));
    place(gui.preset_save, make_rect(SW - P - icon + s(6), s(113), icon, icon));
    const int visible_height = g.profile_list.bottom - g.profile_list.top;
    gui.profile_scroll_offset = clamp_profile_scroll(gui.profile_scroll_offset,
                                                     gui.profiles.size(), g.profile_row_stride,
                                                     visible_height);
    ensure_profile_buttons(gui);
    for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
        const RECT row = profile_row_rect(g, i, gui.profile_scroll_offset);
        const bool visible = row.bottom > g.profile_list.top && row.top < g.profile_list.bottom &&
                             i != gui.renaming_preset;
        place(gui.preset_buttons[i], row);
        ShowWindow(gui.preset_buttons[i], visible ? SW_SHOW : SW_HIDE);
    }
    if (gui.profile_rename && gui.renaming_preset < gui.profiles.size()) {
        const RECT row = profile_row_rect(g, gui.renaming_preset, gui.profile_scroll_offset);
        const int row_height = row.bottom - row.top;
        const int text_height = gui.input_text_height > 0 ? gui.input_text_height : s(18);
        // Vertically centred single-line edit inside the painted row highlight.
        const int edit_top = row.top + (row_height - text_height) / 2;
        place(gui.profile_rename, RECT{row.left + s(Metrics::profile_text_inset), edit_top,
                                       row.right - s(8), edit_top + text_height});
    }

    // ---- Sidebar: switches (bottom-anchored)
    place(gui.auto_switch_button, make_rect(P, g.automation_top + s(12), content, s(32)));
    place(gui.auto_switch_status, make_rect(P, g.automation_top + s(44), content, s(18)));
    place(gui.startup_button, make_rect(P, g.automation_top + s(78), content, s(32)));

    // ---- Work area: profile header
    place(gui.main_heading, g.header_title);
    place(gui.main_subtitle, g.header_subtitle);
    place(gui.before_after_button, g.compare_button);
    place(gui.reset_button, g.reset_button);
    place(gui.profile_menu_button, g.menu_button);

    // ---- Editor card
    const AdjustmentBindings bindings = adjustment_bindings(gui);
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        place(bindings[i].label, g.rows[i].label);
        place(bindings[i].slider, g.rows[i].slider);
        place(bindings[i].edit, g.rows[i].value_edit);
    }

    // ---- Apps card
    const int note_width = s(180);
    place(gui.apps_heading, RECT{g.apps_heading.left, g.apps_heading.top,
                                 g.apps_heading.right - note_width, g.apps_heading.bottom});
    place(gui.apps_note, RECT{g.apps_heading.right - note_width, g.apps_heading.top + s(2),
                              g.apps_heading.right, g.apps_heading.bottom});
    ensure_app_chip_buttons(gui);
    const auto place_chip = [](HWND chip, const RECT& rect) {
        if (!chip) return;
        const bool visible = rect.right > rect.left;
        if (visible) place(chip, rect);
        ShowWindow(chip, visible ? SW_SHOW : SW_HIDE);
    };
    for (std::size_t i = 0; i < gui.app_chip_buttons.size(); ++i) {
        place_chip(gui.app_chip_buttons[i], i < g.chip_rects.size() ? g.chip_rects[i] : RECT{});
    }
    place_chip(gui.add_app_button, g.add_app);
    place_chip(gui.other_apps_button, g.other_apps);
    if (gui.app_overflow_button && g.hidden_chips > 0) {
        SetWindowTextW(gui.app_overflow_button,
                       (L"+" + std::to_wstring(g.hidden_chips) + L" more").c_str());
    }
    place_chip(gui.app_overflow_button, g.overflow);

    // ---- Status line
    const int status_height = s(Metrics::status_height);
    const int retry_width = s(104);
    const int retry_height = s(28);
    const RECT retry{g.main_right - retry_width, g.status_top + (status_height - retry_height) / 2,
                     g.main_right, g.status_top + (status_height + retry_height) / 2};
    place(gui.apply_button, retry);
    const int status_left = g.main_left + s(20);
    place(gui.status, RECT{status_left, g.status_top + (status_height - s(20)) / 2,
                           std::max<LONG>(status_left + s(120), retry.left - s(12)),
                           g.status_top + (status_height + s(20)) / 2});
}


void layout_controls_for_current_size(GuiState& gui) {
    if (!gui.window) return;
    RECT client{};
    GetClientRect(gui.window, &client);
    layout_controls(gui, client.right, client.bottom);
}


void apply_font(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void recreate_fonts(GuiState& gui, UINT dpi) {
    const std::array<HFONT, 5> old{gui.normal_font, gui.heading_font, gui.section_font,
                                   gui.caption_font, gui.small_font};

    gui.normal_font = make_font(dpi, 100, FW_NORMAL, text_face());
    gui.heading_font = make_font(dpi, 190, FW_SEMIBOLD, display_face());
    gui.section_font = make_font(dpi, 100, FW_SEMIBOLD, text_face());
    gui.caption_font = make_font(dpi, 90, FW_SEMIBOLD, text_face());
    gui.small_font = make_font(dpi, 90, FW_NORMAL, text_face());
    gui.input_text_height = font_height(gui.normal_font);

    const std::array<std::pair<HWND, HFONT>, 13> fonts{{
        {gui.display_caption, gui.caption_font},
        {gui.preset_caption, gui.caption_font},
        {gui.display_combo, gui.normal_font},
        {gui.display_status, gui.small_font},
        {gui.auto_switch_button, gui.normal_font},
        {gui.auto_switch_status, gui.small_font},
        {gui.startup_button, gui.normal_font},
        {gui.profile_rename, gui.normal_font},
        {gui.main_heading, gui.heading_font},
        {gui.main_subtitle, gui.normal_font},
        {gui.apps_heading, gui.section_font},
        {gui.apps_note, gui.small_font},
        {gui.status, gui.normal_font},
    }};
    for (const auto& [control, font] : fonts) apply_font(control, font);
    for (HWND button : {gui.apply_button, gui.reset_button, gui.before_after_button}) {
        apply_font(button, gui.section_font);
    }
    for (const auto& binding : adjustment_bindings(gui)) {
        apply_font(binding.label, gui.normal_font);
        apply_font(binding.edit, gui.normal_font);
    }
    for (HWND button : gui.preset_buttons) apply_font(button, gui.normal_font);

    for (HFONT font : old) {
        if (font) DeleteObject(font);
    }
    // Chip widths are measured with these fonts.
    refresh_app_chips(gui, true);
}

}  // namespace gamma_changer
