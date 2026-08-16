cmake_minimum_required(VERSION 3.21)
project(gamma_changer LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gamma_changer_gui WIN32
    src/application.cpp
    src/ui/main_window.cpp
    src/app.rc
    src/wallpaper.rc
    src/calibration_controller.cpp
    src/calibration_session.cpp
    src/gamma_lut.cpp
    src/display_manager.cpp
    src/display_ramp_backend.cpp
    src/instance_manager.cpp
    src/startup_manager.cpp
    src/profile_store.cpp
    src/profile_manager.cpp
    src/profile_list_logic.cpp
    src/ui_rendering.cpp
    src/ui/controls.cpp
    src/ui/layout.cpp
    src/ui/painting.cpp
    src/ui/profile_list.cpp
    src/ui/tray.cpp
    src/logger.cpp
)

add_executable(profile_migration_check
    src/profile_migration_check.cpp
    src/gamma_lut.cpp
    src/profile_store.cpp
    src/profile_manager.cpp
)

add_executable(core_safety_check
    src/core_safety_check.cpp
    src/calibration_session.cpp
    src/gamma_lut.cpp
    src/profile_store.cpp
    src/profile_manager.cpp
    src/profile_list_logic.cpp
)

add_executable(calibration_transaction_check
    src/calibration_transaction_check.cpp
    src/calibration_controller.cpp
    src/display_ramp_backend.cpp
    src/display_manager.cpp
    src/gamma_lut.cpp
    src/profile_store.cpp
    src/logger.cpp
)

target_include_directories(gamma_changer_gui PRIVATE include)
target_include_directories(profile_migration_check PRIVATE include)
target_include_directories(core_safety_check PRIVATE include)
target_include_directories(calibration_transaction_check PRIVATE include)
target_compile_definitions(gamma_changer_gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(profile_migration_check PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(core_safety_check PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(calibration_transaction_check PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gamma_changer_gui PRIVATE user32 gdi32 shell32 comctl32 dwmapi uxtheme)
target_link_libraries(gamma_changer_gui PRIVATE windowscodecs ole32 msimg32 advapi32)
target_link_libraries(calibration_transaction_check PRIVATE user32 gdi32 shell32)

if (MSVC)
    foreach(target gamma_changer_gui profile_migration_check core_safety_check calibration_transaction_check)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
    target_compile_options(gamma_changer_gui PRIVATE /W4 /permissive-)
    target_compile_options(profile_migration_check PRIVATE /W4 /permissive-)
    target_compile_options(core_safety_check PRIVATE /W4 /permissive-)
    target_compile_options(calibration_transaction_check PRIVATE /W4 /permissive-)
    # app.rc provides the DPI/Common Controls manifest as a single embedded resource.
    target_link_options(gamma_changer_gui PRIVATE /MANIFEST:NO)
endif()

add_test(NAME core_safety_check COMMAND core_safety_check)
add_test(NAME profile_migration_check COMMAND profile_migration_check --self-test)
add_test(NAME calibration_transaction_check COMMAND calibration_transaction_check)
