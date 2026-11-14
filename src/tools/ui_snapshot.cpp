// Developer tool: renders the real main window off-screen in several states
// with seeded sample data and saves PNG snapshots for visual review.
//
//   ui_snapshot.exe <output-directory>
//
// Safety: the window uses a temporary data directory (profiles and log) and a
// fake gamma-ramp backend, and it leaves the "Start with Windows" entry alone,
// so it never reads or writes the user's configuration and never changes any
// display. It does not take the single-instance lock.
#include "ui/gui_internal.h"

#include <wincodec.h>

#include <iostream>

using namespace gamma_changer;

namespace {

struct FakeBackend final : DisplayRampBackend {
    GammaRamp current = build_ramp(default_params());
    bool read(const DisplayInfo&, GammaRamp& ramp, std::wstring&) override {
        ramp = current;
        return true;
    }
    bool write(const DisplayInfo&, const GammaRamp& ramp, std::wstring&) override {
        current = ramp;
        return true;
    }
};

void pump(DWORD milliseconds) {
    const DWORD until = GetTickCount() + milliseconds;
    MSG message{};
    while (static_cast<int>(until - GetTickCount()) > 0) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

bool save_png(HBITMAP bitmap, const std::filesystem::path& path) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }
    bool ok = false;
    IWICBitmap* source = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    if (SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha,
                                                   &source)) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->WriteSource(source, nullptr)) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
        ok = true;
    }
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (source) source->Release();
    factory->Release();
    return ok;
}

bool snapshot(HWND window, const std::filesystem::path& path) {
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto old = SelectObject(memory, bitmap);
    constexpr UINT kRenderFullContent = 2;  // PW_RENDERFULLCONTENT
    const bool printed = PrintWindow(window, memory, kRenderFullContent) != FALSE;
    SelectObject(memory, old);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    const bool saved = printed && save_png(bitmap, path);
    DeleteObject(bitmap);
    std::wcout << (saved ? L"saved " : L"FAILED ") << path.wstring() << L'\n';
    return saved;
}

void seed(ProfileStore& store, const std::wstring& display_id) {
    CalibrationSettings reading;
    reading.gamma = 1.15;
    reading.brightness = -0.05;
    reading.b_gain = 0.86;
    CalibrationSettings gaming;
    gaming.gamma = 1.45;
    gaming.contrast = 1.10;
    CalibrationSettings night;
    night.gamma = 0.95;
    night.brightness = -0.12;
    night.g_gain = 0.88;
    night.b_gain = 0.70;
    std::vector<Profile> profiles{builtin_default_profile(),
                                  {L"p-reading", L"Reading", reading, true},
                                  {L"p-gaming", L"Gaming", gaming, true},
                                  {L"p-night", L"Night", night, true},
                                  {L"p-movie", L"Movie", default_params(), true}};
    std::wstring error;
    store.save_profiles(profiles, error);
    store.save_params(display_id, gaming, error);
    store.save_profile_preferences({{display_id, L"p-gaming"}}, error);

    AppProfileConfig config;
    config.enabled = true;
    bind_app_profile_rule(config, {L"C:\\Games\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe",
                                   display_id, L"p-gaming", true, {}, {}});
    bind_app_profile_rule(config, {L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
                                   display_id, L"p-reading", true, {}, {}});
    bind_app_profile_rule(config, {L"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe", display_id,
                                   L"p-movie", true, {}, {}});
    set_app_fallback_profile(config, display_id, L"p-reading");
    store.save_app_profile_config(config, error);
}

// Sample data only: snapshots must never contain the user's real window titles.
std::vector<RunningApplication> sample_running_apps() {
    return {
        {L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"New Tab - Google Chrome", 1, ERROR_SUCCESS},
        {L"C:\\Games\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe", L"Cyberpunk 2077", 2, ERROR_SUCCESS},
        {L"C:\\Program Files\\Adobe\\Photoshop\\Photoshop.exe", L"Untitled-1 @ 100%", 3, ERROR_SUCCESS},
        {L"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe", L"VLC media player", 4, ERROR_SUCCESS},
        {L"", L"Protected game", 5, ERROR_ACCESS_DENIED},
        {L"C:\\Windows\\explorer.exe", L"Downloads", 6, ERROR_SUCCESS},
    };
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : L".";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path root =
        std::filesystem::path(temp) / (L"GammaChangerSnapshot-" +
                                       std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root, ec);
    // The logger resolves its folder from this variable: keep the tool's log
    // lines out of the user's real log.
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());

    const auto displays = DisplayManager::enumerate();
    const std::wstring display_id =
        displays.empty() ? L"snapshot-display" : display_storage_id(displays.front());
    {
        ProfileStore store(root);
        seed(store, display_id);
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = L"GammaChangerSnapshotWindow";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    RegisterClassExW(&window_class);

    FakeBackend backend;
    int exit_code = 0;
    {
        GuiState gui(root, backend);
        gui.manage_startup_entry = false;  // never touch the user's startup entry
        gui.startup_enabled = true;        // sample state for the picture
        const UINT dpi = GetDpiForSystem();
        const int width = MulDiv(1080, static_cast<int>(dpi), 96);
        const int height = MulDiv(700, static_cast<int>(dpi), 96);
        // Far outside the virtual desktop: rendered by DWM, never visible.
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN) - width - 200;
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, window_class.lpszClassName,
                                      kApplicationTitle,
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                          WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX,
                                      x, 0, width, height, nullptr, nullptr, instance, &gui);
        if (!window) return 1;
        gui.running_apps = sample_running_apps;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        pump(900);
        // Sample data only: never show the developer's real foreground app.
        const auto show_sample_app = [&](std::size_t index) {
            pump(400);  // let any scheduled evaluation of the real foreground app run first
            gui.foreground_application = sample_running_apps()[index].executable_path;
            gui.foreground_lookup_error = ERROR_SUCCESS;
            update_automatic_profile_controls(gui);
            set_status(gui, L"Ready", StatusTone::success);
            pump(250);
        };
        const auto profile_index = [&](const wchar_t* id) {
            for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
                if (gui.profiles[i].id == id) return i;
            }
            return std::size_t{0};
        };

        // Gaming: one app of its own.
        show_sample_app(0);
        if (!snapshot(window, output / L"main.png")) exit_code = 1;

        // Reading: an app plus "All other apps".
        select_preset(gui, profile_index(L"p-reading"));
        show_sample_app(0);
        snapshot(window, output / L"main_other_apps.png");

        // Night: used by nothing; an unsaved edit and an inline rename.
        select_preset(gui, profile_index(L"p-night"));
        show_sample_app(0);
        SendMessageW(gui.gamma_slider, TBM_SETPOS, TRUE, 162);
        SendMessageW(window, WM_HSCROLL, 0, reinterpret_cast<LPARAM>(gui.gamma_slider));
        begin_profile_rename(gui, profile_index(L"p-night"));
        pump(250);  // shorter than the auto-save delay
        snapshot(window, output / L"main_states.png");
        finish_profile_rename(gui, false);
        pump(900);

        // Many apps on one profile: chips wrap, then fold into "+N more".
        const std::size_t gaming = profile_index(L"p-gaming");
        select_preset(gui, gaming);
        for (const wchar_t* path :
             {L"C:\\Games\\Elden Ring\\eldenring.exe", L"C:\\Games\\Hades II\\Hades2.exe",
              L"C:\\Games\\Baldurs Gate 3\\bin\\bg3_dx11.exe",
              L"C:\\Games\\Monster Hunter Wilds\\MonsterHunterWilds.exe",
              L"C:\\Games\\Stardew Valley\\Stardew Valley.exe",
              L"C:\\Games\\Helldivers 2\\bin\\helldivers2.exe",
              L"C:\\Games\\Forza Horizon 5\\ForzaHorizon5.exe",
              L"C:\\Games\\Counter-Strike 2\\bin\\win64\\cs2.exe"}) {
            assign_app_to_profile(gui, gaming, path);
        }
        show_sample_app(1);
        snapshot(window, output / L"main_many_apps.png");

        // Follow apps off, at the minimum window size.
        set_automatic_profiles_enabled(gui, false);
        MINMAXINFO limits{};
        SendMessageW(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        SetWindowPos(window, nullptr, 0, 0, limits.ptMinTrackSize.x, limits.ptMinTrackSize.y,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        show_sample_app(1);
        snapshot(window, output / L"main_minimum_off.png");

        if (argc > 2) {  // optional: also capture with the profile list scrolled
            gui.profile_scroll_offset = 1000;
            layout_controls_for_current_size(gui);
            InvalidateRect(window, nullptr, FALSE);
            pump(300);
            snapshot(window, output / L"main_scrolled.png");
        }
        DestroyWindow(window);
        pump(100);
    }
    std::filesystem::remove_all(root, ec);
    CoUninitialize();
    return exit_code;
}
