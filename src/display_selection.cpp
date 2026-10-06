#include "display_selection.h"

#include <cstdint>

namespace gamma_changer {

namespace {

std::uint64_t pixel_area(const DisplayInfo& display) {
    if (display.width <= 0 || display.height <= 0) return 0;
    return static_cast<std::uint64_t>(display.width) *
           static_cast<std::uint64_t>(display.height);
}

}  // namespace

int default_display_index(const std::vector<DisplayInfo>& displays) {
    if (displays.empty()) return -1;

    std::size_t selected = 0;
    std::uint64_t greatest_area = pixel_area(displays.front());
    for (std::size_t index = 1; index < displays.size(); ++index) {
        const std::uint64_t area = pixel_area(displays[index]);
        if (area > greatest_area ||
            (area == greatest_area && displays[index].primary && !displays[selected].primary)) {
            selected = index;
            greatest_area = area;
        }
    }
    return static_cast<int>(selected);
}

}  // namespace gamma_changer
