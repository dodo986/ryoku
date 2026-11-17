#pragma once

// Where a released window drag goes, as a pure function.
//
// endWindowDrag() was 277 lines in which the facts about the drop, the choice
// that followed from them and the compositor calls that carried it out were the
// same code. The ladder below is that choice and nothing else: no Hyprland
// types, no state, no side effects, so it can be walked exhaustively off the
// compositor -- tests/unit/drop_table_test.cpp compiles this header on the host and
// checks every reachable combination against the branch structure it replaced.
//
// The caller gathers facts in three stages and asks again after each, because
// the gathering itself is not free: the pill branch must return before anything
// queries a drop workspace. resolve() is total at every stage --
// the fields a stage has not filled in yet are false, which is why the two
// early actions are decided by the facts that ARE known first.

namespace Hyprdrive::Drop {

    struct SFacts {
        // stage 1 -- the window and where the button came up
        bool haveWindow        = false;
        // stage 2 -- the destination
        bool haveDropWorkspace = false;
        bool retileOnEnd       = false; // startedTiled AND a live layout target/space/algorithm
        bool moveWorkspace     = false; // the drop workspace is not the one the drag started on
        bool startedTiled      = false;
        bool haveTarget        = false;
        bool scrollingLayout   = false;
        bool haveOriginalBox   = false;
        // stage 3 -- where on the destination card
        bool dropPointMapsIntoCard        = false;
        bool restoreSourceFullscreenFocus = false;
    };

    enum class EAction {
        CancelToGrid, // nowhere to go, or its own desktop: keep everything, ease the tile home
        RetileMove,   // a tiled window, onto another workspace
        FloatMove,    // a floating window, onto another workspace
        Nothing,      // a drop with nothing to move (no window, or no layout target)
    };

    // What to do with the layout target once the window is where it belongs.
    enum class EPlacement {
        None,
        ScrollEdgeFar, // push to the far edge of a scrolling workspace
    };

    struct SDecision {
        EAction    action    = EAction::Nothing;
        EPlacement placement = EPlacement::None;
        // A move commits the window's ORIGINAL global size, not the size of the
        // mission-control slot it was drawn at (X41/X42).
        bool useOriginalBox   = false;
        // The cursor can be read as a point inside the destination card, so a
        // floating window lands where it was dropped rather than where it was.
        bool mapPointIntoCard = false;
        bool restoreSourceFullscreenFocus = false;
    };

    // The overview MOVES windows between desktops; it never arranges them. A
    // drop on the window's own desktop is no drop at all (the tile eases back to
    // its slot), and a window arriving on another desktop goes where that
    // desktop's layout puts a new window -- never beside / swapped with the tile
    // under the cursor. (Until 1.9.2 a same-desktop drop swapped or inserted by
    // direction and a drop indicator highlighted the target; the user: "that's
    // not the purpose of the overview at any point ever".)
    constexpr SDecision resolve(const SFacts& f) {
        SDecision d;

        if (!f.haveDropWorkspace || !f.moveWorkspace) {
            d.action = EAction::CancelToGrid;
            return d;
        }

        d.restoreSourceFullscreenFocus = f.restoreSourceFullscreenFocus;
        d.useOriginalBox               = f.haveOriginalBox;
        d.mapPointIntoCard             = f.dropPointMapsIntoCard;

        if (f.retileOnEnd) {
            d.action = EAction::RetileMove;
            if (f.scrollingLayout)
                d.placement = EPlacement::ScrollEdgeFar;
            return d;
        }

        if (f.haveWindow)
            d.action = EAction::FloatMove;

        return d;
    }

}
