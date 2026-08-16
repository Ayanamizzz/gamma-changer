#include "profile_store.h"

#include "gamma_lut.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace gamma_changer {
namespace {

bool replace_file(const std::filesystem::path& temporary,
                  const std::filesystem::path& target,
                  std::wstring& error) {
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE || !FlushFileBuffers(file)) {
        const DWORD code = GetLastError();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        error = L"cannot flush configuration file (Win32 error " +
                std::to_wstring(code) + L")";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    CloseHandle(file);
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = L"cannot replace configuration file (Win32 error " +
            std::to_wstring(GetLastError()) + L")";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
}

HANDLE store_write_mutex() {
    static HANDLE mutex = CreateMutexW(nullptr, FALSE,
                                       L"Local\\GammaChangerCpp.StoreWrites.v1");
    return mutex;
}

class StoreWriteLock {
public:
    StoreWriteLock() : mutex_(store_write_mutex()) {
        if (mutex_ != nullptr) {
            const DWORD wait = WaitForSingleObject(mutex_, INFINITE);
            locked_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~StoreWriteLock() {
        if (mutex_ != nullptr && locked_) ReleaseMutex(mutex_);
    }
    StoreWriteLock(const StoreWriteLock&) = delete;
    StoreWriteLock& operator=(const StoreWriteLock&) = delete;

private:
    HANDLE mutex_ = nullptr;
    bool locked_ = false;
};

bool file_exceeds(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    return !ec && size > limit;
}

constexpr std::uintmax_t kParamsFileLimit = 64 * 1024;
constexpr std::uintmax_t kPresetsFileLimit = 64 * 1024;
constexpr std::uintmax_t kProfilesFileLimit = 4 * 1024 * 1024;
constexpr std::uintmax_t kRampFileLimit = 4096;
constexpr std::size_t kMaxProfileRows = 1024;
constexpr std::size_t kMaxPresetNameLength = 64;

constexpr std::size_t kMaxFilenameStem = 80;

// FNV-1a 64-bit: stable across standard-library versions and platforms, unlike
// std::hash<std::wstring>, so long display identities keep the same file name
// across toolchain updates.
std::uint64_t stable_name_hash(const std::wstring& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const wchar_t ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::wstring sanitize_filename(std::wstring value) {
    const std::uint64_t hash = stable_name_hash(value);
    for (auto& ch : value) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    // Very long display identities (especially monitor device paths) can exceed
    // MAX_PATH after joining with the data root. Keep legacy short names intact
    // for migration compatibility, but fold long names into a bounded hash stem.
    if (value.size() > kMaxFilenameStem) {
        value.resize(kMaxFilenameStem);
        value += L"-" + std::to_wstring(hash);
    }
    return value;
}

// Compatibility fallback for long display identities written by the interim
// std::hash-based implementation. Load paths try this name when the stable FNV
// name does not exist; new saves always use sanitize_filename() above.
std::wstring legacy_hashed_filename(std::wstring value) {
    const std::size_t hash = std::hash<std::wstring>{}(value);
    for (auto& ch : value) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    if (value.size() > kMaxFilenameStem) {
        value.resize(kMaxFilenameStem);
        value += L"-" + std::to_wstring(hash);
    }
    return value;
}

// Returns the base data directory only; ProfileStore appends the application
// folder so every branch produces the same layout.
std::filesystem::path local_app_data() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::filesystem::path(buffer);
    const DWORD temp_length = GetTempPathW(MAX_PATH, buffer);
    if (temp_length > 0 && temp_length < MAX_PATH) return std::filesystem::path(buffer);
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) return cwd;
    return std::filesystem::path{};
}

}  // namespace

ProfileStore::ProfileStore() : ProfileStore(local_app_data() / L"GammaChangerCpp") {}

ProfileStore::ProfileStore(std::filesystem::path root) : root_(std::move(root)) {
    std::error_code ec;
    std::filesystem::create_directories(root_ / L"ramps", ec);
}

std::filesystem::path ProfileStore::params_path(const std::wstring& display_id) const {
    return root_ / (sanitize_filename(display_id) + L".profile");
}

std::filesystem::path ProfileStore::ramp_path(const std::wstring& display_id) const {
    return root_ / L"ramps" / (sanitize_filename(display_id) + L".ramp");
}

std::filesystem::path ProfileStore::profiles_path() const {
    return root_ / L"profiles.v1";
}

std::filesystem::path presets_path(const std::filesystem::path& root) {
    return root / L"presets.profile";
}

GammaParams ProfileStore::load_params(const std::wstring& display_id) const {
    GammaParams params = default_params();
    try_load_params(display_id, params);
    return params;
}

bool ProfileStore::try_load_params(const std::wstring& display_id, GammaParams& params) const {
    params = default_params();
    const auto primary = params_path(display_id);
    if (file_exceeds(primary, kParamsFileLimit)) return false;
    std::wifstream input(primary);
    if (!input) {
        const auto legacy = root_ / (legacy_hashed_filename(display_id) + L".profile");
        if (legacy == primary) return false;
        input.open(legacy);
        if (!input) return false;
    }

    std::wstring key;
    double value = 0.0;
    unsigned fields = 0;
    while (input >> key) {
        if (!(input >> value)) {
            params = default_params();
            return false;
        }
        unsigned field = 0;
        if (key == L"gamma") { field = 1u << 0; params.gamma = value; }
        else if (key == L"brightness") { field = 1u << 1; params.brightness = value; }
        else if (key == L"contrast") { field = 1u << 2; params.contrast = value; }
        else if (key == L"r_gain") { field = 1u << 3; params.r_gain = value; }
        else if (key == L"g_gain") { field = 1u << 4; params.g_gain = value; }
        else if (key == L"b_gain") { field = 1u << 5; params.b_gain = value; }
        else {
            params = default_params();
            return false;
        }
        if ((fields & field) != 0) {
            params = default_params();
            return false;
        }
        fields |= field;
    }

    std::wstring error;
    constexpr unsigned kAllFields = (1u << 6) - 1;
    if (!input.eof() || fields != kAllFields || !validate_params(params, error)) {
        params = default_params();
        return false;
    }
    return true;
}

bool ProfileStore::save_params(const std::wstring& display_id, const GammaParams& params,
                               std::wstring& error) const {
    std::wstring validation_error;
    if (!validate_params(params, validation_error)) {
        error = validation_error;
        return false;
    }

    StoreWriteLock lock;
    const auto target = params_path(display_id);
    const auto temporary = target.wstring() + L".tmp";
    std::wofstream output(temporary, std::ios::trunc);
    if (!output) {
        error = L"cannot write profile file";
        return false;
    }
    output << std::setprecision(17)
           << L"gamma " << params.gamma << L'\n'
           << L"brightness " << params.brightness << L'\n'
           << L"contrast " << params.contrast << L'\n'
           << L"r_gain " << params.r_gain << L'\n'
           << L"g_gain " << params.g_gain << L'\n'
           << L"b_gain " << params.b_gain << L'\n';
    output.close();
    if (!output) {
        error = L"failed while writing profile file";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return replace_file(temporary, target, error);
}

std::array<PresetSlot, kPresetCount> ProfileStore::load_presets() const {
    std::array<PresetSlot, kPresetCount> presets{};
    if (file_exceeds(presets_path(root_), kPresetsFileLimit)) return presets;
    std::wifstream input(presets_path(root_));
    if (!input) return presets;

    std::array<bool, kPresetCount> seen{};
    std::wstring line;
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.find_first_not_of(L" \t\r") == std::wstring::npos) continue;
        if (rows >= kPresetCount) return {};
        std::wistringstream row(line);
        std::size_t index = 0;
        int occupied = 0;
        PresetSlot slot;
        if (!(row >> index >> occupied >> std::quoted(slot.name)
                  >> slot.params.gamma >> slot.params.brightness >> slot.params.contrast
                  >> slot.params.r_gain >> slot.params.g_gain >> slot.params.b_gain)) {
            return {};
        }
        row >> std::ws;
        if (!row.eof() || index >= kPresetCount || seen[index] ||
            (occupied != 0 && occupied != 1)) {
            return {};
        }
        std::wstring validation_error;
        slot.occupied = occupied == 1;
        if (slot.occupied) {
            if (!validate_params(slot.params, validation_error)) return {};
        } else {
            slot.name.clear();
        }
        seen[index] = true;
        presets[index] = slot;
        ++rows;
    }
    return presets;
}

bool ProfileStore::save_presets(const std::array<PresetSlot, kPresetCount>& presets,
                                std::wstring& error) const {
    for (const PresetSlot& slot : presets) {
        if (!slot.occupied) continue;
        std::wstring validation_error;
        if (!validate_params(slot.params, validation_error)) {
            error = L"refusing to save an invalid preset: " + validation_error;
            return false;
        }
        if (slot.name.size() > kMaxPresetNameLength) {
            error = L"refusing to save an overlong preset name";
            return false;
        }
    }
    StoreWriteLock lock;
    const auto target = presets_path(root_);
    const auto temporary = target.wstring() + L".tmp";
    std::wofstream output(temporary, std::ios::trunc);
    if (!output) {
        error = L"cannot write presets file";
        return false;
    }
    output << std::setprecision(17);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const auto& slot = presets[i];
        const auto params = slot.occupied ? slot.params : default_params();
        std::wstring name = slot.name;
        for (auto& ch : name) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
        output << i << L' ' << (slot.occupied ? 1 : 0) << L' ' << std::quoted(name) << L' '
               << params.gamma << L' ' << params.brightness << L' ' << params.contrast << L' '
               << params.r_gain << L' ' << params.g_gain << L' ' << params.b_gain << L'\n';
    }
    output.close();
    if (!output) {
        error = L"failed while writing presets file";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return replace_file(temporary, target, error);
}

ProfileLoadStatus ProfileStore::load_profiles(std::vector<Profile>& profiles) const {
    profiles.clear();
    if (file_exceeds(profiles_path(), kProfilesFileLimit)) {
        return ProfileLoadStatus::corrupt;
    }
    std::wifstream input(profiles_path());
    if (!input) return ProfileLoadStatus::missing;

    std::wstring header;
    std::size_t version = 0;
    if (!(input >> header >> version) || header != L"GammaChangerProfiles") {
        return ProfileLoadStatus::corrupt;
    }
    if (version != 1) return ProfileLoadStatus::unsupported_version;

    std::wstring line;
    std::getline(input, line);
    if (line.find_first_not_of(L" \t\r") != std::wstring::npos) {
        return ProfileLoadStatus::corrupt;
    }
    std::unordered_set<std::wstring> ids;
    while (std::getline(input, line)) {
        if (line.find_first_not_of(L" \t\r") == std::wstring::npos) continue;
        if (profiles.size() >= kMaxProfileRows) {
            profiles.clear();
            return ProfileLoadStatus::corrupt;
        }
        std::wistringstream row(line);
        Profile profile;
        int saved = 0;
        if (!(row >> std::quoted(profile.id) >> std::quoted(profile.name) >> saved
                  >> profile.settings.gamma >> profile.settings.brightness
                  >> profile.settings.contrast >> profile.settings.r_gain
                  >> profile.settings.g_gain >> profile.settings.b_gain)) {
            profiles.clear();
            return ProfileLoadStatus::corrupt;
        }
        row >> std::ws;
        if (!row.eof()) {
            profiles.clear();
            return ProfileLoadStatus::corrupt;
        }
        std::wstring validation_error;
        if (!profile.id.empty() && !profile.name.empty() &&
            validate_params(profile.settings, validation_error) && ids.insert(profile.id).second &&
            (saved == 0 || saved == 1)) {
            profile.saved = saved != 0;
            profiles.push_back(profile);
        } else {
            profiles.clear();
            return ProfileLoadStatus::corrupt;
        }
    }
    if (input.bad() || profiles.empty()) {
        profiles.clear();
        return ProfileLoadStatus::corrupt;
    }
    return ProfileLoadStatus::loaded;
}

bool ProfileStore::save_profiles(const std::vector<Profile>& profiles,
                                 std::wstring& error) const {
    if (profiles.empty()) {
        error = L"refusing to save an empty profile collection";
        return false;
    }
    std::unordered_set<std::wstring> ids;
    for (const auto& profile : profiles) {
        std::wstring validation_error;
        if (profile.id.empty() || profile.name.empty() ||
            !validate_params(profile.settings, validation_error)) {
            error = L"refusing to save an invalid profile";
            return false;
        }
        if (!ids.insert(profile.id).second) {
            error = L"refusing to save duplicate profile ids";
            return false;
        }
    }
    StoreWriteLock lock;
    const auto target = profiles_path();
    const auto temporary = target.wstring() + L".tmp";
    std::wofstream output(temporary, std::ios::trunc);
    if (!output) {
        error = L"cannot write profiles file";
        return false;
    }
    output << L"GammaChangerProfiles 1\n" << std::setprecision(17);
    for (const auto& profile : profiles) {
        std::wstring name = profile.name;
        for (auto& ch : name) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
        output << std::quoted(profile.id) << L' ' << std::quoted(name) << L' '
               << (profile.saved ? 1 : 0) << L' '
               << profile.settings.gamma << L' ' << profile.settings.brightness << L' '
               << profile.settings.contrast << L' ' << profile.settings.r_gain << L' '
               << profile.settings.g_gain << L' ' << profile.settings.b_gain << L'\n';
    }
    output.close();
    if (!output) {
        error = L"failed while writing profiles file";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    return replace_file(temporary, target, error);
}

bool ProfileStore::load_base_ramp(const std::wstring& display_id, GammaRamp& ramp) const {
    const auto primary = ramp_path(display_id);
    if (file_exceeds(primary, kRampFileLimit)) return false;
    std::ifstream input(primary, std::ios::binary);
    if (!input) {
        const auto legacy = root_ / L"ramps" / (legacy_hashed_filename(display_id) + L".ramp");
        if (legacy == primary) return false;
        input.open(legacy, std::ios::binary);
        if (!input) return false;
    }
    input.read(reinterpret_cast<char*>(ramp.channel[0].data()), sizeof(ramp.channel));
    return input.gcount() == static_cast<std::streamsize>(sizeof(ramp.channel)) &&
           input.peek() == std::char_traits<char>::eof();
}

bool ProfileStore::save_base_ramp(const std::wstring& display_id, const GammaRamp& ramp,
                                  std::wstring& error) const {
    StoreWriteLock lock;
    const auto target = ramp_path(display_id);
    const auto temporary = target.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"cannot write base ramp file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(ramp.channel[0].data()), sizeof(ramp.channel));
    output.close();
    if (!output) {
        error = L"failed while writing base ramp file";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return replace_file(temporary, target, error);
}

}  // namespace gamma_changer
