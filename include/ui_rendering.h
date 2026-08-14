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

}  // namespace gamma_changer::ui
