#pragma once

#include "gamma_types.h"

#include <string>

namespace gamma_changer {

class DisplayRampBackend {
public:
    virtual ~DisplayRampBackend() = default;
    virtual bool read(const DisplayInfo& display, GammaRamp& ramp,
                      std::wstring& error) = 0;
    virtual bool write(const DisplayInfo& display, const GammaRamp& ramp,
                       std::wstring& error) = 0;
};

class WindowsDisplayRampBackend final : public DisplayRampBackend {
public:
    bool read(const DisplayInfo& display, GammaRamp& ramp,
              std::wstring& error) override;
    bool write(const DisplayInfo& display, const GammaRamp& ramp,
               std::wstring& error) override;
};

DisplayRampBackend& system_display_ramp_backend();

}  // namespace gamma_changer
