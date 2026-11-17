// Host test for HoverTargetRule.hpp: when a Super+key action that names no
// window acts on the overview's hovered tile instead of the focused one.
#include "HoverTargetRule.hpp"

#include <cstdio>

using namespace Hyprdrive::HoverTarget;

// named, namedFocusedInBind, ownCall, hasTarget, targetIsFocused
static_assert(retarget(false, false, false, true, false));  // Super+F over another window's tile or preview
static_assert(!retarget(false, false, false, true, true));  // over the focused window's own: as before
static_assert(!retarget(false, false, false, false, false)); // no target: the focused window
static_assert(!retarget(true, false, false, true, false));  // a window named (the dock's chip, the pill): the caller's
static_assert(retarget(true, true, false, true, false));    // a bind naming the active window (Omarchy's move_window)
static_assert(!retarget(true, true, true, true, false));    // ... from the plugin's own dispatcher: plugin code names its window
static_assert(!retarget(false, false, true, true, false));  // nested in an action already retargeted, or a hyprdrive dispatcher

// retargeted, silent
static_assert(moveSilently(true, false));  // a hovered window moved by Super+Shift+N: no follow
static_assert(!moveSilently(false, false)); // the focused one: the bind's follow
static_assert(moveSilently(false, true));

int main() {
    int checks = 0, failed = 0;
    for (int m = 0; m < 16; ++m) {
        const bool A = m & 1, B = m & 2, C = m & 4, D = m & 8;
        for (int f = 0; f < 2; ++f) {
            ++checks;
            const bool NAMED = A, STANDIN = B, OWN = C, TARGET = D, FOCUSED = f;
            if (retarget(NAMED, STANDIN, OWN, TARGET, FOCUSED) != ((!NAMED || STANDIN) && !OWN && TARGET && !FOCUSED)) {
                ++failed;
                std::printf("FAIL retarget named=%d standin=%d own=%d target=%d focused=%d\n", NAMED, STANDIN, OWN, TARGET, FOCUSED);
            }
        }
        if (m < 4) {
            ++checks;
            if (moveSilently(A, B) != (A || B)) {
                ++failed;
                std::printf("FAIL moveSilently %d %d\n", A, B);
            }
        }
    }
    std::printf("hover_target_test: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
