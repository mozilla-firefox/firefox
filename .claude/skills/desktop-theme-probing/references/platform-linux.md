# Linux hosts and targets

Linux, as a probe's host and as a bug's target. Everything here is
X11/Wayland/GTK-specific; do not reuse these commands or environment variables
on macOS or Windows.

## HEADLESS

Headless GTK reads fixed look-and-feel values, not the desktop's
(`widget/docs/look-and-feel.md`, "Headless", lists them), so a color or metric
that comes from GTK cannot be reproduced here, and "what is the root font size
on this machine" has no answer. Read the desktop's value off a non-headless run,
or from its own configuration (FONT below has the GTK setting); sweeping root
font sizes by setting `documentElement.style.fontSize` yourself still works
(`desktop-theme-layout` §1). Xvfb reads the desktop's accent and color scheme
where headless reads fixed ones, so pin each one a result depends on and read it
back. `--setpref ui.accentcolor=#rrggbb` with `ui.accentcolortext` sets
`AccentColor` and `AccentColorText`. `ui.systemUsesDarkTheme` set to 0 or 1 pins
the scheme, which a theme's colors follow too; read `(prefers-color-scheme:
dark)` back. Pin `ui.useOverlayScrollbars` as well, since the default differs
between profiles (SCROLLBARS below). Headless GTK reports no compositing
(COMPOSITING below), so `popup.css` forces a popup's border radius and shadow
margin to 0px; a popup radius or shadow reading needs `--setpref
ui.GtkCSDTransparencyAvailable=1`. Headless Linux also lacks the
`customtitlebar` root attribute; to test a rule under it, read §4 "Test another
platform's rule" in [`probe-recipes.md`](probe-recipes.md). Primary-selection
assertions fail only headless.

## DISPLAYS

[`running-a-probe.md`](running-a-probe.md) has the rules every display
follows. The rungs below rise in what they supply; take the cheapest one that
supplies what the measurement needs.

### A virtual X server: real X11, no window manager

`xvfb-run` comes in the `xvfb` package on Debian and Ubuntu,
`xorg-x11-server-Xvfb` on Fedora and `xorg-server-xvfb` on Arch.

```
env -u WAYLAND_DISPLAY GDK_BACKEND=x11 MOZ_ENABLE_WAYLAND=0 xvfb-run -a -s "-screen 0 1600x1000x24" \
    ./mach python --virtualenv wpt .claude/skills/desktop-theme-probing/scripts/firefox-probe.py \
    --binary <objdir>/dist/bin/firefox --no-headless ...
```

- Strip the Wayland environment explicitly, as above. `xvfb-run` sets only
  `DISPLAY`; on a Wayland session `WAYLAND_DISPLAY` survives and Firefox
  prefers it, so the window lands on the desktop while the log reads like a
  clean virtual-display run, and a `grab` step reads an empty X root window.
  Gecko picks the backend from `WAYLAND_DISPLAY`, `DISPLAY`,
  `MOZ_ENABLE_WAYLAND` and `GDK_BACKEND`; `XDG_SESSION_TYPE` does not change
  under `xvfb-run` and proves nothing. Before believing a run was isolated,
  check that `WAYLAND_DISPLAY` is unset in the child's environment.
- Firefox needs 24-bit depth. Size the screen to hold the window plus the
  browser chrome, since a coordinate step must land inside the viewport.

Wrap `./mach mochitest <path> > artifacts/probe.log 2>&1` in the same
`env`/`xvfb-run` prefix.

- **There is no window manager**, so minimize, maximize, restore and the state
  events they emit fail whatever the patch does, and an open popup hides itself
  within about 300 ms; pass `--setpref ui.popup.disable_autohide=true` to keep
  it open for a reading or a grab.
- **There is no compositing manager either**, so this rung reproduces an X11
  desktop without compositing, paint included (COMPOSITING below).
- Xvfb's startup failure reads `Xvfb failed to start`.

### Xvfb plus an X window manager: backend, window manager and seat on X11

Start an X window manager on the Xvfb display before Firefox, inside the same
`xvfb-run` command. Xvfb supplies a core keyboard and pointer, so size-mode
changes and focus behave as on a desktop.

### A nested Wayland compositor: backend, window manager and seat on Wayland

CI's Wayland test jobs start Xvfb, then a compositor nested inside it
(`taskcluster/scripts/tester/test-linux.sh` runs
`mutter --display=:0 --wayland --nested` and exports `WAYLAND_DISPLAY`), and
run Firefox against that compositor. Locally this needs a compositor that can
still run as a client of an X server; mutter 50 has dropped `--nested`, and
other compositors with an X11 backend (weston, wlroots-based ones such as sway
or cage) are untested here. Point Firefox at the nested compositor's socket
(`WAYLAND_DISPLAY`) and confirm the window opened there rather than on the
desktop.

### mutter's headless backend: Wayland, no input seat

`mutter --headless` gives a real compositor with a window manager, but its
`wl_seat` advertises no capabilities, so the browser never takes keyboard focus
and a focus-waiting harness hangs before its first assertion. When neither
nested rung is available, a Wayland-specific failure leaves the user's session
or a try run.

## FONT

Gecko takes the chrome font from GTK's `gtk-font-name` setting, which differs by
desktop (GNOME defaults to 11pt, KDE to 10pt), which puts chrome's root at
roughly 14–15px on GNOME and about 13.3px on KDE. Read the value Gecko sees with
`/usr/bin/python3 -c 'import gi; gi.require_version("Gtk", "3.0"); from
gi.repository import Gtk;
print(Gtk.Settings.get_default().props.gtk_font_name)'` in the user's session,
not `gsettings`.

## SCROLLBARS

GTK defaults to overlay scrollbars
(`widget.gtk.overlay-scrollbars.enabled`), headless included, and the test
profiles turn that pref off, so a probe and a mochitest differ unless `ui.useOverlayScrollbars` (1 overlay, 0 classic, at launch) forces
either.

## CONTRAST

With `ui.useAccessibilityTheme=1` and `browser.display.document_color_use=0`,
chrome reports `forced-colors: false` and `prefers-contrast` `true`/`more`,
while the in-page copy enters forced colors. To put chrome under forced colors,
use the override in `desktop-theme-colors`
§2 "Simulate the other realm's regime".

Under Xvfb with the real GTK backend (DISPLAYS above), system colors come from
the stock GTK theme even with `ui.useAccessibilityTheme=1`: Adwaita gives
`CanvasText` and `MenuText` rgb(61, 61, 61), `Canvas` rgb(250, 250, 250) and
`FieldText` black, where headless reads `CanvasText` black on a white `Canvas`
(HEADLESS above). A prefers-contrast value built on `CanvasText` therefore looks
lower-contrast in an Xvfb grab than headless. `GTK_THEME=HighContrast` (or
`HighContrastInverse`) under Xvfb does not make Firefox report
`prefers-contrast`, so it cannot simulate a high-contrast GTK session (measured
2026-10-21).

## COMPOSITING

`(-moz-gtk-csd-transparency-available)` is true only on a display with a
compositing manager: a Wayland session or a compositing X11 desktop (e.g. GNOME,
KDE) reads true, headless Linux and Xvfb read false. Chrome keys popup and
window shadow margins, rounded corners and transparent backgrounds on it.
Without compositing a pixel a popup window leaves transparent paints black.
`ui.GtkCSDTransparencyAvailable` (int, 0 or 1, at launch) forces either side;
read the media query back in each cell, and take the paint from a grab on a
virtual X server (DISPLAYS above), since a headless run has no paint to grab.

## MENUS

No native menus by default: context and anchored menus are Gecko popups, so
their geometry and `drawSnapshot` read normally.
`widget.gtk.native-context-menus` (default false) opens context menus as GTK
menus instead; check it is false in a profile that reports otherwise.

## PATHS

The binary is `<objdir>/dist/bin/firefox`, and the system temp directory is
`/tmp`. A plain jar.mn entry (e.g. CSS, JS, `.mjs`) is a symlink into the source
tree (§2 "What is live at the next launch" in
[`build-freshness.md`](build-freshness.md)).

## GRABS

A grab takes the X root window of a private Xvfb display, and its coordinates
match the screen's. A `grab` step refuses any other X server (the PID in
`/tmp/.X<n>-lock` names it), since that is usually the live desktop, unless
`--grab-any-display` is given for another isolated server.
Before reading a grab, read §2 "The compositor's output" in
[`measuring-pixels.md`](measuring-pixels.md).
