// The frame pump and its sources (FramePump.hpp says what each is for).

#include "FramePump.hpp"
#include "MotionClock.hpp"

#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <array>
#include <unordered_map>

#include "BorderFade.hpp"
#include "Mosaic.hpp"
#include "Ripple.hpp"
#include "WindowPill.hpp"

namespace Hyprdrive::FramePump {
namespace {
// ONE OUTSTANDING FRAME REQUEST PER MONITOR, and what it has to damage.
//
// The region is accumulated rather than taken from the first caller: within one
// event-loop iteration two animations on one output can both ask, and a request that kept only the first box would leave
// the second one's pixels stale. `full` wins over any box, which is what a
// caller that cannot describe its damage asks for.
struct SPumpRequest {
    bool full    = false;
    bool haveBox = false;
    CBox box; // monitor-local RENDER px, the union of every region asked for
};
std::unordered_map<MONITORID, SPumpRequest> g_pumpPending;
std::unordered_map<MONITORID, uint64_t>     g_pumpSeqs;

// A PUMP THAT OUTLIVES THE PLUGIN IS A CALL INTO UNMAPPED MEMORY.
//
// The doLater a pump queues runs on the NEXT event-loop iteration, and
// PLUGIN_EXIT is followed straight away by the dlclose. releasePointerFocus()
// used to take every outstanding request out, which is part of why main.cpp
// calls it first at unload -- but it is also called from the pointer hook and
// from an overview opening, where dropping another monitor's pump froze its
// dock mid-animation (user, 2026-09-14). So the guarantee moves here, where no
// input event can trigger it: a static whose destructor runs when this object is
// unloaded. Declared AFTER the two maps, so it is destroyed BEFORE them.
//
// Sound whichever way the host unloads: with a dlclose this runs and the
// requests are cancelled; without one the code stays mapped and a late callback
// is merely a damaged monitor.
struct SPumpReaper {
    ~SPumpReaper() {
        if (!g_pEventLoopManager)
            return;

        for (auto [id, seq] : g_pumpSeqs) {
            if (seq)
                g_pEventLoopManager->removeDoLater(seq);
        }
        g_pumpSeqs.clear();
        g_pumpPending.clear();
    }
};
SPumpReaper g_pumpReaper;

// EVERY ANIMATION THAT RUNS ON A DESKTOP NOTHING ELSE REPAINTS: a fade, a
// spring settling, a ring. Asked at every render.pre (main.cpp); the first one
// that is live names the reason `hyprdrive:debug frames` prints, and the next
// frame is booked. A new one is one line here.
constexpr std::array SOURCES{
    // The SUPER-hold pill over the hovered tile eases in and out.
    SSource{"window-pill", [](const PHLMONITOR&) { return WindowPill::animating(); }},
    // The titles Super shows over windows (Mosaic::renderZoomLabels) fade over
    // a pointer that may have stopped.
    SSource{"super-labels", [](const PHLMONITOR&) { return Mosaic::labelsAnimating(); }},
    // The pill's and the tile rings' border fade (BorderFade.hpp).
    SSource{"border-fade", [](const PHLMONITOR& monitor) { return BorderFade::animatingOn(monitor); }},
    // The hot corner's ripple (Ripple.hpp): the whole output every frame while
    // a ring is live, and once more to draw it clean.
    SSource{"ripple", [](const PHLMONITOR& monitor) { return Ripple::activeOn(monitor); }},
};
}

// ASK FOR ANOTHER FRAME. These animations run on a desktop where nothing else
// is moving, so nothing else damages the monitor -- and a damage
// requested from INSIDE the render pass is cleared with that pass, so it has to
// be posted for after it. doLater is that, and it both starts and sustains the
// loop.
//
// ONE REQUEST PER FRAME: callers can ask far faster than frames happen, and a
// doLater per ask would damage the monitor per ask, each damage a frame whose
// render.pre asks again -- a flood. The collapse is exact rather than approximate: two damageMonitor calls in one
// event-loop iteration are the same region twice, so dropping the second
// changes nothing but the flood.
//
// THE MARK IS ERASED, NOT SET FALSE, and the difference is a latch. `PENDING`
// is the only thing standing between a motion event and another doLater, so a
// mark that survives its callback is a dock that never asks for a frame again:
// the entry reads true, every request() returns early, and the reveal, the
// bounce and the flights all stop mid-animation on that output with nothing in
// any log. The callback erases, and forget() erases on the way out --
// because a doLater queued against a monitor that is then removed is dropped
// with the loop, and its callback is exactly what would have cleared the mark.
void request(PHLMONITOR monitor, std::optional<CBox> regionRenderPx) {
    if (!monitor)
        return;

    const bool ALREADY = g_pumpPending.contains(monitor->m_id);

    auto&      REQ     = g_pumpPending[monitor->m_id];
    if (!regionRenderPx)
        REQ.full = true;
    else if (!REQ.full && !regionRenderPx->empty()) {
        if (!REQ.haveBox) {
            REQ.box     = *regionRenderPx;
            REQ.haveBox = true;
        } else {
            // The union, spelled out: CBox has no "grow to contain" of its own.
            const double X0 = std::min(REQ.box.x, regionRenderPx->x);
            const double Y0 = std::min(REQ.box.y, regionRenderPx->y);
            const double X1 = std::max(REQ.box.x + REQ.box.width, regionRenderPx->x + regionRenderPx->width);
            const double Y1 = std::max(REQ.box.y + REQ.box.height, regionRenderPx->y + regionRenderPx->height);
            REQ.box         = CBox{X0, Y0, X1 - X0, Y1 - Y0};
        }
    }

    if (ALREADY)
        return;

    if (g_pEventLoopManager) {
        if (const auto IT = g_pumpSeqs.find(monitor->m_id); IT != g_pumpSeqs.end() && IT->second) {
            g_pEventLoopManager->removeDoLater(IT->second);
            g_pumpSeqs.erase(IT);
        }
        g_pumpSeqs[monitor->m_id] = g_pEventLoopManager->doLater([MONITOR = PHLMONITORREF{monitor}, ID = monitor->m_id] {
            SPumpRequest request;
            if (const auto IT = g_pumpPending.find(ID); IT != g_pumpPending.end())
                request = IT->second;
            g_pumpPending.erase(ID);
            g_pumpSeqs.erase(ID);
            if (!MONITOR)
                return;

            const auto MON = MONITOR.lock();
            if (request.full || !request.haveBox || request.box.empty()) {
                g_pHyprRenderer->damageMonitor(MON);
                return;
            }

            // damageBox wants GLOBAL LOGICAL coordinates and every caller holds
            // monitor-local render px: divide out the scale, add the
            // output's position. Rounded OUTWARD, because a box that lands half
            // a pixel inside its own damage leaves a stale row.
            const double SCALE = std::max(MON->m_scale, 0.01F);
            CBox         box{request.box.x / SCALE + MON->m_position.x, request.box.y / SCALE + MON->m_position.y, request.box.width / SCALE, request.box.height / SCALE};
            box.expand(1.0);
            g_pHyprRenderer->damageBox(box.round());
        });
    }
}

void forget(MONITORID id) {
    g_pumpPending.erase(id);
    if (g_pEventLoopManager) {
        if (const auto IT = g_pumpSeqs.find(id); IT != g_pumpSeqs.end() && IT->second) {
            g_pEventLoopManager->removeDoLater(IT->second);
            g_pumpSeqs.erase(IT);
        }
    }
}

const char* reason(const PHLMONITOR& monitor) {
    if (!monitor)
        return nullptr;
    for (const auto& SOURCE : SOURCES) {
        if (SOURCE.active(monitor))
            return SOURCE.name;
    }
    return nullptr;
}
}
