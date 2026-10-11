# Gamma Changer

Gamma Changer is a native Windows desktop utility for adjusting the software gamma ramp of
each connected display. It provides a modern, high-DPI Win32 interface with per-monitor
settings, a scrollable profile list, direct numeric input, and a live tone-response preview.

Current release version: **2.6.1**

## Features

- Detects multiple displays and uses friendly monitor names when Windows exposes them.
- Adjusts Gamma, Brightness, Contrast, and individual Red/Green/Blue gain.
- Supports sliders and precise numeric entry with immediate visual feedback.
- Shows the reference and adjusted tone-response curves.
- Provides a scrollable list of persistent, renameable profiles with no four-slot limit.
- Remembers the preferred profile for each display and applies it when switching displays.
- Automatically switches profiles for the foreground application using per-display executable bindings.
- Provides a separate filter list for each application, with custom names, parameter editing, and filter selection.
- Defaults to the display with the highest pixel count; keeps your selection when refreshing displays.
- Automatically saves changes 0.7 seconds after the last adjustment (Live Preview + Auto-Save).
- Preserves settings by stable display identity across refreshes and hot-plug events.
- Reapplies saved calibration after display topology changes and system resume.
- Supports Start with Windows, single-instance activation, and system tray controls.
- Uses a native Windows 11-inspired translucent interface with Per-Monitor V2 DPI support.

## Important limitations

Gamma Changer modifies the GPU's software lookup table (LUT). It does **not** control the
monitor's physical backlight or hardware contrast. Hardware controls would require a separate
DDC/CI implementation.

HDR, Advanced Color, graphics-driver behavior, color-management software, games, and protected
video paths may override or bypass a software gamma ramp. The application reports compatible
driver failures but cannot force unsupported display pipelines to accept calibration.

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 Build Tools with **Desktop development with C++**
- Windows 10/11 SDK
- CMake 3.21 or newer

## Build

The simplest build command is:

```powershell
.\build-release.bat
```

The script uses the CMake installation bundled with Visual Studio Build Tools, builds the GUI
and all automated checks, then runs the Release test suite. The GUI binary is written to:

```text
build\vs2022-x64\Release\gamma_changer_gui.exe
```

You can also use CMake directly:

```powershell
cmake --preset vs2022-x64
cmake --build build\vs2022-x64 --config Release
ctest --preset release
```

If the linker reports `LNK1104` for `gamma_changer_gui.exe`, close the running application
before rebuilding so Windows can replace the executable.

## Usage

Launch the graphical application:

```powershell
.\build\vs2022-x64\Release\gamma_changer_gui.exe
```

Choose a display, then adjust the values with the sliders or numeric fields. Changes are
previewed live and saved automatically after a 0.7-second debounce. Use **Restore defaults** to
return the selected display to a neutral LUT.

- Minimizing the window keeps the application available in the system tray.
- Left-clicking the tray icon restores the window.
- Right-clicking the tray icon provides profile switching, Show Window, and Exit commands.
- Closing the main window exits the application completely.

### Application profiles

Select a display and profile, then click **Bind app...** (or right-click a profile and choose
**Bind application...**) to choose a running application. Binding an application enables **Auto-switch**.
The chosen filter becomes that application's default for the selected display, including when
rebinding an application that already has filters. Rebinding the same app/display/filter reuses
its existing rule. For example, bind a browser to a Common filter and a game to a Gaming filter;
switching browser → game → browser applies Common → Gaming → Common.
When that executable becomes the foreground application, its profile applies to the bound
display. Switching to another application restores that display's desktop settings.
When a custom default for unassigned applications is configured, it applies instead.
The effect covers the whole display, including other visible windows.

Use **App rules...** to open the application configuration interface. Select any application
on the left to see only its filters on the right. **Add app...** accepts any executable;
**Add filter...** creates another named filter for that application. **Add app...** opens a list
of applications with open or minimized desktop windows, showing their executable name,
window title, and full path. Multiple windows of the same executable appear once; copies in
different directories remain separate. The last active application is preselected when available.
Double-click a row or click **Select** to continue to its filter settings. **Refresh** updates
the snapshot while preserving selection when the application remains open. **Browse...** still
adds an executable that is not currently listed. Unreadable executable paths show their Windows
error code and cannot be selected directly. There are no built-in
application categories or predefined filter lists.

Choose **Set as default** to select an existing filter for the application. Adding or editing
an enabled filter also selects it as the default when the configuration dialog is saved,
and resumes automatic switching after manual adjustments. Changes stay in the dialog's draft
until **Save**; **Cancel** preserves the previous assignments and switching preference.
The selected filter is enabled and placed ahead of the application's other filters for the
same display. Each display can have its own selected filter. You can rename, edit, reorder,
disable, or remove filters without replacing the application's other filters.

**Edit settings...** adjusts Gamma, Brightness, Contrast, and RGB gains for the chosen filter.
Editing a shared library profile creates an application filter copy so the original profile
and other applications retain their settings. Changes remain in the configuration dialog
until **Save**; **Cancel** discards them. Existing application bindings are loaded automatically.

Paths are matched in full, so two applications with the same filename in different directories
can use different profiles.
Profiles can be renamed without losing their bindings. Deleting a profile removes its bindings.

For games started by a launcher, open **App rules... > Add app...** and choose the running
game using its title and full path. **Add active app...** remains a shortcut for the last
active executable. **Active app** in the filter editor
can also replace a binding with the last observed executable. Review the path before saving.
An identification failure shows its Windows error code; an unbound application is shown as
having no assigned filter. Application matching uses the executable, independently of tab
titles or document names. Existing 2.6.0 rules load without changing their assignments.

### Default for unassigned applications

In **App rules...**, choose a display under **Unassigned applications** and set its
**Default filter**. **Desktop settings (default)** restores that display's existing desktop
calibration. Alternatively, choose any saved filter, such as Common, to use for all applications
without an enabled, available binding on that display. Specific application bindings take
priority, so a game's Gaming filter overrides the Common fallback.

Click **Save** to persist the selection and resume automatic switching. **Cancel** discards
changes. Each display has its own fallback; unconfigured displays keep their desktop settings.
Turning off automatic switching or exiting restores desktop settings. Deleting the chosen
profile resets its fallback to desktop settings. Editing an app filter shared with a fallback
creates a separate copy so the default remains unchanged.

Custom fallbacks use version 3 of `app_rules.v1`; configurations without custom fallbacks
continue to save in version 2. Fallbacks and application bindings are saved together atomically.

Focus changes reapply the matching filter even when its parameters have not changed, since
the application may have changed the display ramp. Notifications are coalesced over 150 ms
without repeatedly restarting the deadline. Automatic switching preserves desktop settings
and restores them when the active application has neither a binding nor a custom default.

Opening the editor restores desktop settings and pauses switching while you edit. Pending
adjustments save before automatic switching resumes when you return to another application.
Manually selecting a profile or restoring defaults pauses automatic switching; click
**Resume auto-switch** or use the tray menu to resume. Turning off automatic switching or
exiting restores desktop settings. Automatic application changes do not overwrite saved
profiles or per-display default settings.

At launch, the display with the largest current resolution (`width × height`) is selected.
Equal resolutions prefer the Windows primary display. Refreshing keeps your current display
selected while it remains connected.

### Planned sidebar redesign

The [sidebar interaction proposal](docs/design/sidebar-interaction-proposal.md) describes
a future neutral-gray interface, a single application-settings entry, and recognition
assistance that waits for the user to apply a suggested filter. These are design proposals,
not features implemented in 2.6.1. This release retains the existing automatic-switching
controls and manual pause behavior.

## Local data

Profiles, per-display settings, captured base ramps, and diagnostic logs are stored locally under:

```text
%LOCALAPPDATA%\GammaChangerCpp\
```

The application does not require an online service to adjust displays or store profiles.
Application bindings and the automatic-switching preference are stored separately in `app_rules.v1`.

## Project structure

```text
assets/       Embedded wallpaper, application icon, and visual resources
include/      Application, display, calibration, profile, and UI headers
src/          Calibration core, application services, persistence, and checks
src/ui/       Win32 UI modules (window, controls, layout, painting, profiles, tray)
```

Display-ramp access is isolated behind `DisplayRampBackend`, allowing transaction and rollback
behavior to be tested without modifying a real monitor.
