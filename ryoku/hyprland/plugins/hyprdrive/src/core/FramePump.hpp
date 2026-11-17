#pragma once

// FRAMES FOR WHAT MOVES ON AN IDLE DESKTOP. Anything that moves with no pointer
// motion and no client commit to repaint it (a fade, a spring settling, a ring)
// names itself in FramePump.cpp's SOURCES; main.cpp's render.pre asks reason()
// and books the next frame with request() while one is live. Nothing damages
// from inside the render pass: request() posts it for after the pass and
// coalesces it to one per output per event-loop iteration.

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>

#include <cstdint>
#include <optional>

namespace Hyprdrive::FramePump {
    // Damage `monitor` after this pass: the whole output, or the union of the
    // regions asked for (monitor-local render px) within one iteration.
    void request(PHLMONITOR monitor, std::optional<CBox> regionRenderPx = std::nullopt);

    // A monitor that went away: its pending request is dropped.
    void forget(MONITORID id);

    struct SSource {
        const char* name;                          // what `hyprdrive:debug frames` prints
        bool (*active)(const PHLMONITOR& monitor); // never null; monitor is never null
    };

    // The first animation still running on this monitor, or nullptr.
    const char* reason(const PHLMONITOR& monitor);
}
