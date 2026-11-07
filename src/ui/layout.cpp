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

// Card slot (0..2) for each adjustment, in adjustment_bindings() order:
// gamma, brightness, contrast | red, green, blue. Tone reads top-down as
// brightness, contrast, gamma.
constexpr int kAdjustmentSlot[6] = {2, 0, 1, 0, 1, 2};

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

    // Sidebar: fixed header, flexible profile list, bottom-anchored app block.
    g.sidebar_divider_y = height - s(72);
    g.apps_top = g.sidebar_divider_y - s(20) - s(128);
    g.profile_row_height = s(Metrics::profile_row_height);
    g.profile_row_stride = g.profile_row_height + s(Metrics::profile_row_gap);
    g.profile_list = RECT{s(Metrics::profile_row_inset), s(228),
                          g.sidebar_width - s(Metrics::profile_row_inset),
                          std::max(s(228) + g.profile_row_height, g.apps_top - s(20))};

    // Work area: header, flexible curve card, two fixed adjustment cards, footer.
    g.main_left = g.sidebar_width + s(Metrics::main_gap);
    g.main_right = std::max(g.main_left + s(320), width - s(Metrics::main_gap));
    g.footer_top = height - s(Metrics::footer_height);
    const int region_bottom = g.footer_top - s(20);
    const int card_height = s(Metrics::adjustment_card_height);
    const int gap = s(Metrics::card_gap);
    g.color_card = RECT{g.main_left, region_bottom - card_height, g.main_right, region_bottom};
    g.tone_card = RECT{g.main_left, g.color_card.top - gap - card_height, g.main_right,
                       g.color_card.top - gap};
    g.curve_card = RECT{g.main_left, s(96), g.main_right,
                        std::max<LONG>(s(96) + s(120), g.tone_card.top - gap)};
    g.curve_plot = RECT{g.curve_card.left + s(20), g.curve_card.top + s(50),
                        g.curve_card.right - s(20), g.curve_card.bottom - s(18)};

    const int text_height = gui.input_text_height > 0 ? gui.input_text_height : s(18);
    for (std::size_t index = 0; index < g.rows.size(); ++index) {
        const RECT& card = index < 3 ? g.tone_card : g.color_card;
        const int top = card.top + s(54) + kAdjustmentSlot[index] * s(Metrics::adjustment_row_stride);
        auto& row = g.rows[index];
        row.label = make_rect(card.left + s(20), top + s(5), s(Metrics::adjustment_label_width),
                              s(22));
        row.value_box = make_rect(card.right - s(20) - s(Metrics::value_box_width), top,
                                  s(Metrics::value_box_width), s(Metrics::value_box_height));
        const int slider_left = row.label.right + s(4);
        row.slider = RECT{slider_left, top, std::max<LONG>(slider_left + s(80), row.value_box.left - s(20)),
                          top + s(Metrics::value_box_height)};
        const int edit_top = row.value_box.top + (s(Metrics::value_box_height) - text_height) / 2;
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

    // ---- Sidebar header
    place(gui.title, make_rect(P, s(24), content, s(30)));
    place(gui.subtitle, make_rect(P, s(54), content, s(18)));
    place(gui.display_caption, make_rect(P, s(102), content - icon, s(16)));
    place(gui.refresh_button, make_rect(SW - P - icon + s(6), s(96), icon, icon));
    // The window height includes the drop-down list; the field height is set
    // separately with CB_SETITEMHEIGHT(-1).
    place(gui.display_combo, make_rect(P, s(122), content, s(260)));
    SendMessageW(gui.display_combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 s(Metrics::control_height) - 6);
    SendMessageW(gui.display_combo, CB_SETDROPPEDWIDTH,
                 static_cast<WPARAM>(std::max(content, s(320))), 0);
    place(gui.display_status, make_rect(P, s(162), content, s(18)));

    // ---- Profiles
    place(gui.preset_caption, make_rect(P, s(206), content - icon, s(16)));
    place(gui.preset_save, make_rect(SW - P - icon + s(6), s(200), icon, icon));
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

    // ---- App switching block and startup toggle (bottom-anchored)
    place(gui.apps_caption, make_rect(P, g.apps_top, content, s(16)));
    place(gui.auto_switch_button, make_rect(P, g.apps_top + s(22), content, s(32)));
    place(gui.auto_switch_status, make_rect(P, g.apps_top + s(56), content, s(34)));
    place(gui.app_rules_button, make_rect(P, g.apps_top + s(94), content, s(34)));
    place(gui.startup_button, make_rect(P, height - s(56), content, s(36)));

    // ---- Work area
    const int main_width = g.main_right - g.main_left;
    place(gui.main_heading, make_rect(g.main_left, s(22), main_width, s(36)));
    place(gui.main_subtitle, make_rect(g.main_left, s(58), main_width, s(20)));
    place(gui.preview_heading,
          make_rect(g.curve_card.left + s(20), g.curve_card.top + s(16), s(240), s(24)));
    const auto place_card_header = [&](HWND heading, HWND caption, const RECT& card) {
        place(heading, make_rect(card.left + s(20), card.top + s(16), s(220), s(24)));
        place(caption, RECT{card.left + s(240), card.top + s(19), card.right - s(20),
                            card.top + s(39)});
    };
    place_card_header(gui.tone_heading, gui.tone_caption, g.tone_card);
    place_card_header(gui.color_heading, gui.color_caption, g.color_card);

    const AdjustmentBindings bindings = adjustment_bindings(gui);
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        place(bindings[i].label, g.rows[i].label);
        place(bindings[i].slider, g.rows[i].slider);
        place(bindings[i].edit, g.rows[i].value_edit);
    }

    // ---- Footer
    const int button_height = s(36);
    const int button_top = g.footer_top + (s(Metrics::footer_height) - button_height) / 2;
    const RECT apply = RECT{g.main_right - s(132), button_top, g.main_right,
                            button_top + button_height};
    const RECT reset = RECT{apply.left - s(10) - s(136), button_top, apply.left - s(10),
                            button_top + button_height};
    const RECT compare = RECT{reset.left - s(10) - s(144), button_top, reset.left - s(10),
                              button_top + button_height};
    place(gui.apply_button, apply);
    place(gui.reset_button, reset);
    place(gui.before_after_button, compare);
    const int status_left = g.main_left + s(20);
    place(gui.status, RECT{status_left, button_top + (button_height - s(22)) / 2,
                           std::max<LONG>(status_left + s(120), compare.left - s(16)),
                           button_top + (button_height + s(22)) / 2});
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
    const std::array<HFONT, 7> old{gui.normal_font, gui.title_font,   gui.heading_font,
                                   gui.panel_font,  gui.section_font, gui.caption_font,
                                   gui.small_font};

    gui.normal_font = make_font(dpi, 100, FW_NORMAL, text_face());
    gui.title_font = make_font(dpi, 150, FW_SEMIBOLD, display_face());
    gui.heading_font = make_font(dpi, 180, FW_SEMIBOLD, display_face());
    gui.panel_font = make_font(dpi, 110, FW_SEMIBOLD, text_face());
    gui.section_font = make_font(dpi, 100, FW_SEMIBOLD, text_face());
    gui.caption_font = make_font(dpi, 80, FW_BOLD, text_face());
    gui.small_font = make_font(dpi, 90, FW_NORMAL, text_face());
    gui.input_text_height = font_height(gui.normal_font);

    const std::array<std::pair<HWND, HFONT>, 20> fonts{{
        {gui.title, gui.title_font},
        {gui.subtitle, gui.small_font},
        {gui.display_caption, gui.caption_font},
        {gui.preset_caption, gui.caption_font},
        {gui.apps_caption, gui.caption_font},
        {gui.display_combo, gui.normal_font},
        {gui.display_status, gui.small_font},
        {gui.auto_switch_button, gui.normal_font},
        {gui.auto_switch_status, gui.small_font},
        {gui.app_rules_button, gui.normal_font},
        {gui.startup_button, gui.normal_font},
        {gui.profile_rename, gui.normal_font},
        {gui.main_heading, gui.heading_font},
        {gui.main_subtitle, gui.normal_font},
        {gui.preview_heading, gui.panel_font},
        {gui.tone_heading, gui.panel_font},
        {gui.color_heading, gui.panel_font},
        {gui.tone_caption, gui.small_font},
        {gui.color_caption, gui.small_font},
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
}

}  // namespace gamma_changer
