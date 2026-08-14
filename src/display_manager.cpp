#include "display_manager.h"

#include "logger.h"

#include <sstream>
#include <unordered_map>
#include <utility>

namespace gamma_changer {
namespace {

std::wstring last_error_message(const wchar_t* operation) {
    const DWORD code = GetLastError();
    std::wstringstream stream;
    if (code == ERROR_SUCCESS) {
        stream << operation
               << L" was rejected by the display driver; HDR, a GPU reset, or a temporary "
                  L"display transition may be blocking software calibration";
    } else {
        stream << operation << L" failed (Win32 error " << code << L")";
    }
    return stream.str();
}

std::wstring display_context(const DisplayInfo& display) {
    std::wstringstream stream;
    stream << L" [device=" << display.device_name;
    if (!display.device_string.empty()) stream << L", name=" << display.device_string;
    if (!display.stable_id.empty()) stream << L", stable-id=" << display.stable_id;
    stream << L", mode=" << display.width << L'x' << display.height;
    if (display.refresh_rate > 0) stream << L'@' << display.refresh_rate;
    stream << L", HDR=" << (display.hdr_active ? L"on" : L"off") << L']';
    return stream.str();
}

class HdcGuard {
public:
    explicit HdcGuard(HDC hdc) : hdc_(hdc) {}
    ~HdcGuard() {
        if (hdc_ != nullptr) DeleteDC(hdc_);
    }
    HDC get() const { return hdc_; }
    HdcGuard(const HdcGuard&) = delete;
    HdcGuard& operator=(const HdcGuard&) = delete;

private:
    HDC hdc_;
};

struct DisplayIdentity {
    std::wstring friendly_name;
    std::wstring stable_id;
    bool hdr_active = false;
};

std::unordered_map<std::wstring, DisplayIdentity> display_identities() {
    std::unordered_map<std::wstring, DisplayIdentity> identities;
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
        return identities;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                           &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
        return identities;
    }

    for (UINT32 index = 0; index < path_count; ++index) {
        const auto& path = paths[index];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
            DisplayIdentity identity;
            if (target.monitorDevicePath[0] != L'\0') {
                identity.stable_id = target.monitorDevicePath;
            }
            if (target.monitorFriendlyDeviceName[0] != L'\0') {
                identity.friendly_name = target.monitorFriendlyDeviceName;
            }
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            color.header.size = sizeof(color);
            color.header.adapterId = path.targetInfo.adapterId;
            color.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
                identity.hdr_active = color.advancedColorEnabled != 0;
            }
            identities.emplace(source.viewGdiDeviceName, std::move(identity));
        }
    }
    return identities;
}

struct EnumerationContext {
    std::vector<DisplayInfo>* displays = nullptr;
    const std::unordered_map<std::wstring, DisplayIdentity>* identities = nullptr;
};

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    const auto* enumeration = reinterpret_cast<const EnumerationContext*>(context);
    auto* displays = enumeration->displays;
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info)) return TRUE;

    DisplayInfo info;
    info.device_name = monitor_info.szDevice;
    const auto identity = enumeration->identities->find(info.device_name);
    if (identity != enumeration->identities->end()) {
        info.stable_id = identity->second.stable_id;
        info.device_string = identity->second.friendly_name;
        info.hdr_active = identity->second.hdr_active;
    }
    info.width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    info.height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    info.primary = (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(info.device_name.c_str(), ENUM_CURRENT_SETTINGS, &mode)) {
        info.width = static_cast<int>(mode.dmPelsWidth);
        info.height = static_cast<int>(mode.dmPelsHeight);
        info.refresh_rate = static_cast<int>(mode.dmDisplayFrequency);
    }

    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0)) break;
        if (info.device_name != adapter.DeviceName) continue;
        info.state_flags = adapter.StateFlags;

        if (info.device_string.empty()) {
            DISPLAY_DEVICEW physical_monitor{};
            physical_monitor.cb = sizeof(physical_monitor);
            if (EnumDisplayDevicesW(adapter.DeviceName, 0, &physical_monitor, 0)) {
                info.device_string = physical_monitor.DeviceString;
            }
        }
        break;
    }

    if (!info.device_name.empty()) displays->push_back(std::move(info));
    return TRUE;
}

}  // namespace

std::vector<DisplayInfo> DisplayManager::enumerate() {
    std::vector<DisplayInfo> displays;
    const auto identities = display_identities();
    const EnumerationContext context{&displays, &identities};
    if (!EnumDisplayMonitors(nullptr, nullptr, collect_monitor,
                             reinterpret_cast<LPARAM>(&context))) {
        log_message(LogLevel::error, last_error_message(L"EnumDisplayMonitors"));
    } else {
        log_message(LogLevel::info, L"Displays detected: " + std::to_wstring(displays.size()));
    }
    return displays;
}

bool DisplayManager::read_ramp(const DisplayInfo& display, GammaRamp& ramp, std::wstring& error) {
    HDC raw = CreateDCW(L"DISPLAY", display.device_name.c_str(), nullptr, nullptr);
    if (raw == nullptr) {
        error = last_error_message(L"CreateDCW");
        log_message(LogLevel::error, error);
        return false;
    }
    HdcGuard guard(raw);
    if (!GetDeviceGammaRamp(guard.get(), ramp.channel[0].data())) {
        error = last_error_message(L"GetDeviceGammaRamp");
        log_message(LogLevel::error, error + display_context(display));
        return false;
    }
    return true;
}

bool DisplayManager::write_ramp(const DisplayInfo& display, const GammaRamp& ramp, std::wstring& error) {
    HDC raw = CreateDCW(L"DISPLAY", display.device_name.c_str(), nullptr, nullptr);
    if (raw == nullptr) {
        error = last_error_message(L"CreateDCW");
        log_message(LogLevel::error, error);
        return false;
    }
    HdcGuard guard(raw);
    if (!SetDeviceGammaRamp(guard.get(), const_cast<std::uint16_t*>(ramp.channel[0].data()))) {
        error = last_error_message(L"SetDeviceGammaRamp");
        log_message(LogLevel::error, error + display_context(display));
        return false;
    }
    return true;
}

}  // namespace gamma_changer
