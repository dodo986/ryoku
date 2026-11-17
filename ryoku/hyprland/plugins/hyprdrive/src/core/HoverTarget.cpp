#include "HoverTarget.hpp"
#include "IOverview.hpp"

#include <hyprland/src/desktop/view/window/Window.hpp>
#include "HyprCompat.hpp"

namespace Hyprdrive::HoverTarget {
    namespace {
        PHLWINDOW overviewTile() {
            if (scrollOverviews().empty())
                return {};
            // The overview under the pointer (or the one the keyboard is in), if open.
            const auto OVERVIEW = activeScrollOverview();
            if (!OVERVIEW || !OVERVIEW->pMonitor || openScrollOverviewForMonitor(OVERVIEW->pMonitor.lock()) != OVERVIEW)
                return {};
            const auto TILE = OVERVIEW->actionTile();
            return TILE && TILE->mapped() ? TILE : PHLWINDOW{};
        }
    }

    PHLWINDOW window() {
        return overviewTile();
    }
}
