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
  ProfileListLogic

Infrastructure (Win32 and filesystem adapters)
  DisplayManager / DisplayRampBackend
  ProfileStore / ProfileManager
  InstanceManager / StartupManager / Logger
```

Rules:

1. Domain modules never include `<windows.h>`; Win32 and persistence adapters live in
   the infrastructure layer.
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

## Planned next steps

1. Introduce `ProfileService` as the single writer of `profiles.v1`; UI keeps a
   working copy and calls `replace_profiles` through the service.
2. Introduce `DisplayWatcher` to debounce topology/resume events and enforce the
   "never write LUT to an unconfigured display" rule in one place.
3. Optionally virtualize the profile list when profile counts grow beyond a few
   dozen rows.
4. Move Profile, per-display settings, preference, and actual Ramp snapshots behind
   one transaction service so compensation does not depend on a second generated LUT.
5. Add an accessibility theme adapter for Windows High Contrast and UI Automation
   status announcements.

## Testing strategy

1. Unit-test all pure logic (LUT, ranges, indexes, scroll math, naming).
2. Integration-test persistence with temporary directories.
3. Transaction-test calibration with `DisplayRampBackend` fakes.
4. Keep GUI automation limited to message-level smoke tests; avoid pixel tests.

## Release engineering

- `/W4 /WX` builds and CTest are mandatory.
- CI already builds Release/Debug on Windows and runs tests.
- CMake is the single version source and generates both the C++ version header
  and Windows `VERSIONINFO`; release tags must match it exactly.
- Tagged builds create a draft portable release, record SHA-256 checksums, and
  report the executable's actual Authenticode state. Signing remains optional
  until a project certificate is configured.
- Future: clang-format, an ASan/Clang job, installer packaging, and a documented
  license/attribution policy for bundled visual assets.
