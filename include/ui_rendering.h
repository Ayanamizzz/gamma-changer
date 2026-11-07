#pragma once

#include <windows.h>

#include <filesystem>

namespace gamma_changer::ui {

enum class BackgroundScaling {
    cover,
};

struct BackgroundOptions {
    BYTE image_opacity = 255;
    COLORREF sidebar_overlay_color = RGB(35, 25, 42);
    COLORREF main_overlay_color = RGB(241, 240, 245);
    BYTE sidebar_overlay = 216;
    BYTE main_overlay = 208;
    int blur_radius = 0;
    double position_x = 0.5;
    double position_y = 0.5;
    BackgroundScaling scaling = BackgroundScaling::cover;
};

class BackgroundRenderer {
public:
    BackgroundRenderer() = default;
    ~BackgroundRenderer();

    BackgroundRenderer(const BackgroundRenderer&) = delete;
    BackgroundRenderer& operator=(const BackgroundRenderer&) = delete;

    bool load(const std::filesystem::path& path);
    bool load_resource(HINSTANCE instance, int resource_id);
    void set_options(const BackgroundOptions& options);
    const BackgroundOptions& options() const { return options_; }
    void draw(HDC dc, const RECT& client, int sidebar_width) const;

private:
    void invalidate_cache();
    void render_uncached(HDC dc, const RECT& client, int sidebar_width) const;

    HBITMAP bitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    BackgroundOptions options_{};
    mutable HBITMAP composed_bitmap_ = nullptr;
    mutable int composed_width_ = 0;
    mutable int composed_height_ = 0;
    mutable int composed_sidebar_width_ = 0;
};

void fill_surface(HDC dc, const RECT& rect, COLORREF color);
void fill_layered_surface(HDC dc, const RECT& rect, COLORREF color, BYTE opacity);
void draw_panel(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius);
void draw_layered_panel(HDC dc, const RECT& rect, COLORREF fill, BYTE opacity,
                        COLORREF border, int radius);
void draw_separator(HDC dc, int left, int top, int right, COLORREF color);

// ---------------------------------------------------------------------------
// Antialiased primitives (GDI+ under the hood; text stays GDI/ClearType).
// Coordinates are device pixels on `dc`; `alpha` is 0..255.
// ---------------------------------------------------------------------------
void aa_fill_round_rect(HDC dc, const RECT& rect, float radius, COLORREF color,
                        BYTE alpha = 255);
void aa_stroke_round_rect(HDC dc, const RECT& rect, float radius, COLORREF color,
                          float width = 1.0f, BYTE alpha = 255);
void aa_fill_ellipse(HDC dc, float cx, float cy, float radius, COLORREF color,
                     BYTE alpha = 255);
void aa_stroke_ellipse(HDC dc, float cx, float cy, float radius, COLORREF color,
                       float width, BYTE alpha = 255);
void aa_line(HDC dc, float x1, float y1, float x2, float y2, COLORREF color, float width,
             BYTE alpha = 255, bool dashed = false);

struct CurvePoint {
    float x;
    float y;
};
// Polyline; when `fill_to_y` is set the area between the curve and that
// baseline is filled with `fill_alpha` of the same color first.
void aa_curve(HDC dc, const CurvePoint* points, int count, COLORREF color, float width,
              int fill_to_y = -1, BYTE fill_alpha = 0);

void draw_chevron_down(HDC dc, int cx, int cy, int half_width, COLORREF color);
void draw_plus_icon(HDC dc, int cx, int cy, int half_size, COLORREF color);
void draw_refresh_icon(HDC dc, int cx, int cy, int radius, COLORREF color);

}  // namespace gamma_changer::ui
