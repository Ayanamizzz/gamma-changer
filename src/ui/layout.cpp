#include "gui_internal.h"

namespace gamma_changer {

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
    const int profile_list_bottom = profile_first_y +
        kProfileVisibleRows * profile_row_height +
        (kProfileVisibleRows - 1) * scale(ui::Metrics::profile_row_gap);
    const int profile_visible_height = profile_list_bottom - profile_first_y;
    gui.profile_scroll_offset =
        clamp_profile_scroll(gui.profile_scroll_offset, gui.profiles.size(),
                             profile_row_stride, profile_visible_height);
    ensure_profile_buttons(gui);
    for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
        const int row_y = profile_first_y +
                          static_cast<int>(i) * profile_row_stride -
                          gui.profile_scroll_offset;
        const bool row_visible = row_y + profile_row_height > profile_first_y &&
                                 row_y < profile_list_bottom;
        MoveWindow(gui.preset_buttons[i], profile_content_x, row_y,
                   profile_content_width, profile_row_height, TRUE);
        ShowWindow(gui.preset_buttons[i], row_visible ? SW_SHOW : SW_HIDE);
    }
    if (gui.profile_rename && gui.renaming_preset != kNoProfile &&
        gui.renaming_preset < gui.profiles.size()) {
        const int edit_x = profile_content_x;
        const int edit_y = profile_first_y +
                           static_cast<int>(gui.renaming_preset) * profile_row_stride -
                           gui.profile_scroll_offset;
        const int edit_width = profile_content_width;
        const int edit_height = profile_row_height;
        MoveWindow(gui.profile_rename, edit_x, edit_y, edit_width, edit_height, TRUE);
        // Match the profile-row geometry exactly: same rounded corners and the
        // same 14 px text inset as the owner-drawn button label.
        SetWindowRgn(gui.profile_rename,
                     CreateRoundRectRgn(0, 0, edit_width + 1, edit_height + 1,
                                        scale(ui::Metrics::control_radius),
                                        scale(ui::Metrics::control_radius)),
                     TRUE);
        const int text_inset = scale(ui::Metrics::profile_text_inset);
        SendMessageW(gui.profile_rename, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(text_inset, text_inset));
        RECT format{text_inset, 0, edit_width - text_inset, edit_height};
        SendMessageW(gui.profile_rename, EM_SETRECT, 0,
                     reinterpret_cast<LPARAM>(&format));
    }
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


}  // namespace gamma_changer
