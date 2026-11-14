#pragma once

#include <windows.h>

namespace gamma_changer::ui {

// Colour tokens. The sidebar is a dark plum surface over the wallpaper; the
// work area is a light surface with near-opaque white cards. One accent.
struct Theme {
    // Wallpaper overlays
    static constexpr COLORREF wallpaper_sidebar_overlay = RGB(28, 21, 36);
    static constexpr COLORREF wallpaper_main_overlay = RGB(244, 242, 247);

    // Accent and states
    static constexpr COLORREF primary = RGB(118, 98, 236);
    static constexpr COLORREF primary_hover = RGB(132, 114, 242);
    static constexpr COLORREF primary_pressed = RGB(98, 80, 214);
    static constexpr COLORREF primary_soft = RGB(234, 230, 252);
    static constexpr COLORREF success = RGB(34, 177, 96);
    static constexpr COLORREF warning = RGB(224, 145, 20);
    static constexpr COLORREF error = RGB(214, 48, 49);
    static constexpr COLORREF idle = RGB(160, 152, 170);

    // Work area
    static constexpr COLORREF text = RGB(32, 27, 40);
    static constexpr COLORREF text_secondary = RGB(104, 96, 116);
    static constexpr COLORREF text_muted = RGB(140, 132, 152);
    static constexpr COLORREF card = RGB(255, 255, 255);
    static constexpr COLORREF card_border = RGB(226, 221, 233);
    static constexpr COLORREF divider = RGB(232, 228, 238);
    static constexpr COLORREF grid = RGB(238, 235, 243);
    static constexpr COLORREF reference_curve = RGB(186, 180, 196);
    static constexpr COLORREF track_inactive = RGB(222, 218, 230);
    static constexpr COLORREF control_surface = RGB(255, 255, 255);
    static constexpr COLORREF input_surface = RGB(248, 247, 251);
    static constexpr COLORREF input_border = RGB(218, 212, 227);
    static constexpr COLORREF secondary_surface = RGB(255, 255, 255);
    static constexpr COLORREF disabled_surface = RGB(236, 233, 241);
    static constexpr COLORREF disabled_text = RGB(160, 152, 170);
    static constexpr COLORREF curve_red = RGB(232, 76, 76);
    static constexpr COLORREF curve_green = RGB(46, 176, 96);
    static constexpr COLORREF curve_blue = RGB(66, 128, 238);

    // Sidebar
    static constexpr COLORREF sidebar = RGB(35, 26, 44);
    static constexpr COLORREF sidebar_text = RGB(246, 242, 250);
    static constexpr COLORREF sidebar_secondary = RGB(196, 184, 210);
    static constexpr COLORREF sidebar_muted = RGB(150, 136, 166);
    static constexpr COLORREF sidebar_field = RGB(56, 44, 68);
    static constexpr COLORREF sidebar_field_border = RGB(84, 70, 100);
    static constexpr COLORREF sidebar_hover = RGB(255, 255, 255);   // drawn translucent
    static constexpr COLORREF sidebar_selected = RGB(255, 255, 255);  // drawn translucent
    static constexpr COLORREF sidebar_divider = RGB(70, 58, 84);
    static constexpr COLORREF toggle_off = RGB(98, 86, 114);

    // App chips
    static constexpr COLORREF chip = RGB(238, 234, 252);
    static constexpr COLORREF chip_hover = RGB(226, 220, 250);
    static constexpr COLORREF chip_off = RGB(236, 233, 241);
    static constexpr COLORREF plot_surface = RGB(250, 249, 253);
};

// Layout metrics in 96-DPI pixels; scale with Scale{dpi}(value).
struct Metrics {
    static constexpr int sidebar_width = 248;
    static constexpr int sidebar_padding = 20;
    static constexpr int main_gap = 28;
    static constexpr int card_radius = 12;
    static constexpr int control_radius = 8;
    static constexpr int control_height = 34;
    static constexpr int header_height = 86;        // profile title, usage line, actions
    static constexpr int status_height = 40;        // status line under the cards
    static constexpr int card_gap = 14;
    static constexpr int card_padding = 20;
    static constexpr int adjustment_row_min = 34;
    static constexpr int adjustment_row_max = 50;
    static constexpr int adjustment_group_gap = 18;  // between tone and colour rows
    static constexpr int adjustment_label_width = 84;
    static constexpr int value_box_width = 68;
    static constexpr int value_box_height = 30;
    static constexpr int profile_row_height = 34;
    static constexpr int profile_row_gap = 2;
    static constexpr int profile_row_inset = 10;   // row rect inset from sidebar edge
    static constexpr int profile_text_inset = 12;  // label inset inside the row
    static constexpr int chip_height = 28;
    static constexpr int chip_gap = 8;
    static constexpr int chip_rows_max = 2;        // further apps fold into "+N more"
};

}  // namespace gamma_changer::ui
