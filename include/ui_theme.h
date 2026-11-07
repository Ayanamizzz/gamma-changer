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
};

// Layout metrics in 96-DPI pixels; scale with Scale{dpi}(value).
struct Metrics {
    static constexpr int sidebar_width = 272;
    static constexpr int sidebar_padding = 24;
    static constexpr int main_gap = 32;
    static constexpr int card_radius = 12;
    static constexpr int control_radius = 8;
    static constexpr int control_height = 34;
    static constexpr int footer_height = 72;
    static constexpr int card_gap = 16;
    static constexpr int adjustment_card_height = 180;
    static constexpr int adjustment_row_stride = 40;
    static constexpr int adjustment_label_width = 112;
    static constexpr int value_box_width = 76;
    static constexpr int value_box_height = 30;
    static constexpr int profile_row_height = 34;
    static constexpr int profile_row_gap = 2;
    static constexpr int profile_row_inset = 12;   // row rect inset from sidebar edge
    static constexpr int profile_text_inset = 12;  // label inset inside the row
};

}  // namespace gamma_changer::ui
