#include "Rows.hpp"
#include "DesktopOrder.hpp"
#include "FullscreenDesktop.hpp"

#include <hyprland/src/workspace/HLWorkspace.hpp>
#include "HyprCompat.hpp"
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

#include <algorithm>
#include <format>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace Hyprdrive::Rows {
    namespace {
        // Ryoku's PER (modules/workspaces.lua).
        constexpr Row::Id RYOKU_SLOTS = 10;

        Row::Id             g_slots     = RYOKU_SLOTS; // 0 = blocks off
        wl_event_source*    g_roomTimer = nullptr;
        CHyprSignalListener g_removedHook;

        bool ordinary(const PHLWORKSPACE& ws) {
            return DesktopOrder::ordinary(ws);
        }

        bool exists(WORKSPACEID id) {
            for (const auto& REF : State::workspaceState()->workspaces())
                if (const auto WS = REF.lock(); ordinary(WS) && Hyprdrive::Compat::id(WS) == id)
                    return true;
            return false;
        }

        // OVERFLOW FLOWS BACK: a row with an overflow desktop and a free place
        // in its block is compacted (after the usual settle), so the desktop
        // gets a number Super+N reaches again.
        int checkRoom(void*) {
            if (!g_pCompositor || g_pCompositor->m_isShuttingDown || !blocks())
                return 0;
            for (const auto& MON : State::monitorState()->monitors()) {
                if (!MON)
                    continue;
                const Row ROW = of(MON);
                if (!ROW.bounded() || ROW.provisional)
                    continue;
                bool overflow = false;
                for (const auto& WS : members(ROW))
                    overflow |= ROW.inOverflow(Hyprdrive::Compat::id(WS));
                if (!overflow)
                    continue;
                for (Row::Id s = 1; s <= ROW.capacity; ++s)
                    if (!exists(ROW.idOf(s))) {
                        FullscreenDesktop::compactSoon("an overflow desktop has room in its block");
                        return 0;
                    }
            }
            return 0;
        }
    }

    bool blocks() {
        return g_slots > 0;
    }

    Row of(const PHLMONITOR& monitor) {
        if (!blocks() || !monitor)
            return {};
        const auto SHOWN = monitor->m_activeWorkspace;
        if (SHOWN && ordinary(SHOWN))
            return Row::block(std::clamp<Row::Id>(Row::blockOf(Hyprdrive::Compat::id(SHOWN), g_slots), 0, 99'999), g_slots);
        return Row::block(0, g_slots);
    }

    Row ofWorkspace(const PHLWORKSPACE& ws) {
        return ws ? of(ws->m_monitor.lock()) : Row{};
    }

    Row containing(WORKSPACEID id) {
        if (!blocks())
            return {};
        for (const auto& MON : State::monitorState()->monitors())
            if (MON)
                if (const Row R = of(MON); R.contains(id))
                    return R;
        return {};
    }

    std::string label(const PHLWORKSPACE& ws) {
        if (!ws)
            return {};
        const Row R = ofWorkspace(ws);
        if (R.contains(Hyprdrive::Compat::id(ws)))
            return std::to_string(R.slot(Hyprdrive::Compat::id(ws)));
        return std::to_string(Row::slotIn(Hyprdrive::Compat::id(ws), g_slots));
    }

    std::vector<PHLWORKSPACE> members(const Row& row) {
        std::vector<PHLWORKSPACE> out;
        for (const auto& REF : State::workspaceState()->workspaces())
            if (const auto WS = REF.lock(); ordinary(WS) && row.contains(Hyprdrive::Compat::id(WS)) && sameRow(ofWorkspace(WS), row))
                out.push_back(WS);
        std::ranges::sort(out, [&](const PHLWORKSPACE& a, const PHLWORKSPACE& b) { return row.slot(Hyprdrive::Compat::id(a)) < row.slot(Hyprdrive::Compat::id(b)); });
        return out;
    }

    WORKSPACEID firstEmpty(const PHLMONITOR& monitor) {
        const Row R = of(monitor);
        for (Row::Id s = 1; s <= R.lastSlot(); ++s) {
            const WORKSPACEID ID = R.idOf(s);
            PHLWORKSPACE      found;
            for (const auto& REF : State::workspaceState()->workspaces())
                if (const auto WS = REF.lock(); WS && !Hyprdrive::Compat::isSpecial(WS) && Hyprdrive::Compat::id(WS) == ID)
                    found = WS;
            if (!found || (found->m_monitor.lock() == monitor && found->getWindowCount() == 0))
                return ID;
        }
        return WORKSPACE_INVALID;
    }

    int luaWorkspaceBlocks(lua_State* L) {
        long long n = RYOKU_SLOTS;
        if (lua_gettop(L) >= 1 && lua_isnumber(L, 1))
            n = sc<long long>(lua_tointeger(L, 1));
        n = std::clamp<long long>(n, 0, 100);
        if (n != g_slots) {
            LOG(Log::INFO, "[hyprdrive] rows: workspace blocks {} -> {}", g_slots, n);
            g_slots = n;
        }
        return 0;
    }

    void init() {
        if (g_pCompositor && g_pCompositor->m_wlEventLoop && !g_roomTimer)
            g_roomTimer = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, checkRoom, nullptr);
        g_removedHook = Event::bus()->m_events.workspace.removed.listen([](PHLWORKSPACEREF) {
            if (g_roomTimer && blocks())
                wl_event_source_timer_update(g_roomTimer, 1); // not inside the removal
        });
    }

    void shutdown() {
        g_removedHook.reset();
        if (g_roomTimer) {
            wl_event_source_remove(g_roomTimer);
            g_roomTimer = nullptr;
        }
        g_slots = RYOKU_SLOTS;
    }

    std::string debugReport() {
        std::string s = std::format("hyprdrive rows: blocks {} (slots {})", blocks() ? "on" : "off", g_slots);
        for (const auto& MON : State::monitorState()->monitors()) {
            if (!MON)
                continue;
            const Row R = of(MON);
            s += std::format("\n  {}: row base {} capacity {} overflow {}", MON->m_name, R.base, R.capacity, R.overflow);
            s += "\n    members:";
            for (const auto& WS : members(R))
                s += std::format(" {}={}{}", Hyprdrive::Compat::id(WS), label(WS), WS->getWindowCount() ? "" : "(empty)");
            s += "\n    visitors:";
            for (const auto& REF : State::workspaceState()->workspaces())
                if (const auto WS = REF.lock(); ordinary(WS) && WS->m_monitor.lock() == MON && !R.contains(Hyprdrive::Compat::id(WS)))
                    s += std::format(" {}={}", Hyprdrive::Compat::id(WS), label(WS));
            s += std::format("\n    first empty: {}", firstEmpty(MON));
        }
        return s;
    }
}
