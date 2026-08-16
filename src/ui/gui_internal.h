#pragma once

#include "app_version.h"
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
#include <cmath>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace gamma_changer {

constexpr wchar_t kWindowClass[] = L"GammaChangerCppWindow";
constexpr wchar_t kBuiltinProfileId[] = L"builtin-default";
constexpr std::size_t kNoProfile = std::numeric_limits<std::size_t>::max();
constexpr int kProfileVisibleRows = 4;
constexpr int kProfileIdBase = 1000;
constexpr UINT kPreviewTimer = 1;
constexpr UINT kDisplayRefreshTimer = 2;
constexpr UINT kAutoSaveTimer = 3;
constexpr UINT kRecoveryRetryTimer = 4;
constexpr UINT kAutoSaveDelayMs = 700;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kProfileRenameMessage = WM_APP + 2;
constexpr UINT kProfileContextMessage = WM_APP + 3;
constexpr UINT kProfileCommitRenameMessage = WM_APP + 4;
constexpr UINT kUndoProfileSwitchMessage = WM_APP + 5;
constexpr UINT kTrayId = 1;
constexpr UINT kTrayShowCommand = 1;
constexpr UINT kTrayExitCommand = 2;
constexpr UINT kTrayProfileCommandBase = 100;

enum ControlId : int {
    kTitleLabel = 100,
    kSubtitleLabel = 101,
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
    kApplyButton = 116,
    kResetButton = 117,
    kStatusLabel = 118,
    kPresetSave = 124,
    kPresetDelete = 126,
    kDisplayCaption = 127,
    kPresetCaption = 128,
    kMainHeading = 129,
    kMainSubtitle = 130,
    kToneHeading = 131,
    kColorHeading = 132,
    kStartupToggle = 133,
    kPreviewHeading = 134,
    kPreviewCaption = 135,
    kToneCaption = 136,
    kColorCaption = 137,
    kDisplayStatus = 138,
    kProfileRename = 139,
    kBeforeAfter = 140,
};

enum class StatusTone {
    idle,
    success,
    warning,
    error,
};

struct GuiState {
    HWND window = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND display_combo = nullptr;
    HWND refresh_button = nullptr;
    HWND display_caption = nullptr;
    HWND preset_caption = nullptr;
    HWND main_heading = nullptr;
    HWND main_subtitle = nullptr;
    HWND tone_heading = nullptr;
    HWND color_heading = nullptr;
    HWND preview_heading = nullptr;
    HWND preview_caption = nullptr;
    HWND tone_caption = nullptr;
    HWND color_caption = nullptr;
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
    HWND apply_button = nullptr;
    HWND reset_button = nullptr;
    HWND status = nullptr;
    std::vector<HWND> preset_buttons;
    HWND preset_save = nullptr;
    HWND preset_delete = nullptr;
    HWND profile_rename = nullptr;
    HWND before_after_button = nullptr;
    HFONT normal_font = nullptr;
    HFONT title_font = nullptr;
    HFONT heading_font = nullptr;
    HFONT panel_font = nullptr;
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
    HWND hovered_profile = nullptr;
    HWND hovered_numeric = nullptr;
    bool profile_keyboard_focus = false;
    StatusTone status_tone = StatusTone::success;
    std::vector<DisplayInfo> displays;
    int active_display_index = -1;
    std::vector<Profile> profiles;
    std::size_t active_preset = 0;
    int profile_scroll_offset = 0;
    std::size_t renaming_preset = kNoProfile;
    CalibrationSession session;
    ProfileStore store;
    CalibrationController controller{store};
    ProfileManager profile_manager{store};
    bool reapply_after_display_refresh = false;
    int recovery_retry_attempt = 0;
    std::unordered_map<std::wstring, CalibrationSettings> pending_adjustments;
    bool profile_store_available = true;
};

// Shared internal functions used across the UI translation units.
GuiState* state(HWND window);
GammaParams params_from_sliders(const GuiState& gui);
void set_status(GuiState& gui, const std::wstring& text, StatusTone tone);
const DisplayInfo* selected_display(const GuiState& gui);
void set_params_to_controls(GuiState& gui, const GammaParams& params);
void refresh_preset_buttons(GuiState& gui);
bool save_and_apply_current(GuiState& gui, bool automatic);

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
void layout_controls(GuiState& gui, int width, int height);
void layout_controls_for_current_size(GuiState& gui);
void apply_font(HWND control, HFONT font);
void recreate_fonts(GuiState& gui, UINT dpi);

bool cancel_active_preview(GuiState& gui);
void create_controls(GuiState& gui);
void normalize_all_edits(GuiState& gui);
void update_value_labels(GuiState& gui);
void set_adjustment_enabled(GuiState& gui, bool enabled);
void edit_changed(GuiState& gui, HWND edit, HWND slider, double minimum, double maximum);
void normalize_edit(GuiState& gui, HWND edit, HWND slider, double minimum, double maximum);
void preview_selected(GuiState& gui);
void mark_changed(GuiState& gui);
void enable_modern_backdrop(HWND window);
std::wstring display_metadata(const DisplayInfo& display);
int selected_display_index(const GuiState& gui);
void select_display_item(GuiState& gui, int display_index);
void load_selected_profile(GuiState& gui);
void reset_selected(GuiState& gui);
void refresh_displays(GuiState& gui);
bool reapply_all_committed(GuiState& gui);
void schedule_recovery_retry(GuiState& gui);
void run_recovery_retry(GuiState& gui);
bool flush_before_exit(GuiState& gui);
bool confirm_close_after_save_failure(GuiState& gui);
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
LRESULT CALLBACK profile_proc(HWND item, UINT message, WPARAM wparam,
                              LPARAM lparam, UINT_PTR subclass_id,
                              DWORD_PTR ref_data);

}  // namespace gamma_changer
