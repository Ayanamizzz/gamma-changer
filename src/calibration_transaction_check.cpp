#include "calibration_controller.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

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
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) throw std::runtime_error("could not create the transaction-check temp directory");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

class FakeRampBackend final : public DisplayRampBackend {
public:
    GammaRamp initial = build_ramp(default_params());
    bool fail_read = false;
    int fail_write_number = 0;
    int write_count = 0;

    bool read(const DisplayInfo& display, GammaRamp& ramp, std::wstring& error) override {
        if (fail_read) {
            error = L"injected read failure";
            return false;
        }
        const auto found = ramps_.find(display_storage_id(display));
        ramp = found == ramps_.end() ? initial : found->second;
        return true;
    }

    bool write(const DisplayInfo& display, const GammaRamp& ramp,
               std::wstring& error) override {
        ++write_count;
        if (fail_write_number == write_count) {
            error = L"injected write failure";
            return false;
        }
        ramps_[display_storage_id(display)] = ramp;
        return true;
    }

    GammaRamp ramp_for(const DisplayInfo& display) const {
        const auto found = ramps_.find(display_storage_id(display));
        return found == ramps_.end() ? initial : found->second;
    }

    void set_ramp(const DisplayInfo& display, const GammaRamp& ramp) {
        ramps_[display_storage_id(display)] = ramp;
    }

private:
    std::unordered_map<std::wstring, GammaRamp> ramps_;
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

    {
        DisplayInfo second_display = display;
        second_display.device_name = L"\\\\.\\TESTDISPLAY2";
        second_display.stable_id = L"stable-test-display-2";
        std::wstring preview_error;
        const CalibrationSettings preview_settings{1.10, 0.05, 1.05, 1.0, 1.0, 1.0};
        ok &= expect(controller.preview(display, preview_settings, preview_error),
                     L"preview fixture must activate on the first display");
        const int writes_before_wrong_display = backend.write_count;
        const CommitResult wrong_display = controller.commit(second_display, settings);
        ok &= expect(wrong_display.status == CommitStatus::rolled_back &&
                         backend.write_count == writes_before_wrong_display,
                     L"a commit must never cross an active preview onto another display");

        DisplayInfo renumbered_display = display;
        renumbered_display.device_name = L"\\\\.\\RENAMEDDISPLAY";
        ok &= expect(controller.cancel_preview(renumbered_display, preview_error),
                     L"preview cancellation must follow the stable display identity");
    }

    FakeRampBackend write_failure;
    write_failure.fail_write_number = 1;
    ProfileStore second_store(temporary.path / L"write-failure");
    CalibrationController second_controller(second_store, write_failure);
    const GammaRamp original = write_failure.ramp_for(display);
    const CommitResult failed = second_controller.commit(display, settings);
    ok &= expect(failed.status == CommitStatus::rolled_back,
                 L"display write failure must report rollback-safe failure");
    ok &= expect(write_failure.ramp_for(display).channel == original.channel,
                  L"write failure must preserve the original ramp");

    {
        FakeRampBackend preview_write_failure;
        preview_write_failure.fail_write_number = 1;
        ProfileStore preview_store(temporary.path / L"preview-write-failure");
        CalibrationController preview_controller(preview_store, preview_write_failure);
        DisplayInfo second_display = display;
        second_display.device_name = L"\\\\.\\TESTDISPLAY2";
        second_display.stable_id = L"stable-test-display-2";
        std::wstring error;
        ok &= expect(!preview_controller.preview(display, settings, error),
                     L"an injected first preview write must fail");
        preview_write_failure.fail_write_number = 0;
        ok &= expect(preview_controller.preview(second_display, settings, error),
                     L"a failed first preview must not retain ownership of the old display");
        ok &= expect(preview_controller.cancel_preview(second_display, error),
                     L"the later successful preview must remain cancellable");
    }

    {
        FakeRampBackend unplug_backend;
        ProfileStore unplug_store(temporary.path / L"preview-unplug");
        CalibrationController unplug_controller(unplug_store, unplug_backend);
        DisplayInfo second_display = display;
        second_display.device_name = L"\\\\.\\TESTDISPLAY2";
        second_display.stable_id = L"stable-test-display-2";
        std::wstring error;
        ok &= expect(unplug_controller.preview(display, settings, error),
                     L"offline-preview fixture must activate");
        unplug_controller.abandon_preview_for_offline_display(display);
        ok &= expect(unplug_controller.preview(second_display, settings, error),
                     L"abandoning an offline preview must release display ownership");
        ok &= expect(unplug_controller.cancel_preview(second_display, error),
                     L"the replacement display preview must remain cancellable");
    }

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
    std::filesystem::remove(temporary.path / L"save-failure" /
                                L"stable-test-display.profile",
                            ignored);
    std::filesystem::create_directories(temporary.path / L"save-failure" /
                                        L"stable-test-display.profile");
    const CommitResult cannot_save = fourth_controller.commit(display, settings);
    ok &= expect(cannot_save.status == CommitStatus::rolled_back,
                 L"an atomic settings-save failure with a restored ramp must be rolled back");
    ok &= expect(rollback_failure.ramp_for(display).channel ==
                     build_ramp(default_params()).channel,
                 L"settings persistence failure must restore the previous ramp");

    FakeRampBackend rollback_write_failure;
    ProfileStore fifth_store(temporary.path / L"rollback-write-failure");
    CalibrationController fifth_controller(fifth_store, rollback_write_failure);
    const CommitResult second_baseline = fifth_controller.commit(display, default_params());
    ok &= expect(second_baseline.status == CommitStatus::success,
                 L"rollback failure test must establish a baseline transaction");
    std::filesystem::remove(temporary.path / L"rollback-write-failure" /
                                L"stable-test-display.profile",
                            ignored);
    std::filesystem::create_directories(temporary.path / L"rollback-write-failure" /
                                        L"stable-test-display.profile");
    rollback_write_failure.fail_write_number = rollback_write_failure.write_count + 2;
    const CommitResult rollback_failed = fifth_controller.commit(display, settings);
    ok &= expect(rollback_failed.status == CommitStatus::rollback_failed,
                 L"a failed ramp rollback must be reported distinctly");

    {
        ProfileStore identity_store(temporary.path / L"identity");
        FakeRampBackend identity_backend;
        CalibrationController identity_controller(identity_store, identity_backend);
        ok &= expect(!identity_controller.has_saved_settings(display),
                     L"an unconfigured display must not be treated as configured");
        std::wstring error;
        ok &= expect(identity_store.save_params(display.stable_id, settings, error),
                     L"identity fixture must save stable-id settings");
        ok &= expect(identity_controller.has_saved_settings(display),
                     L"stable-id settings must be recognized");
    }

    {
        ProfileStore identity_store(temporary.path / L"identity-upgrade");
        FakeRampBackend identity_backend;
        CalibrationController identity_controller(identity_store, identity_backend);
        std::wstring error;
        DisplayInfo device_only = display;
        device_only.stable_id.clear();
        const GammaRamp legacy_base = build_ramp(settings);
        ok &= expect(identity_store.save_params(device_only.device_name, settings, error) &&
                         identity_store.save_base_ramp(device_only.device_name, legacy_base,
                                                       error),
                     L"identity-upgrade fixture must save device-name state");
        ok &= expect(identity_controller.migrate_display_identity(device_only, display, error),
                     L"device-name state must migrate when a stable id appears");
        CalibrationSettings migrated_settings{};
        GammaRamp migrated_base{};
        ok &= expect(identity_store.try_load_params(display.stable_id, migrated_settings) &&
                         settings_equal(migrated_settings, settings) &&
                         identity_store.load_base_ramp(display.stable_id, migrated_base) &&
                         migrated_base.channel == legacy_base.channel,
                     L"stable-id migration must preserve params and the exact base ramp");
    }

    {
        ProfileStore reapply_store(temporary.path / L"reapply-base");
        FakeRampBackend reapply_backend;
        reapply_backend.set_ramp(display, build_ramp(settings));
        const GammaRamp ramp_before_reapply = reapply_backend.ramp_for(display);
        CalibrationController reapply_controller(reapply_store, reapply_backend);
        std::wstring error;
        ok &= expect(reapply_controller.reapply_committed(display, default_params(), error),
                     L"reapply must capture a missing base ramp before writing");
        GammaRamp captured{};
        ok &= expect(reapply_store.load_base_ramp(display.stable_id, captured) &&
                          captured.channel == ramp_before_reapply.channel,
                      L"reapply must preserve the pre-existing display ramp for restore");

        std::filesystem::remove_all(temporary.path / L"reapply-base" / L"ramps", ignored);
        const GammaRamp replacement_base = build_ramp(
            CalibrationSettings{1.40, 0.05, 1.05, 1.0, 1.0, 1.0});
        reapply_backend.set_ramp(display, replacement_base);
        const bool recovered =
            reapply_controller.reapply_committed(display, settings, error);
        if (!recovered) std::wcerr << L"INFO: base-ramp recapture failed: " << error << L'\n';
        ok &= expect(recovered,
                     L"reapply must recover if an externally deleted base ramp disappears");
        captured = {};
        ok &= expect(reapply_store.load_base_ramp(display.stable_id, captured) &&
                          captured.channel == replacement_base.channel,
                      L"an externally deleted base ramp must be captured again");
    }


    {
        const auto restore_failure_root = temporary.path / L"restore-save-failure";
        ProfileStore restore_store(restore_failure_root);
        FakeRampBackend restore_backend;
        CalibrationController restore_controller(restore_store, restore_backend);
        const GammaRamp original_ramp = build_ramp(default_params());
        const GammaRamp adjusted_ramp = build_ramp(settings);
        std::wstring error;
        ok &= expect(restore_store.save_base_ramp(display.stable_id, original_ramp, error),
                     L"restore rollback fixture must save its base ramp");
        restore_backend.set_ramp(display, adjusted_ramp);
        std::filesystem::create_directories(
            restore_failure_root / L"stable-test-display.profile");
        ok &= expect(!restore_controller.restore_original(display, error),
                     L"restore must report a settings persistence failure");
        ok &= expect(restore_backend.ramp_for(display).channel == adjusted_ramp.channel,
                     L"restore persistence failure must put the exact previous ramp back");
    }

    {
        ProfileStore legacy_store(temporary.path / L"legacy-restore");
        FakeRampBackend legacy_backend;
        CalibrationController legacy_controller(legacy_store, legacy_backend);
        const GammaRamp legacy_ramp = build_ramp(settings);
        std::wstring error;
        ok &= expect(legacy_store.save_base_ramp(display.device_name, legacy_ramp, error),
                     L"legacy restore fixture must save a device-name ramp");
        ok &= expect(legacy_controller.has_saved_settings(display) == false,
                     L"legacy ramp alone must not imply saved calibration settings");
        ok &= expect(legacy_controller.restore_original(display, error),
                     L"restore must fall back to the legacy device-name ramp");
        ok &= expect(legacy_backend.ramp_for(display).channel == legacy_ramp.channel,
                     L"the legacy ramp must be written back to the display");
        GammaRamp migrated{};
        ok &= expect(legacy_store.load_base_ramp(display.stable_id, migrated) &&
                         migrated.channel == legacy_ramp.channel,
                     L"the legacy ramp must be migrated to the stable identity");

        ProfileStore empty_store(temporary.path / L"empty-restore");
        FakeRampBackend empty_backend;
        CalibrationController empty_controller(empty_store, empty_backend);
        const int writes_before = empty_backend.write_count;
        ok &= expect(!empty_controller.restore_original(display, error),
                     L"restore must refuse a display without any saved base ramp");
        ok &= expect(empty_backend.write_count == writes_before,
                     L"a refused restore must not touch the display ramp");
    }

    if (!ok) return 1;
    std::wcout << L"calibration transaction checks passed\n";
    return 0;
}
