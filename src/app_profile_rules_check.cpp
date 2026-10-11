#include "app_profile_rules.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

#if defined(_WIN32)
#include "foreground_app_watcher.h"
#include "profile_store.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#endif

using namespace gamma_changer;

namespace {

bool expect(bool condition, const wchar_t* message) {
    if (condition) return true;
    std::wcerr << L"FAIL: " << message << L'\n';
    return false;
}

bool check_rules() {
    bool ok = true;
    AppProfileConfig config;
    set_app_profile_rule(config, {L"C:\\Apps\\Code.exe", L"display-a", L"coding", true});
    ok &= expect(!find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-a"),
                 L"automatic switching must start disabled");
    config.enabled = true;
    const auto* found = find_app_profile_rule(config, L"c:/APPS/code.EXE", L"display-a");
    ok &= expect(found && found->profile_id == L"coding",
                 L"matching must ignore path case and slash direction");
    ok &= expect(!find_app_profile_rule(config, L"D:\\Apps\\Code.exe", L"display-a"),
                 L"a matching filename in another directory must not match");
    ok &= expect(!find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-b"),
                 L"a rule must affect only its bound display");
    ok &= expect(!find_app_profile_rule(config, L"", L"display-a") &&
                     !find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L""),
                 L"unknown application or display identities must not match");

    const std::wstring first_id = config.rules.front().id;
    set_app_profile_rule(config, {L"c:/apps/CODE.exe", L"display-a", L"new-coding", true});
    found = find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-a");
    ok &= expect(config.rules.size() == 2 && found && found->profile_id == L"coding" &&
                     config.rules[0].id == L"rule-1" && config.rules[1].id == L"rule-2",
                 L"the same application and display must allow distinct filters with ordered defaults");
    AppProfileRule updated = config.rules[1];
    updated.name = L"Reading filter";
    set_app_profile_rule(config, updated);
    const auto* by_id = find_app_profile_rule_by_id(config, updated.id);
    ok &= expect(config.rules.size() == 2 && by_id && by_id->name == L"Reading filter" &&
                     config.rules[0].id == first_id,
                 L"editing by stable rule id must update in place without duplicating or reprioritizing");

    std::swap(config.rules[0], config.rules[1]);
    found = find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-a");
    ok &= expect(found && found->profile_id == L"new-coding",
                 L"moving a filter first must select it as the application's default");
    updated = config.rules.front();
    updated.enabled = false;
    set_app_profile_rule(config, updated);
    found = find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-a");
    ok &= expect(found && found->profile_id == L"coding" && config.rules.size() == 2,
                 L"disabling the selected filter must use the next enabled filter without duplicating rules");
    updated.enabled = true;
    set_app_profile_rule(config, updated);
    found = find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-a");
    ok &= expect(found && found->profile_id == L"new-coding",
                 L"reenabling a filter must preserve its selected priority");
    std::swap(config.rules[0], config.rules[1]);

    set_app_profile_rule(config, {L"C:\\Apps\\Code.exe", L"display-b", L"coding", true});
    ok &= expect(config.rules.size() == 3 &&
                     find_app_profile_rule(config, L"C:\\Apps\\Code.exe", L"display-b"),
                 L"the same executable must support distinct per-display rules");

    const std::wstring unicode_path = L"C:\\\u5e94\u7528\\Editor.exe";
    set_app_profile_rule(config, {unicode_path, L"display-a", L"coding", true});
    found = find_app_profile_rule(config, L"c:/\u5e94\u7528/editor.EXE", L"display-a");
    ok &= expect(found && found->executable_path == unicode_path,
                 L"normalization must preserve Unicode paths and stored spelling");
    ok &= expect(remove_app_profile_rules_for_profile(config, L"coding") == 3 &&
                     config.rules.size() == 1,
                 L"deleting a profile must remove all of its display and application bindings");
    ok &= expect(remove_app_profile_rules_for_profile(config, L"missing-profile") == 0,
                 L"removing an unreferenced profile must leave the rule collection unchanged");
    const std::wstring custom_path = L"D:\\My Tools\\Made Up App.exe";
    set_app_profile_rule(config, {custom_path, L"display-a", L"custom-filter", true,
                                 L"", L"My own software"});
    found = find_app_profile_rule(config, L"d:/my tools/made up app.EXE", L"display-a");
    ok &= expect(found && found->profile_id == L"custom-filter" && found->executable_path == custom_path,
                 L"arbitrary user-selected applications must match without any built-in application list");
    config.enabled = false;
    ok &= expect(!find_app_profile_rule(config, custom_path, L"display-a") &&
                     !find_app_profile_rule_by_id(config, L"missing-rule"),
                 L"global disabling must suppress matching and unknown rule ids must not resolve");
    return ok;
}

bool check_selection() {
    AppProfileConfig config{false, {
        {L"C:\\Tools\\Other.exe", L"screen-a", L"other-a", true, L"other-a"},
        {L"C:\\Tools\\My App.exe", L"screen-a", L"old", true, L"old"},
        {L"C:\\Tools\\My App.exe", L"screen-b", L"second-screen", true, L"second-screen"},
        {L"C:\\Tools\\Other.exe", L"screen-b", L"other-b", true, L"other-b"},
        {L"c:/tools/MY APP.exe", L"screen-a", L"chosen", false, L"chosen"},
        {L"C:\\Tools\\Other.exe", L"screen-a", L"other-alternative", true, L"other-alternative"},
    }};
    std::vector<std::wstring> unchanged_order;
    for (const auto& rule : config.rules) if (rule.id != L"chosen") unchanged_order.push_back(rule.id);
    bool ok = expect(select_app_profile_rule(config, L"chosen") && config.enabled,
                     L"selecting a filter must enable automation");
    const auto* selected = find_app_profile_rule(config, L"C:\\Tools\\My App.exe", L"screen-a");
    ok &= expect(selected && selected->id == L"chosen" && selected->enabled,
                 L"Use for app must reenable a disabled filter and select it for the matching display");
    const auto* second_screen = find_app_profile_rule(config, L"C:\\Tools\\My App.exe", L"screen-b");
    const auto* other_a = find_app_profile_rule(config, L"C:\\Tools\\Other.exe", L"screen-a");
    const auto* other_b = find_app_profile_rule(config, L"C:\\Tools\\Other.exe", L"screen-b");
    std::vector<std::wstring> remaining_order;
    for (const auto& rule : config.rules) if (rule.id != L"chosen") remaining_order.push_back(rule.id);
    ok &= expect(second_screen && second_screen->id == L"second-screen" &&
                     other_a && other_a->id == L"other-a" && other_b && other_b->id == L"other-b" &&
                     remaining_order == unchanged_order,
                 L"selecting one app filter must preserve other software and display defaults and relative order");
    ok &= expect(!select_app_profile_rule(config, L"missing-rule") && config.rules.size() == 6,
                 L"selecting an unknown filter must leave the rule collection unchanged");
    return ok;
}

bool check_binding() {
    AppProfileConfig config{false, {
        {L"C:\\Apps\\Browser.exe", L"screen-a", L"old", true, L"browser-old"},
        {L"C:\\Games\\Game.exe", L"screen-a", L"old-game", true, L"game-old"},
        {L"C:\\Apps\\Browser.exe", L"screen-b", L"other-screen", true, L"browser-other"},
        {L"c:/apps/BROWSER.exe", L"screen-a", L"common", false, L"browser-common"},
    }};
    const std::wstring browser = bind_app_profile_rule(config,
        {L"C:\\Apps\\Browser.exe", L"screen-a", L"common", true, {}, L"Common"});
    const auto* rule = find_app_profile_rule(config, L"C:\\Apps\\Browser.exe", L"screen-a");
    bool ok = expect(config.enabled && browser == L"browser-common" && config.rules.size() == 4 &&
                         rule && rule->profile_id == L"common" && rule->enabled,
                     L"binding an existing browser filter must activate it ahead of old bindings and enable switching");
    ok &= expect(bind_app_profile_rule(config,
                     {L"c:/apps/browser.exe", L"screen-a", L"common", true}) == browser && config.rules.size() == 4,
                 L"repeated binding must reuse the same rule without adding duplicates");
    bind_app_profile_rule(config, {L"C:\\Games\\Game.exe", L"screen-a", L"gaming", true});
    const auto* game = find_app_profile_rule(config, L"C:\\Games\\Game.exe", L"screen-a");
    const auto* browser_again = find_app_profile_rule(config, L"C:\\Apps\\Browser.exe", L"screen-a");
    const auto* other = find_app_profile_rule(config, L"C:\\Apps\\Browser.exe", L"screen-b");
    ok &= expect(game && game->profile_id == L"gaming" && browser_again && browser_again->profile_id == L"common" &&
                     other && other->profile_id == L"other-screen",
                 L"browser/game/browser matching must follow each chosen default while preserving other display bindings");
    AppProfileRule edited = *browser_again;
    edited.profile_id = L"new-common";
    bind_app_profile_rule(config, edited);
    rule = find_app_profile_rule(config, edited.executable_path, edited.display_id);
    ok &= expect(rule && rule->id == browser && rule->profile_id == L"new-common" && config.rules.size() == 5,
                 L"editing an assignment must preserve its id and select the newly chosen profile");
    return ok;
}

bool check_fallback_defaults() {
    AppProfileConfig config;
    bool ok = expect(!find_app_fallback_profile(config, L"screen-a"),
                     L"new configurations must keep desktop settings for unbound apps");
    ok &= expect(set_app_fallback_profile(config, L"screen-a", L"common") &&
                     !set_app_fallback_profile(config, L"screen-a", L"common") && config.fallback_profiles.size() == 1,
                 L"choosing a fallback must keep one stable preference per display");
    set_app_fallback_profile(config, L"screen-b", L"second-common");
    ok &= expect(find_app_fallback_profile(config, L"screen-a")->profile_id == L"common" &&
                     find_app_fallback_profile(config, L"screen-b")->profile_id == L"second-common",
                 L"fallback preferences must remain editable when switching is off and independent across displays");
    ok &= expect(set_app_fallback_profile(config, L"screen-a", L"") &&
                     !find_app_fallback_profile(config, L"screen-a") && config.fallback_profiles.size() == 1,
                 L"choosing desktop settings must clear only the selected display's fallback");
    ok &= expect(remove_app_profile_rules_for_profile(config, L"second-common") == 1 &&
                     config.fallback_profiles.empty(),
                 L"deleting a fallback-only profile must remove and persist its preference");
    return ok;
}

bool check_filter_settings() {
    const CalibrationSettings baseline{1.0, 0.0, 1.0, 1.0, 1.0, 1.0};
    const CalibrationSettings edited{1.3, -0.12, 1.2, 1.1, 0.9, 0.8};
    const CalibrationSettings second_edit{1.6, 0.1, 1.1, 0.8, 1.2, 1.3};
    const auto same_settings = [](const CalibrationSettings& left, const CalibrationSettings& right) {
        return left.gamma == right.gamma && left.brightness == right.brightness &&
               left.contrast == right.contrast && left.r_gain == right.r_gain &&
               left.g_gain == right.g_gain && left.b_gain == right.b_gain;
    };
    std::vector<Profile> profiles{
        {L"library-template", L"Soft template", baseline, true},
        {L"app-filter-1", L"Existing application filter", baseline, true},
    };
    AppProfileConfig config;
    set_app_profile_rule(config, {L"C:\\Tools\\Made Up App.exe", L"screen-a", L"library-template",
                                 true, L"", L"Night"});
    const std::wstring first_id = config.rules.back().id;
    set_app_profile_rule(config, {L"C:\\Tools\\Another App.exe", L"screen-a", L"library-template",
                                 true, L"", L"Default"});
    const std::wstring other_id = config.rules.back().id;
    bool ok = expect(update_app_filter_settings(config, profiles, first_id, edited),
                     L"editing a valid application filter must succeed");
    const auto* first = find_app_profile_rule_by_id(config, first_id);
    const auto* other = find_app_profile_rule_by_id(config, other_id);
    ok &= expect(first && first->profile_id == L"app-filter-2" &&
                     other && other->profile_id == L"library-template" && profiles.size() == 3 &&
                     same_settings(profiles[0].settings, baseline) &&
                     same_settings(profiles[1].settings, baseline) &&
                     same_settings(profiles[2].settings, edited) &&
                     profiles[2].name == L"Made Up App.exe - Night" && profiles[2].saved,
                 L"editing app A must clone its named filter without changing app B, the shared template or existing ids");
    ok &= expect(update_app_filter_settings(config, profiles, first_id, second_edit) &&
                     profiles.size() == 3 && config.rules[0].profile_id == L"app-filter-2" &&
                     same_settings(profiles[2].settings, second_edit) &&
                     same_settings(profiles[0].settings, baseline),
                 L"editing an exclusive app-owned filter again must reuse it without an extra clone");

    set_app_profile_rule(config, {L"D:\\Third App.exe", L"screen-b", L"app-filter-2", false});
    const std::wstring shared_id = config.rules.back().id;
    ok &= expect(update_app_filter_settings(config, profiles, shared_id, edited) && profiles.size() == 4 &&
                     config.rules[0].profile_id == L"app-filter-2" &&
                     config.rules.back().profile_id == L"app-filter-3" &&
                     same_settings(profiles[2].settings, second_edit) &&
                     same_settings(profiles[3].settings, edited) &&
                     profiles[3].name == L"Third App.exe - Made Up App.exe - Night",
                 L"editing a shared app-owned filter must clone it and use its template name when the rule has no name");
    ok &= expect(!update_app_filter_settings(config, profiles, L"missing-rule", edited) && profiles.size() == 4,
                 L"a missing application rule must not create a filter");
    set_app_profile_rule(config, {L"C:\\Missing Profile.exe", L"screen-a", L"removed-template", true});
    ok &= expect(!update_app_filter_settings(config, profiles, config.rules.back().id, edited) &&
                     profiles.size() == 4 && config.rules.back().profile_id == L"removed-template",
                 L"a missing template must leave the rule and profile collection unchanged");
    set_app_fallback_profile(config, L"screen-a", L"app-filter-2");
    ok &= expect(update_app_filter_settings(config, profiles, first_id, edited) && profiles.size() == 5 &&
                     find_app_profile_rule_by_id(config, first_id)->profile_id == L"app-filter-4" &&
                     find_app_fallback_profile(config, L"screen-a")->profile_id == L"app-filter-2" &&
                     same_settings(profiles[2].settings, second_edit),
                 L"editing a filter shared with the unassigned-app default must preserve the default's settings");
    return ok;
}

#if defined(_WIN32)

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        wchar_t base[MAX_PATH]{};
        GetTempPathW(MAX_PATH, base);
        path = std::filesystem::path(base) /
               (L"GammaChangerAppRulesCheck-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) throw std::runtime_error("could not create application-rule test directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("could not write application-rule test fixture");
}

bool check_store() {
    TemporaryDirectory temporary;
    ProfileStore store(temporary.path);
    const auto path = temporary.path / L"app_rules.v1";
    bool ok = true;
    AppProfileConfig loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty() && !std::filesystem::exists(path),
                 L"a missing rule file must leave automatic switching disabled without creating it");

    AppProfileConfig config;
    config.enabled = true;
    const std::wstring unicode_path = L"C:\\\u5e94\u7528\\\u7f16\u8f91\u5668.exe";
    const std::wstring unicode_display = L"\u663e\u793a\u5668-\U0001f600";
    set_app_profile_rule(config, {unicode_path, unicode_display, L"profile-\u4e2d\u6587", true,
                                 L"", L"\u8bfb\u4e66\u6ee4\u955c"});
    set_app_profile_rule(config, {unicode_path, unicode_display, L"night-profile", true,
                                 L"", L"Night filter"});
    set_app_profile_rule(config, {L"C:\\Apps\\Game.exe", L"display-b", L"gaming", false});
    std::wstring error;
    ok &= expect(store.save_app_profile_config(config, error),
                 L"application profile rules must save atomically");
    loaded = store.load_app_profile_config();
    ok &= expect(loaded.enabled && loaded.rules.size() == 3 &&
                     loaded.rules[0].executable_path == unicode_path &&
                     loaded.rules[0].display_id == unicode_display &&
                     loaded.rules[0].profile_id == L"profile-\u4e2d\u6587" &&
                     loaded.rules[0].id == L"rule-1" &&
                     loaded.rules[0].name == L"\u8bfb\u4e66\u6ee4\u955c" &&
                     loaded.rules[1].profile_id == L"night-profile" &&
                     loaded.rules[0].enabled && !loaded.rules[2].enabled,
                 L"version 2 must round-trip stable ids, Unicode names and multiple application filters");
    const std::string valid_bytes = read_bytes(path);
    ok &= expect(valid_bytes.find(u8"\u5e94\u7528") != std::string::npos,
                 L"the application rule file must encode Unicode as UTF-8");

    AppProfileConfig duplicate = config;
    duplicate.rules.push_back(config.rules.front());
    error.clear();
    ok &= expect(!store.save_app_profile_config(duplicate, error) && !error.empty() &&
                     read_bytes(path) == valid_bytes,
                 L"duplicate stable rule ids must be rejected without changing the saved file");
    AppProfileConfig invalid = config;
    invalid.rules[0].profile_id.clear();
    ok &= expect(!store.save_app_profile_config(invalid, error) && read_bytes(path) == valid_bytes,
                 L"an incomplete rule must not replace valid configuration");
    invalid = config;
    invalid.rules[0].executable_path += L'\n';
    ok &= expect(!store.save_app_profile_config(invalid, error) && read_bytes(path) == valid_bytes,
                 L"line breaks in rule identities must be rejected");

    std::filesystem::create_directory(temporary.path / L"app_rules.v1.tmp");
    AppProfileConfig changed = config;
    changed.enabled = false;
    error.clear();
    ok &= expect(!store.save_app_profile_config(changed, error) && !error.empty() &&
                     read_bytes(path) == valid_bytes && store.load_app_profile_config().enabled,
                 L"a failed temporary-file write must preserve the previously saved configuration");

    const std::string corrupt = "GammaChangerAppRules 1 1\n\"C:\\\\Apps\\\\Game.exe\" "
                                "\"display-b\" \"gaming\" 1\nbroken row\n";
    write_bytes(path, corrupt);
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty() && read_bytes(path) == corrupt,
                 L"a corrupt file must disable all rules without partial recovery or overwriting it");
    write_bytes(path, "GammaChangerAppRules 4 1\n");
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty(),
                 L"a newer rule format must leave automatic switching disabled");
    write_bytes(path, "GammaChangerAppRules 1 2\n");
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty(),
                 L"an invalid global enabled flag must be rejected");
    write_bytes(path, "GammaChangerAppRules 2 1\n\"rule-1\" \"One\" \"c:/apps/game.exe\" \"display-a\" \"one\" 1\n"
                      "\"rule-1\" \"Two\" \"c:/apps/game.exe\" \"display-a\" \"two\" 1\n");
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty(),
                 L"duplicate stable ids in a file must disable its configuration");

    const std::string legacy = u8"GammaChangerAppRules 1 1\n\"C:/\u5e94\u7528/\u7f16\u8f91\u5668.exe\" "
                              "\"display-a\" \"profile-one\" 1\n"
                              "\"C:/Apps/Game.exe\" \"display-b\" \"profile-two\" 0\n";
    write_bytes(path, legacy);
    loaded = store.load_app_profile_config();
    ok &= expect(loaded.enabled && loaded.rules.size() == 2 &&
                     loaded.rules[0].id == L"rule-1" && loaded.rules[1].id == L"rule-2" &&
                     loaded.rules[0].name.empty() &&
                     loaded.rules[0].executable_path == L"C:/\u5e94\u7528/\u7f16\u8f91\u5668.exe" &&
                     !loaded.rules[1].enabled && read_bytes(path) == legacy,
                 L"version 1 must migrate Unicode rules in memory without rewriting their file");
    ok &= expect(store.save_app_profile_config(loaded, error) &&
                     read_bytes(path).find("GammaChangerAppRules 2 1\n") == 0 &&
                     store.load_app_profile_config().rules[1].id == L"rule-2",
                 L"saving migrated rules must use version 2 and preserve their assigned ids");

    std::swap(config.rules[0], config.rules[1]);
    ok &= expect(store.save_app_profile_config(config, error),
                 L"the application's selected default filter must save");
    loaded = store.load_app_profile_config();
    const auto* selected = find_app_profile_rule(loaded, unicode_path, unicode_display);
    ok &= expect(selected && selected->profile_id == L"night-profile" && selected->id == L"rule-2",
                 L"saved rule ordering must preserve the selected default among one application's filters");

    AppProfileConfig assignments;
    bind_app_profile_rule(assignments, {L"C:\\Apps\\Browser.exe", L"screen-a", L"old-common", true});
    bind_app_profile_rule(assignments, {L"C:\\Games\\Game.exe", L"screen-a", L"gaming", true});
    bind_app_profile_rule(assignments, {L"C:\\Apps\\Browser.exe", L"screen-a", L"common", true});
    set_app_fallback_profile(assignments, L"screen-a", L"common");
    set_app_fallback_profile(assignments, L"\u663e\u793a\u5668", L"\u9ed8\u8ba4\u6ee4\u955c");
    ok &= expect(store.save_app_profile_config(assignments, error), L"assigned defaults must save");
    loaded = store.load_app_profile_config();
    const auto* browser_default = find_app_profile_rule(loaded, L"C:\\Apps\\Browser.exe", L"screen-a");
    const auto* game_default = find_app_profile_rule(loaded, L"C:\\Games\\Game.exe", L"screen-a");
    const auto* fallback = find_app_fallback_profile(loaded, L"screen-a");
    const auto* unicode_fallback = find_app_fallback_profile(loaded, L"\u663e\u793a\u5668");
    ok &= expect(loaded.enabled && browser_default && browser_default->profile_id == L"common" &&
                     game_default && game_default->profile_id == L"gaming" && fallback && fallback->profile_id == L"common" &&
                     unicode_fallback && unicode_fallback->profile_id == L"\u9ed8\u8ba4\u6ee4\u955c",
                 L"version 3 must restore app bindings and Unicode fallback preferences after restart");
    const std::string fallback_bytes = read_bytes(path);
    invalid = assignments;
    invalid.fallback_profiles.push_back(assignments.fallback_profiles.front());
    ok &= expect(!store.save_app_profile_config(invalid, error) && read_bytes(path) == fallback_bytes,
                 L"duplicate fallback displays must not replace the saved configuration");
    write_bytes(path, "GammaChangerAppRules 3 1 2\n\"screen-a\" \"common\"\n");
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.empty() && loaded.fallback_profiles.empty(),
                 L"a truncated fallback section must not load a partial configuration");

    config.enabled = false;
    ok &= expect(store.save_app_profile_config(config, error),
                 L"disabled configurations must preserve their editable rules");
    loaded = store.load_app_profile_config();
    ok &= expect(!loaded.enabled && loaded.rules.size() == 3,
                 L"turning automation off must not discard application bindings");
    ok &= expect(store.save_app_profile_config({}, error) &&
                     store.load_app_profile_config().rules.empty(),
                 L"an empty disabled configuration must save and load");
    return ok;
}

bool check_window_identity() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"GammaChangerIdentityCheck";
    if (!RegisterClassW(&window_class)) return expect(false, L"window fixture must register");
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"Identity fixture", WS_OVERLAPPED,
                                   0, 0, 100, 100, nullptr, nullptr, window_class.hInstance, nullptr);
    bool ok = expect(window != nullptr, L"hidden window fixture must create");
    if (window) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        DWORD error = ERROR_ACCESS_DENIED;
        ok &= expect(normalize_executable_path(foreground_executable_path(window, &error)) ==
                         normalize_executable_path(executable) && error == ERROR_SUCCESS,
                     L"running-app capture must query the actual owning executable and clear stale errors");
        ForegroundAppWatcher watcher;
        ForegroundAppWatcher second;
        ok &= expect(watcher.start(window, WM_APP + 1) && !second.start(window, WM_APP + 1),
                     L"foreground events must have one watcher owner");
        watcher.stop();
        ok &= expect(second.start(window, WM_APP + 1), L"stopping must release the event hook");
        second.stop();
        DestroyWindow(window);
        ok &= expect(foreground_executable_path(window, &error).empty() && error == ERROR_INVALID_WINDOW_HANDLE,
                     L"a vanished application must report a lookup failure");
    }
    const auto make_running_window = [&](const wchar_t* title, DWORD extra_style) {
        return CreateWindowExW(WS_EX_NOACTIVATE | extra_style, window_class.lpszClassName, title,
                               WS_POPUP | WS_VISIBLE, -32000, -32000, 100, 50, nullptr, nullptr,
                               window_class.hInstance, nullptr);
    };
    HWND first = make_running_window(L"Running fixture \u5e94\u7528 A", 0);
    HWND second = make_running_window(L"Running fixture \u5e94\u7528 B", 0);
    HWND tool = make_running_window(L"Tool fixture", WS_EX_TOOLWINDOW);
    ok &= expect(first && second && tool, L"off-screen running-window fixtures must create without activation");
    const auto applications = running_applications(0);
    const auto own = std::count_if(applications.begin(), applications.end(), [](const RunningApplication& application) {
        return application.process_id == GetCurrentProcessId();
    });
    const auto found = std::find_if(applications.begin(), applications.end(), [](const RunningApplication& application) {
        return application.process_id == GetCurrentProcessId();
    });
    ok &= expect(own == 1 && found != applications.end() && found->error == ERROR_SUCCESS &&
                     found->executable_path == foreground_executable_path(first) &&
                     (found->window_title == L"Running fixture \u5e94\u7528 A" || found->window_title == L"Running fixture \u5e94\u7528 B"),
                 L"running apps must preserve Unicode captions, group windows by executable, and ignore tool windows");
    const auto filtered = running_applications();
    ok &= expect(std::none_of(filtered.begin(), filtered.end(), [](const RunningApplication& application) {
        return application.process_id == GetCurrentProcessId();
    }), L"the running-app picker must exclude Gamma Changer's own process");
    if (first) DestroyWindow(first);
    if (second) DestroyWindow(second);
    if (tool) DestroyWindow(tool);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    return ok;
}

#endif

}  // namespace

int main() {
    bool ok = check_rules();
    ok &= check_selection();
    ok &= check_binding();
    ok &= check_fallback_defaults();
    ok &= check_filter_settings();
#if defined(_WIN32)
    ok &= check_store();
    ok &= check_window_identity();
#endif
    if (!ok) return 1;
    std::wcout << L"application profile rule checks passed\n";
    return 0;
}
