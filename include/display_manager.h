#pragma once

#include "gamma_types.h"

#include <windows.h>

#include <string>
#include <vector>

namespace gamma_changer {

class DisplayManager {
public:
    static std::vector<DisplayInfo> enumerate();

    static bool read_ramp(const DisplayInfo& display, GammaRamp& ramp, std::wstring& error);
    static bool write_ramp(const DisplayInfo& display, const GammaRamp& ramp, std::wstring& error);
};

}  // namespace gamma_changer
