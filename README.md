# Gamma Changer

Gamma Changer is a native Windows desktop utility for adjusting the software gamma ramp of
each connected display. It provides a modern, high-DPI Win32 interface with per-monitor
settings, four reusable profiles, direct numeric input, and a live tone-response preview.

Current version: **2.4.0**

## Features

- Detects multiple displays and uses friendly monitor names when Windows exposes them.
- Adjusts Gamma, Brightness, Contrast, and individual Red/Green/Blue gain.
- Supports sliders and precise numeric entry with immediate visual feedback.
- Shows the reference and adjusted tone-response curves.
- Provides a scrollable list of persistent, renameable profiles with no four-slot limit.
- Automatically saves changes after a short delay without interrupting profile switching.
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

Choose a display, then adjust the values with the sliders or numeric fields. Changes are applied
and saved automatically. Use **Restore defaults** to return the selected display to a neutral LUT.

- Minimizing the window keeps the application available in the system tray.
- Left-clicking the tray icon restores the window.
- Right-clicking the tray icon provides profile switching, Show Window, and Exit commands.
- Closing the main window exits the application completely.

## Local data

Profiles, per-display settings, captured base ramps, and diagnostic logs are stored locally under:

```text
%LOCALAPPDATA%\GammaChangerCpp\
```

The application does not require an online service to adjust displays or store profiles.

## Project structure

```text
assets/       Embedded wallpaper, application icon, and visual resources
include/      Application, display, calibration, profile, and UI headers
src/          Calibration core, application services, persistence, and checks
src/ui/       Win32 UI modules (window, controls, layout, painting, profiles, tray)
```

Display-ramp access is isolated behind `DisplayRampBackend`, allowing transaction and rollback
behavior to be tested without modifying a real monitor.
