// CalibrationSession: the editor's settings live in the session, not in the
// controls. These checks pin the transitions the window relies on.
#include "calibration_session.h"
#include "gamma_lut.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace gamma_changer;

namespace {

bool expect(bool condition, const wchar_t* message) {
    if (condition) return true;
    std::wcerr << L"FAIL: " << message << L'\n';
    return false;
}

bool exactly(double value, double expected) {
    return std::abs(value - expected) < 1e-12;
}

}  // namespace

int wmain() {
    bool ok = true;

    // Quantization matches what a 0.01-step slider can show.
    {
        const CalibrationSettings q =
            quantize_settings({1.15, -0.29, 1.104, 0.996, 2.5, -1.0});
        ok &= expect(exactly(q.gamma, 1.15), L"1.15 must survive quantization (rounding, not truncation)");
        ok &= expect(exactly(q.brightness, -0.29), L"-0.29 must survive quantization");
        ok &= expect(exactly(q.contrast, 1.10), L"1.104 must round to 1.10");
        ok &= expect(exactly(q.r_gain, 1.00), L"0.996 must round to 1.00");
        ok &= expect(exactly(q.g_gain, calibration_ranges::gain.maximum), L"gain above range must clamp");
        ok &= expect(exactly(q.b_gain, calibration_ranges::gain.minimum), L"gain below range must clamp");

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const CalibrationSettings bad = quantize_settings({nan, nan, 99.0, nan, nan, nan});
        ok &= expect(exactly(bad.gamma, calibration_ranges::gamma.default_value),
                     L"a non-finite value must fall back to the neutral value");
        ok &= expect(exactly(bad.contrast, calibration_ranges::contrast.maximum),
                     L"contrast above range must clamp");
        ok &= expect(settings_equal(quantize_settings(default_params()), default_params()),
                     L"neutral settings are already on the grid");
    }

    // Loading saved settings: the editor shows them and nothing is pending.
    {
        CalibrationSession session;
        session.edit({2.0, 0, 1, 1, 1, 1});
        session.load({1.153, 0, 1, 1, 1, 1});
        ok &= expect(exactly(session.working.gamma, 1.15), L"load must quantize what the editor shows");
        ok &= expect(settings_equal(session.committed_settings, session.working),
                     L"after load, committed equals working");
        ok &= expect(!session.dirty, L"after load, nothing is pending");
    }

    // Editing: dirty tracks the difference from the saved settings.
    {
        CalibrationSession session;
        session.load({1.15, 0, 1, 1, 1, 1});
        ok &= expect(session.edit({1.30, 0, 1, 1, 1, 1}), L"a real change must report dirty");
        ok &= expect(session.dirty && exactly(session.working.gamma, 1.30), L"edit must update working");
        ok &= expect(exactly(session.committed_settings.gamma, 1.15), L"edit must not touch committed");
        ok &= expect(!session.edit({1.15, 0, 1, 1, 1, 1}), L"editing back to the saved value is clean");
        ok &= expect(!session.dirty, L"dirty must clear when the edit returns to the saved value");
        ok &= expect(!session.edit({1.151, 0, 1, 1, 1, 1}),
                     L"a change below the 0.01 step is not a change");
    }

    // Committing: what is shown becomes what is saved.
    {
        CalibrationSession session;
        session.load(default_params());
        session.edit({1.40, 0.10, 1, 1, 1, 1});
        session.mark_committed(session.working);
        ok &= expect(!session.dirty, L"commit must clear dirty");
        ok &= expect(settings_equal(session.committed_settings, {1.40, 0.10, 1, 1, 1, 1}),
                     L"commit must record the committed settings");
    }

    // show(): the editor displays settings without deciding whether they are
    // pending (used while a use case is mid-transaction).
    {
        CalibrationSession session;
        session.load({1.20, 0, 1, 1, 1, 1});
        session.show(default_params());
        ok &= expect(settings_equal(session.working, default_params()), L"show must update working");
        ok &= expect(!session.dirty && exactly(session.committed_settings.gamma, 1.20),
                     L"show must leave dirty and committed alone");
    }

    if (!ok) return 1;
    std::wcout << L"calibration session checks passed\n";
    return 0;
}
