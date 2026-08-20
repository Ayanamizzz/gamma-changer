#include "profile_list_logic.h"

#include <algorithm>
#include <climits>

namespace gamma_changer {

std::wstring next_profile_id(const std::vector<Profile>& profiles) {
    for (std::size_t number = 1;; ++number) {
        const std::wstring candidate = L"profile-" + std::to_wstring(number);
        const bool used = std::any_of(profiles.begin(), profiles.end(),
                                      [&](const Profile& profile) { return profile.id == candidate; });
        if (!used) return candidate;
    }
}

std::wstring next_custom_profile_name(const std::vector<Profile>& profiles) {
    for (std::size_t number = 1;; ++number) {
        const std::wstring candidate = L"Custom " + std::to_wstring(number);
        const bool used = std::any_of(profiles.begin(), profiles.end(),
                                      [&](const Profile& profile) { return profile.name == candidate; });
        if (!used) return candidate;
    }
}

bool remove_orphan_profile_preferences(
    std::vector<DisplayProfilePreference>& preferences,
    const std::vector<Profile>& profiles) {
    const std::size_t previous_size = preferences.size();
    preferences.erase(
        std::remove_if(
            preferences.begin(), preferences.end(),
            [&](const DisplayProfilePreference& preference) {
                return std::none_of(
                    profiles.begin(), profiles.end(),
                    [&](const Profile& profile) {
                        return profile.id == preference.profile_id;
                    });
            }),
        preferences.end());
    return preferences.size() != previous_size;
}

ProfileRenameGeometry make_profile_rename_geometry(
    int row_x, int row_y, int row_width, int row_height,
    int text_inset, int edit_padding, int line_height) {
    row_width = std::max(1, row_width);
    row_height = std::max(1, row_height);
    text_inset = std::clamp(text_inset, 0, row_width - 1);
    edit_padding = std::clamp(edit_padding, 0, text_inset);

    ProfileRenameGeometry geometry;
    const int edit_offset_x = text_inset - edit_padding;
    const int right_inset = std::min(row_width - edit_offset_x - 1,
                                     std::max(1, edit_padding * 2));
    const int vertical_inset = std::min(row_height / 2,
                                        std::max(1, edit_padding / 2));
    geometry.x = row_x + edit_offset_x;
    geometry.y = row_y + vertical_inset;
    geometry.width = std::max(1, row_width - edit_offset_x - right_inset);
    geometry.height = std::max(1, row_height - vertical_inset * 2);

    geometry.format_left = std::min(edit_padding, geometry.width - 1);
    geometry.format_right = std::max(
        geometry.format_left + 1,
        geometry.width - std::min(edit_padding, geometry.width - 1));
    line_height = std::clamp(line_height, 1, geometry.height);
    geometry.format_top = (geometry.height - line_height) / 2;
    geometry.format_bottom = geometry.format_top + line_height;
    return geometry;
}

std::size_t active_index_after_delete(std::size_t size_after_delete,
                                      std::size_t deleted_index,
                                      std::size_t active_index) {
    if (size_after_delete == 0) return 0;
    if (deleted_index < active_index) return active_index - 1;
    if (deleted_index == active_index) return deleted_index > 0 ? deleted_index - 1 : 0;
    const std::size_t adjusted = active_index;
    return adjusted < size_after_delete ? adjusted : size_after_delete - 1;
}

int clamp_profile_scroll(int offset, std::size_t profile_count,
                         int row_stride, int row_height, int visible_height) {
    if (profile_count == 0 || row_stride <= 0 || row_height <= 0 ||
        visible_height <= 0) {
        return 0;
    }
    const long long total_height =
        static_cast<long long>(profile_count - 1) * row_stride + row_height;
    const long long max_scroll = std::max(0LL, total_height - visible_height);
    const int max_scroll_int = max_scroll > INT_MAX ? INT_MAX
                                                    : static_cast<int>(max_scroll);
    return std::clamp(offset, 0, max_scroll_int);
}

int profile_scroll_to_show(std::size_t index, std::size_t profile_count,
                           int current_offset, int first_y, int list_bottom,
                           int row_stride, int row_height) {
    if (profile_count == 0 || index >= profile_count) {
        return clamp_profile_scroll(current_offset, profile_count,
                                    row_stride, row_height,
                                    list_bottom - first_y);
    }
    const int row_top = first_y + static_cast<int>(index) * row_stride;
    const int row_bottom = row_top + row_height;
    int offset = current_offset;
    if (row_top - offset < first_y) {
        offset -= first_y - (row_top - offset);
    } else if (row_bottom - offset > list_bottom) {
        offset += (row_bottom - offset) - list_bottom;
    }
    return clamp_profile_scroll(offset, profile_count,
                                row_stride, row_height,
                                list_bottom - first_y);
}

}  // namespace gamma_changer
