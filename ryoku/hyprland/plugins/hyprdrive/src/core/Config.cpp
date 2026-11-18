#include "ConfigParts.hpp"
#include "MotionClock.hpp"
using namespace ConfigDetail;

namespace ConfigDetail {

Hyprdrive::Config::TGestureRegistrar g_gestureRegistrar   = nullptr;

} // namespace ConfigDetail

namespace {

// ONE registration body, laid down TWICE: once under plugin:hyprdrive: and once
// under the deprecated plugin:scrolloverview: prefix. Registering the alias from
// the same expression is the point -- a hand-copied twin drifts, and a default
// or a range that drifts between the two names is a compat layer that lies.
bool                    g_legacyPass = false;
std::deque<std::string> g_keyNames;   // stable: IValue keeps the char*, it does not copy it

std::vector<SP<::Config::Values::IValue>> g_modernValues, g_legacyValues;

const char* KEY(const std::string_view tail) {
    const auto* PREFIX = g_legacyPass ? Hyprdrive::Compat::LEGACY_KEY_PREFIX : Hyprdrive::Compat::KEY_PREFIX;
    return g_keyNames.emplace_back(std::string{PREFIX} + std::string{tail}).c_str();
}

// A formatted description, kept alive the way KEY keeps names: values keep
// the const char* they are given.
const char* stableText(std::string text) {
    return g_keyNames.emplace_back(std::move(text)).c_str();
}

void addKey(SP<::Config::Values::IValue> value) {
    HyprlandAPI::addConfigValueV2(HYPRDRIVE_HANDLE, value);
    (g_legacyPass ? g_legacyValues : g_modernValues).emplace_back(value);
}

}

namespace ConfigDetail {

// WHICH BINDS ARE OURS (the settings' Keybinds pane, `hyprctl hyprdrive-binds`).
// Under Lua every bind is `__lua` with a registry ref, so `hyprctl binds` cannot
// say what a bind does. Each action built here is a fresh Lua function; its
// identity is remembered with the dispatcher and argument it calls, and a bind's
// ref is looked up against them in the same Lua state. The state is rebuilt on
// every reload (and every bind with it), so both are dropped at preReload.
lua_State*                                                        g_bindLua = nullptr;
std::unordered_map<const void*, std::pair<std::string, std::string>> g_bindActions;

} // namespace ConfigDetail

namespace Hyprdrive::Config {

using namespace ConfigPart;

static void registerConfigValuesFor() {
    using namespace ::Config::Values;

    addKey(makeShared<CIntValue>(KEY("gesture_distance"), "gesture distance in pixels", 200, SIntValueOptions{.min = 1}));
    // MAGNETIC DETENT on the 3-finger workspace drag. The accumulator stays
    // linear in finger travel; only the value handed to viewOffset is warped,
    // by g(u) = u - (K/2pi)*sin(2pi*u) across each card-to-card interval.
    // g(0) = 0 and g(1) = 1 EXACTLY, so a boundary still costs exactly one
    // interval of pad travel -- the curve redistributes motion inside a cell
    // and never adds any. That property is the whole point rather than a nicety:
    // a detent that charged extra travel per workspace would put the far
    // workspaces off the end of a 110mm pad.
    // g'(u) = 1 - K*cos(2pi*u), so at K = 0.35 a card centre moves at 0.65x (it
    // clings) and the midpoint at 1.35x (it releases). Monotonic for |K| < 1,
    // which is why max is 0.9: at K = 1 the derivative touches zero and the
    // cards stall dead under a moving finger.
    addKey(makeShared<CFloatValue>(KEY("gesture_detent"), "magnetic detent strength on the 3-finger workspace drag; 0 = linear", 0.35F,
                                   SFloatValueOptions{.min = 0.F, .max = 0.9F}));
    addKey(makeShared<CIntValue>(KEY("gesture_detent_label"), "show the workspace name pill on the card the workspace drag is settling on", 1,
                                 SIntValueOptions{.min = 0, .max = 1}));
    // Fired once per boundary crossed, through CProcess::runAsync -- never a
    // fork on the compositor thread.
    addKey(makeShared<CStringValue>(KEY("gesture_detent_sound"),
                                    "executable run once per workspace boundary crossed during the drag; empty = silent, missing file = silent",
                                    Hyprlang::STRING{""}));

    // THE GLYPH FONT (A15, P1-6; Glyphs.hpp, Fonts.hpp).
    addKey(makeShared<CStringValue>(KEY("glyph_font"),
                                    "font for menu/search glyphs: auto (an installed Nerd/Symbols font that covers them), builtin (hyprdrive's own drawn icons), or a family name",
                                    Hyprlang::STRING{"auto"}));
    addKey(makeShared<CFloatValue>(KEY("scale"), "overview scale", 0.82F, SFloatValueOptions{.min = 0.1F, .max = 0.9F}));
    // GLOBAL ANIMATION SPEED. 1 = shipped timings, higher = slower, and it
    // reaches EVERYTHING through animElapsedMs: the overview's open/close, the
    // overview, the strip, the search and Mosaic. For watching a frame at a
    // time, and for the stress rig.
    addKey(makeShared<CFloatValue>(KEY("anim_scale"), "global animation slowdown; 1 = normal, 10 = ten times slower, 0 = snap", 1.F,
                                   SFloatValueOptions{.min = 0.F, .max = 100.F}));

    // The overview used to borrow `windowsMove` for its own open and close,
    // which meant one curve for both directions and no way to shape either
    // without also changing how every window on the desk moves. These are in
    // Hyprland's animation units -- tenths of a second, so 3.0 is 300ms.
    //
    // The defaults are the house motion rule: an entrance decelerates, an exit
    // accelerates and is ~25% shorter, and neither overshoots.
    addKey(makeShared<CFloatValue>(KEY("open_speed"), "overview open duration, tenths of a second", 2.6F,
                                   SFloatValueOptions{.min = 0.1F, .max = 20.F}));
    addKey(makeShared<CFloatValue>(KEY("close_speed"), "overview close duration, tenths of a second", 1.9F,
                                   SFloatValueOptions{.min = 0.1F, .max = 20.F}));
    addKey(makeShared<CIntValue>(KEY("workspace_gap"), "gap between overview workspaces", 40, SIntValueOptions{.min = 0}));
    addKey(makeShared<CStringValue>(KEY("layout"), "overview layout", Hyprlang::STRING{"vertical"}));
    // The overview search's dictionary and translator (user, 2026-09-22). OFF by
    // default: they send what is typed to a third-party service, and a
    // compositor plugin must not start doing that on its own.
    addKey(makeShared<CBoolValue>(KEY("search:dictionary"), "`define <word>` in the overview search looks the word up on Wiktionary (sends the word to en.wiktionary.org)", false));
    // A FULLSCREEN APP GETS ITS OWN DESKTOP (FullscreenDesktop.hpp, user 2026-11-05).
    addKey(makeShared<CStringValue>(KEY("general:fullscreen_desktop"),
                                    "where an app going fullscreen goes: next (a desktop of its own right after yours; the ones after move along), free (the next free "
                                    "desktop) or stay (where it is). An app alone on its desktop stays in any case",
                                    Hyprlang::STRING{"free"}));
    addKey(makeShared<CStringValue>(KEY("general:file_manager"),
                                    "the file manager folders open in: a desktop entry such as org.kde.dolphin.desktop, or empty for the system's default for folders",
                                    Hyprlang::STRING{""}));
    // WINDOWS FLOAT ONLY WHEN YOU FLOAT THEM (WindowPlacement.cpp; owner,
    // 2026-11-10/11: "add an option to never load windows in floating mode to
    // general"; off by default, always on on Mosaic desktops).
    // THE BAR STRIP OVER A SHOWN SCRATCHPAD CAN BE TURNED OFF (inumineq, PR
    // #13, 2026-11-12: keyboard-only scratchpad users had the strip on their
    // bar for good, and the dwell fired on the way to the clock). On, as it
    // was. The drag zone is not this key's: it is the only way to drop a file
    // outside the scratchpad.
    addKey(makeShared<CBoolValue>(KEY("general:scratchpad_bar_exit"),
                                  "while a scratchpad is shown, stripe the bar with 'hover to exit scratchpad' and hide the scratchpad on a dwell there", true));
    addKey(makeShared<CBoolValue>(KEY("general:open_tiled"),
                                  "windows float only when you float them: a window the app opens floating (a dialog, a fixed-size window, Wine and Proton's too) is "
                                  "tiled, on every desktop -- Mosaic desktops always do this; float rules, the hyprdrive-float tag, splash screens and tool palettes "
                                  "still float",
                                  false));
    addKey(makeShared<CBoolValue>(KEY("mosaic:enabled"), "mosaic: the Mosaic layout is offered: the layout cycle goes Dwindle -> Scrolling -> Mosaic", true));
    addKey(makeShared<CBoolValue>(KEY("mosaic:default"), "mosaic: new desktops use the Mosaic layout (needs mosaic:enabled)", true));
    // THE INFINITE CANVAS (MosaicCanvas.hpp, user 2026-10-26). Opt-in; once on, all
    // directions and the Super zoom-out by default (user, 2026-10-27).
    addKey(makeShared<CBoolValue>(KEY("mosaic:infinite_canvas"),
                                  "mosaic: windows that no longer fit the screen spill onto a canvas you pan across, at their own sizes, instead of tiling", true));
    // WINDOWS YOU DID NOT ASK FOR (Intent.hpp, user 2026-11-04).
    addKey(makeShared<CBoolValue>(KEY("mosaic:quiet_new_windows"),
                                  "mosaic: a window you did not open (a loading screen, an app reopening itself) opens without taking the focus, and the pointer stays on your window",
                                  true));
    addKey(makeShared<CBoolValue>(KEY("mosaic:spawn_at_cursor"),
                                  "mosaic: a new window opens where the pointer is, the others packed around it, on the mosaic and the infinite canvas", true));
    // PUSH THROUGH THE EDGE (Mosaic.cpp pushThrough; user, 2026-11-09).
    addKey(makeShared<CBoolValue>(KEY("mosaic:edge_push"),
                                  "mosaic: on the infinite canvas, pushing the pointer firmly against a screen edge slides the canvas to show the windows hidden past it",
                                  true));
    addKey(makeShared<CIntValue>(KEY("mosaic:edge_push_resistance"),
                                 "mosaic: how far (px) the pointer must keep pushing into the edge before the canvas gives way (the bottom edge takes half again "
                                 "as much); lower = easier",
                                 400, SIntValueOptions{.min = 50, .max = 1500}));
    addKey(makeShared<CStringValue>(KEY("mosaic:canvas_directions"),
                                    "mosaic: the infinite canvas grows horizontal (a strip) or all (a grid); on "
                                    "either, three fingers down (or Super and a pointer move, with mosaic:super_zoom) zoom out to every window, hover one to focus it",
                                    Hyprlang::STRING{"all"}));
    // THE SUPER ZOOM IS A CHOICE. Holding Super and moving the pointer zooms
    // the canvas out; under Ryoku, Super+drag moves windows and every shortcut
    // holds Super, so the move zoomed out by accident: a switch of its own, with a
    // distance to make an accidental zoom rarer; three fingers down zoom either way.
    addKey(makeShared<CBoolValue>(KEY("mosaic:super_zoom"),
                                  "mosaic: on the infinite canvas, holding Super and moving the pointer zooms out to every window (three fingers down zoom out either way)",
                                  true));
    addKey(makeShared<CStringValue>(KEY("mosaic:zoom_key"),
                                    "mosaic: the chord held for the canvas zoom (and the titles it shows): modifiers, a key or both, such as SUPER + ALT, SUPER + Z or F13; it zooms out the moment it is held", Hyprlang::STRING{"SUPER + ALT"}));
    addKey(makeShared<CIntValue>(KEY("mosaic:super_zoom_distance"),
                                 "mosaic: how far (px) the pointer moves with Super held before the canvas zooms out and the titles show", 32,
                                 SIntValueOptions{.min = 8, .max = 400}));
    // RETIRED (user, 2026-10-29). Still registered so a config or settings-panel
    // fragment that sets it loads; nothing reads it (mosaic:super_zoom does).
    addKey(makeShared<CStringValue>(KEY("mosaic:canvas_super_move"), "mosaic: retired and ignored (see mosaic:super_zoom)",
                                    Hyprlang::STRING{"zoom"}));
    // Window titles while Super is held (user, 2026-10-30): any desktop, any
    // layout; kept in the Mosaic settings.
    addKey(makeShared<CStringValue>(KEY("mosaic:zoom_labels"),
                                    "window titles while Super is held and the pointer moves, on any desktop (the zoomed-out canvas too): none, hover (the window "
                                    "under the pointer) or all",
                                    Hyprlang::STRING{"hover"}));
    addKey(makeShared<CBoolValue>(KEY("search:browse_on_click"),
                                  "clicking the overview's search pill with nothing typed opens the results: the menu, folder shortcuts and AI & Tools rows", false));
    addKey(makeShared<CBoolValue>(KEY("search:easter_egg"), "overview search: type a letter and backspace five times in four seconds and the pill plays search:easter_egg_video (off unless you turn it on)", false));
    addKey(makeShared<CStringValue>(KEY("search:easter_egg_video"), "the video the overview's easter egg plays, with its sound (needs ffmpeg); a missing file turns the egg off",
                                    Hyprlang::STRING{"~/.local/share/hyprdrive/easter-egg.mp4"}));
    addKey(makeShared<CBoolValue>(KEY("search:translate"), "`tr <lang> <text>` in the overview search translates it (sends the text to the translate endpoint)", false));
    addKey(makeShared<CStringValue>(KEY("search:translate_endpoint"), "LibreTranslate-compatible base URL for `tr`; empty uses the free MyMemory API",
                                    Hyprlang::STRING{""}));
    addKey(makeShared<CStringValue>(KEY("search:translate_target"), "language `translate <text>` translates into when none is given; empty uses $LANG",
                                    Hyprlang::STRING{""}));
    addKey(makeShared<CStringValue>(KEY("terminal"), "terminal used to launch Terminal=true desktop entries; empty falls back to $TERMINAL",
                                    Hyprlang::STRING{""}));
    // The terminal an AI agent row opens in. A command the agent's argv is appended to ("kitty", "foot --app-id ai"),
    // or a line with {cmd} where it goes ("wezterm start --cwd ~ -- {cmd}").
    // Empty detects: plugin:hyprdrive:terminal, $TERMINAL, xdg-terminal-exec,
    // then kitty, foot, alacritty, ghostty, wezterm, konsole, gnome-terminal,
    // ptyxis -- each told to run a program its own way (NativeSearch.hpp).
    addKey(makeShared<CStringValue>(KEY("search:terminal"),
                                    "terminal AI agent rows open in: a command the agent is appended to, or one with {cmd}; empty = plugin:hyprdrive:terminal, $TERMINAL, xdg-terminal-exec, kitty, foot, alacritty...",
                                    Hyprlang::STRING{""}));
    addKey(makeShared<CIntValue>(KEY("input:scroll_event_delay"), "minimum delay (ms) between discrete scroll steps (wheel workspace nav and trackpad focus stepping)", 200, SIntValueOptions{.min = 0}));
    addKey(makeShared<CFloatValue>(KEY("input:touchpad_scroll_factor"), "overview touchpad scroll factor", 1.F,
                                   SFloatValueOptions{.min = 0.F}));
    addKey(makeShared<CIntValue>(KEY("input:left_handed"), "overview left handed mouse buttons, 2 follows input:left_handed", 2,
                                 SIntValueOptions{.min = 0, .max = 2}));
    addKey(makeShared<CIntValue>(KEY("input:scrolling_mode"), "overview mouse wheel behavior", 0,
                                 SIntValueOptions{.min = 0, .max = 3}));
    addKey(makeShared<CIntValue>(KEY("input:drag_mode"), "overview mouse drag behavior", 0,
                                 SIntValueOptions{.min = 0, .max = 1}));
    addKey(makeShared<CIntValue>(KEY("input:drag_threshold"), "overview drag threshold", 10,
                                 SIntValueOptions{.min = 0}));
    addKey(makeShared<CFloatValue>(KEY("drag_mini_scale"),
                                   "size of the drag proxy while it hovers another workspace, as a fraction of its tile size", 0.45F,
                                   SFloatValueOptions{.min = 0.05F, .max = 1.F}));
    addKey(makeShared<CIntValue>(KEY("drag_mini_ms"), "ms for the drag proxy to shrink to, or grow back from, mini scale", 150,
                                 SIntValueOptions{.min = 0}));
    addKey(makeShared<CIntValue>(KEY("drag_recentre_ms"),
                                 "ms for the drag proxy's grab point to slide to its centre while over a drop target", 160,
                                 SIntValueOptions{.min = 0}));
    // Upstream scrolloverview's modes were 0 global only, 1 per-workspace only,
    // 2 both. Since E8-c (3933dd0) every card carries the wallpaper itself
    // (cardWallpaperFB, rounded in the shader), so "global only" no longer
    // exists and 2 renders exactly as 0: kept so an old config stays valid.
    addKey(makeShared<CIntValue>(KEY("wallpaper"), "wallpaper behind the cards: 0 blurred and dimmed, 1 black, 2 same as 0; the cards always show it", 0,
                                 SIntValueOptions{.min = 0, .max = 2}));
    // Kept so an old `blur = ...` line still parses; nothing reads it (the frost
    // was always on). plugin:hyprdrive:backdrop picks what it shows.
    addKey(makeShared<CBoolValue>(KEY("blur"), "unused: see plugin:hyprdrive:backdrop", false));
    addKey(makeShared<CStringValue>(KEY("backdrop"),
                                    "what the overview blurs behind the desktop cards: off (the wallpaper darkened, no blur), wallpaper (the static "
                                    "wallpaper), snapshot (a live wallpaper, frozen as the overview opens), live (re-blurred as it moves), auto (live "
                                    "while it changes rarely, snapshot while it animates)",
                                    Hyprlang::STRING{"off"}));

    addKey(makeShared<CBoolValue>(KEY("drag:spring_edges"),
                                  "a file/folder/window drag held at the left/right screen edge switches desktop that way every spring_delay_ms (a new one past the last)",
                                  true));
    addKey(makeShared<CIntValue>(KEY("drag:spring_delay_ms"), "how long a drag is held at a screen edge before it springs", 1500,
                                 SIntValueOptions{.min = 200, .max = 10000}));
    // Hold to swap and the hot corner.
    // Overview.cpp (updateSwapHold), HotCorner.cpp.
    addKey(makeShared<CBoolValue>(KEY("drag:hold_to_swap"),
                                  "in the overview, a window dragged and held over another window (in the grid or the desktop strip) for hold_to_swap_ms swaps "
                                  "places with it when let go there",
                                  true));
    addKey(makeShared<CIntValue>(KEY("drag:hold_to_swap_ms"), "how long a dragged window is held over another before letting go swaps them", 1500,
                                 SIntValueOptions{.min = 300, .max = 5000}));
    addKey(makeShared<CBoolValue>(KEY("hot_corner:enabled"), "resting the pointer in a screen corner opens the overview, and closes it again, like GNOME's Activities corner", false));
    addKey(makeShared<CStringValue>(KEY("hot_corner:corner"),
                                    "the hot corner: top-left, top-right, bottom-left or bottom-right (an outer corner only; between two monitors the pointer crosses)",
                                    Hyprlang::STRING{"top-left"}));
    addKey(makeShared<CIntValue>(KEY("hot_corner:delay_ms"), "how long the pointer rests in the hot corner before the overview opens", 150,
                                 SIntValueOptions{.min = 0, .max = 2000}));
    addKey(makeShared<CBoolValue>(KEY("hot_corner:ripple"), "the hot corner sends a ripple across the screen from the corner, bending what is under it", true));
    addKey(makeShared<CBoolValue>(KEY("gestures:keep_swipe"),
                                  "a 3/4-finger swipe keeps going after fingers lift, as long as one finger that started it is down (reads the touchpad's own touches)",
                                  true));
    addKey(makeShared<CBoolValue>(KEY("gestures:swipe_while_dragging"),
                                  "with one finger holding a drag (button down), 3/4 other fingers swipe as a normal gesture instead of flinging the drag", true));

    addKey(makeShared<CIntValue>(KEY("layout_motion_blur"),
                                 "motion blur samples while the layout moves or resizes a window -- a dwindle retile, the scrolling tape, a window opening (0 = off); never an interactive drag", 16,
                                 SIntValueOptions{.min = 0, .max = 32}));
    // Motion.hpp.
    addKey(makeShared<CStringValue>(KEY("animation:curve"),
                                    "the curve family for everything hyprdrive animates: hyprdrive (the house curves), serpantinum (its window curve), serpantinum_glide, serpantinum_soft, serpantinum_intro, serpantinum_pop, swift, springy, linear",
                                    Hyprlang::STRING{"serpantinum"}));
    // THE INFINITE CANVAS'S MOTION (Mosaic.cpp canvasEase, user 2026-10-26):
    // focus panning a window into view, the Super zoom.
    addKey(makeShared<CStringValue>(KEY("animation:canvas_curve"),
                                    "the infinite canvas's pans and zoom: a curve family as animation:curve names them (its travel curve); hyprdrive (the house curve, no overshoot) by default",
                                    Hyprlang::STRING{"hyprdrive"}));
    addKey(makeShared<CIntValue>(KEY("animation:canvas_pan_ms"), "the infinite canvas's pans and zoom, in ms (scaled by anim_scale; 0 = instant)", 420,
                                 SIntValueOptions{.min = 0, .max = 2000}));
    // DESKTOP SWITCHES (DesktopSwitch.hpp, user 2026-10-15). Super+scroll walks
    // the desktops by number, empty ones included; 50-desktop-scroll.lua binds it
    // (replacing whatever had <mods>+mouse_down/up) while this is on.
    addKey(makeShared<CBoolValue>(KEY("binds:scroll_desktops"),
                                  "<mods>+scroll wheel moves through the desktops on the focused monitor, empty ones included, no wrap -- the row of desktops slides with the wheel", true));
    addKey(makeShared<CStringValue>(KEY("binds:scroll_desktops_mods"), "the modifiers for binds:scroll_desktops, e.g. SUPER or SUPER + ALT",
                                    Hyprlang::STRING{"SUPER"}));
    // THE DESKTOP STRIP (DesktopStrip.hpp, DesktopRowModel.hpp; user,
    // 2026-11-02): how the wheel moves the row and which desktops are in it.
    addKey(makeShared<CBoolValue>(KEY("binds:scroll_desktops_smooth"),
                                  "the row of desktops follows the wheel continuously (two notches a desktop) and settles on the nearest once the wheel stops; false = a desktop per binds:scroll_desktops_ticks notches",
                                  false));
    addKey(makeShared<CIntValue>(KEY("binds:scroll_desktops_ticks"),
                                 "wheel notches per desktop while binds:scroll_desktops_smooth is off; a notch short shows as a peek toward the next desktop", 1,
                                 SIntValueOptions{.min = 1, .max = 10}));
    addKey(makeShared<CBoolValue>(KEY("binds:scroll_desktops_sticky"),
                                  "after the first desktop of a quick scroll, each further one takes more push (two notches more, or a short dead zone when smooth); let go after 0.4 s still",
                                  false));
    addKey(makeShared<CBoolValue>(KEY("binds:scroll_desktops_stop_at_empty"),
                                  "the wheel stops at the first empty desktop past the last one with windows, as the overview does; false = it goes on to 10 or the highest desktop",
                                  true));
    addKey(makeShared<CBoolValue>(KEY("binds:scroll_desktops_populated_only"),
                                  "the wheel visits only desktops with windows, the first empty one after them and the one shown; false = every desktop number between", false));
    // Desktop switches by cause: off makes that kind of switch instant (no
    // slide or fade), the other kind unaffected. A no-op while Hyprland's
    // workspaces animation is off (Omarchy's default).
    addKey(makeShared<CBoolValue>(KEY("animation:desktop_keyboard"),
                                  "animate desktop switches made by a key or scroll bind (Super+1..0, Super+Tab, Super+scroll, move to desktop and follow)", true));
    addKey(makeShared<CBoolValue>(KEY("animation:desktop_focus"),
                                  "animate desktop switches made by focus moving to a window on another desktop (an app asking for focus, a notification, focuswindow)",
                                  true));
    addKey(makeShared<CBoolValue>(KEY("animation:desktop_row_bounce"),
                                  "the row of desktops stretches a little past its first and last desktop and springs back", true));
    addKey(makeShared<CBoolValue>(KEY("animation:desktop_jump_through"),
                                  "a key jump of several desktops (Super+5 from desktop 1) slides through the desktops between", true));
    // MOTION BLUR, PER ANIMATION (user, 2026-09-30). Each site smears a share
    // of its frame-to-frame motion (strength, 0..1; 0 = that site off) shaped
    // over the animation by `fade`: early (full at the start, gone by the
    // halfway point -- the house default), full (fades over the whole
    // animation), late (holds, then fades over the second half), linear, or
    // constant (no fade; the continuous motions -- swipes, keyboard pans --
    // have no progress to fade over). The defaults are the values these sites
    // shipped with. Sample counts are layout_motion_blur and motion_blur:samples.
    addKey(makeShared<CBoolValue>(KEY("motion_blur:enabled"), "motion blur on hyprdrive's animations (false = every site off; samples keys untouched)", true));
    addKey(makeShared<CIntValue>(KEY("motion_blur:samples"), "motion blur samples for the overview, the search and the drag proxy (0 = off)", 16,
                                 SIntValueOptions{.min = 0, .max = 32}));
    for (const auto& SITE : FlightBlur::SITES) {
        addKey(makeShared<CFloatValue>(KEY(std::format("motion_blur:{}:strength", SITE.name)), stableText(std::format("motion blur: {} -- share of the motion smeared (0 = off)", SITE.label)),
                                       SITE.strength, SFloatValueOptions{.min = 0.F, .max = 1.F}));
        addKey(makeShared<CStringValue>(KEY(std::format("motion_blur:{}:fade", SITE.name)), stableText(std::format("motion blur: {} -- early, full, late, linear or constant", SITE.label)),
                                        Hyprlang::STRING{SITE.fade}));
    }

    addKey(makeShared<CBoolValue>(KEY("live_layer_frost"),
                                  "evaluate the bar's frost against the overview backdrop instead of the stale optimized blur FB", true));
    addKey(makeShared<CBoolValue>(KEY("smooth_frames"),
                                  "render ahead when the GPU runs late: holds render:new_render_scheduling on for the session and restores yours when off; a late frame then costs one refresh, not half the frame rate", true));
    addKey(makeShared<CFloatValue>(KEY("prefilter_below"),
                                   "mipmap-prefilter a tile's texture when it is minified below this fraction (0 = never)", 0.55F,
                                   SFloatValueOptions{.min = 0.F, .max = 1.F}));
    addKey(makeShared<CBoolValue>(KEY("mission_control"), "lay the focused workspace out as a mission-control grid", true));
    addKey(makeShared<CIntValue>(KEY("mission_control_gap"), "gap between mission-control grid tiles", 40, SIntValueOptions{.min = 0}));
    addKey(makeShared<CIntValue>(KEY("mission_control_reflow_ms"), "ms for a grid tile to travel to a new slot", 300, SIntValueOptions{.min = 0}));
    // The default: Config.hpp OVERVIEW_TILE_ORDER_DEFAULT, spelled here as the
    // literal docs/CONFIG.md and the settings read (config_defaults_test).
    addKey(makeShared<CStringValue>(KEY("overview_tile_order"),
                                    "the order of the overview grid's tiles, top left to bottom right: recent (the most recently used window first), created (the "
                                    "order the windows were opened) or position (as the windows sit on the desktop); taken as the overview opens",
                                    Hyprlang::STRING{"position"}));
    addKey(makeShared<CBoolValue>(KEY("strip"), "workspace thumbnail strip across the top of the overview", true));
    addKey(makeShared<CIntValue>(KEY("strip_height"), "logical px the strip band reserves at the top of the workspace", 260,
                                 SIntValueOptions{.min = 0}));
    addKey(makeShared<CIntValue>(KEY("strip_max_screen_pct"), "ceiling on the strip band as a percent of screen height, so a strip cell never dwarfs a window preview", 30,
                                 SIntValueOptions{.min = 1, .max = 100}));
    addKey(makeShared<CIntValue>(KEY("strip_gap"), "gap between strip cells", 5, SIntValueOptions{.min = 0}));
    addKey(makeShared<CBoolValue>(KEY("strip_cleanup_button"),
                                  "hold Super over the overview's desktop strip: a Clean Up Empty Desktops button under it removes that monitor's empty desktops between the others", true));
    addKey(makeShared<CBoolValue>(KEY("tap_super"), "tapping SUPER alone toggles the overview", false));
    addKey(makeShared<CIntValue>(KEY("tap_super_timeout_ms"), "longest press-to-release still counted as a SUPER tap (0 = no limit)",
                                 250, SIntValueOptions{.min = 0}));
    addKey(makeShared<CBoolValue>(KEY("shadow:enabled"), "draw a shadow around each workspace card", false));
    addKey(makeShared<CIntValue>(KEY("shadow:range"), "workspace card shadow range", -1));
    addKey(makeShared<CIntValue>(KEY("shadow:render_power"), "workspace card shadow render power", -1));
    addKey(makeShared<CGradientValue>(KEY("shadow:color"), "workspace card shadow color", -1));
}

// Both passes lay the SAME keys down in the SAME order, so entry i of one list
// is the alias of entry i of the other and the pairing needs no name matching.
static void registerConfigValues() {
    g_legacyPass = false;
    registerConfigValuesFor();

    g_legacyPass = true;          // ONE RELEASE
    registerConfigValuesFor();
    g_legacyPass = false;

    for (size_t i = 0; i < g_modernValues.size() && i < g_legacyValues.size(); ++i)
        Hyprdrive::Compat::pairKey(g_modernValues[i]->name(), g_legacyValues[i]->name(), g_modernValues[i]->underlying());
}

void registerConfig() {
    registerLuaFunctions();
    registerConfigValues();
    g_pluginKeysRegistered = true;
    HyprlandAPI::reloadConfig();
}

int getGestureDistance() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:gesture_distance");
    return std::max<int>(1, sc<int>(*VAL));
}

float getGestureDetent() {
    // Clamped again here rather than trusted from the key: |K| >= 1 makes the
    // warp non-monotonic, and a non-monotonic warp means the cards run backwards
    // mid-drag. Cheap insurance against a hand-edited hyprland.conf.
    static auto& VAL = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:gesture_detent");
    return std::clamp(sc<float>(*VAL), 0.F, 0.9F);
}

bool getGestureDetentLabel() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:gesture_detent_label");
    return *VAL != 0;
}

std::string getGestureDetentSound() {
    auto path = getValue<std::string>("plugin:hyprdrive:gesture_detent_sound");

    // Expanded here rather than at the call site: CProcess exec's the path
    // directly, so there is no shell downstream to expand a ~ later, and the
    // default has to be able to name a file in the user's home without the
    // plugin carrying an absolute path for one machine.
    if (path.starts_with("~/")) {
        if (const auto* HOME = std::getenv("HOME"); HOME && *HOME)
            path = std::string{HOME} + path.substr(1);
        else
            return {};
    }

    return path;
}

namespace ConfigPart {
// Bumped by invalidateThemeCache(); every cache below re-checks when the number
// it last saw is stale. A counter rather than a flag because there is more than
// one cache and each has to see the invalidation exactly once.
uint64_t g_themeGeneration = 1;

} // namespace ConfigPart

float getScale() {
    static auto& VAL = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:scale");
    return std::clamp(sc<float>(*VAL), 0.1F, 0.9F);
}

float getOpenSpeed() {
    static auto& VAL = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:open_speed");
    return std::max(0.1F, sc<float>(*VAL));
}

float getCloseSpeed() {
    static auto& VAL = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:close_speed");
    return std::max(0.1F, sc<float>(*VAL));
}

int getWorkspaceGap() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:workspace_gap");
    return std::max<int>(0, sc<int>(*VAL));
}

bool getSearchDictionary() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:search:dictionary");
    return *VAL != 0;
}

bool getSearchBrowseOnClick() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:search:browse_on_click");
    return *VAL != 0;
}

bool getSearchTranslate() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:search:translate");
    return *VAL != 0;
}

std::string getSearchTranslateEndpoint() {
    return getValue<std::string>("plugin:hyprdrive:search:translate_endpoint");
}

std::string getSearchTranslateTarget() {
    return getValue<std::string>("plugin:hyprdrive:search:translate_target");
}

std::string getTerminal() {
    return getValue<std::string>("plugin:hyprdrive:terminal");
}

std::string getSearchTerminal() {
    return getValue<std::string>("plugin:hyprdrive:search:terminal");
}

ELayout getLayout() {
    static auto& VAL    = valueRef<std::string>("plugin:hyprdrive:layout");
    const auto   LAYOUT = *VAL;
    return LAYOUT == "horizontal" ? ELayout::HORIZONTAL : ELayout::VERTICAL;
}

bool getLeftHanded() {
    static auto& VAL         = valueRef<Hyprlang::INT>("plugin:hyprdrive:input:left_handed");
    const auto   LEFT_HANDED = sc<int>(*VAL);
    if (LEFT_HANDED <= 1)
        return LEFT_HANDED != 0;

    static auto& HOST = valueRef<Hyprlang::INT>("input:left_handed");
    return *HOST != 0;
}

int getDragMode() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:input:drag_mode");
    return std::clamp<int>(sc<int>(*VAL), 0, 1);
}

int getDragThreshold() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:input:drag_threshold");
    return std::max<int>(0, sc<int>(*VAL));
}

// Forwarders to the one animation clock, motion/MotionClock.hpp.
float animScale() {
    return Motion::Clock::scale();
}

bool motionEnabled() {
    return Motion::Clock::enabled();
}

float realElapsedMs(const Time::steady_tp& since) {
    return Motion::Clock::realMs(since);
}

float animElapsedMs(const Time::steady_tp& since) {
    return Motion::Clock::animMs(since);
}

float getTouchpadScrollFactor() {
    static constexpr float OVERVIEWTOUCHPADSCROLLFACTOR = 1.5F;

    static auto& HOST = valueRef<Hyprlang::FLOAT>("input:touchpad:scroll_factor");
    static auto& OURS = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:input:touchpad_scroll_factor");

    return OVERVIEWTOUCHPADSCROLLFACTOR * std::max<float>(0.F, sc<float>(*HOST)) * std::max<float>(0.F, sc<float>(*OURS));
}

static EScrollAction defaultVerticalScrollAction(ELayout layout) {
    return layout == ELayout::HORIZONTAL ? EScrollAction::COLUMN : EScrollAction::WORKSPACE;
}

EScrollAction getVerticalScrollAction(ELayout layout) {
    static auto& VAL  = valueRef<Hyprlang::INT>("plugin:hyprdrive:input:scrolling_mode");
    const auto   MODE = std::clamp<int>(sc<int>(*VAL), 0, 3);

    switch (MODE) {
        case 1: return defaultVerticalScrollAction(layout) == EScrollAction::WORKSPACE ? EScrollAction::COLUMN : EScrollAction::WORKSPACE;
        case 2: return EScrollAction::WORKSPACE;
        case 3: return EScrollAction::COLUMN;
        case 0:
        // ponytail: mission control draws a grid, not scrolling columns, so COLUMN
        // has nothing to select there - the vertical wheel is only useful as a
        // desktop switch. Explicit scrolling_mode above still overrides this.
        default: return getMissionControl() ? EScrollAction::WORKSPACE : defaultVerticalScrollAction(layout);
    }
}

EScrollAction getHorizontalScrollAction(ELayout layout) {
    return getVerticalScrollAction(layout) == EScrollAction::WORKSPACE ? EScrollAction::COLUMN : EScrollAction::WORKSPACE;
}

int getScrollEventDelay() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:input:scroll_event_delay");
    return std::max<int>(0, sc<int>(*VAL));
}

int getWallpaperMode() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:wallpaper");
    return std::clamp<int>(sc<int>(*VAL), 0, 2);
}

EBackdrop getBackdrop() {
    static auto& VAL = valueRef<std::string>("plugin:hyprdrive:backdrop");
    const auto&  V   = *VAL;
    if (V == "off")
        return EBackdrop::OFF;
    if (V == "wallpaper")
        return EBackdrop::WALLPAPER;
    if (V == "live")
        return EBackdrop::LIVE;
    if (V == "auto")
        return EBackdrop::AUTO;
    return EBackdrop::SNAPSHOT;
}

bool getMissionControl() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:mission_control");
    return *VAL != 0;
}

int getMissionControlGap() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:mission_control_gap");
    return std::max<int>(0, sc<int>(*VAL));
}

int getMissionControlReflowMs() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:mission_control_reflow_ms");
    return std::max<int>(0, sc<int>(*VAL));
}

ETileOrder getOverviewTileOrder() {
    static auto& VAL = valueRef<std::string>("plugin:hyprdrive:overview_tile_order");
    const auto&  V   = *VAL;
    if (V == "recent")
        return ETileOrder::RECENT;
    if (V == "created")
        return ETileOrder::CREATED;
    if (V == "position")
        return ETileOrder::POSITION;
    return OVERVIEW_TILE_ORDER_DEFAULT;
}

bool getStrip() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:strip");
    return *VAL != 0;
}

int getStripHeight() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:strip_height");
    return std::max<int>(0, sc<int>(*VAL));
}

int getStripMaxScreenPct() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:strip_max_screen_pct");
    return std::clamp<int>(sc<int>(*VAL), 1, 100);
}

int getStripGap() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:strip_gap");
    return std::max<int>(0, sc<int>(*VAL));
}

float getDragMiniScale() {
    static auto& VAL = valueRef<Hyprlang::FLOAT>("plugin:hyprdrive:drag_mini_scale");
    return std::clamp(sc<float>(*VAL), 0.05F, 1.F);
}

int getDragMiniMs() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:drag_mini_ms");
    return std::max<int>(0, sc<int>(*VAL));
}

int getDragRecentreMs() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:drag_recentre_ms");
    return std::max<int>(0, sc<int>(*VAL));
}

bool getScrollDesktops() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:binds:scroll_desktops");
    return *VAL != 0;
}

std::string getScrollDesktopsMods() {
    auto mods = getValue<std::string>("plugin:hyprdrive:binds:scroll_desktops_mods");
    return mods.empty() || mods == "[[EMPTY]]" ? std::string{"SUPER"} : mods;
}

bool getAnimateDesktopKeyboard() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:animation:desktop_keyboard");
    return *VAL != 0;
}

bool getAnimateDesktopFocus() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:animation:desktop_focus");
    return *VAL != 0;
}

bool getTapSuper() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:tap_super");
    return *VAL != 0;
}

int getTapSuperTimeoutMs() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:tap_super_timeout_ms");
    return std::max<int>(0, sc<int>(*VAL));
}

::Config::CCssGapData getCssGapData(const std::string& name) {
    auto& VALUE = valueRef<::Config::IComplexConfigValue>(name);
    if (!VALUE.good())
        return {};

    auto* const GAPS = dc<::Config::CCssGapData*>(VALUE.ptr());
    if (!GAPS)
        return {};

    return *GAPS;
}

int getShadowEnabled() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:shadow:enabled");
    return *VAL != 0 ? 1 : 0;
}

int getShadowRange() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:shadow:range");
    return sc<int>(*VAL);
}

int getShadowRenderPower() {
    static auto& VAL = valueRef<Hyprlang::INT>("plugin:hyprdrive:shadow:render_power");
    return sc<int>(*VAL);
}

// By pointer, not by optional-of-copy: the card shadow asks for this once per
// card and once per strip cell, and a CGradientValueData copy is a heap
// allocation. nullptr means "not pinned" exactly as std::nullopt did.
const ::Config::CGradientValueData* getShadowColor() {
    constexpr auto NAME = "plugin:hyprdrive:shadow:color";

    // 0.57: setByUser is a `const bool*`; the pointer alone is always true.
    if (const auto* SET = ::Config::mgr()->getConfigValue(NAME).setByUser; !SET || !*SET)
        return nullptr;

    static auto& VAL = valueRef<::Config::IComplexConfigValue>(NAME);
    return gradientPtr(VAL);
}

}
