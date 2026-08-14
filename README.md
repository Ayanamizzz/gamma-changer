# Gamma Changer (C++ rewrite)

## v2.3.1 explicit close diagnostics

- Routes the title-bar close command explicitly through the final-save-and-exit path.
- Logs minimize-to-Tray, close request, window destruction, and process-exit milestones so an
  off-screen/minimized window can be distinguished from a process that failed to terminate.

## v2.3 reliability release

- Flushes the final debounced adjustment before closing and only interrupts exit when that save
  fails, offering Retry, Exit without saving, and Cancel.
- Enforces one GUI instance: a second manual launch activates the existing window while duplicate
  Windows-startup launches exit silently.
- Uses a testable display-ramp backend and a transactional calibration commit that restores the
  previous ramp when persistence fails.
- Preserves pending per-display adjustments across refresh and hot-plug events, then reapplies them
  by stable monitor ID when the display returns.
- Retries resume/topology recovery without blocking the UI and records useful monitor/HDR context
  when a graphics driver rejects software calibration.
- Validates and repairs a stale Windows startup path after the portable executable is moved.
- Rejects an entire invalid profile collection instead of silently omitting damaged entries and
  registers migration plus calibration transaction checks with CTest.

## v2.2 simpler daily controls

- Replaces `Reset changes` with `Restore defaults`: Gamma/Contrast/RGB return to `1.00`,
  Brightness to `0.00`, and the result is applied and auto-saved immediately.
- Keeps the previous profile contents as a one-step `Ctrl+Z` recovery after restoring defaults.
- Replaces the user-facing Live Preview switch with a real `Start with Windows` setting under
  the current user's Windows startup registration. Automatic preview remains always available.
- Startup launches silently to the Tray and reapplies saved display calibration without opening
  the main window; ordinary manual launches continue to open normally.
- Closing the main window now exits the application completely instead of leaving it running in
  the notification area. Minimizing still keeps the utility available from the Tray.
- Consolidates profile management into one row: a clear `+ New profile` primary action and a
  compact trash action that is enabled only when the selected profile can be deleted.

## v2.1 interruption-free auto-save

- Removes the modal Save/Discard/Cancel dialog when switching profiles.
- Applies a selected profile immediately and keeps a one-step `Ctrl+Z` route back to the
  previous profile and values.
- Slider and numeric changes update the visual preview immediately, then atomically save the
  active profile and commit the display after 700 ms without further input.
- The former Save Profile action is now a passive `Saving...` / `Saved` indicator. It becomes
  an actionable `Retry save` button only if persistence or display application fails.
- Keeps explicit confirmation only for destructive profile deletion.

## v2.0.1 anti-flicker patch

- Renders the parent wallpaper, translucent panels, graph, and footer into a persistent
  off-screen buffer and presents each invalid region in one `BitBlt` operation.
- Replaces whole-window invalidation during clicks, numeric focus/hover, slider changes,
  status updates, and profile refreshes with targeted graph, footer, input, or item redraws.
- Keeps full-window repaint only for actual resize and DPI transitions.
- Verified profile switching, Live Preview, dirty-state actions, and dual-display discovery;
  160 resize/repaint cycles retain the same GDI object count.

## v2.0 reliability and daily-use release

- Separates temporary Live Preview from committed display settings and saved profiles.
  Unsaved previews are restored when Live Preview is disabled, displays are switched,
  displays are refreshed, Windows ends the session, or the application exits normally.
- Replaces index-only display persistence with the stable DisplayConfig monitor path,
  while retaining automatic reads and migration of legacy `DISPLAY1/2` settings and ramps.
- Responds to display topology changes and system resume without polling; committed
  calibration is reapplied after Windows or a graphics driver resets the display pipeline.
- Centralizes safe ranges for Gamma, Brightness, Contrast, and RGB gain. Invalid, partial,
  duplicate, non-finite, and out-of-range values cannot enter LUT generation.
- Makes every settings and ramp write atomic and flushes temporary files before replacement.
  Truncated or oversized base-ramp files are rejected rather than used by Reset.
- Distinguishes missing, corrupt, and unsupported profile files. Damaged/future files stay
  untouched and profile writes are disabled instead of silently overwriting user data.
- Makes dirty state compare the actual settings. Profile switching offers Save, Discard,
  and Cancel; failed saves/applies roll back instead of ending with a misleading success.
- Adds inline profile Rename (`F2` or double click), a right-click menu for Rename,
  Duplicate, and Delete, and preserves the compact four-slot compatibility layout.
- Renames the former ambiguous Reset action to `Reset changes`; it restores the last
  committed settings without erasing the original system ramp or saved profile.
- Adds `Hold original` for press-and-hold Before/After comparison, Escape cancellation and
  double-click defaults in numeric inputs, normal Tab navigation, and Per-Monitor DPI font
  recreation.
- Caches the fully composed wallpaper/overlay surface by window size and caches validated
  base-ramp presence, avoiding repeated full-image scaling and disk reads during slider use.
- Adds HDR/Advanced Color detection with a restrained warning, resilient Tray registration,
  direct switching between saved profiles from the Tray, a lightweight rotating
  INFO/WARN/ERROR log, and one source of truth for the application version.
- Adds `core_safety_check`/CTest coverage for LUT limits, NaN/Infinity, monotonic defaults,
  settings/profile round-trips, and corrupt/truncated/oversized file rejection.
- Embeds the Per-Monitor V2/Common Controls manifest and statically links the Release C++
  runtime, so the exported x64 GUI remains sharp across DPI changes and needs no separate
  Visual C++ Redistributable installation.

The existing Mica/Acrylic layout, Sakura wallpaper, 264 px sidebar, calibration rows, and
Profile spacing are intentionally preserved.

## v1.5 compact profile correction

- Corrects the v1.4 over-spacing without reverting any earlier UI or business changes.
- Aligns the Profiles title, item containers, and New Profile action to the same 30 px
  sidebar content boundary; text uses only 14 px of internal item padding.
- Restores desktop-utility density with 34 px rows, 2 px item gaps, and a 9 px title gap.
- Keeps full-width click and hover surfaces while removing borders from selected/hovered
  rows; selection is expressed by a subtle surface plus a 3 px internal accent bar.
- Separates mouse selection from keyboard focus: mouse clicks never draw a focus rectangle,
  while keyboard navigation may show a restrained 1 px solid focus-visible ring.
- Uses 11 px before New Profile and 8 px before the low-weight Remove Selected action.

## v1.4 profile spacing polish

- Keeps the 264 px sidebar and all existing sections unchanged while giving the Profiles
  module its own 32 px section inset and 40 px list inset.
- Uses 42 px profile rows with 6 px gaps and 14 px between the section label and first row.
- Separates the 3 px selected accent bar from its label by 13 px and retains the subtle
  selected background, muted empty slots, hover state, and independent focus treatment.
- Aligns New Profile to the same content bounds as the list, with 14 px above it and a
  further 10 px separation before the low-weight Remove Selected action.
- Verified at the default window size and 1280 px width without changing display controls,
  the main workspace, wallpaper, footer, or profile behavior.

## v1.3 release polish

- Keeps the established v1.2 layout and makes the wallpaper subordinate to the UI with
  a darker 85% sidebar overlay, a neutral 82% main overlay, and reduced image intensity.
- Completes profile navigation states: selected surface plus accent bar, subtle hover,
  muted empty slots, and a separate keyboard-focus ring.
- Adds hover treatment to numeric fields, slightly stronger desktop-scale slider tracks,
  and theme-based hover/focus/disabled colors without changing input behavior.
- Gives reference and adjusted response curves distinct neutral/indigo weights and a
  matching text legend; neutral calibration still produces correctly overlapping curves.
- Compacts footer actions while preserving green active/ready status semantics and a clear
  primary Save Profile action.
- Consolidates remaining component colors and control metrics in `ui_theme.h`.

## v1.2 UI refinement

- Preserves the v1.1 layout while reducing the main workspace overlay to about 78%,
  keeping the wallpaper visible without compromising calibration readability.
- Presents profiles as navigation rows with a subtle selected surface and 3 px accent bar,
  rather than bordered rectangular buttons.
- Replaces native-blue trackbars with compact custom lavender tracks, active ranges,
  circular thumbs, and focus/hover halos while retaining native keyboard and pointer input.
- Softens numeric fields with rounded opaque interaction surfaces and focus-only accent
  borders; row geometry remains centralized in the existing reusable layout helper.
- Uses safe DWM caption, text, and border coloring instead of a risky custom title bar.
- Centralizes wallpaper opacity, overlays, position, scaling, and reserved blur strength in
  `BackgroundOptions`, ready for a future Appearance page.
- Normalizes footer state colors: green for success, amber for warning, red for errors,
  and neutral gray for idle or pending changes.

## v1.1 Sakura Mica UI

- Uses the supplied cherry-blossom illustration as an embedded, offline UI asset.
- Replaced whole-window opacity with layered rendering: wallpaper, deep plum acrylic
  sidebar, warm translucent workspace, denser response card, and an independent footer.
- Derived the selected state, response curve, borders, and supporting text from the
  wallpaper's restrained plum/pink palette while keeping numeric controls opaque.
- Replaced the large live-preview action with a compact green state toggle.
- The save action is disabled until values change; pending changes use the accent color
  instead of a warning color.
- Keeps Per-Monitor V2 rendering and verified both attached displays remain available in
  the monitor selector.

## v1.0 Calibration UI

- Replaced the 2x2 preset grid with a compact vertical profile list.
- Moved display refresh into a small sidebar action and removed duplicate device text.
- Reorganized calibration into one compact workflow: Brightness, Contrast, Gamma, then RGB gain.
- Added a low-weight reference line alongside the adjusted LUT response curve.
- Uses a restrained Windows compositor treatment; native text, sliders, and numeric fields
  remain readable and do not rely on glass-only GDI rendering.
- Centralized the primary UI palette and spacing constants used by the new layout.
- `New profile` fills the next available compatibility slot; `Save profile` persists the
  selected profile and current display settings.

The minimum window is 960 x 760 logical pixels and the main content expands horizontally.
The display selector retains its full dropdown height so multi-monitor selection remains usable.

## v0.9 Profile foundation

- Expanded display metadata with resolution, refresh rate, and primary-display state.
- Added a versioned `Profile` model and `ProfileManager` without removing the original
  four-slot preset compatibility layer.
- Existing `presets.profile` data migrates into `profiles.v1`; the legacy file remains
  readable and is still updated during the UI transition.
- Profile writes use a temporary file and an atomic Windows replacement operation.
- Added an isolated migration-check target with an injectable configuration directory.

This foundation allows the next UI stage to replace the 2x2 preset buttons with a compact,
vertical profile list and to present display metadata without changing calibration behavior.

## v0.8 Core refactor

- Added `CalibrationSettings` as the shared calibration model while retaining the
  `GammaParams` compatibility alias for existing callers and saved data.
- Added `LutGenerator` and a small `CalibrationController` used by both the GUI and CLI.
- Live preview now applies the temporary LUT without rewriting the saved display profile
  on every debounced slider update.
- The response graph is generated from the exact same `GammaRamp` used by the display,
  preventing preview/actual curve drift.
- No profile or ramp file format was changed.

The next UI phase will use restrained Windows 11 transparency: a system Mica/Acrylic
backdrop with lightly translucent navigation and graph surfaces, while keeping text,
numeric inputs, and calibration controls opaque for readability.

## v0.7 Studio UI

The desktop interface has been rebuilt around a monitor-first workflow:

- dark device and preset sidebar with four persistent slots;
- live RGB LUT response-curve preview;
- separate Tone and Color balance workspaces;
- direct numeric entry plus Up/Down arrow increments of `0.01`;
- Enter applies the current values immediately;
- switchable Live preview and Manual apply modes;
- unsaved-change confirmation when switching displays in manual mode;
- confirmation before clearing a stored preset;
- high-DPI, rounded-window and Windows common-control styling.

The selected display is always shown below the selector. Friendly monitor names are used when
Windows exposes them, with `Screen 1`, `Screen 2`, and so on as stable fallbacks.

This is a Windows-native C++ rewrite of the original Gamma Changer, with a
modern native GUI and a small testable LUT/display core.

## Current features

- Enumerates attached Windows displays.
- Applies a 256-entry, 16-bit-per-channel software Gamma Ramp.
- Supports Gamma, brightness, contrast and independent RGB gain.
- Stores parameters per display device name.
- Captures the original Ramp before the first apply.
- Restores the captured Ramp with `reset`.
- Uses RAII for display DC cleanup.
- Uses Per-Monitor V2 DPI awareness and DPI-scaled layout to keep text sharp.
- Provides four fixed persistent defaults (`Default 1` to `Default 4`) with save,
  clear and one-click switching.

## Build

Requirements:

- Windows 10/11
- Visual Studio 2022 or newer
- CMake 3.21+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The repository also includes `CMakePresets.json`. In VS Code, open the folder
containing `CMakeLists.txt`, run `CMake: Select Configure Preset`, choose
`Visual Studio 2022 x64`, then run `CMake: Build` and select `release-gui`.

From a Developer PowerShell for VS 2022, the preset commands are:

```powershell
cmake --preset vs2022-x64
cmake --build --preset release-gui
```

If `cmake` is not available in the current terminal, run `build-release.bat`
from this folder. It uses the CMake bundled with Visual Studio Build Tools.

If a previous copy of the tray application is still running, exit it from the
tray menu before rebuilding; otherwise the linker may report `LNK1104` because
the old executable is locked.

## Usage

```powershell
gamma_changer.exe list
gamma_changer.exe apply --display "\\.\DISPLAY1" --gamma 1.15 --brightness -0.05 --contrast 1.05
gamma_changer.exe reset --display "\\.\DISPLAY1"

gamma_changer_gui.exe
```

Profiles and captured base ramps are stored under:

```text
%LOCALAPPDATA%\GammaChangerCpp\
```

## Next stage

The GUI target provides a lightweight Windows 11-style, high-DPI native layout with
friendly display names from the active Windows display topology (falling back to
`Screen N`), reliable per-display selection, four persistent default slots,
Gamma/brightness/contrast controls, independent RGB gain controls, direct numeric
editing, auto-apply, Reset, display refresh, and a system tray icon.

The current layout uses a compact left navigation rail for display and preset
selection, with separate Tone and Color balance work areas on the right. Common
Controls v6, Explorer theming, Mica window treatment, and Segoe UI typography keep
the native C++ interface visually consistent with current Windows applications.

The monitor selector reserves a full drop-down list height, so every detected display
is visible rather than only the first item. UI text uses a slightly heavier Segoe UI
weight to match the compact Codex desktop style on Windows.
The next iteration should add global hotkeys, display topology notifications,
and a separate DDC/CI backend for hardware brightness and contrast.
