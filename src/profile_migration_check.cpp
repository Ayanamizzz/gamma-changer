#include "profile_manager.h"

#include <windows.h>

#include <filesystem>
#include <iostream>

using namespace gamma_changer;

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path self_test_path;
    if (argc > 1 && std::wstring(argv[1]) == L"--self-test") {
        wchar_t temporary[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temporary);
        self_test_path = std::filesystem::path(temporary) /
                         (L"GammaChangerMigrationCheck-" +
                          std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(self_test_path, ignored);
    }
    ProfileStore store = !self_test_path.empty() ? ProfileStore(self_test_path)
                         : argc > 1 ? ProfileStore(argv[1]) : ProfileStore();
    ProfileManager manager(store);
    std::wstring error;
    if (!manager.load(error)) {
        std::wcerr << error << L'\n';
        if (!self_test_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(self_test_path, ignored);
        }
        return 1;
    }
    std::wcout << manager.profiles().size() << L'\n';
    for (const auto& profile : manager.profiles()) {
        std::wcout << profile.id << L'\t' << profile.name << L'\t'
                   << (profile.saved ? L"saved" : L"empty") << L'\n';
    }
    if (!self_test_path.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(self_test_path, ignored);
    }
    return 0;
}
