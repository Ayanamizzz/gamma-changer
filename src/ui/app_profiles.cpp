// "Follow apps": glue between the window (sidebar switch, the Apps card of the
// selected profile, its menus) and the runtime engine (AppProfileRuntime).
// Decisions live in core; this file turns clicks into configuration changes
// and engine events into text.
//
// The model the user sees:
//   - Follow apps off: the selected profile is used for everything.
//   - Follow apps on:  an app with its own profile gets that profile; every
//                      other app gets the profile marked "All other apps".
//   - Each change saves immediately. There is no draft and no dialog.
#include "gui_internal.h"

#include <shobjidl.h>

namespace gamma_changer {
namespace {

ForegroundApp read_foreground(const GuiState& gui) {
    return gui.foreground_app ? gui.foreground_app() : current_foreground_app();
}

bool editor_visible(const GuiState& gui) {
    return IsWindowVisible(gui.window) && !IsIconic(gui.window);
}

const Profile* profile_by_id(const GuiState& gui, const std::wstring& id) {
    const auto found = std::find_if(gui.profiles.begin(), gui.profiles.end(),
                                    [&](const Profile& profile) { return profile.id == id; });
    return found == gui.profiles.end() ? nullptr : &*found;
}

const Profile* active_profile(const GuiState& gui) {
    return gui.active_preset < gui.profiles.size() ? &gui.profiles[gui.active_preset] : nullptr;
}

bool ensure_foreground_watcher(GuiState& gui) {
    if (gui.foreground_watcher_running) return true;
    gui.foreground_watcher_running =
        gui.foreground_watcher.start(gui.window, kForegroundProfileMessage);
    if (!gui.foreground_watcher_running) {
        set_status(gui, L"Could not monitor the foreground application", StatusTone::error);
    }
    return gui.foreground_watcher_running;
}

// The single persist path for app-profile configuration.
bool save_app_config(GuiState& gui, const AppProfileConfig& config) {
    std::wstring error;
    if (config.enabled && !ensure_foreground_watcher(gui)) {
        error = L"cannot monitor the foreground application";
    } else if (gui.store.save_app_profile_config(config, error)) {
        gui.app_profile_config = config;
        update_automatic_profile_controls(gui);
        return true;
    }
    set_status(gui, L"Could not save app settings: " + error, StatusTone::error);
    return false;
}

// The profile pinned for unlisted apps on a display, when it still exists.
const Profile* pinned_other_apps_profile(const GuiState& gui, const AppProfileConfig& config,
                                         const std::wstring& display_id) {
    const auto* pinned = find_app_fallback_profile(config, display_id);
    const Profile* profile = pinned ? profile_by_id(gui, pinned->profile_id) : nullptr;
    return profile && profile->saved ? profile : nullptr;
}

// What unlisted apps actually get: the pinned profile, otherwise the display's
// own calibration, which is the selected profile.
const Profile* other_apps_profile(const GuiState& gui, const std::wstring& display_id) {
    if (const Profile* pinned = pinned_other_apps_profile(gui, gui.app_profile_config, display_id)) {
        return pinned;
    }
    return active_profile(gui);
}

std::wstring selected_display_label(const GuiState& gui) {
    const auto* display = selected_display(gui);
    if (!display) return {};
    return display_label(*display, static_cast<std::size_t>(display - gui.displays.data()));
}

void set_text(GuiState& gui, HWND control, const std::wstring& text) {
    if (!control) return;
    wchar_t current[256]{};
    GetWindowTextW(control, current, static_cast<int>(std::size(current)));
    if (text == current) return;
    SetWindowTextW(control, text.c_str());
    invalidate_control_background(gui, control, 0);
}

int measure(HDC dc, HFONT font, const std::wstring& text) {
    const auto old = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old) SelectObject(dc, old);
    return size.cx;
}

bool same_chips(const AppChipModel& left, const AppChipModel& right) {
    if (left.following != right.following || left.show_other != right.show_other ||
        left.holds_other != right.holds_other || left.chips.size() != right.chips.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.chips.size(); ++i) {
        if (left.chips[i].path != right.chips[i].path ||
            left.chips[i].enabled != right.chips[i].enabled) {
            return false;
        }
    }
    return true;
}

std::wstring app_count(std::size_t count) {
    return count == 1 ? L"1 app" : std::to_wstring(count) + L" apps";
}

// Where the selected profile is used, for the line under its name.
std::wstring usage_text(const GuiState& gui, const AppChipModel& model) {
    if (!selected_display(gui)) return L"No display connected";
    if (!model.following) return L"In use on " + selected_display_label(gui);
    std::wstring text;
    if (!model.chips.empty()) {
        text = L"Used for " + model.chips.front().label;
        if (model.chips.size() > 1) {
            text += L" and " + std::to_wstring(model.chips.size() - 1) + L" more";
        }
    }
    if (model.holds_other) {
        return text.empty() ? L"Used for every app without its own profile"
                            : text + L", and every app without its own profile";
    }
    return text.empty() ? L"No app uses this profile. It shows while this window is open."
                        : text;
}

// What the switch is doing right now, for the line under "Follow apps".
std::wstring follow_status_text(const GuiState& gui) {
    if (!gui.app_profile_config.enabled) return L"Off. The selected profile stays on.";
    if (gui.foreground_lookup_error != ERROR_SUCCESS) {
        return L"Can't identify the active app (error " +
               std::to_wstring(gui.foreground_lookup_error) + L")";
    }
    const std::wstring app = executable_display_name(gui.foreground_application);
    const auto* display = selected_display(gui);
    if (app.empty() || !display) return L"Waiting for an app";
    const std::wstring display_id = display_storage_id(*display);
    const AppProfileResolution resolved = resolve_app_profile(
        gui.app_profile_config, gui.profiles, gui.foreground_application, display_id);
    const Profile* profile = resolved.profile ? resolved.profile : active_profile(gui);
    return profile ? app + L" uses " + profile->name : app;
}

std::wstring menu_label(std::wstring text) {
    for (auto& ch : text) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    }
    std::size_t ampersand = 0;
    while ((ampersand = text.find(L'&', ampersand)) != std::wstring::npos) {
        text.insert(ampersand, 1, L'&');
        ampersand += 2;
    }
    return text;
}

std::wstring shortened(const std::wstring& text, std::size_t limit) {
    return text.size() <= limit ? text : text.substr(0, limit - 1) + L"\u2026";
}

std::wstring choose_executable(HWND owner) {
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&picker)))) {
        return {};
    }
    const COMDLG_FILTERSPEC filter{L"Applications (*.exe)", L"*.exe"};
    picker->SetTitle(L"Choose an app");
    picker->SetFileTypes(1, &filter);
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    std::wstring path;
    if (SUCCEEDED(picker->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(picker->GetResult(&item))) {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
                path = selected;
                CoTaskMemFree(selected);
            }
            item->Release();
        }
    }
    picker->Release();
    return path;
}

// Opens `menu` directly under (or, near the screen edge, above) `anchor`.
UINT track_menu_below(GuiState& gui, HMENU menu, HWND anchor) {
    RECT rect{};
    GetWindowRect(anchor, &rect);
    TPMPARAMS avoid{sizeof(avoid)};
    avoid.rcExclude = rect;
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL,
        rect.left, rect.bottom + 2, gui.window, &avoid));
    PostMessageW(gui.window, WM_NULL, 0, 0);
    return command;
}

}  // namespace

std::wstring executable_display_name(const std::wstring& path) {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

HICON app_icon(GuiState& gui, const std::wstring& path) {
    const std::wstring key = normalize_executable_path(path);
    const auto cached = gui.app_icons.find(key);
    if (cached != gui.app_icons.end()) return cached->second;
    HICON icon = nullptr;
    SHFILEINFOW info{};
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES &&
        SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON)) {
        icon = info.hIcon;
    }
    gui.app_icons.emplace(key, icon);
    return icon;
}

void release_app_icons(GuiState& gui) {
    for (auto& [path, icon] : gui.app_icons) {
        if (icon) DestroyIcon(icon);
    }
    gui.app_icons.clear();
}

void ensure_app_chip_buttons(GuiState& gui) {
    if (!gui.window || !gui.add_app_button) return;
    while (gui.app_chip_buttons.size() < gui.apps.chips.size()) {
        const int id = kAppChipIdBase + static_cast<int>(gui.app_chip_buttons.size());
        HWND chip = make_control(WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, L"BUTTON", L"", gui.window,
                                 id, 0, 0, 0, 0);
        if (!chip) return;
        SetWindowSubclass(chip, hover_button_proc, 1, 0);
        // Keep Tab order: chips sit just before "Add app".
        SetWindowPos(chip, GetWindow(gui.add_app_button, GW_HWNDPREV), 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        gui.app_chip_buttons.push_back(chip);
    }
    while (gui.app_chip_buttons.size() > gui.apps.chips.size()) {
        DestroyWindow(gui.app_chip_buttons.back());
        gui.app_chip_buttons.pop_back();
    }
    for (std::size_t i = 0; i < gui.app_chip_buttons.size(); ++i) {
        SetWindowTextW(gui.app_chip_buttons[i], gui.apps.chips[i].label.c_str());
    }
}

void refresh_app_chips(GuiState& gui, bool remeasure) {
    AppChipModel model;
    model.following = gui.app_profile_config.enabled;
    std::vector<std::wstring> notes(gui.profiles.size());
    const auto* display = selected_display(gui);
    const Profile* active = active_profile(gui);
    if (display) {
        const std::wstring display_id = display_storage_id(*display);
        const Profile* other = model.following ? other_apps_profile(gui, display_id) : nullptr;
        for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
            const std::size_t count =
                apps_using_profile(gui.app_profile_config, display_id, gui.profiles[i].id).size();
            const bool others = other && other->id == gui.profiles[i].id;
            notes[i] = count > 0 && others ? std::to_wstring(count) + L" + others"
                       : count > 0         ? app_count(count)
                       : others            ? L"others"
                                           : L"";
        }
        if (active) {
            for (const auto& app :
                 apps_using_profile(gui.app_profile_config, display_id, active->id)) {
                model.chips.push_back({app.executable_path,
                                       executable_display_name(app.executable_path), app.enabled, 0});
            }
            model.show_other = model.following && active->saved && gui.profile_store_available;
            model.holds_other = other && other->id == active->id;
        }
    }

    const bool changed = remeasure || !same_chips(gui.apps, model);
    if (changed) {
        if (gui.window) {
            const Scale s{GetDpiForWindow(gui.window)};
            HDC dc = GetDC(gui.window);
            for (auto& chip : model.chips) {
                chip.width = s(8) + s(16) + s(7) +
                             std::min(s(200), measure(dc, gui.normal_font, chip.label)) + s(28);
            }
            const int other_text = measure(dc, gui.normal_font, L"All other apps");
            model.other_width = model.holds_other ? other_text + s(28) : other_text + s(39);
            model.add_width = measure(dc, gui.normal_font, L"Add app") + s(39);
            model.overflow_width = measure(dc, gui.normal_font, L"+99 more") + s(24);
            ReleaseDC(gui.window, dc);
        }
        gui.apps = std::move(model);
    }
    const bool notes_changed = notes != gui.profile_notes;
    if (notes_changed) gui.profile_notes = std::move(notes);

    set_text(gui, gui.main_heading,
             !active ? L"" : active->name.empty() ? L"Unnamed profile" : active->name);
    set_text(gui, gui.main_subtitle, usage_text(gui, gui.apps));
    set_text(gui, gui.apps_note,
             !gui.apps.following && !gui.apps.chips.empty() ? L"Follow apps is off" : L"");

    if (!gui.window || !gui.add_app_button) return;
    EnableWindow(gui.add_app_button,
                 display && active && active->saved && gui.profile_store_available ? TRUE : FALSE);
    if (changed) {
        // The Apps card grows with its chips and the editor card gives way.
        layout_controls_for_current_size(gui);
        RedrawWindow(gui.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    } else if (notes_changed) {
        for (HWND row : gui.preset_buttons) InvalidateRect(row, nullptr, FALSE);
    }
}

void update_automatic_profile_controls(GuiState& gui) {
    if (gui.auto_switch_button) InvalidateRect(gui.auto_switch_button, nullptr, FALSE);
    set_text(gui, gui.auto_switch_status, follow_status_text(gui));
    refresh_app_chips(gui);
}

void initialize_automatic_profiles(GuiState& gui) {
    gui.app_profile_config = gui.store.load_app_profile_config();
    if (!ensure_foreground_watcher(gui)) gui.app_profile_config.enabled = false;
    update_automatic_profile_controls(gui);
    schedule_automatic_profiles(gui);
}

void schedule_automatic_profiles(GuiState& gui, bool force) {
    gui.foreground_reapply_requested = gui.foreground_reapply_requested || force;
    if (!gui.foreground_evaluation_pending) {
        gui.foreground_evaluation_pending =
            SetTimer(gui.window, kForegroundProfileTimer, 150, nullptr) != 0;
    }
}

bool restore_automatic_profiles(GuiState& gui) {
    const RuntimeRestoreResult result = gui.app_runtime.restore_all(gui.displays);
    if (!result.success) {
        set_status(gui, L"Could not restore the selected profile: " + result.error,
                   StatusTone::error);
    }
    return result.success;
}

void evaluate_automatic_profiles(GuiState& gui, bool force) {
    const ForegroundApp front = read_foreground(gui);
    const bool own_process = front.own_process;
    const bool external = front.has_window && !own_process;
    if (external) gui.foreground_lookup_error = front.error;
    const std::wstring path = own_process ? gui.foreground_application : front.executable_path;
    // Remember the last external app: it leads the "Add app" menu.
    if (external) gui.foreground_application = path;
    if (own_process) {
        // The visible editor shows the selected profile. Hidden tray menus
        // retain the last application's effect until focus returns.
        const bool editing = editor_visible(gui);
        if (editing) restore_automatic_profiles(gui);
        update_automatic_profile_controls(gui);
        // Display recovery may have overwritten the LUT while a hidden tray
        // menu owns focus. Reapply the last external app before deduplicating.
        if (editing || !force) return;
    }
    if (!gui.app_profile_config.enabled) {
        restore_automatic_profiles(gui);
        update_automatic_profile_controls(gui);
        return;
    }
    if (path.empty() && !front.has_window) return;
    normalize_all_edits(gui);
    if (gui.session.dirty && !save_and_apply_current(gui, true)) return;
    KillTimer(gui.window, kPreviewTimer);
    if (!cancel_active_preview(gui)) return;
    apply_application_profiles(gui, path, force);
    if (path.empty()) {
        set_status(gui, L"Could not identify the active app (Win32 error " +
                        std::to_wstring(gui.foreground_lookup_error) +
                        L"); using the profile for other apps",
                   StatusTone::error);
    }
}

void apply_application_profiles(GuiState& gui, const std::wstring& path, bool force) {
    gui.foreground_application = path;
    const auto events = gui.app_runtime.apply(gui.app_profile_config, gui.profiles,
                                              gui.displays, path, force);
    for (const RuntimeEvent& event : events) {
        switch (event.kind) {
        case RuntimeEventKind::applied:
            set_status(gui, executable_display_name(path) + L" is in front: " + event.label,
                       StatusTone::success);
            break;
        case RuntimeEventKind::restored:
            set_status(gui, L"Back to the selected profile", StatusTone::success);
            break;
        case RuntimeEventKind::restore_failed:
            set_status(gui, L"Could not restore the selected profile: " + event.error,
                       StatusTone::error);
            break;
        case RuntimeEventKind::capture_failed:
            set_status(gui, L"Could not read the display settings: " + event.error,
                       StatusTone::error);
            break;
        case RuntimeEventKind::apply_failed:
            set_status(gui, L"Could not switch profiles: " + event.error, StatusTone::error);
            break;
        }
    }
    update_automatic_profile_controls(gui);
}

bool set_automatic_profiles_enabled(GuiState& gui, bool enabled) {
    if (gui.app_profile_config.enabled == enabled) return true;
    AppProfileConfig config = gui.app_profile_config;
    config.enabled = enabled;
    if (enabled) {
        // Turning it on must not change what the desktop looks like: apps
        // without their own profile keep the profile that is on now.
        const auto* display = selected_display(gui);
        const Profile* current = active_profile(gui);
        if (display && current && current->saved &&
            !pinned_other_apps_profile(gui, config, display_storage_id(*display))) {
            set_app_fallback_profile(config, display_storage_id(*display), current->id);
        }
    } else if (!restore_automatic_profiles(gui)) {
        return false;
    }
    if (!save_app_config(gui, config)) return false;
    set_status(gui, enabled ? L"Follow apps is on"
                            : L"Follow apps is off. The selected profile stays on.",
               enabled ? StatusTone::success : StatusTone::idle);
    schedule_automatic_profiles(gui);
    return true;
}

void toggle_automatic_profiles(GuiState& gui) {
    set_automatic_profiles_enabled(gui, !gui.app_profile_config.enabled);
}

bool keep_other_apps_profile(GuiState& gui) {
    if (!gui.app_profile_config.enabled || !gui.profile_store_available) return true;
    const auto* display = selected_display(gui);
    const Profile* current = active_profile(gui);
    if (!display || !current || !current->saved) return true;
    const std::wstring display_id = display_storage_id(*display);
    if (pinned_other_apps_profile(gui, gui.app_profile_config, display_id)) return true;
    AppProfileConfig config = gui.app_profile_config;
    set_app_fallback_profile(config, display_id, current->id);
    return save_app_config(gui, config);
}

bool assign_app_to_profile(GuiState& gui, std::size_t profile_index, const std::wstring& path) {
    const auto* display = selected_display(gui);
    if (!display || profile_index >= gui.profiles.size() || path.empty()) return false;
    const Profile& profile = gui.profiles[profile_index];
    if (!gui.profile_store_available || !profile.saved) {
        set_status(gui, L"Apps can only use saved profiles", StatusTone::warning);
        return false;
    }
    const std::wstring display_id = display_storage_id(*display);
    AppProfileConfig config = gui.app_profile_config;
    assign_app_profile(config, path, display_id, profile.id);

    // First app on this display: decide what everything else keeps using.
    std::wstring others_note;
    if (!pinned_other_apps_profile(gui, config, display_id)) {
        const Profile* shown = active_profile(gui);
        const auto previous = gui.previous_profile_ids.find(display_id);
        const std::wstring other = default_other_apps_profile(
            gui.profiles, profile.id, shown ? shown->id : std::wstring{},
            previous == gui.previous_profile_ids.end() ? std::wstring{} : previous->second,
            kBuiltinProfileId);
        if (!other.empty()) {
            set_app_fallback_profile(config, display_id, other);
            if (const Profile* other_profile = profile_by_id(gui, other)) {
                others_note = L" Other apps use " + other_profile->name + L".";
            }
        }
    }
    config.enabled = true;
    if (!save_app_config(gui, config)) return false;
    set_status(gui, executable_display_name(path) + L" now switches to " + profile.name + L"." +
                        others_note,
               StatusTone::success);
    schedule_automatic_profiles(gui);
    return true;
}

bool unassign_app(GuiState& gui, const std::wstring& path) {
    const auto* display = selected_display(gui);
    if (!display) return false;
    AppProfileConfig config = gui.app_profile_config;
    if (!unassign_app_profile(config, path, display_storage_id(*display))) return false;
    if (!save_app_config(gui, config)) return false;
    set_status(gui, executable_display_name(path) + L" no longer has its own profile",
               StatusTone::success);
    schedule_automatic_profiles(gui);
    return true;
}

bool use_profile_for_other_apps(GuiState& gui, std::size_t profile_index) {
    const auto* display = selected_display(gui);
    if (!display || profile_index >= gui.profiles.size()) return false;
    const Profile& profile = gui.profiles[profile_index];
    if (!gui.profile_store_available || !profile.saved) return false;
    const std::wstring display_id = display_storage_id(*display);
    if (const Profile* current = other_apps_profile(gui, display_id);
        current && current->id == profile.id &&
        pinned_other_apps_profile(gui, gui.app_profile_config, display_id)) {
        set_status(gui, L"To change this, open another profile and choose All other apps there",
                   StatusTone::idle);
        return true;
    }
    AppProfileConfig config = gui.app_profile_config;
    set_app_fallback_profile(config, display_id, profile.id);
    if (!save_app_config(gui, config)) return false;
    set_status(gui, L"Apps without their own profile now use " + profile.name,
               StatusTone::success);
    schedule_automatic_profiles(gui);
    return true;
}

AddAppMenu build_add_app_menu(const GuiState& gui) {
    AddAppMenu result;
    const auto* display = selected_display(gui);
    if (!display || gui.active_preset >= gui.profiles.size()) return result;
    const std::wstring profile_id = gui.profiles[gui.active_preset].id;
    const std::wstring display_id = display_storage_id(*display);

    std::vector<RunningApplication> running =
        gui.running_apps ? gui.running_apps() : running_applications();
    // The app used last is almost always the one being added: list it first.
    const std::wstring last = normalize_executable_path(gui.foreground_application);
    const auto recent = std::find_if(running.begin(), running.end(),
                                     [&](const RunningApplication& app) {
        return !last.empty() && normalize_executable_path(app.executable_path) == last;
    });
    if (recent != running.end()) std::rotate(running.begin(), recent, recent + 1);

    const auto assigned = app_assignments(gui.app_profile_config, display_id);
    result.menu = CreatePopupMenu();
    for (const auto& app : running) {
        if (app.executable_path.empty()) {
            // Windows would not say where this app lives; Browse still works.
            AppendMenuW(result.menu, MF_STRING | MF_GRAYED, 0,
                        (menu_label(shortened(app.window_title, 36)) +
                         L"\tprotected, use Browse").c_str());
            continue;
        }
        const std::wstring normalized = normalize_executable_path(app.executable_path);
        const auto existing = std::find_if(assigned.begin(), assigned.end(),
                                           [&](const AppAssignment& item) {
            return normalize_executable_path(item.executable_path) == normalized;
        });
        const bool here = existing != assigned.end() && existing->profile_id == profile_id &&
                          existing->enabled;
        std::wstring detail = shortened(app.window_title, 36);
        if (existing != assigned.end() && existing->profile_id != profile_id) {
            if (const Profile* owner = profile_by_id(gui, existing->profile_id)) {
                detail = L"moves from " + owner->name;
            }
        }
        const std::wstring text = menu_label(executable_display_name(app.executable_path)) +
                                  L"\t" + menu_label(detail);
        AppendMenuW(result.menu, MF_STRING | (here ? MF_CHECKED | MF_GRAYED : 0),
                    kAddAppFirstCommand + static_cast<UINT>(result.paths.size()), text.c_str());
        result.paths.push_back(app.executable_path);
    }
    if (running.empty()) {
        AppendMenuW(result.menu, MF_STRING | MF_GRAYED, 0, L"No apps with an open window");
    }
    AppendMenuW(result.menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(result.menu, MF_STRING, kAddAppBrowseCommand, L"Browse for an app\u2026");
    return result;
}

void show_add_app_menu(GuiState& gui) {
    const std::size_t profile_index = gui.active_preset;
    AddAppMenu add = build_add_app_menu(gui);
    if (!add.menu) return;
    const UINT command = track_menu_below(gui, add.menu, gui.add_app_button);
    DestroyMenu(add.menu);

    std::wstring chosen;
    if (command == kAddAppBrowseCommand) {
        chosen = choose_executable(gui.window);
    } else if (command >= kAddAppFirstCommand &&
               command - kAddAppFirstCommand < add.paths.size()) {
        chosen = add.paths[command - kAddAppFirstCommand];
    }
    if (!chosen.empty()) assign_app_to_profile(gui, profile_index, chosen);
}

void show_app_chip_menu(GuiState& gui, std::size_t chip_index) {
    if (chip_index >= gui.apps.chips.size() || chip_index >= gui.app_chip_buttons.size() ||
        gui.active_preset >= gui.profiles.size()) {
        return;
    }
    const AppChip chip = gui.apps.chips[chip_index];
    constexpr UINT kRemove = 1;
    constexpr UINT kTurnOn = 2;
    constexpr UINT kFirstProfile = 100;
    HMENU menu = CreatePopupMenu();
    HMENU move = CreatePopupMenu();
    std::vector<std::size_t> targets;
    for (std::size_t i = 0; i < gui.profiles.size(); ++i) {
        if (i == gui.active_preset || !gui.profiles[i].saved) continue;
        AppendMenuW(move, MF_STRING, kFirstProfile + static_cast<UINT>(targets.size()),
                    menu_label(gui.profiles[i].name).c_str());
        targets.push_back(i);
    }
    if (!chip.enabled) AppendMenuW(menu, MF_STRING, kTurnOn, L"Turn on");
    AppendMenuW(menu, MF_POPUP | (targets.empty() ? MF_GRAYED : 0),
                reinterpret_cast<UINT_PTR>(move), L"Move to");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kRemove, L"Remove\tDel");
    const UINT command = track_menu_below(gui, menu, gui.app_chip_buttons[chip_index]);
    DestroyMenu(menu);  // also destroys the submenu

    if (command == kRemove) {
        unassign_app(gui, chip.path);
    } else if (command == kTurnOn) {
        assign_app_to_profile(gui, gui.active_preset, chip.path);
    } else if (command >= kFirstProfile && command - kFirstProfile < targets.size()) {
        assign_app_to_profile(gui, targets[command - kFirstProfile], chip.path);
    }
}

void show_app_overflow_menu(GuiState& gui) {
    const UiGeometry g = ui_geometry(gui);
    constexpr UINT kFirstApp = 100;
    std::vector<std::wstring> paths;
    HMENU menu = CreatePopupMenu();
    for (std::size_t i = 0; i < gui.apps.chips.size(); ++i) {
        const bool hidden = i >= g.chip_rects.size() ||
                            g.chip_rects[i].right <= g.chip_rects[i].left;
        if (!hidden) continue;
        HMENU actions = CreatePopupMenu();
        AppendMenuW(actions, MF_STRING, kFirstApp + static_cast<UINT>(paths.size()), L"Remove");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(actions),
                    menu_label(gui.apps.chips[i].label).c_str());
        paths.push_back(gui.apps.chips[i].path);
    }
    const UINT command = track_menu_below(gui, menu, gui.app_overflow_button);
    DestroyMenu(menu);
    if (command >= kFirstApp && command - kFirstApp < paths.size()) {
        unassign_app(gui, paths[command - kFirstApp]);
    }
}

void remove_application_profile_rules(GuiState& gui, const std::wstring& profile_id) {
    AppProfileConfig config = gui.app_profile_config;
    if (remove_app_profile_rules_for_profile(config, profile_id) > 0) save_app_config(gui, config);
    schedule_automatic_profiles(gui);
}

}  // namespace gamma_changer
