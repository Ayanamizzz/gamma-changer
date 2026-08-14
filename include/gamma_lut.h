#pragma once

#include "gamma_types.h"

#include <string>

namespace gamma_changer {

class LutGenerator {
public:
    bool validate(const CalibrationSettings& settings, std::wstring& error) const;
    GammaRamp generate(const CalibrationSettings& settings) const;
};

bool validate_params(const GammaParams& params, std::wstring& error);
GammaRamp build_ramp(const GammaParams& params);
GammaParams default_params();
bool settings_equal(const CalibrationSettings& left, const CalibrationSettings& right,
                    double epsilon = 1e-9);

}  // namespace gamma_changer
