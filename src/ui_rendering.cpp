#include "ui_rendering.h"

#include "ui_theme.h"

#include <wincodec.h>

#include <algorithm>
#include <cstdint>

namespace gamma_changer::ui {
namespace {

template <typename T>
void release_com(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

constexpr UINT kMaxImageDimension = 16384;
constexpr std::uint64_t kMaxImageBytes = 512ull * 1024 * 1024;

bool valid_image_size(UINT width, UINT height) {
    return width > 0 && height > 0 &&
           width <= kMaxImageDimension && height <= kMaxImageDimension &&
           static_cast<std::uint64_t>(width) * height * 4 <= kMaxImageBytes;
}

}  // namespace

BackgroundRenderer::~BackgroundRenderer() {
    if (bitmap_) DeleteObject(bitmap_);
    if (composed_bitmap_) DeleteObject(composed_bitmap_);
}

void BackgroundRenderer::invalidate_cache() {
    if (composed_bitmap_) DeleteObject(composed_bitmap_);
    composed_bitmap_ = nullptr;
    composed_width_ = 0;
    composed_height_ = 0;
    composed_sidebar_width_ = 0;
}

void BackgroundRenderer::set_options(const BackgroundOptions& options) {
    options_ = options;
    invalidate_cache();
}

bool BackgroundRenderer::load(const std::filesystem::path& path) {
    invalidate_cache();
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool loaded = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        converter->GetSize(&width, &height);
        if (valid_image_size(width, height)) {
            const UINT stride = width * 4;
            const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            HDC screen = GetDC(nullptr);
            bitmap_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (bitmap_ && pixels && SUCCEEDED(converter->CopyPixels(
                                         nullptr, stride, static_cast<UINT>(bytes),
                                         static_cast<BYTE*>(pixels)))) {
                width_ = static_cast<int>(width);
                height_ = static_cast<int>(height);
                loaded = true;
            }
        }
    }

    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(factory);
    if (!loaded && bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    return loaded;
}

bool BackgroundRenderer::load_resource(HINSTANCE instance, int resource_id) {
    invalidate_cache();
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded_resource = LoadResource(instance, resource);
    if (!loaded_resource) return false;
    const DWORD resource_size = SizeofResource(instance, resource);
    const void* resource_data = LockResource(loaded_resource);
    if (!resource_data || resource_size == 0) return false;

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool loaded = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(
            const_cast<BYTE*>(static_cast<const BYTE*>(resource_data)), resource_size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr,
                                                   WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        converter->GetSize(&width, &height);
        if (valid_image_size(width, height)) {
            const UINT stride = width * 4;
            const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            HDC screen = GetDC(nullptr);
            HBITMAP decoded = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (decoded && pixels && SUCCEEDED(converter->CopyPixels(
                                        nullptr, stride, static_cast<UINT>(bytes),
                                        static_cast<BYTE*>(pixels)))) {
                if (bitmap_) DeleteObject(bitmap_);
                bitmap_ = decoded;
                width_ = static_cast<int>(width);
                height_ = static_cast<int>(height);
                loaded = true;
            } else if (decoded) {
                DeleteObject(decoded);
            }
        }
    }

    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(stream);
    release_com(factory);
    return loaded;
}

void BackgroundRenderer::render_uncached(HDC dc, const RECT& client, int sidebar_width) const {
    fill_surface(dc, client, Theme::sidebar);
    if (bitmap_ && width_ > 0 && height_ > 0) {
        HDC source = CreateCompatibleDC(dc);
        const auto old = SelectObject(source, bitmap_);
        const int target_width = client.right - client.left;
        const int target_height = client.bottom - client.top;
        const double scale = std::max(target_width / static_cast<double>(width_),
                                      target_height / static_cast<double>(height_));
        const int drawn_width = static_cast<int>(width_ * scale);
        const int drawn_height = static_cast<int>(height_ * scale);
        // Centered aspect-fill keeps the illustrated subject beneath the sidebar
        // and the brighter cherry-blossom clearing beneath the workspace.
        const int x = static_cast<int>((target_width - drawn_width) * options_.position_x);
        const int y = static_cast<int>((target_height - drawn_height) * options_.position_y);
        SetStretchBltMode(dc, HALFTONE);
        if (options_.image_opacity == 255) {
            StretchBlt(dc, x, y, drawn_width, drawn_height, source,
                       0, 0, width_, height_, SRCCOPY);
        } else {
            BLENDFUNCTION blend{AC_SRC_OVER, 0, options_.image_opacity, 0};
            AlphaBlend(dc, x, y, drawn_width, drawn_height, source,
                       0, 0, width_, height_, blend);
        }
        SelectObject(source, old);
        DeleteDC(source);
    }

    RECT sidebar{client.left, client.top, sidebar_width, client.bottom};
    RECT main{sidebar_width, client.top, client.right, client.bottom};
    if (bitmap_) {
        // Acrylic-style layers: the artwork remains perceptible in the sidebar,
        // while the main workspace gets a much denser readability surface.
        fill_layered_surface(dc, sidebar, options_.sidebar_overlay_color,
                             options_.sidebar_overlay);
        fill_layered_surface(dc, main, options_.main_overlay_color,
                             options_.main_overlay);
    } else {
        fill_surface(dc, sidebar, Theme::sidebar);
        fill_surface(dc, main, Theme::main_surface);
    }
}

void BackgroundRenderer::draw(HDC dc, const RECT& client, int sidebar_width) const {
    const int target_width = client.right - client.left;
    const int target_height = client.bottom - client.top;
    if (target_width <= 0 || target_height <= 0) return;

    if (!composed_bitmap_ || composed_width_ != target_width ||
        composed_height_ != target_height || composed_sidebar_width_ != sidebar_width) {
        HDC target = CreateCompatibleDC(dc);
        HBITMAP composed = CreateCompatibleBitmap(dc, target_width, target_height);
        if (target && composed) {
            const auto old = SelectObject(target, composed);
            RECT local{0, 0, target_width, target_height};
            render_uncached(target, local, sidebar_width);
            SelectObject(target, old);
            if (composed_bitmap_) DeleteObject(composed_bitmap_);
            composed_bitmap_ = composed;
            composed_width_ = target_width;
            composed_height_ = target_height;
            composed_sidebar_width_ = sidebar_width;
        } else if (composed) {
            DeleteObject(composed);
        }
        if (target) DeleteDC(target);
    }

    if (!composed_bitmap_) {
        render_uncached(dc, client, sidebar_width);
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    if (!source) {
        render_uncached(dc, client, sidebar_width);
        return;
    }
    const auto old = SelectObject(source, composed_bitmap_);
    BitBlt(dc, client.left, client.top, target_width, target_height,
           source, 0, 0, SRCCOPY);
    SelectObject(source, old);
    DeleteDC(source);
}

void fill_surface(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void fill_layered_surface(HDC dc, const RECT& rect, COLORREF color, BYTE opacity) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HDC source = CreateCompatibleDC(dc);
    HBITMAP pixel = CreateCompatibleBitmap(dc, 1, 1);
    const auto old = SelectObject(source, pixel);
    SetPixelV(source, 0, 0, color);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, 0};
    AlphaBlend(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               source, 0, 0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(pixel);
    DeleteDC(source);
}

void draw_panel(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_layered_panel(HDC dc, const RECT& rect, COLORREF fill, BYTE opacity,
                        COLORREF border, int radius) {
    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1,
                                   radius, radius);
    SelectClipRgn(dc, clip);
    fill_layered_surface(dc, rect, fill, opacity);
    SelectClipRgn(dc, nullptr);
    RestoreDC(dc, saved);
    DeleteObject(clip);

    if (border != CLR_INVALID) {
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
}

void draw_separator(HDC dc, int left, int top, int right, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const auto old = SelectObject(dc, pen);
    MoveToEx(dc, left, top, nullptr);
    LineTo(dc, right, top);
    SelectObject(dc, old);
    DeleteObject(pen);
}

}  // namespace gamma_changer::ui
