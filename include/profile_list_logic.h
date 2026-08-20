#pragma once

#include "gamma_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gamma_changer {

// Pure profile-list helpers. This module deliberately has no Win32 dependency so
// the index and scroll behavior can be unit-tested independently of the UI.

std::wstring next_profile_id(const std::vector<Profile>& profiles);
std::wstring next_custom_profile_name(const std::vector<Profile>& profiles);
// Removes display associations whose Profile no longer exists. Returns true
// when the caller must persist the filtered collection.
bool remove_orphan_profile_preferences(
    std::vector<DisplayProfilePreference>& preferences,
    const std::vector<Profile>& profiles);

// Pixel geometry for the inline Profile rename editor. The formatting
// rectangle is relative to the edit control, while x/y are relative to the
// common parent shared by the Profile row and editor.
struct ProfileRenameGeometry {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    int format_left = 0;
    int format_top = 0;
    int format_right = 1;
    int format_bottom = 1;
};

// Keeps the editor text origin on the same horizontal and vertical baseline
// as the owner-drawn Profile label at every DPI.
ProfileRenameGeometry make_profile_rename_geometry(
    int row_x, int row_y, int row_width, int row_height,
    int text_inset, int edit_padding, int line_height);

// Returns the active index after deleting `deleted_index` from a collection
// whose size after deletion is `size_after_delete`.
std::size_t active_index_after_delete(std::size_t size_after_delete,
                                      std::size_t deleted_index,
                                      std::size_t active_index);

// Clamps a scroll offset for `profile_count` rows inside a viewport. The final
// row has no trailing gap, so both stride and actual row height are required.
int clamp_profile_scroll(int offset, std::size_t profile_count,
                         int row_stride, int row_height, int visible_height);

// Returns a scroll offset that keeps `index` fully visible between `first_y`
// and `list_bottom`, starting from `current_offset`.
int profile_scroll_to_show(std::size_t index, std::size_t profile_count,
                           int current_offset, int first_y, int list_bottom,
                           int row_stride, int row_height);

}  // namespace gamma_changer
