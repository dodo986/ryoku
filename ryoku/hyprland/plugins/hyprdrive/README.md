# Hyprdrive

A Hyprland plugin that brings a mission-control overview of every window,
Mosaic layouts and smooth window motion to Ryoku. It is a fork of
[Hyprdrive](https://github.com/dragosol/Hyprdrive) by dragos, made for
Omarchy, with its dock, its folder stacks and its Omarchy integration taken
out: Ryoku has its own dock, launcher and theme, and Hyprdrive now wears them.

It is off until you turn it on in Ryoku Settings > Windows > Hyprdrive
(`ryoku-shell hub open hyprdrive`). The package is `ryoku-hyprdrive`; the
plugin loads as `hyprdrive.so`.

## What it adds

- **The overview.** Tap Super (press and release it alone), or swipe up with
  three fingers: every window of the workspace as a grid, and the workspaces
  of your Ryoku desktop (its block of ten, the ones Super+1 to Super+0 reach)
  as a strip along the top. Drag a window onto a strip cell to move it there; hold
  it over another window to swap the two. Start typing to search apps, open
  windows and files, work out sums, or start an AI agent with `/claude ...`.
- **Mosaic**, a third layout beside Dwindle and Scrolling: windows keep their
  own size and gather in one tight cluster, and tile only once they no longer
  fit. Optionally an infinite canvas you pan across.
- **Window behaviour**: show desktop, a fullscreen app on a desktop of its
  own, fixed-size windows tiled at their size, a dragged window carried to the
  next desktop at the screen edge, a hot corner.
- **Motion**: one curve for everything it animates, and motion blur.

## How it fits into Ryoku

- **Off by default, one switch.** The switch is `plugins.hyprdrive.enabled` in
  the window manager's store. Ryoku Settings writes it; the provider
  (`ryoku/wm/hyprland/hyprdrive.go`) does the rest.
- **It touches no Lua Ryoku ships.** Everything the plugin needs (the load,
  its settings, its binds, each desktop's layout) goes into one fenced block at
  the end of `~/.config/hypr/user.lua`, between `-- ryoku-hyprdrive:begin` and
  `-- ryoku-hyprdrive:end`. Ryoku rewrites only the block; your own lines
  above and below it stay as you wrote them. Turning Hyprdrive off removes the
  block.
- **It asks before taking a shortcut.** Every shortcut it adds is listed on
  its page with the Ryoku shortcut it would replace, checked against your own
  rebinds. One that replaces a Ryoku shortcut stays off until you turn it on;
  one that replaces nothing is on. `ryoku-wm-hyprland hyprdrive status` prints
  the same list.
- **It wears the desktop's style.** The plugin draws in C++, where the Lithic
  library cannot be linked, so it runs `ryoku-hyprdrive-look --watch`
  ([look/](look/)), which reads your choice in Settings > Appearance > Style
  through the Lithic Go library and hands the plugin its roles, planes and
  corner rule as text. The overview's chrome is a solid page plane with a vein
  hairline, cut at 45 degrees in Lithic and rounded in Material, Adwaita and
  Fluent; text is Geologica; a style or colour change shows on the next frame.
  The window cards keep the window's own corner, since they are the windows.
- **Its layout cycle is runtime state, not a setting.** `Super+Shift+L` runs
  `ryoku-wm-hyprland hyprdrive cycle-layout`, which records each desktop's
  layout in `~/.local/state/ryoku/hyprdrive-layouts`; the block replays it.
- **Rebuilds with Hyprland.** It is one of the bundled plugins
  (`ryoku-hub desktop plugins list`), so the doctor and Settings > Windows >
  Window manager rebuild it when Hyprland moves.

## Building

```sh
make            # hyprdrive.so, symcheck, the key table check and the host tests
make clean
```

The build needs Hyprland's headers for the version you run, a C++23 compiler
with `#embed` (GCC 15 or Clang 19), and the development files of pixman,
libdrm, pango, cairo, fontconfig, libinput, systemd, wayland, xkbcommon, glib,
lua and xcb. Every key the plugin registers is in [docs/CONFIG.md](docs/CONFIG.md),
generated from `src/core/Config.cpp`. [src/README.md](src/README.md) maps the
source.

## Licence

BSD 3-Clause, as upstream ([LICENSE](LICENSE)). Ryoku's NOTICE carries the
credit.
