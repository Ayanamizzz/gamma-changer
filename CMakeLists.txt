cmake_minimum_required(VERSION 3.21)
project(gamma_changer LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # Static CRT: the release is a single portable executable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /WX /permissive-)
endif()

# ---------------------------------------------------------------------------
# gamma_core: domain + application services. No window, dialog, or GDI code.
# Every test links this instead of re-listing source files.
# ---------------------------------------------------------------------------
add_library(gamma_core STATIC
    src/app_profile_rules.cpp
    src/app_profile_runtime.cpp
    src/calibration_controller.cpp
    src/calibration_session.cpp
    src/display_manager.cpp
    src/display_ramp_backend.cpp
    src/display_selection.cpp
    src/file_io.cpp
    src/foreground_app_watcher.cpp
    src/gamma_lut.cpp
    src/instance_manager.cpp
    src/logger.cpp
    src/profile_list_logic.cpp
    src/profile_manager.cpp
    src/profile_store.cpp
    src/startup_manager.cpp
)
target_include_directories(gamma_core PUBLIC include)
target_compile_definitions(gamma_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gamma_core PUBLIC user32 gdi32 shell32 advapi32 dwmapi)

# ---------------------------------------------------------------------------
# gamma_ui: the Win32 presentation layer.
# ---------------------------------------------------------------------------
add_library(gamma_ui STATIC
    src/application.cpp
    src/ui/app_dialogs.cpp
    src/ui/app_profiles.cpp
    src/ui_rendering.cpp
    src/ui/controls.cpp
    src/ui/layout.cpp
    src/ui/main_window.cpp
    src/ui/painting.cpp
    src/ui/profile_list.cpp
    src/ui/tray.cpp
)
target_include_directories(gamma_ui PUBLIC src)
target_link_libraries(gamma_ui PUBLIC gamma_core
    comctl32 uxtheme windowscodecs ole32 msimg32 uuid gdiplus)

set(gamma_resources src/app.rc src/wallpaper.rc)

add_executable(gamma_changer_gui WIN32
    src/main.cpp
    ${gamma_resources}
)
target_link_libraries(gamma_changer_gui PRIVATE gamma_ui)

# Developer tool: off-screen PNG snapshots of the real UI with sample data and a
# fake gamma backend. Not part of the release package.
add_executable(gamma_ui_snapshot src/tools/ui_snapshot.cpp ${gamma_resources})
target_link_libraries(gamma_ui_snapshot PRIVATE gamma_ui)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
function(gamma_core_test name)
    add_executable(${name} src/${name}.cpp)
    target_link_libraries(${name} PRIVATE gamma_core)
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
endfunction()

gamma_core_test(core_safety_check)
gamma_core_test(profile_migration_check --self-test)
gamma_core_test(calibration_transaction_check)
gamma_core_test(app_profile_rules_check)
gamma_core_test(app_profile_runtime_check)

# Drives the real dialogs and window procedure (resources needed).
add_executable(foreground_profile_check src/foreground_profile_check.cpp ${gamma_resources})
target_link_libraries(foreground_profile_check PRIVATE gamma_ui)
add_test(NAME foreground_profile_check COMMAND foreground_profile_check)
set_tests_properties(foreground_profile_check PROPERTIES TIMEOUT 20)

if (MSVC)
    # app.rc provides the DPI/Common Controls manifest as a single embedded resource.
    target_link_options(gamma_changer_gui PRIVATE /MANIFEST:NO)
    target_link_options(foreground_profile_check PRIVATE /MANIFEST:NO)
    target_link_options(gamma_ui_snapshot PRIVATE /MANIFEST:NO)
endif()
