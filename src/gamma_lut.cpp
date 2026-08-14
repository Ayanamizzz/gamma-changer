#include "gamma_lut.h"

#include <cmath>
#include <limits>

namespace gamma_changer {
namespace {

double clamp01(double value) {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

std::uint16_t to_u16(double value) {
    value = clamp01(value) * 65535.0;
    return static_cast<std::uint16_t>(value + 0.5);
}

bool in_range(double value, const CalibrationRange& range) {
    return std::isfinite(value) && value >= range.minimum && value <= range.maximum;
}

}  // namespace

GammaParams default_params() {
    return {};
}

bool settings_equal(const CalibrationSettings& left, const CalibrationSettings& right,
                    double epsilon) {
    return std::abs(left.gamma - right.gamma) <= epsilon &&
           std::abs(left.brightness - right.brightness) <= epsilon &&
           std::abs(left.contrast - right.contrast) <= epsilon &&
           std::abs(left.r_gain - right.r_gain) <= epsilon &&
           std::abs(left.g_gain - right.g_gain) <= epsilon &&
           std::abs(left.b_gain - right.b_gain) <= epsilon;
}

bool LutGenerator::validate(const CalibrationSettings& params, std::wstring& error) const {
    if (!in_range(params.gamma, calibration_ranges::gamma)) {
        error = L"gamma must be between 0.30 and 4.40";
        return false;
    }
    if (!in_range(params.brightness, calibration_ranges::brightness)) {
        error = L"brightness must be between -1.00 and 1.00";
        return false;
    }
    if (!in_range(params.contrast, calibration_ranges::contrast)) {
        error = L"contrast must be between 0.10 and 3.00";
        return false;
    }
    if (!in_range(params.r_gain, calibration_ranges::gain) ||
        !in_range(params.g_gain, calibration_ranges::gain) ||
        !in_range(params.b_gain, calibration_ranges::gain)) {
        error = L"RGB gains must be between 0.01 and 2.00";
        return false;
    }
    return true;
}

GammaRamp LutGenerator::generate(const CalibrationSettings& params) const {
    GammaRamp ramp{};
    const double inverse_gamma = 1.0 / params.gamma;

    auto transform = [&](double x, double gain) {
        double y = (x - 0.5) * params.contrast + 0.5 + params.brightness;
        y = clamp01(y);
        y = std::pow(y, inverse_gamma);
        return clamp01(y * gain);
    };

    for (std::size_t i = 0; i < kRampSize; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(kRampSize - 1);
        ramp.channel[0][i] = to_u16(transform(x, params.r_gain));
        ramp.channel[1][i] = to_u16(transform(x, params.g_gain));
        ramp.channel[2][i] = to_u16(transform(x, params.b_gain));
    }
    return ramp;
}

bool validate_params(const GammaParams& params, std::wstring& error) {
    return LutGenerator{}.validate(params, error);
}

GammaRamp build_ramp(const GammaParams& params) {
    return LutGenerator{}.generate(params);
}

}  // namespace gamma_changer
