# Gamma Changer 2.2 Completion Report

Date: 2026-08-13  
Target: Windows x64, C++17, native Win32

## Completed

- Preserved the current Mica/Acrylic + Sakura wallpaper layout and compact sidebar.
- Added strict shared ranges and validation for Gamma, Brightness, Contrast, and RGB gain.
- Separated temporary preview, committed display state, and saved profile state.
- Restores uncommitted preview on cancel, display switch/refresh, session end, and normal exit.
- Added stable physical-display identity with legacy `DISPLAY1/2` migration.
- Re-enumerates and reapplies committed values after display changes and system resume.
- Detects HDR/Advanced Color and shows a restrained compatibility warning.
- Uses atomic, flushed writes for display settings, profiles, presets, and captured ramps.
- Rejects corrupt, partial, duplicate-field, unsupported-version, truncated, and oversized data.
- Added real dirty-state comparison, explicit Reset changes, and press-and-hold Before/After.
- Added inline profile rename, duplicate/delete actions, and Tray profile switching.
- Added keyboard navigation, numeric Escape, per-field double-click defaults, and DPI font refresh.
- Cached wallpaper composition and base-ramp availability to remove repeated hot-path work.
- Added rotating INFO/WARN/ERROR diagnostics without logging slider-preview spam.
- Embedded the Per-Monitor V2 manifest and statically linked the Release runtime.
- Eliminated click-time whole-window repaint and added persistent double-buffered presentation
  for the wallpaper, translucent surfaces, curve, and footer.
- Replaced modal unsaved-change prompts with 700 ms debounced atomic auto-save, immediate
  profile application, passive Saving/Saved feedback, retry-on-error, and one-step Ctrl+Z.
- Simplified daily controls with Restore defaults, a real per-user Start with Windows switch,
  silent startup-to-Tray restoration, and a consolidated New profile/trash action row.

## Verification

- Release x64 GUI, CLI, and safety-test targets build successfully with `/W4` and optimization.
- `core_safety_check`: 100% passed.
- Real display enumeration: 2 displays (`S65`, `25G3Z`).
- GUI smoke test: profile switch, Live Preview toggle, dirty-state action, and resize passed.
- Default-width and 1280 px screenshots visually inspected with no overlap or clipping.
- 160 resize/repaint cycles: GDI objects `42 -> 42` (delta `0`).
- Final binary dependency inspection shows only Windows system DLLs.
- Embedded manifest extraction confirms `PerMonitorV2` and Common Controls v6.

## Deliberate boundaries

- Forced termination by Task Manager cannot run in-process preview cleanup. A future guarantee
  for that case would require a separate watchdog process and is intentionally not added to
  this lightweight release.
- Startup registration, global hotkeys, profile import/export, and a full Appearance page remain
  optional future features; none are required for the completed reliability baseline.
