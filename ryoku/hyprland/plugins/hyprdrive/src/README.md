# The source, by area

One compiled plugin (`hyprdrive.so`). Each folder is one subsystem; every
`#include` names a file bare, so a file can move between folders without an edit.

| folder | what lives there |
| --- | --- |
| `core/` | plugin entry (`main.cpp`), the settings registry (`Config`), names kept for old configs (`Compat`), small shared helpers |
| `overview/` | the overview: its window grid, cards, hit-testing, input, rendering and frame policy |
| `search/` | type-to-search: the result sources (apps, windows, files, tools, GNOME providers), the session state and the letter animation |
| `windows/` | what happens to a window outside a layout: the minimize stash, the scratchpad and its exit band, the edge springs |
| `apps/` | what an app is: icons, games and Wine windows |
| `layout/` | window management: Mosaic and its packer, own-size tiles, fullscreen desktops, show desktop |
| `desktops/` | switching and reordering desktops, and the strip that shows them |
| `motion/` | motion blur, the shared curves, border fades |
| `input/` | trackpad gestures: raw fingers, swipe models, the gesture driver |
| `platform/` | the desktop around the plugin: its Lithic look (Look, through the ryoku-hyprdrive-look bridge), fonts, glyphs, the hyprctl answers Ryoku Settings reads |
| `debug/` | the `hyprdrive:debug` instruments: profiler, synthetic gestures, log paths |

A header that ends in `Model`, `Math`, `Layout`, `Plan` or `Decision` is pure
(no compositor) and has a host test in `tests/unit/`.

Every Hyprland function hook is created through `core/FunctionHooks`
(`required()` for the ones the plugin cannot load without, `firstMatch()` for
the ones that only disable a feature when missing), which records each one in
creation order -- `hyprctl dispatch 'hl.plugin.hyprdrive.debug("hooks")'`
lists them. It refuses a second hook on an address already hooked. Only the
find-and-create step is shared: each module still decides when it hooks, calls
`hook()`, keeps its pointer and trampoline, and unhooks on its own schedule.

**A new worker thread** is started through `core/Workers` (`start()` for a
handle the module keeps and joins, `launch()` for a job nobody waits on), never
as a bare `std::thread` and never detached: PLUGIN_EXIT joins every one of them
within one shared deadline, and one that misses it is left running in code
that is kept loaded (Workers.hpp has the whole policy).

**A new animation that needs frames.** Anything that moves with no pointer
motion and no client commit left to repaint it (a fade, a spring settling, a
ring) has to say so, or the desktop stops drawing mid-way: one line in the
`SOURCES` table in `core/FramePump.cpp`, the reason name `hyprdrive:debug
frames` prints and its predicate. Nothing asks for a frame by damaging from
inside the render pass: `FramePump::request` posts it for after the pass and
coalesces it to one per output per event-loop iteration.

**A new colour or corner.** Every colour is a Lithic role and every chrome
corner a Lithic cut, through `core/Theme.cpp` and `platform/Look.hpp`; nothing
names a colour value or a radius of its own (docs/lithic/styles.md).
