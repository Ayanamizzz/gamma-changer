#include "calibration_session.h"

namespace gamma_changer {

void CalibrationSession::clear_undo() {
    profile_switch_undo_available = false;
    undo_display_id.clear();
}

bool CalibrationSession::undo_applies_to(const std::wstring& display_id) const {
    return profile_switch_undo_available && undo_display_id == display_id;
}

void CalibrationSession::mark_clean() {
    dirty = false;
}

}  // namespace gamma_changer
