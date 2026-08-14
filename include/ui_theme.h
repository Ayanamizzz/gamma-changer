#pragma once

#include <windows.h>

namespace gamma_changer::ui {

struct Theme {
    static constexpr COLORREF wallpaper_sidebar_overlay = RGB(24, 20, 34);
    static constexpr COLORREF wallpaper_main_overlay = RGB(241, 240, 245);
    static constexpr COLORREF primary = RGB(123, 110, 246);
    static constexpr COLORREF primary_pressed = RGB(101, 88, 222);
    static constexpr COLORREF primary_soft = RGB(232, 228, 253);
    static constexpr COLORREF success = RGB(34, 197, 94);
    static constexpr COLORREF warning = RGB(245, 158, 11);
    static constexpr COLORREF error = RGB(220, 38, 38);
    static constexpr COLORREF idle = RGB(148, 130, 144);

    static constexpr COLORREF text = RGB(39, 31, 42);
    static constexpr COLORREF text_secondary = RGB(112, 91, 108);
    static constexpr COLORREF border = RGB(228, 214, 224);
    static constexpr COLORREF border_subtle = RGB(239, 226, 236);
    static constexpr COLORREF grid = RGB(236, 224, 233);
    static constexpr COLORREF reference_curve = RGB(174, 169, 181);
    static constexpr COLORREF track_inactive = RGB(194, 190, 200);

    static constexpr COLORREF sidebar = RGB(35, 25, 42);
    static constexpr COLORREF sidebar_selected = RGB(61, 43, 65);
    static constexpr COLORREF sidebar_text = RGB(255, 247, 252);
    static constexpr COLORREF sidebar_secondary = RGB(207, 177, 199);
    static constexpr COLORREF sidebar_muted = RGB(156, 137, 157);
    static constexpr COLORREF sidebar_action = RGB(30, 35, 47);
    static constexpr COLORREF sidebar_action_border = RGB(70, 76, 94);

    static constexpr COLORREF main_surface = RGB(249, 245, 248);
    static constexpr COLORREF panel_surface = RGB(255, 252, 254);
    static constexpr COLORREF control_surface = RGB(255, 255, 255);
    static constexpr COLORREF footer_surface = RGB(247, 241, 246);
    static constexpr COLORREF input_border = RGB(226, 215, 224);
    static constexpr COLORREF disabled_surface = RGB(239, 232, 238);
    static constexpr COLORREF disabled_text = RGB(148, 130, 144);
};

struct Metrics {
    static constexpr int sidebar_width = 264;
    static constexpr int radius = 10;
    static constexpr int space_1 = 4;
    static constexpr int space_2 = 8;
    static constexpr int space_3 = 12;
    static constexpr int space_4 = 16;
    static constexpr int space_6 = 24;
    static constexpr int space_8 = 32;
    static constexpr int control_height = 32;
    static constexpr int footer_height = 72;
    static constexpr int control_radius = 6;
    static constexpr int panel_radius = 10;
    static constexpr int numeric_width = 84;
    static constexpr int row_height = 44;
    static constexpr int profile_section_inset = 30;
    static constexpr int profile_content_inset = 30;
    static constexpr int profile_title_gap = 9;
    static constexpr int profile_row_height = 34;
    static constexpr int profile_row_gap = 2;
    static constexpr int profile_text_inset = 14;
    static constexpr int profile_action_gap = 11;
    static constexpr int profile_secondary_gap = 8;
};

}  // namespace gamma_changer::ui
