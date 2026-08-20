cmake_minimum_required(VERSION 3.21)
project(gamma_changer VERSION 2.4.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GAMMA_CHANGER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GAMMA_CHANGER_GENERATED_DIR}")
configure_file(
    include/app_version.h.in
    "${GAMMA_CHANGER_GENERATED_DIR}/app_version.h"
    @ONLY
)
configure_file(
    src/app_version.rc.in
    "${GAMMA_CHANGER_GENERATED_DIR}/app_version.rc"
    @ONLY
)

add_executable(gamma_changer_gui WIN32
    src/application.cpp
    src/ui/main_window.cpp
    src/app.rc
    "${GAMMA_CHANGER_GENERATED_DIR}/app_version.rc"
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

if (BUILD_TESTING)
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
        src/test_logger.cpp
    )
endif()

target_include_directories(gamma_changer_gui PRIVATE "${GAMMA_CHANGER_GENERATED_DIR}" include)
target_compile_definitions(gamma_changer_gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gamma_changer_gui PRIVATE user32 gdi32 shell32 comctl32 dwmapi uxtheme)
target_link_libraries(gamma_changer_gui PRIVATE windowscodecs ole32 msimg32 advapi32)

if (BUILD_TESTING)
    foreach(target profile_migration_check core_safety_check calibration_transaction_check)
        target_include_directories(${target} PRIVATE include)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endforeach()
    target_link_libraries(calibration_transaction_check PRIVATE user32 gdi32 shell32)
endif()

if (MSVC)
    set_property(TARGET gamma_changer_gui PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(gamma_changer_gui PRIVATE /W4 /WX /permissive-)

    if (BUILD_TESTING)
        foreach(target profile_migration_check core_safety_check calibration_transaction_check)
            set_property(TARGET ${target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
        endforeach()
    endif()

    # app.rc provides the DPI/Common Controls manifest as a single embedded resource.
    target_link_options(gamma_changer_gui PRIVATE /MANIFEST:NO)
endif()

if (BUILD_TESTING)
    add_test(NAME core_safety_check COMMAND core_safety_check)
    add_test(NAME profile_migration_check COMMAND profile_migration_check --self-test)
    add_test(NAME calibration_transaction_check COMMAND calibration_transaction_check)
endif()
