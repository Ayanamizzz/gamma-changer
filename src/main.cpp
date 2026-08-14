#include "app_version.h"
#include "calibration_controller.h"
#include "display_manager.h"
#include "gamma_lut.h"
#include "profile_store.h"

#include <windows.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace gamma_changer;

namespace {

void print_usage() {
    std::wcout << kApplicationName << L" " << kApplicationVersion << L"\n\n"
               << L"Usage:\n"
               << L"  gamma_changer.exe list\n"
               << L"  gamma_changer.exe apply --display \\\\.\\DISPLAY1 [options]\n"
               << L"  gamma_changer.exe reset --display \\\\.\\DISPLAY1\n\n"
               << L"Options:\n"
               << L"  --gamma <value>\n"
               << L"  --brightness <value>\n"
               << L"  --contrast <value>\n"
               << L"  --r-gain <value>\n"
               << L"  --g-gain <value>\n"
               << L"  --b-gain <value>\n";
}

std::optional<std::wstring> option_value(int argc, wchar_t** argv, const std::wstring& name) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == name) return std::wstring(argv[i + 1]);
    }
    return std::nullopt;
}

bool parse_double(const std::optional<std::wstring>& value, double& output) {
    if (!value) return false;
    try {
        std::size_t consumed = 0;
        output = std::stod(*value, &consumed);
        return consumed == value->size();
    } catch (...) {
        return false;
    }
}

const DisplayInfo* find_display(const std::vector<DisplayInfo>& displays, const std::wstring& name) {
    for (const auto& display : displays) {
        if (display.device_name == name) return &display;
    }
    return nullptr;
}

int run_list() {
    const auto displays = DisplayManager::enumerate();
    if (displays.empty()) {
        std::wcerr << L"No attached displays found.\n";
        return 1;
    }
    for (const auto& display : displays) {
        std::wcout << display.device_name << L"\t" << display.device_string << L"\n";
    }
    return 0;
}

int run_apply(int argc, wchar_t** argv) {
    const auto name = option_value(argc, argv, L"--display");
    if (!name) {
        std::wcerr << L"--display is required.\n";
        return 2;
    }

    const auto displays = DisplayManager::enumerate();
    const auto* display = find_display(displays, *name);
    if (display == nullptr) {
        std::wcerr << L"Display not found: " << *name << L"\n";
        return 1;
    }

    ProfileStore store;
    CalibrationController controller(store);
    GammaParams params = controller.load_settings(*display);
    double value = 0.0;
    if (parse_double(option_value(argc, argv, L"--gamma"), value)) params.gamma = value;
    if (parse_double(option_value(argc, argv, L"--brightness"), value)) params.brightness = value;
    if (parse_double(option_value(argc, argv, L"--contrast"), value)) params.contrast = value;
    if (parse_double(option_value(argc, argv, L"--r-gain"), value)) params.r_gain = value;
    if (parse_double(option_value(argc, argv, L"--g-gain"), value)) params.g_gain = value;
    if (parse_double(option_value(argc, argv, L"--b-gain"), value)) params.b_gain = value;

    std::wstring error;
    if (!controller.apply_and_save(*display, params, error)) {
        std::wcerr << error << L"\n";
        return 1;
    }
    std::wcout << L"Applied to " << display->device_name << L"\n";
    return 0;
}

int run_reset(int argc, wchar_t** argv) {
    const auto name = option_value(argc, argv, L"--display");
    if (!name) {
        std::wcerr << L"--display is required.\n";
        return 2;
    }
    const auto displays = DisplayManager::enumerate();
    const auto* display = find_display(displays, *name);
    if (display == nullptr) {
        std::wcerr << L"Display not found: " << *name << L"\n";
        return 1;
    }

    ProfileStore store;
    CalibrationController controller(store);
    std::wstring error;
    if (!controller.restore_original(*display, error)) {
        std::wcerr << error << L"\n";
        return 1;
    }
    std::wcout << L"Restored base ramp on " << display->device_name << L"\n";
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::wstring command = argv[1];
    if (command == L"list") return run_list();
    if (command == L"apply") return run_apply(argc, argv);
    if (command == L"reset") return run_reset(argc, argv);
    print_usage();
    return 2;
}
