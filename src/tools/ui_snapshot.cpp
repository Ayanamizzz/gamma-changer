// Developer tool: renders the real main window (and the app-profiles dialog)
// off-screen with seeded sample data and saves PNG snapshots for visual review.
//
//   ui_snapshot.exe <output-directory>
//
// Safety: the window uses a temporary data directory and a fake gamma-ramp
// backend, so it never reads or writes the user's configuration and never
// changes any display. It does not take the single-instance lock.
#include "ui/app_dialogs.h"
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

HWND show_snapshot_dialog(HWND owner, int id, DLGPROC proc, LPARAM draft, int x) {
    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), owner,
                                     proc, draft);
    if (!dialog) return nullptr;
    SetWindowPos(dialog, nullptr, x, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(dialog, SW_SHOWNOACTIVATE);
    pump(500);
    return dialog;
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
        const UINT dpi = GetDpiForSystem();
        const int width = MulDiv(1120, static_cast<int>(dpi), 96);
        const int height = MulDiv(800, static_cast<int>(dpi), 96);
        // Far outside the virtual desktop: rendered by DWM, never visible.
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN) - width - 200;
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, window_class.lpszClassName,
                                      kApplicationTitle,
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                          WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX,
                                      x, 0, width, height, nullptr, nullptr, instance, &gui);
        if (!window) return 1;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        pump(900);
        if (!snapshot(window, output / L"main.png")) exit_code = 1;

        // Secondary states: paused switching, an unsaved edit, inline rename.
        gui.auto_switch_paused = true;
        update_automatic_profile_controls(gui);
        SendMessageW(gui.gamma_slider, TBM_SETPOS, TRUE, 162);
        SendMessageW(window, WM_HSCROLL, 0, reinterpret_cast<LPARAM>(gui.gamma_slider));
        begin_profile_rename(gui, 3);
        pump(250);  // shorter than the auto-save delay
        snapshot(window, output / L"main_states.png");
        finish_profile_rename(gui, false);
        gui.auto_switch_paused = false;

        if (argc > 2) {  // optional: also capture with the profile list scrolled
            gui.profile_scroll_offset = 1000;
            layout_controls_for_current_size(gui);
            InvalidateRect(window, nullptr, FALSE);
            pump(300);
            snapshot(window, output / L"main_scrolled.png");
        }

        AppProfilesDraft profiles;
        profiles.config = gui.app_profile_config;
        profiles.profiles = gui.profiles;
        for (std::size_t i = 0; i < gui.displays.size(); ++i) {
            profiles.displays.push_back({display_storage_id(gui.displays[i]),
                                         display_label(gui.displays[i], i)});
        }
        profiles.display_id = display_id;
        profiles.running_apps = sample_running_apps;
        if (HWND dialog = show_snapshot_dialog(window, IDD_APP_PROFILES, app_profiles_dialog_proc,
                                               reinterpret_cast<LPARAM>(&profiles), x)) {
            if (!snapshot(dialog, output / L"app_profiles.png")) exit_code = 1;
            DestroyWindow(dialog);
        }

        AppAssignDraft assign;
        assign.profiles = gui.profiles;
        assign.profile_id = L"p-gaming";
        assign.last_active_path = sample_running_apps()[1].executable_path;
        assign.running_apps = sample_running_apps;
        assign.title = L"Add app";
        if (HWND dialog = show_snapshot_dialog(window, IDD_APP_ASSIGN, app_assign_dialog_proc,
                                               reinterpret_cast<LPARAM>(&assign), x)) {
            if (!snapshot(dialog, output / L"app_assign.png")) exit_code = 1;
            DestroyWindow(dialog);
        }
        DestroyWindow(window);
        pump(100);
    }
    std::filesystem::remove_all(root, ec);
    CoUninitialize();
    return exit_code;
}
