#pragma once

#include "app_version.h"
#include "app_profile_rules.h"
#include "app_profile_runtime.h"
#include "foreground_app_watcher.h"
#include "calibration_controller.h"
#include "calibration_session.h"
#include "display_manager.h"
#include "gamma_lut.h"
#include "instance_manager.h"
#include "logger.h"
#include "profile_list_logic.h"
#include "profile_store.h"
#include "profile_manager.h"
#include "startup_manager.h"
#include "resource.h"
#include "ui_rendering.h"
#include "ui_theme.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gamma_changer {

constexpr wchar_t kWindowClass[] = L"GammaChangerCppWindow";
constexpr std::size_t kNoProfile = std::numeric_limits<std::size_t>::max();
constexpr int kProfileIdBase = 1000;
constexpr UINT kPreviewTimer = 1;
constexpr UINT kDisplayRefreshTimer = 2;
constexpr UINT kAutoSaveTimer = 3;
constexpr UINT kRecoveryRetryTimer = 4;
constexpr UINT kForegroundProfileTimer = 5;
constexpr UINT kAutoSaveDelayMs = 700;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kProfileRenameMessage = WM_APP + 2;
constexpr UINT kProfileContextMessage = WM_APP + 3;
constexpr UINT kProfileCommitRenameMessage = WM_APP + 4;
constexpr UINT kUndoProfileSwitchMessage = WM_APP + 5;
constexpr UINT kForegroundProfileMessage = WM_APP + 6;
constexpr UINT kTrayId = 1;
constexpr UINT kTrayShowCommand = 1;
constexpr UINT kTrayExitCommand = 2;
constexpr UINT kTrayFollowAppsCommand = 3;
constexpr UINT kTrayProfileCommandBase = 100;
constexpr int kAppChipIdBase = 2000;

enum ControlId : int {
    kDisplayCombo = 102,
    kRefreshButton = 103,
    kGammaSlider = 104,
    kBrightnessSlider = 105,
    kContrastSlider = 106,
    kRGainSlider = 107,
    kGGainSlider = 108,
    kBGainSlider = 109,
    kGammaValue = 110,
    kBrightnessValue = 111,
    kContrastValue = 112,
    kRGainValue = 113,
    kGGainValue = 114,
    kBGainValue = 115,
    kApplyButton = 116,   // "Retry save": shown only after a failed save
    kResetButton = 117,
    kStatusLabel = 118,
    kPresetSave = 124,
    kDisplayCaption = 127,
    kPresetCaption = 128,
    kMainHeading = 129,   // name of the selected profile
    kMainSubtitle = 130,  // where that profile is used
    kStartupToggle = 133,
    kDisplayStatus = 138,
    kProfileRename = 139,
    kBeforeAfter = 140,
    kAutoSwitchToggle = 141,  // "Follow apps"
    kAutoSwitchStatus = 144,
    kProfileMenu = 146,
    kAppsHeading = 147,
    kAppsNote = 148,
    kAddApp = 149,
    kOtherApps = 150,
    kAppOverflow = 151,
};

enum class StatusTone {
    idle,
    success,
    warning,
    error,
};

// One app that switches to the selected profile on the selected display.
struct AppChip {
    std::wstring path;
    std::wstring label;   // executable file name
    bool enabled = true;  // false: legacy assignment whose rules are all off
    int width = 0;        // device pixels at the current DPI
};

// Everything the Apps card shows for the selected profile. Rebuilt by
// refresh_app_chips(); geometry and painting only read it.
struct AppChipModel {
    std::vector<AppChip> chips;
    bool following = false;    // Follow apps is on
    bool show_other = false;   // the "All other apps" chip is offered
    bool holds_other = false;  // other apps use this profile
    int add_width = 0;
    int other_width = 0;
    int overflow_width = 0;
};

struct GuiState {
    GuiState() = default;
    GuiState(std::filesystem::path data_root, DisplayRampBackend& backend)
        : ramp_backend(backend), store(std::move(data_root)) {}

    HWND window = nullptr;
    HWND display_combo = nullptr;
    HWND refresh_button = nullptr;
    HWND display_caption = nullptr;
    HWND preset_caption = nullptr;
    HWND main_heading = nullptr;
    HWND main_subtitle = nullptr;
    HWND display_status = nullptr;
    HWND startup_button = nullptr;
    HWND gamma_label = nullptr;
    HWND brightness_label = nullptr;
    HWND contrast_label = nullptr;
    HWND r_gain_label = nullptr;
    HWND g_gain_label = nullptr;
    HWND b_gain_label = nullptr;
    HWND gamma_slider = nullptr;
    HWND brightness_slider = nullptr;
    HWND contrast_slider = nullptr;
    HWND r_gain_slider = nullptr;
    HWND g_gain_slider = nullptr;
    HWND b_gain_slider = nullptr;
    HWND gamma_value = nullptr;
    HWND brightness_value = nullptr;
    HWND contrast_value = nullptr;
    HWND r_gain_value = nullptr;
    HWND g_gain_value = nullptr;
    HWND b_gain_value = nullptr;
    HWND apply_button = nullptr;         // "Retry save", hidden unless a save failed
    HWND reset_button = nullptr;
    HWND status = nullptr;
    std::vector<HWND> preset_buttons;
    HWND preset_save = nullptr;
    HWND profile_rename = nullptr;
    HWND before_after_button = nullptr;
    HWND profile_menu_button = nullptr;  // rename / duplicate / delete
    HWND auto_switch_button = nullptr;   // "Follow apps" toggle row
    HWND auto_switch_status = nullptr;
    HWND apps_heading = nullptr;
    HWND apps_note = nullptr;
    HWND add_app_button = nullptr;
    HWND other_apps_button = nullptr;
    HWND app_overflow_button = nullptr;
    std::vector<HWND> app_chip_buttons;  // one per AppChipModel::chips entry
    HFONT normal_font = nullptr;
    HFONT heading_font = nullptr;
    HFONT section_font = nullptr;
    HFONT caption_font = nullptr;
    HFONT small_font = nullptr;
    HBRUSH background_brush = nullptr;
    HBRUSH card_brush = nullptr;
    HBRUSH sidebar_brush = nullptr;
    HDC paint_buffer_dc = nullptr;
    HBITMAP paint_buffer_bitmap = nullptr;
    HGDIOBJ paint_buffer_original = nullptr;
    int paint_buffer_width = 0;
    int paint_buffer_height = 0;
    ui::BackgroundRenderer background;
    bool syncing_controls = false;
    bool live_preview = true;
    bool startup_enabled = false;
    bool startup_launch = false;
    // The real app reads and repairs its "Start with Windows" entry. Tools and
    // tests that reuse the window must leave the user's entry alone: a repair
    // from their executable would register that executable to start instead.
    bool manage_startup_entry = true;
    HWND hovered_profile = nullptr;
    HWND hovered_numeric = nullptr;
    HWND hovered_control = nullptr;  // buttons and chips in the work area
    bool profile_keyboard_focus = false;
    StatusTone status_tone = StatusTone::success;
    std::vector<DisplayInfo> displays;
    int active_display_index = -1;
    std::vector<Profile> profiles;
    std::size_t active_preset = 0;
    std::unordered_map<std::wstring, std::wstring> preferred_profile_ids;
    int profile_scroll_offset = 0;
    int input_text_height = 0;  // single-line text height of normal_font
    std::size_t renaming_preset = kNoProfile;
    CalibrationSession session;
    DisplayRampBackend& ramp_backend = system_display_ramp_backend();
    ProfileStore store;
    CalibrationController controller{store, ramp_backend};
    ProfileManager profile_manager{store};
    bool reapply_after_display_refresh = false;
    int recovery_retry_attempt = 0;
    std::unordered_map<std::wstring, CalibrationSettings> pending_adjustments;
    bool profile_store_available = true;
    AppProfileConfig app_profile_config;
    ForegroundAppWatcher foreground_watcher;
    bool foreground_watcher_running = false;
    bool foreground_reapply_requested = false;
    bool foreground_evaluation_pending = false;
    DWORD foreground_lookup_error = ERROR_SUCCESS;
    std::wstring foreground_application;  // last app in front other than this one
    AppProfileRuntime app_runtime{controller};
    AppChipModel apps;                        // apps of the selected profile
    std::vector<std::wstring> profile_notes;  // sidebar row annotations ("2 apps")
    // Per display: the profile selected before the current one. Lets the first
    // app assignment keep other apps on what the desktop showed before.
    std::unordered_map<std::wstring, std::wstring> previous_profile_ids;
    std::unordered_map<std::wstring, HICON> app_icons;
    // Source of the "Add app" list; tests and the snapshot tool inject samples.
    std::function<std::vector<RunningApplication>()> running_apps;
    // Source of "what is in front"; the snapshot tool injects a sample so the
    // real foreground window never leaks into (or races with) its pictures.
    std::function<ForegroundApp()> foreground_app;
};

// One row of the adjustment panel: an edit box mirroring a slider, its valid
// range, and the CalibrationSettings field it edits. All per-adjustment UI code
// iterates this table instead of repeating six hand-written cases.
struct AdjustmentBinding {
    HWND label;
    HWND edit;
    HWND slider;
    CalibrationRange range;
    double CalibrationSettings::*field;
};
using AdjustmentBindings = std::array<AdjustmentBinding, 6>;
AdjustmentBindings adjustment_bindings(const GuiState& gui);
const AdjustmentBinding* find_adjustment(const AdjustmentBindings& bindings, HWND edit);

// ---------------------------------------------------------------------------
// Geometry: the one place that knows where things are. Layout positions
// controls from it; painting, hit-testing and scrolling read the same rects.
// ---------------------------------------------------------------------------
struct Scale {
    UINT dpi = 96;
    int operator()(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};

struct AdjustmentRowGeometry {
    RECT label{};
    RECT slider{};
    RECT value_box{};   // painted rounded input
    RECT value_edit{};  // the EDIT control, vertically centred inside the box
};

struct UiGeometry {
    Scale scale;
    int width = 0;
    int height = 0;
    int sidebar_width = 0;
    int padding = 0;  // sidebar horizontal padding
    // Sidebar
    RECT profile_list{};  // visible viewport of the profile rows
    int profile_row_height = 0;
    int profile_row_stride = 0;
    int automation_top = 0;  // divider above "Follow apps" / "Start with Windows"
    // Work area
    int main_left = 0;
    int main_right = 0;
    RECT header_title{};
    RECT header_subtitle{};
    RECT compare_button{};
    RECT reset_button{};
    RECT menu_button{};
    RECT editor_card{};   // tone curve + the six adjustments
    RECT curve_plot{};    // square plot inside the editor card
    int group_divider_y = 0;  // hairline between tone and colour rows
    int controls_left = 0;
    int controls_right = 0;
    std::array<AdjustmentRowGeometry, 6> rows{};  // index = adjustment_bindings() order
    RECT apps_card{};
    RECT apps_heading{};
    std::vector<RECT> chip_rects;  // per AppChipModel chip; empty = folded into overflow
    RECT add_app{};
    RECT other_apps{};
    RECT overflow{};
    int hidden_chips = 0;
    int status_top = 0;
};

UiGeometry ui_geometry(const GuiState& gui);
UiGeometry ui_geometry(const GuiState& gui, int width, int height);
RECT profile_row_rect(const UiGeometry& geometry, std::size_t index, int scroll_offset);

// Shared internal functions used across the UI translation units.
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
GuiState* state(HWND window);
GammaParams params_from_sliders(const GuiState& gui);
void set_status(GuiState& gui, const std::wstring& text, StatusTone tone);
void set_display_status(GuiState& gui, const std::wstring& text);
const DisplayInfo* selected_display(const GuiState& gui);
void set_params_to_controls(GuiState& gui, const GammaParams& params);
void refresh_preset_buttons(GuiState& gui);
bool save_and_apply_current(GuiState& gui, bool automatic);
// The single place that turns "this is what the display now shows and what is
// persisted" into session state and the save/compare button state.
void mark_committed(GuiState& gui, const CalibrationSettings& settings);
void show_save_failed(GuiState& gui, const std::wstring& error);

void add_tray_icon(HWND window);
void remove_tray_icon(HWND window);
void show_main_window(HWND window);
bool apply_profile_from_tray(GuiState& gui, std::size_t index);
void show_tray_menu(HWND window);

void invalidate_control_background(GuiState& gui, HWND control, int padding = 2);
void invalidate_preview_curve(GuiState& gui);
void invalidate_status_area(GuiState& gui);
void paint_parent_layer(HWND control, HDC dc, GuiState& gui);
void draw_owner_button(const DRAWITEMSTRUCT& item, const GuiState& gui);
void paint_background(HWND window, GuiState& gui);

HWND make_control(DWORD style, LPCWSTR class_name, LPCWSTR text, HWND parent,
                  int id, int x, int y, int width, int height);
HWND make_control_ex(DWORD ex_style, DWORD style, LPCWSTR class_name, LPCWSTR text,
                     HWND parent, int id, int x, int y, int width, int height);
void ensure_profile_buttons(GuiState& gui);
void ensure_app_chip_buttons(GuiState& gui);
void layout_controls(GuiState& gui, int width, int height);
void layout_controls_for_current_size(GuiState& gui);
void apply_font(HWND control, HFONT font);
void recreate_fonts(GuiState& gui, UINT dpi);

bool cancel_active_preview(GuiState& gui);
void create_controls(GuiState& gui);
void normalize_all_edits(GuiState& gui);
void update_value_labels(GuiState& gui);
void set_adjustment_enabled(GuiState& gui, bool enabled);
void edit_changed(GuiState& gui, const AdjustmentBinding& binding);
void normalize_edit(GuiState& gui, const AdjustmentBinding& binding);
void preview_selected(GuiState& gui);
void mark_changed(GuiState& gui);
void enable_modern_backdrop(HWND window);
std::wstring display_metadata(const DisplayInfo& display);
std::wstring display_label(const DisplayInfo& display, std::size_t index);
int selected_display_index(const GuiState& gui);
void select_display_item(GuiState& gui, int display_index);
bool load_selected_profile(GuiState& gui);
bool persist_profile_preferences(GuiState& gui);
void remember_active_profile_for_display(GuiState& gui, const DisplayInfo& display);
bool apply_preferred_profile_to_display(GuiState& gui, const DisplayInfo& display);
void reset_selected(GuiState& gui);
void refresh_displays(GuiState& gui);
bool reapply_all_committed(GuiState& gui);
void schedule_recovery_retry(GuiState& gui);
void run_recovery_retry(GuiState& gui);
bool flush_before_exit(GuiState& gui);
bool confirm_close_after_save_failure(GuiState& gui);
void initialize_automatic_profiles(GuiState& gui);
void schedule_automatic_profiles(GuiState& gui, bool force = false);
void evaluate_automatic_profiles(GuiState& gui, bool force = false);
void apply_application_profiles(GuiState& gui, const std::wstring& path, bool force = false);
bool restore_automatic_profiles(GuiState& gui);
void toggle_automatic_profiles(GuiState& gui);
bool set_automatic_profiles_enabled(GuiState& gui, bool enabled);
void update_automatic_profile_controls(GuiState& gui);
// Apps of the selected profile (the chips) and the per-row sidebar notes.
void refresh_app_chips(GuiState& gui, bool remeasure = false);
bool assign_app_to_profile(GuiState& gui, std::size_t profile_index, const std::wstring& path);
bool unassign_app(GuiState& gui, const std::wstring& path);
bool use_profile_for_other_apps(GuiState& gui, std::size_t profile_index);
// Selecting another profile must not change what unlisted apps get while
// Follow apps is on: pins the current profile for them if nothing is pinned.
void keep_other_apps_profile(GuiState& gui);
// The "Add app" menu: running apps (last used first), then Browse. Built
// separately from being shown so its contents can be checked without a screen.
constexpr UINT kAddAppBrowseCommand = 1;
constexpr UINT kAddAppFirstCommand = 100;
struct AddAppMenu {
    HMENU menu = nullptr;             // caller destroys
    std::vector<std::wstring> paths;  // command kAddAppFirstCommand + index
};
AddAppMenu build_add_app_menu(const GuiState& gui);
void show_add_app_menu(GuiState& gui);
void show_app_chip_menu(GuiState& gui, std::size_t chip_index);
void show_app_overflow_menu(GuiState& gui);
// Rename / Duplicate / Delete for one profile (right-click and the header's
// "more" button share it).
void show_profile_menu(GuiState& gui, std::size_t index, POINT screen_point,
                       UINT alignment = TPM_LEFTALIGN);
void remove_application_profile_rules(GuiState& gui, const std::wstring& profile_id);
HICON app_icon(GuiState& gui, const std::wstring& path);
void release_app_icons(GuiState& gui);
std::wstring executable_display_name(const std::wstring& path);
void scroll_profile_into_view(GuiState& gui, std::size_t index);
bool persist_presets(GuiState& gui);
void begin_profile_rename(GuiState& gui, std::size_t index);
void finish_profile_rename(GuiState& gui, bool commit);
void duplicate_profile(GuiState& gui, std::size_t source_index);
void create_profile_from_current(GuiState& gui);
void select_preset(GuiState& gui, std::size_t index);
void undo_profile_switch(GuiState& gui);
void delete_preset(GuiState& gui, std::size_t index);
void delete_active_preset(GuiState& gui);
LRESULT CALLBACK profile_rename_proc(HWND edit, UINT message, WPARAM wparam,
                                     LPARAM lparam, UINT_PTR subclass_id,
                                     DWORD_PTR ref_data);
LRESULT CALLBACK display_combo_proc(HWND combo, UINT message, WPARAM wparam,
                                    LPARAM lparam, UINT_PTR subclass_id,
                                    DWORD_PTR ref_data);
LRESULT CALLBACK profile_proc(HWND item, UINT message, WPARAM wparam,
                              LPARAM lparam, UINT_PTR subclass_id,
                              DWORD_PTR ref_data);
// "Hold to compare" (subclass of that button; chains to hover_button_proc).
LRESULT CALLBACK compare_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                              UINT_PTR subclass_id, DWORD_PTR ref_data);
// Hover tracking for owner-drawn buttons in the work area; chips also map
// Delete to "remove this app".
LRESULT CALLBACK hover_button_proc(HWND button, UINT message, WPARAM wparam,
                                   LPARAM lparam, UINT_PTR subclass_id,
                                   DWORD_PTR ref_data);

}  // namespace gamma_changer
