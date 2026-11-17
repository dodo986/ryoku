#include "Retiled.hpp"
#include "Config.hpp"

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include "HyprCompat.hpp"

#include <algorithm>
#include <vector>

namespace Hyprdrive::Retiled {
    namespace {
        // Long enough for the retile and for a client that reflows a beat later.
        constexpr float                                       GRACE_MS = 1500.F;
        std::vector<std::pair<PHLWINDOWREF, Time::steady_tp>> g_retiled;
    }

    void note(const PHLWINDOW& window) {
        if (!window)
            return;
        std::erase_if(g_retiled, [](const auto& e) { return !e.first.lock() || Hyprdrive::Config::realElapsedMs(e.second) >= GRACE_MS; });
        for (auto& e : g_retiled) {
            if (e.first.lock() == window) {
                e.second = Time::steadyNow();
                return;
            }
        }
        g_retiled.emplace_back(PHLWINDOWREF{window}, Time::steadyNow());
    }

    bool active(const PHLWINDOW& window) {
        return window && std::ranges::any_of(g_retiled, [&](const auto& e) { return e.first.lock() == window && Hyprdrive::Config::realElapsedMs(e.second) < GRACE_MS; });
    }

    void pump(const PHLWINDOW& window, PHLMONITOR monitor, const Time::steady_tp& now) {
        if (!monitor || !active(window) || !window->wlSurface() || !window->wlSurface()->resource())
            return;
        std::pair<PHLMONITOR, Time::steady_tp> data = {monitor, now};
        window->wlSurface()->resource()->breadthfirst(
            [](SP<CWLSurfaceResource> surface, const Vector2D&, void* data) {
                const auto [MONITOR, NOW] = *sc<std::pair<PHLMONITOR, Time::steady_tp>*>(data);
                surface->presentFeedback(NOW, MONITOR, false);
            },
            &data);
    }
}
