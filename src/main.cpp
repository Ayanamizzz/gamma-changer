// Process entry point: single-instance guard, DPI/COM setup, window creation and
// the message loop. Everything else lives behind window_proc.
#include "ui/gui_internal.h"

using namespace gamma_changer;

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                    _In_ PWSTR command_line, _In_ int show_command) {
    const bool startup_launch = command_line && wcsstr(command_line, L"--startup") != nullptr;
    SingleInstanceLock instance_lock;
    if (!instance_lock.is_primary()) {
        if (!startup_launch) activate_existing_window(kWindowClass);
        return 0;
    }

    // Prevent Windows from bitmap-scaling the whole window on 125%/150% displays.
    // Per-monitor v2 keeps text and common controls rendered at native resolution.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    if (!RegisterClassExW(&window_class)) {
        log_message(LogLevel::error, L"Could not register the main window class (Win32 error " +
                                         std::to_wstring(GetLastError()) + L")");
    }

    GuiState gui;
    gui.startup_launch = startup_launch;
    const UINT initial_dpi = GetDpiForSystem();
    const int initial_width = MulDiv(1120, static_cast<int>(initial_dpi), 96);
    const int initial_height = MulDiv(800, static_cast<int>(initial_dpi), 96);
    HWND window = CreateWindowExW(0, kWindowClass, kApplicationTitle,
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                      WS_THICKFRAME | WS_MAXIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, initial_width, initial_height,
                                  nullptr, nullptr, instance, &gui);
    if (!window) {
        if (SUCCEEDED(com_result)) CoUninitialize();
        return 1;
    }

    if (gui.startup_launch) {
        ShowWindow(window, SW_HIDE);
    } else {
        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        const bool undo_shortcut = message.message == WM_KEYDOWN &&
                                   message.wParam == L'Z' &&
                                   (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const HWND focus = GetFocus();
        const bool editing_text =
            focus == gui.profile_rename || find_adjustment(adjustment_bindings(gui), focus);
        const bool rename_commit_key =
            message.message == WM_KEYDOWN &&
            (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) &&
            focus == gui.profile_rename && gui.renaming_preset != kNoProfile;
        if (rename_commit_key) {
            // Handle these keys before IsDialogMessage so dialog-manager
            // navigation can never swallow Enter/Escape while renaming.
            SendMessageW(window, kProfileCommitRenameMessage,
                         message.wParam == VK_RETURN ? TRUE : FALSE, 0);
            continue;
        }
        if (undo_shortcut && !editing_text) {
            SendMessageW(window, kUndoProfileSwitchMessage, 0, 0);
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com_result)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
