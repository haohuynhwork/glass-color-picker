# glass-color-picker

Screen color picker for KDE Wayland with a glassy magnifier lens (C++ / Qt6).

## Build
    cmake -B build && cmake --build build

Or use the `Makefile` shortcuts: `make help` lists them (`make reload`, `make watch`, `make release`, …).

## Install
    cmake --install build --prefix ~/.local    # picker in ~/.local/bin, widget in ~/.local/share/plasma/plasmoids

## Release
The version is read from `plasmoid/metadata.json`; bump it there.

    cmake --build build --target release

Produces in `build/`: `glass-color-picker-<ver>.plasmoid` (for store.kde.org),
`glass-color-picker-<ver>-src.tar.gz` (for AUR), the stripped `glass-color-picker-<ver>-<arch>`
binary and a `.sha256` file. `--target plasmoid` or `--target package_source` build one each.

## Use
    ./build/glass-color-picker [--format FORMAT] [--no-copy]

Formats: hex, hex-nohash, hex-lower, hex-lower-nohash, rgb-plain, rgb, rgba,
qt-rgba, hsl, hsv, cmyk, latex.

The format defaults to `hex` (`#RRGGBB`). Set a permanent default with
`COLOR_PICKER_FORMAT=rgb` in your environment; `--format` overrides it.

- Move mouse: lens follows, showing zoomed pixels and the HEX/RGB value
- Scroll: zoom in/out
- Alt: toggle the magnifier on/off
- B: toggle the color badge (the value label) on/off
- F: cycle color format while picking
- Tab: cycle lens position (right of cursor (default), bottom-right, bottom-left, top-left, top-right)
- Arrow keys: move the cursor 1 px (Shift: 10 px); Enter picks
- Left click: copy the color (in the current format) to clipboard (via wl-copy) and print it
- Esc / right click: cancel

Works across multiple monitors. Samples the live screen: the overlay is clear and the lens is fed by small
native-resolution captures around the cursor through KWin's ScreenShot2 D-Bus API (`CaptureArea`), so panels,
animations and video stay live. If that is unavailable it freezes a full screenshot (falling back to `spectacle`). Requires `wl-clipboard`.

## Plasma widget

`plasmoid/` is a panel/desktop widget modeled on KDE's built-in Color Picker:
a pipette button that launches the glass picker, a dot showing the last color,
and a popup with the color history (click = copy, right click = copy as
another format / remove). Settings (like the stock widget): default color format, auto-copy to
clipboard, what the keyboard shortcut does (pick / show history), preview
count (colors shown in the panel), history size, picker command.

Install / update:

    kpackagetool6 -t Plasma/Applet -i plasmoid   # first time
    kpackagetool6 -t Plasma/Applet -u plasmoid   # after changes

Then right-click the panel → Add Widgets → "Glass Color Picker".

After editing the widget, run `scripts/reload-widget.sh` (updates it and restarts
plasmashell, force-killing it if it hangs on exit), or
`scripts/reload-widget.sh --window` to preview it in a window without restarting.

Fastest loop: `scripts/watch-widget.sh` watches `plasmoid/` and relaunches a
preview window on every save (`--panel` restarts the real panel instead).
