#include "calibration_controller.h"

#include <windows.h>

#include <filesystem>
#include <iostream>

using namespace gamma_changer;

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        wchar_t base[MAX_PATH]{};
        GetTempPathW(MAX_PATH, base);
        path = std::filesystem::path(base) /
               (L"GammaChangerTransactionCheck-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

class FakeRampBackend final : public DisplayRampBackend {
public:
    GammaRamp current = build_ramp(default_params());
    bool fail_read = false;
    int fail_write_number = 0;
    int write_count = 0;

    bool read(const DisplayInfo&, GammaRamp& ramp, std::wstring& error) override {
        if (fail_read) {
            error = L"injected read failure";
            return false;
        }
        ramp = current;
        return true;
    }

    bool write(const DisplayInfo&, const GammaRamp& ramp, std::wstring& error) override {
        ++write_count;
        if (fail_write_number == write_count) {
            error = L"injected write failure";
            return false;
        }
        current = ramp;
        return true;
    }
};

bool expect(bool condition, const wchar_t* message) {
    if (condition) return true;
    std::wcerr << L"FAIL: " << message << L'\n';
    return false;
}

}  // namespace

int wmain() {
    TemporaryDirectory temporary;
    ProfileStore store(temporary.path);
    FakeRampBackend backend;
    CalibrationController controller(store, backend);
    DisplayInfo display;
    display.device_name = L"\\\\.\\TESTDISPLAY";
    display.stable_id = L"stable-test-display";

    const CalibrationSettings settings{1.25, -0.10, 1.15, 1.05, 0.95, 1.10};
    bool ok = true;
    const CommitResult success = controller.commit(display, settings);
    ok &= expect(success.status == CommitStatus::success, L"transaction must commit");
    CalibrationSettings loaded{};
    ok &= expect(store.try_load_params(display.stable_id, loaded) &&
                     settings_equal(loaded, settings),
                 L"successful transaction must persist settings");

    FakeRampBackend write_failure;
    write_failure.fail_write_number = 1;
    ProfileStore second_store(temporary.path / L"write-failure");
    CalibrationController second_controller(second_store, write_failure);
    const GammaRamp original = write_failure.current;
    const CommitResult failed = second_controller.commit(display, settings);
    ok &= expect(failed.status == CommitStatus::rolled_back,
                 L"display write failure must report rollback-safe failure");
    ok &= expect(write_failure.current.channel == original.channel,
                 L"write failure must preserve the original ramp");

    FakeRampBackend read_failure;
    read_failure.fail_read = true;
    ProfileStore third_store(temporary.path / L"read-failure");
    CalibrationController third_controller(third_store, read_failure);
    const CommitResult unreadable = third_controller.commit(display, settings);
    ok &= expect(unreadable.status == CommitStatus::rolled_back,
                 L"unreadable display state must refuse a transaction");

    FakeRampBackend rollback_failure;
    ProfileStore fourth_store(temporary.path / L"save-failure");
    CalibrationController fourth_controller(fourth_store, rollback_failure);
    const CommitResult baseline = fourth_controller.commit(display, default_params());
    ok &= expect(baseline.status == CommitStatus::success,
                 L"save failure test must establish a baseline transaction");
    std::error_code ignored;
    std::filesystem::remove_all(temporary.path / L"save-failure", ignored);
    const CommitResult cannot_save = fourth_controller.commit(display, settings);
    ok &= expect(cannot_save.status == CommitStatus::rolled_back ||
                     cannot_save.status == CommitStatus::rollback_failed,
                 L"settings persistence failure must report a rollback outcome");
    ok &= expect(rollback_failure.current.channel == build_ramp(default_params()).channel,
                 L"settings persistence failure must restore the previous ramp");

    FakeRampBackend rollback_write_failure;
    ProfileStore fifth_store(temporary.path / L"rollback-write-failure");
    CalibrationController fifth_controller(fifth_store, rollback_write_failure);
    const CommitResult second_baseline = fifth_controller.commit(display, default_params());
    ok &= expect(second_baseline.status == CommitStatus::success,
                 L"rollback failure test must establish a baseline transaction");
    std::filesystem::remove_all(temporary.path / L"rollback-write-failure", ignored);
    rollback_write_failure.fail_write_number = rollback_write_failure.write_count + 2;
    const CommitResult rollback_failed = fifth_controller.commit(display, settings);
    ok &= expect(rollback_failed.status == CommitStatus::rollback_failed,
                 L"a failed ramp rollback must be reported distinctly");

    if (!ok) return 1;
    std::wcout << L"calibration transaction checks passed\n";
    return 0;
}
