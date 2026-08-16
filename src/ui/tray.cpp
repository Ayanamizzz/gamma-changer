#include "gui_internal.h"

namespace gamma_changer {

void add_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcscpy_s(data.szTip, L"Gamma Changer");
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        log_message(LogLevel::warning, L"Could not add the system tray icon");
        return;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
        log_message(LogLevel::warning, L"Could not enable the modern tray notification format");
    }
}

void remove_tray_icon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &data);
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
    if (gui.session.dirty) {
        KillTimer(gui.window, kAutoSaveTimer);
        if (!save_and_apply_current(gui, true)) {
            show_main_window(gui.window);
            return false;
        }
    }
    const auto* display = selected_display(gui);
    if (!display) {
        set_status(gui, L"No display selected", StatusTone::warning);
        return false;
    }

    std::wstring error;
    if (!gui.controller.apply_and_save(*display, gui.profiles[index].settings, error)) {
        set_status(gui, L"Tray profile apply failed: " + error, StatusTone::error);
        return false;
    }
    gui.active_preset = index;
    gui.session.committed_settings = gui.profiles[index].settings;
    set_params_to_controls(gui, gui.session.committed_settings);
    gui.session.dirty = false;
    EnableWindow(gui.apply_button, FALSE);
    EnableWindow(gui.before_after_button, FALSE);
    refresh_preset_buttons(gui);
    set_status(gui, gui.profiles[index].name + L" applied", StatusTone::success);
    log_message(LogLevel::info, L"Tray profile applied: " + gui.profiles[index].name);
    return true;
}

void show_tray_menu(HWND window) {
    auto* gui = state(window);
    HMENU menu = CreatePopupMenu();
    HMENU profiles = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayShowCommand, L"Show Window");
    if (gui) {
        for (std::size_t i = 0; i < gui->profiles.size(); ++i) {
            std::wstring name = gui->profiles[i].name;
            std::size_t ampersand = 0;
            while ((ampersand = name.find(L'&', ampersand)) != std::wstring::npos) {
                name.insert(ampersand, 1, L'&');
                ampersand += 2;
            }
            const UINT flags = MF_STRING | (i == gui->active_preset ? MF_CHECKED : MF_UNCHECKED);
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
    GetCursorPos(&point);
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
