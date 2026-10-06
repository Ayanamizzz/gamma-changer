#pragma once

#include "gamma_types.h"

#include <vector>

namespace gamma_changer {

// Select the greatest current pixel area, preferring the primary display on
// ties and otherwise preserving enumeration order. No connected display is -1.
int default_display_index(const std::vector<DisplayInfo>& displays);

}  // namespace gamma_changer
