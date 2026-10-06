# Gamma Changer

Gamma Changer is a native Windows desktop utility for adjusting the software gamma ramp of
each connected display. It provides a modern, high-DPI Win32 interface with per-monitor
settings, a scrollable profile list, direct numeric input, and a live tone-response preview.

Current version: **2.6.0**

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
**Bind application...**) to select an `.exe`. Binding an application enables **Auto-switch**.
When that executable becomes the foreground application, its profile applies to the bound
display. Switching to another application restores that display's desktop settings.
The effect covers the whole display, including other visible windows.

Use **App rules...** to open the application configuration interface. Select any application
on the left to see only its filters on the right. **Add app...** accepts any executable;
**Add filter...** creates another named filter for that application. There are no built-in
application categories or predefined filter lists.

Choose **Use for app** to select the filter that applies when that application is active.
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

Opening the editor restores desktop settings and pauses switching while you edit. Pending
adjustments save before automatic switching resumes when you return to another application.
Manually selecting a profile or restoring defaults pauses automatic switching; click
**Resume auto-switch** or use the tray menu to resume. Turning off automatic switching or
exiting restores desktop settings. Automatic application changes do not overwrite saved
profiles or per-display default settings.

At launch, the display with the largest current resolution (`width × height`) is selected.
Equal resolutions prefer the Windows primary display. Refreshing keeps your current display
selected while it remains connected.

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
