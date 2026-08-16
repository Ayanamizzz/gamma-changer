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

// Returns the active index after deleting `deleted_index` from a collection
// whose size after deletion is `size_after_delete`.
std::size_t active_index_after_delete(std::size_t size_after_delete,
                                      std::size_t deleted_index,
                                      std::size_t active_index);

// Clamps a scroll offset for `profile_count` rows with `row_stride` pixels
// inside a viewport of `visible_height` pixels.
int clamp_profile_scroll(int offset, std::size_t profile_count,
                         int row_stride, int visible_height);

// Returns a scroll offset that keeps `index` fully visible between `first_y`
// and `list_bottom`, starting from `current_offset`.
int profile_scroll_to_show(std::size_t index, std::size_t profile_count,
                           int current_offset, int first_y, int list_bottom,
                           int row_stride, int row_height);

}  // namespace gamma_changer
