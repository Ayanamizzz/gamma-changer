# Gamma Changer Architecture

## Goals

- Keep the current lightweight native Win32 product. No UI framework, database,
  WebView, or background service is planned.
- Move business logic out of `gui_main.cpp` into testable modules.
- Keep behavior-compatible incremental refactors; never rewrite the UI in one
  step.

## Target layers

```text
Presentation (Win32 only)
  MainWindow / Renderer / Layout / Tray / Dialogs

Application services
  CalibrationSession
  ProfileService / ProfileListLogic
  DisplayWatcher
  StartupService
  LogService

Domain (no windows.h)
  CalibrationSettings / LutGenerator
  ProfileStore / ProfileManager / DisplayInfo
  ProfileListLogic
```

Rules:

1. Domain modules never include `<windows.h>`.
2. Application services own all state transitions and persistence decisions.
3. Presentation modules only translate window messages into service calls and
   render the resulting state.

## Current refactor status

- `profile_list_logic.{h,cpp}`: pure helpers for profile ids/names, deletion
  index adjustment, scroll clamping, and scroll-into-view calculations.
- `calibration_session.{h,cpp}`: pure UI-facing calibration state (committed
  settings, dirty flag, undo snapshot, comparing-original) extracted from
  `GuiState`.
- `src/ui/gui_internal.h`: shared `GuiState`, control ids, message ids, and UI
  function declarations.
- `src/ui/tray.cpp`: tray icon/menu/profile application moved out of
  `gui_main.cpp`.
- `src/ui/painting.cpp`: invalidation helpers, owner-draw buttons, preview
  curves, background compositing, and the double-buffer paint path moved out of
  `gui_main.cpp`.
- `src/ui/layout.cpp`: DPI layout, font creation/application, and the shared
  layout helpers moved out of `gui_main.cpp`.
- `src/ui/profile_list.cpp`: dynamic profile list, inline rename, create/
  duplicate/delete, selection/undo, and button lifecycle moved out of
  `gui_main.cpp`.
- `src/ui/controls.cpp`: control creation, numeric/slider subclasses, control
  synchronization helpers, and `create_controls` moved out of `gui_main.cpp`.
- `src/application.cpp`: calibration/display application services.
- `src/ui/main_window.cpp`: window procedure and `wWinMain` entry point.
  `gui_main.cpp` has been removed.
- `calibration_controller`: application-level calibration transactions.
- `profile_store` / `profile_manager`: persistence and profile collection
  ownership.
- `app_profile_rules`: pure application filter matching, default selection, and
  shared-template copying for independent application settings. Rules reference
  stable IDs and accept user-selected executables and names. Binding a filter selects
  it as the application's default and reuses an existing matching app/display/profile rule.
  Per-display fallback preferences apply to unbound applications; missing preferences
  restore desktop calibration. Fallback references participate in shared-template copying
  and profile deletion. The version 3 rule format saves fallback preferences with app bindings.
- `src/ui/app_profiles.cpp`: application-specific configuration views, filter
  and parameter dialogs, and foreground switching. Dialog edits stay in a draft
  until saved; runtime application preserves the desktop LUT.
- `foreground_app_watcher`: Win32 foreground notifications delivered to the UI
  message loop, with no polling or separate service. Focus events request a runtime
  reapply and are coalesced over a fixed 150 ms interval. The rule editor can bind
  the last observed executable directly and reports process-query errors.
- `running_applications`: reads visible desktop windows and groups them by complete
  executable path. Tool windows, the desktop shell, cloaked windows and the current
  process are excluded. Unreadable process paths retain a title and error code.
  The application picker takes a snapshot and refreshes it on request; titles are
  display metadata and never become matching conditions or saved bindings.
- `apply_application_profiles`: applies resolved executable assignments using the
  same `DisplayRampBackend` as the calibration controller. Its browser/game/desktop
  transitions can be exercised with a fake backend without changing a real display.
- `display_selection`: pure highest-pixel-count default selection; refreshing
  preserves the selected physical display.

## Planned next steps

1. Introduce `ProfileService` as the single writer of `profiles.v1`; UI keeps a
   working copy and calls `replace_profiles` through the service.
2. Introduce `DisplayWatcher` to debounce topology/resume events and enforce the
   "never write LUT to an unconfigured display" rule in one place.
3. Optionally virtualize the profile list when profile counts grow beyond a few
   dozen rows.

## Testing strategy

1. Unit-test all pure logic (LUT, ranges, indexes, scroll math, naming).
2. Integration-test persistence with temporary directories.
3. Transaction-test calibration with `DisplayRampBackend` fakes.
4. Keep GUI automation limited to message-level smoke tests; avoid pixel tests.

`foreground_profile_check` runs the actual foreground scheduling and application-dialog
paths against a fake ramp backend, with isolated temporary profile storage. It covers
application/default transitions, non-starving event coalescing, running-app selection,
and save/cancel behavior for enabling rules and editing software filter settings.

## Release engineering

- `/W4 /WX` builds and CTest are mandatory.
- CI already builds Release/Debug on Windows and runs tests.
- Future: clang-format, ASan Debug job, single version source, code signing,
  installer or portable package.
