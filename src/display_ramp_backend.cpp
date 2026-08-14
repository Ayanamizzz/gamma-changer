#include "display_ramp_backend.h"

#include "display_manager.h"

namespace gamma_changer {

bool WindowsDisplayRampBackend::read(const DisplayInfo& display, GammaRamp& ramp,
                                     std::wstring& error) {
    return DisplayManager::read_ramp(display, ramp, error);
}

bool WindowsDisplayRampBackend::write(const DisplayInfo& display, const GammaRamp& ramp,
                                      std::wstring& error) {
    return DisplayManager::write_ramp(display, ramp, error);
}

DisplayRampBackend& system_display_ramp_backend() {
    static WindowsDisplayRampBackend backend;
    return backend;
}

}  // namespace gamma_changer
