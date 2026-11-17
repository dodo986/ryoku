#pragma once

#include "globals.hpp"
#include "Row.hpp"

#include <string>
#include <vector>

struct lua_State; // lua.h's, at global scope

// EACH MONITOR'S ROW OF DESKTOPS, live (Row.hpp has the what and why).
//
// Ryoku's rule and nothing else: a monitor's row is the ten-id block of the
// desktop it shows. Ten is Ryoku's PER (modules/workspaces.lua) and the
// default; hl.plugin.hyprdrive.workspace_blocks(n) changes it, 0 turning
// blocks off. Nothing is read from disk.
namespace Hyprdrive::Rows {
    void init();
    void shutdown();

    // Blocks are on: every monitor's row is bounded.
    bool blocks();
    // The monitor's row (blocks off, or no monitor: the implicit row).
    Row of(const PHLMONITOR& monitor);
    // The row of the monitor the desktop is on.
    Row ofWorkspace(const PHLWORKSPACE& ws);
    // The row of the connected monitor whose row holds `id` (blocks off, or
    // nobody's: the implicit row).
    Row containing(WORKSPACEID id);
    // What a strip cell says for this desktop: its slot in its block, the
    // number Super+N reaches it by; blocks off, the id.
    std::string label(const PHLWORKSPACE& ws);
    // The ordinary desktops in `row` (on a monitor with that row, numbered in
    // it), by place.
    std::vector<PHLWORKSPACE> members(const Row& row);
    // The lowest id in the monitor's row with no desktop, or with an empty one
    // on this monitor. (Rows never fill up: overflow.)
    WORKSPACEID firstEmpty(const PHLMONITOR& monitor);

    // hl.plugin.hyprdrive.workspace_blocks(slots): 0 = off; no argument, Ryoku's ten.
    int luaWorkspaceBlocks(lua_State* L);
    // `hyprdrive:debug rows`.
    std::string debugReport();
}
