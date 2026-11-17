#!/usr/bin/env bash
# The host tests: every pure part of the plugin (a header named *Model, *Math,
# *Layout, *Plan, *Rule or *Decision, and the workers and parsers that need no
# compositor), each a binary built from tests/unit/<name>.cpp and run here. A
# test prints its count last; that line is what `make check` shows.
#
# Anything that needs a running Hyprland is checked in a session, not here.
set -u
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
# HOST_TEST_SANITIZE=1 (make check-asan): every test built with AddressSanitizer
# and UndefinedBehaviorSanitizer, and any report fails it.
# A compiler whose sanitizer runtime is not installed (Fedora's g++ without
# libasan/libubsan) is swapped for clang++, which ships its own.
SAN=()
if [ "${HOST_TEST_SANITIZE:-0}" = 1 ]; then
    SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    probe=$(mktemp -d)
    echo 'int main() { return 0; }' > "$probe/p.cpp"
    if ! "$CXX" "${SAN[@]}" -o "$probe/p" "$probe/p.cpp" 2>/dev/null; then
        if command -v clang++ >/dev/null && clang++ "${SAN[@]}" -o "$probe/p" "$probe/p.cpp" 2>/dev/null; then
            echo "host_tests: $CXX has no sanitizer runtime here; using clang++" >&2
            CXX=clang++
        else
            rm -rf "$probe"
            echo "host_tests: no compiler here can build with -fsanitize=address,undefined (install libasan + libubsan, or clang)" >&2
            exit 1
        fi
    fi
    rm -rf "$probe"
    export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0:strict_string_checks=1:detect_stack_use_after_return=1}
    export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
fi
# Every src/<area>/ folder is on the include path, as in the Makefile: the tests
# and the sources name headers bare ("StackModel.hpp"), a module's folder
# (src/apps/identity/) included.
INC=()
for d in src/*/ src/*/*/; do INC+=("-I${d%/}"); done
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
fail=0

run() {
    local name=$1; shift
    if ! "$CXX" -std=c++2b -O2 -Wall "${SAN[@]}" "${INC[@]}" -o "$OUT/$name" "$@" 2>"$OUT/$name.build"; then
        echo "host_tests: $name FAILED TO BUILD" >&2
        cat "$OUT/$name.build" >&2
        fail=1
        return
    fi
    if ! "$OUT/$name" > "$OUT/$name.out" 2>&1; then
        echo "host_tests: $name FAILED" >&2
        cat "$OUT/$name.out" >&2
        fail=1
        return
    fi
    echo "host_tests: $name OK -- $(tail -1 "$OUT/$name.out")"
}

run drop_table_test tests/unit/drop_table_test.cpp
# shellcheck disable=SC2046
run geometry_test tests/unit/geometry_test.cpp src/overview/OverviewGeometry.cpp $(pkg-config --cflags --libs hyprutils)
run strip_scrolling_test tests/unit/strip_scrolling_test.cpp $(pkg-config --cflags --libs hyprutils)
run search_letters_test tests/unit/search_letters_test.cpp
run workspace_plan_test tests/unit/workspace_plan_test.cpp
run strip_layout_test tests/unit/strip_layout_test.cpp
run tool_search_test tests/unit/tool_search_test.cpp
# shellcheck disable=SC2046
run file_uri_test tests/unit/file_uri_test.cpp $(pkg-config --cflags --libs glib-2.0 libsystemd)
run mosaic_pack_test tests/unit/mosaic_pack_test.cpp
run show_desktop_test tests/unit/show_desktop_test.cpp
run x_state_request_test tests/unit/x_state_request_test.cpp
run preview_grid_test tests/unit/preview_grid_test.cpp
run motion_spacing_test tests/unit/motion_spacing_test.cpp
# shellcheck disable=SC2046
run motion_math_test tests/unit/motion_math_test.cpp $(pkg-config --cflags --libs hyprutils)
run plate_color_test tests/unit/plate_color_test.cpp
# shellcheck disable=SC2046
run grid_reflow_test tests/unit/grid_reflow_test.cpp $(pkg-config --cflags --libs hyprutils)
# shellcheck disable=SC2046
run grid_solve_test tests/unit/grid_solve_test.cpp $(pkg-config --cflags --libs hyprutils)
run identity_parsers_test tests/unit/identity_parsers_test.cpp
# shellcheck disable=SC2046
run steam_library_test tests/unit/steam_library_test.cpp src/apps/identity/SteamLibrary.cpp src/apps/identity/Sources.cpp $(pkg-config --cflags --libs cairo)
# shellcheck disable=SC2046
run app_identity_test tests/unit/app_identity_test.cpp src/apps/identity/AppIdentity.cpp src/apps/identity/Resolvers.cpp src/apps/identity/Facts.cpp \
    src/apps/identity/SteamLibrary.cpp src/apps/identity/LutrisLibrary.cpp src/apps/identity/Sources.cpp $(pkg-config --cflags --libs cairo)
# shellcheck disable=SC2046
run identity_engine_test tests/unit/identity_engine_test.cpp src/apps/identity/Engine.cpp src/apps/identity/AppIdentity.cpp src/apps/identity/Resolvers.cpp \
    src/apps/identity/Facts.cpp src/apps/identity/SteamLibrary.cpp src/apps/identity/LutrisLibrary.cpp src/apps/identity/Sources.cpp src/core/Workers.cpp -pthread $(pkg-config --cflags --libs cairo)
# shellcheck disable=SC2046
run identity_live_test tests/unit/identity_live_test.cpp src/apps/identity/ProcFacts.cpp src/apps/identity/AppIdentity.cpp src/apps/identity/Resolvers.cpp \
    src/apps/identity/Facts.cpp src/apps/identity/SteamLibrary.cpp src/apps/identity/LutrisLibrary.cpp src/apps/identity/Sources.cpp $(pkg-config --cflags --libs cairo)
run finger_swipe_test tests/unit/finger_swipe_test.cpp
run debug_gesture_test tests/unit/debug_gesture_test.cpp
run debug_paths_test tests/unit/debug_paths_test.cpp
run workers_test tests/unit/workers_test.cpp src/core/Workers.cpp -pthread
run terminate_log_test tests/unit/terminate_log_test.cpp src/debug/TerminateLog.cpp
run demangle_test tests/unit/demangle_test.cpp
run overview_gesture_decision_test tests/unit/overview_gesture_decision_test.cpp
run hot_corner_test tests/unit/hot_corner_test.cpp
run desktop_switch_test tests/unit/desktop_switch_test.cpp
run row_test tests/unit/row_test.cpp
run desktop_plan_test tests/unit/desktop_plan_test.cpp
run strip_cleanup_test tests/unit/strip_cleanup_test.cpp
run desktop_rows_test tests/unit/desktop_rows_test.cpp
run placement_model_test tests/unit/placement_model_test.cpp
run fullscreen_book_test tests/unit/fullscreen_book_test.cpp
run fullscreen_carry_test tests/unit/fullscreen_carry_test.cpp
run desktop_row_test tests/unit/desktop_row_test.cpp
run desktop_strip_test tests/unit/desktop_strip_test.cpp
run submap_borrow_test tests/unit/submap_borrow_test.cpp
run hover_target_test tests/unit/hover_target_test.cpp
run mosaic_trip_hold_test tests/unit/mosaic_trip_hold_test.cpp
run trip_sizes_test tests/unit/trip_sizes_test.cpp
run trip_frame_rule_test tests/unit/trip_frame_rule_test.cpp
# shellcheck disable=SC2046
run builtin_icons_test tests/unit/builtin_icons_test.cpp src/apps/BuiltinIcons.cpp --embed-dir=. $(pkg-config --cflags --libs librsvg-2.0)
run search_backend_test tests/unit/search_backend_test.cpp src/search/SearchBackend.cpp src/core/Workers.cpp $(pkg-config --cflags --libs hyprutils libsystemd glib-2.0)
# shellcheck disable=SC2046
run search_providers_deadbus_test tests/unit/search_providers_deadbus_test.cpp src/search/SearchProviders.cpp src/core/Workers.cpp $(pkg-config --cflags --libs libsystemd)
run icon_score_test tests/unit/icon_score_test.cpp
# shellcheck disable=SC2046
run icon_size_test tests/unit/icon_size_test.cpp $(pkg-config --cflags --libs cairo)
run image_guard_test tests/unit/image_guard_test.cpp
run native_search_test tests/unit/native_search_test.cpp
# shellcheck disable=SC2046
run glyphs_test tests/unit/glyphs_test.cpp src/platform/Glyphs.cpp $(pkg-config --cflags --libs librsvg-2.0)
# shellcheck disable=SC2046
run fonts_test tests/unit/fonts_test.cpp src/platform/Fonts.cpp src/platform/Glyphs.cpp $(pkg-config --cflags --libs fontconfig)
# shellcheck disable=SC2046
run search_providers_nobus_test tests/unit/search_providers_nobus_test.cpp src/search/SearchProviders.cpp src/core/Workers.cpp $(pkg-config --cflags --libs libsystemd)

# The animation clocks are measured through MotionClock.hpp (msBetween /
# secondsBetween against a `now` the site already holds) or Config's
# realElapsedMs / animElapsedMs -- never a raw std::chrono::duration read, so
# a site's clock kind is visible at the call. The files that animate; their
# raw reads went in the A2 merge, and a new one fails here.
MOTION_CLOCK_FILES=(src/motion/BorderFade.cpp src/motion/FlightBlur.cpp src/motion/Motion.cpp
    src/overview/TileChrome.cpp src/overview/OverviewStrip.cpp src/overview/OverviewCards.cpp src/overview/Ripple.cpp
    src/layout/MosaicGestures.cpp src/layout/MosaicParts.hpp src/layout/FullscreenDesktop.cpp src/layout/ShowDesktop.cpp
    src/desktops/DesktopStrip.cpp src/windows/SpringLoad.cpp src/windows/ScratchpadExit.cpp
    src/search/SearchSession.hpp src/search/SearchLetters.hpp src/search/KeyboardCat.cpp)
if raw=$(grep -n 'std::chrono::duration<' "${MOTION_CLOCK_FILES[@]}"); then
    echo "host_tests: motion_clock_reads FAILED -- a raw duration read in motion code (use MotionClock.hpp):" >&2
    echo "$raw" >&2
    fail=1
else
    echo "host_tests: motion_clock_reads OK -- ${#MOTION_CLOCK_FILES[@]} files, no raw duration read"
fi

exit $fail
