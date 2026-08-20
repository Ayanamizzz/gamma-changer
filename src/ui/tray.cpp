#include "gui_internal.h"

namespace gamma_changer {

bool add_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_MAIN_ICON));
    if (!data.hIcon) {
        log_message(LogLevel::warning, L"Could not load the system tray icon");
        return false;
    }
    wcscpy_s(data.szTip, L"Gamma Changer");
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        log_message(LogLevel::warning, L"Could not add the system tray icon");
        return false;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
        log_message(LogLevel::warning, L"Could not enable the modern tray notification format");
    }
    return true;
}

void remove_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    if (auto* gui = state(window)) gui->tray_available = false;
}

void show_main_window(HWND window) {
    if (!IsWindow(window)) return;
    ShowWindow(window, SW_RESTORE);
    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
}

bool apply_profile_from_tray(GuiState& gui, std::size_t index) {
    if (index >= gui.profiles.size()) return false;
    if (!selected_display_ready(gui)) {
        set_status(gui, L"This display is still being identified or restored",
                   StatusTone::warning);
        show_main_window(gui.window);
        return false;
    }
    // Reuse the exact same transactional switch/undo path as a Profile-row click.
    // A separate tray implementation previously left a stale Ctrl+Z snapshot.
    select_preset(gui, index);
    if (!gui.active_profile_linked || gui.active_preset != index) {
        show_main_window(gui.window);
        return false;
    }
    log_message(LogLevel::info, L"Tray profile applied: " + gui.profiles[index].name);
    return true;
}

void show_tray_menu(HWND window, const POINT* anchor) {
    auto* gui = state(window);
    HMENU menu = CreatePopupMenu();
    HMENU profiles = CreatePopupMenu();
    if (!menu || !profiles) {
        if (profiles) DestroyMenu(profiles);
        if (menu) DestroyMenu(menu);
        log_message(LogLevel::error, L"Could not create the system tray menu");
        show_main_window(window);
        return;
    }
    if (gui) {
        if (const auto* display = selected_display(*gui)) {
            std::wstring target = L"Target: ";
            target += display->device_string.empty() ? display->device_name
                                                     : display->device_string;
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, target.c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
    }
    AppendMenuW(menu, MF_STRING, kTrayShowCommand, L"Show Window");
    if (gui) {
        for (std::size_t i = 0; i < gui->profiles.size(); ++i) {
            std::wstring name = gui->profiles[i].name;
            std::size_t ampersand = 0;
            while ((ampersand = name.find(L'&', ampersand)) != std::wstring::npos) {
                name.insert(ampersand, 1, L'&');
                ampersand += 2;
            }
            const UINT flags =
                MF_STRING | (gui->active_profile_linked && i == gui->active_preset
                                 ? MF_CHECKED
                                 : MF_UNCHECKED);
            AppendMenuW(profiles, flags, kTrayProfileCommandBase + static_cast<UINT>(i),
                        name.c_str());
        }
    }
    if (GetMenuItemCount(profiles) == 0) {
        AppendMenuW(profiles, MF_STRING | MF_GRAYED, 0, L"No saved profiles");
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), L"Profiles");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit");
    POINT point{};
    if (anchor) point = *anchor;
    else GetCursorPos(&point);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenuEx(menu,
                                          TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                          point.x, point.y, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kTrayShowCommand) {
        show_main_window(window);
    } else if (command == kTrayExitCommand) {
        SendMessageW(window, WM_CLOSE, 0, 0);
    } else if (gui && command >= kTrayProfileCommandBase &&
               command < kTrayProfileCommandBase + gui->profiles.size()) {
        apply_profile_from_tray(*gui,
                                static_cast<std::size_t>(command - kTrayProfileCommandBase));
    }
}

}  // namespace gamma_changer
