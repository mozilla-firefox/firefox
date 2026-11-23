# Windows hosts and targets

Windows, as a probe's host and as a bug's target.

## HEADLESS

A headless run reads the host's own look and feel
(`widget/docs/look-and-feel.md`, "Headless"), the accent color in the title
bar included. Pin the default
regime with
`--setpref ui.systemUsesDarkTheme=0`, `--setpref ui.useAccessibilityTheme=0`,
`--setpref ui.useOverlayScrollbars=0` (or `=1` when the reading needs overlay
scrollbars) and `--setpref ui.windowsAccentColorInTitlebar=0`, and read each one
back. A headless run also reproduces chrome's `requested` forced-colors regime
exactly.

## DISPLAYS

No isolated display: a non-headless run opens on the logged-in desktop whatever
you pass it, so the rungs go from headless straight to the user's session or a
try run, which is the only isolated real display.
`firefox-probe.py --no-headless` prints a warning that the run contends with
whatever else is on screen.

## FONT

Chrome's root is 12px: the Segoe UI 9pt message font at any scale factor, with
the text-size setting applied as a zoom on top. A headless run reads this
desktop's own (HEADLESS).

## SCROLLBARS

Windows 11 shows overlay scrollbars unless the user turns on "Always show
scrollbars" (`widget.windows.overlay-scrollbars.enabled` lets Firefox follow
that setting); Windows 10 shows classic ones (HEADLESS).

## CONTRAST

Chrome is `requested`, not `active` (css_guidelines "High contrast mode"); to
simulate it, `desktop-theme-colors` §2 "Simulate the other realm's regime".

In the light scheme that high contrast forces, `Highlight`, `SelectedItem` and
`-moz-menuhover` resolve to one color, and `HighlightText`, `SelectedItemText`
and usually `-moz-menuhovertext` to another (the source is in
`desktop-theme-colors`' `references/system-color-pairs.md` §1).

## COMPOSITING

Mica and acrylic cannot be painted on another host. Sample the reporter's
backdrop as `desktop-theme-screenshots`
§6 "Read a reporter's screenshot by sampling it" says. To test a Windows-only
rule's cascade (`ui.windowsMica=1`) elsewhere, read §4 "Test another platform's
rule" in [`probe-recipes.md`](probe-recipes.md).

## MENUS

No native menus: context and anchored menus are Gecko popups, so their geometry
and `drawSnapshot` read normally.

## PATHS

The binary is `<objdir>/dist/bin/firefox.exe`, and the system temp directory is
`%TEMP%`. A jar.mn entry is a copy where the build cannot create a symlink
(`build/docs/jar-manifests.md`, "In a Local Build"); on Windows it is a copy
unless Developer Mode or an elevated shell allows symlinks.
`find <objdir>/dist/bin -type l` printing nothing means copies, so run
`./mach build faster` after each edit (§2 "What is live at the next launch" in
[`build-freshness.md`](build-freshness.md)).

## GRABS

An uncropped grab spans every screen with (0, 0) at the virtual desktop's
top-left corner, so when a monitor sits left of or above the primary one, the
recorded origin is off by that monitor's extent. Before reading a grab, read
§2 "The compositor's output" in [`measuring-pixels.md`](measuring-pixels.md).
