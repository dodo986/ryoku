#pragma once

// The pure rules behind HoverTarget.hpp and HoverActions.hpp, host-tested in
// tests/unit/hover_target_test.cpp.

namespace Hyprdrive::HoverTarget {

    // Is an action given the hover target instead of what it was called
    // with? Only when it named no window (a window named is the caller's: the
    // dock's chip, the window pill, a script's address) -- or named the
    // FOCUSED one from inside one of the user's key binds, which is how a bind
    // written in Lua says "the active window" (Omarchy's
    // o.workspaces.move_window passes hl.get_active_window()'s address, the
    // legacy movetoworkspace the focused window) --, the plugin's own code is
    // not the caller (`ownCall`: a hyprdrive dispatcher, or an action run
    // from inside one already retargeted: plugin code names its windows),
    // there is a target, and it is not the focused window already (then
    // nothing changes).
    constexpr bool retarget(bool named, bool namedFocusedInBind, bool ownCall, bool hasTarget, bool targetIsFocused) {
        return (!named || namedFocusedInBind) && !ownCall && hasTarget && !targetIsFocused;
    }

    // Super+Shift+N on a hovered window moves it SILENTLY, whatever the bind
    // asked: the user is pointing at a preview, not at a desktop, so the
    // overview (or the dock's pane) stays where it is, relays out without the
    // window, and the focus and the pointer stay put -- "follow" would warp
    // the pointer to the window's new place, off the preview it was on, and
    // switch the desktop under the overview. The focused window's own move
    // keeps the bind's follow.
    constexpr bool moveSilently(bool retargeted, bool silent) {
        return retargeted || silent;
    }
}
