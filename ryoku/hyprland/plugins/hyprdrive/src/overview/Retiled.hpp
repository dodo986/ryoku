#pragma once

// A WINDOW RETILED ONTO A DESKTOP NOBODY IS LOOKING AT has a new size and an
// old buffer. The overview's strip draws it in its cell, and drawing it shows
// that old buffer letterboxed inside the new frame. note() starts a short
// grace; while it lasts, pump() hands the client the presentation feedback a
// shown surface gets, which is what makes it draw at its new size.

#include "globals.hpp"

#include <hyprland/src/helpers/time/Time.hpp>

namespace Hyprdrive::Retiled {
    void note(const PHLWINDOW& window);
    bool active(const PHLWINDOW& window);
    void pump(const PHLWINDOW& window, PHLMONITOR monitor, const Time::steady_tp& now);
}
