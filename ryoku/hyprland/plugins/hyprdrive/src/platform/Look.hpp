#pragma once

// THE DESKTOP'S LOOK. Every Ryoku surface wears the style and colours the reader
// chose once in Settings > Appearance > Style (docs/lithic/styles.md), and the
// plugin draws in C++, where the Lithic library cannot be linked. So it runs the
// bridge, ryoku-hyprdrive-look --watch, which resolves the choice through the
// library itself and prints it as key=value blocks; this module reads them on
// the event loop and swaps the look in one step. Until the first block arrives
// (or with no bridge at all) it is Lithic's own signature, dark.
//
// A style changes corners and planes, never colours. Lithic cuts its corners at
// 45 degrees; the round styles take Lithic.cornerRadius. Both are drawn through
// Hyprland's rect shader, whose corner is |x|^p + |y|^p = r^p: p = 1 is a cut.

#include <hyprutils/math/Box.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Hyprdrive::Look {

struct SRoles {
    CHyprColor ground, stone1, stone2, stone3, stone4, slab, field, vein, edge;
    CHyprColor fg, fg2, fg3, sel, onSel, gold, onGold, goldInk;
    CHyprColor jade, lapis, amber, jasper, scrim, container, onContainer;
};

// Lithic.planes(): the sidebar, the page and its stroke, a group of rows and its
// stroke. A stroke of alpha 0 is none (the round styles draw no lines).
struct SPlanes {
    CHyprColor side, pane, paneStroke, card, cardStroke;
};

// Lithic.designs[style]: how a cut becomes a corner.
struct SCorner {
    bool  round = false;
    float scale = 1.F;
    float pill  = 0.F;
    float max   = 16.F;
};

// The cut scale (docs/lithic/design.md#5-shape-the-cut-scale), in dp.
struct SCuts {
    float large = 16.F, mid = 7.F, small = 6.F, menu = 8.F, overlay = 8.F, tooltip = 4.F, token = 3.F;
};

struct SLook {
    std::string design = "lithic";
    bool        dark   = true;
    bool        highContrast = false;
    bool        reduceMotion = false;
    SRoles      roles;
    SPlanes     planes;
    SCorner     corner;
    SCuts       cuts;
    std::string sans = "Geologica";
    std::string mono = "Martian Mono";
    std::string fontDir;
};

void init();
void shutdown();

const SLook& current();
// Bumped on every look that differs from the last; caches key on it.
uint64_t generation();

// Lithic.cornerRadius: the corner a w x h shape with this cut wears in the style
// in use. All lengths in the same unit (layout or device pixels).
float corner(float cut, float w, float h);
// The rect shader's power for that corner: 1 cuts, 2 rounds.
float cornerPower();

// One block of the bridge's output parsed onto `into`; keys it does not carry
// leave their field alone. Pure, for the host tests.
void parseBlock(std::string_view block, SLook& into);
SLook signature();

}
