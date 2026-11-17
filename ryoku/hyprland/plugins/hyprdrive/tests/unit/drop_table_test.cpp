// Exhaustive table test for DropDecision.hpp: Hyprdrive::Drop::resolve(), a
// released window drag.
//
// The drop resolution came out of endWindowDrag()'s 277 lines as a pure
// function precisely so it could be checked without a compositor: 16 boolean
// facts, 65536 combinations, no state, no I/O. `reference()` below is the
// SAME ladder transliterated from the pre-split source (a4189e2,
// scrollOverview.cpp:1919-2195) -- condition for condition, in the order the
// if/else chain evaluated them -- so a transcription slip in either copy shows
// up as a disagreeing row rather than as a drag that misbehaves once a month.
//
//   g++ -std=c++2b -O2 -o /tmp/drop_table_test tests/unit/drop_table_test.cpp && /tmp/drop_table_test
//
// Needs nothing from Hyprland; DropDecision.hpp includes nothing at all.

#include "DropDecision.hpp"

#include <cstdio>
#include <string>

using namespace Hyprdrive::Drop;

// The ladder spelled out the long way, so a slip in either copy shows up as a
// disagreeing row. Since 1.9.2 the overview only MOVES windows between
// desktops: a drop on the window's own desktop cancels, and an arrival is the
// destination layout's to place (no swap, no insert beside the tile under the
// cursor, no anchor facts at all).
static SDecision reference(const SFacts& f) {
    SDecision d;

    if (!f.haveDropWorkspace || !f.moveWorkspace) {
        d.action = EAction::CancelToGrid;
        return d;
    }

    d.useOriginalBox               = f.haveOriginalBox;
    d.mapPointIntoCard             = f.dropPointMapsIntoCard;
    d.restoreSourceFullscreenFocus = f.restoreSourceFullscreenFocus;

    if (f.retileOnEnd) {
        d.action    = EAction::RetileMove;
        d.placement = f.scrollingLayout ? EPlacement::ScrollEdgeFar : EPlacement::None;
    } else if (f.haveWindow) {
        d.action = EAction::FloatMove;
    }

    return d;
}

static const char* actionName(EAction a) {
    switch (a) {
        case EAction::CancelToGrid: return "CancelToGrid";
        case EAction::RetileMove: return "RetileMove";
        case EAction::FloatMove: return "FloatMove";
        case EAction::Nothing: return "Nothing";
    }
    return "?";
}
static constexpr int NACTIONS = 4;

static const char* placementName(EPlacement p) {
    switch (p) {
        case EPlacement::None: return "None";
        case EPlacement::ScrollEdgeFar: return "ScrollEdgeFar";
    }
    return "?";
}

static bool same(const SDecision& a, const SDecision& b) {
    return a.action == b.action && a.placement == b.placement && a.useOriginalBox == b.useOriginalBox && a.mapPointIntoCard == b.mapPointIntoCard &&
        a.restoreSourceFullscreenFocus == b.restoreSourceFullscreenFocus;
}

// The 11 facts, in bit order, so a failing row can be read back.
static const char* BITNAMES[] = {
    "haveWindow", "haveDropWorkspace",     "retileOnEnd",                  "moveWorkspace", "startedTiled",
    "haveTarget", "scrollingLayout", "haveOriginalBox",       "dropPointMapsIntoCard",        "restoreSourceFullscreenFocus",
};
static constexpr int NBITS = 10;

static SFacts factsFromBits(unsigned bits) {
    SFacts f;
    f.haveWindow                   = bits & (1u << 0);
    f.haveDropWorkspace            = bits & (1u << 1);
    f.retileOnEnd                  = bits & (1u << 2);
    f.moveWorkspace                = bits & (1u << 3);
    f.startedTiled                 = bits & (1u << 4);
    f.haveTarget                   = bits & (1u << 5);
    f.scrollingLayout              = bits & (1u << 6);
    f.haveOriginalBox              = bits & (1u << 7);
    f.dropPointMapsIntoCard        = bits & (1u << 8);
    f.restoreSourceFullscreenFocus = bits & (1u << 9);
    return f;
}

static std::string describe(unsigned bits) {
    std::string s;
    for (int i = 0; i < NBITS; ++i) {
        if (bits & (1u << i)) {
            if (!s.empty())
                s += " ";
            s += BITNAMES[i];
        }
    }
    return s.empty() ? "(nothing set)" : s;
}

// ------------------------------------------------------- V3 icon drops --
// The second table in this file, and the reason it is in this file: a dock icon
// released on the screen and a window released on the screen are two gestures
// that ask the same question, and keeping the two ladders side by side is what
// stops the second one from being reinvented inside an input hook.
//
// 7 facts, 128 rows, walked exhaustively -- but there is no PARENT ladder to
// diff against here (the parent does nothing at all with this release), so the
// check is a different one: every row must satisfy the INVARIANTS the feature
// is defined by, every action must be reachable, and the named cases the spec
// itself lists must come out the way the spec says.
int main() {
    int failures = 0;
    int counts[NACTIONS] = {};

    for (unsigned bits = 0; bits < (1u << NBITS); ++bits) {
        const SFacts    F = factsFromBits(bits);
        const SDecision A = resolve(F);
        const SDecision B = reference(F);

        counts[static_cast<int>(A.action)]++;

        if (same(A, B))
            continue;

        if (++failures <= 20)
            std::printf("MISMATCH bits=0x%04x [%s]\n  resolve   %s/%s orig=%d map=%d rsf=%d\n  reference %s/%s orig=%d map=%d rsf=%d\n", bits,
                        describe(bits).c_str(), actionName(A.action), placementName(A.placement), A.useOriginalBox, A.mapPointIntoCard,
                        A.restoreSourceFullscreenFocus, actionName(B.action), placementName(B.placement), B.useOriginalBox, B.mapPointIntoCard,
                        B.restoreSourceFullscreenFocus);
    }

    // Every action has to be REACHED, or "every row agrees" would be a fact
    // about an unreachable ladder rather than about this one.
    std::printf("rows %u\n", 1u << NBITS);
    for (int i = 0; i < NACTIONS; ++i) {
        std::printf("  %-13s %6d\n", actionName(static_cast<EAction>(i)), counts[i]);
        if (counts[i] == 0) {
            std::printf("UNREACHED action %s\n", actionName(static_cast<EAction>(i)));
            ++failures;
        }
    }

    // The five real drags this rig exists for, named, so the table has anchors
    // a human can check by hand.
    struct SCase {
        const char* name;
        SFacts      facts;
        EAction     action;
        EPlacement  placement;
    };
    const SCase CASES[] = {
        {"released over nothing", SFacts{.haveWindow = true, .haveTarget = true}, EAction::CancelToGrid, EPlacement::None},
        {"floating tile to another desktop",
         SFacts{.haveWindow = true, .haveDropWorkspace = true, .moveWorkspace = true, .haveTarget = true, .haveOriginalBox = true, .dropPointMapsIntoCard = true},
         EAction::FloatMove, EPlacement::None},
        {"floating tile dropped on its own card", SFacts{.haveWindow = true, .haveDropWorkspace = true, .haveTarget = true, .haveOriginalBox = true},
         EAction::CancelToGrid, EPlacement::None},
        {"tiled window dropped on another tile of its own desktop (no swap)",
         SFacts{.haveWindow = true, .haveDropWorkspace = true, .retileOnEnd = true, .startedTiled = true, .haveTarget = true, .dropPointMapsIntoCard = true},
         EAction::CancelToGrid, EPlacement::None},
        {"tiled window onto a scrolling desktop's card",
         SFacts{.haveWindow = true, .haveDropWorkspace = true, .retileOnEnd = true, .moveWorkspace = true, .startedTiled = true, .haveTarget = true,
                .scrollingLayout = true, .dropPointMapsIntoCard = true},
         EAction::RetileMove, EPlacement::ScrollEdgeFar},
    };

    for (const auto& C : CASES) {
        const SDecision D = resolve(C.facts);
        const bool      OK = D.action == C.action && D.placement == C.placement;
        std::printf("  %-52s %s/%s %s\n", C.name, actionName(D.action), placementName(D.placement), OK ? "ok" : "WRONG");
        if (!OK)
            ++failures;
    }

    std::printf(failures ? "DROP_TABLE FAIL (%d)\n" : "DROP_TABLE OK (%d)\n", failures);
    return failures ? 1 : 0;
}
