# macOS hosts and targets

macOS, as a probe's host and as a bug's target.

## HEADLESS

A headless run reads the real system: the host's system colors, interface font,
dark mode, contrast or high-contrast setting and overlay scrollbars. Pin the
default regime with `--setpref ui.systemUsesDarkTheme=0`, `--setpref
ui.useAccessibilityTheme=0` and `--setpref ui.useOverlayScrollbars=0` (or `=1`
when the reading needs overlay scrollbars), and read each one back.

## DISPLAYS

No isolated display: a non-headless run opens on the logged-in desktop whatever
you pass it, so the rungs go from headless straight to the user's session or a
try run, which is the only isolated real display.
`firefox-probe.py --no-headless` prints a warning that the run contends with
whatever else is on screen.

## FONT

Chrome's root is about 11px; a headless run reads this desktop's own (HEADLESS).

## SCROLLBARS

Scrollbars follow the system's scroller style (HEADLESS).

## CONTRAST

The forced-colors prefs give chrome `prefers-contrast`, not forced colors: see
`desktop-theme-colors` §2 "Simulate the other realm's regime" for the override
that puts chrome under forced colors.

## COMPOSITING

Not applicable: no rule here depends on vibrancy or other macOS-only
compositing.

## MENUS

A non-headless run opens context and anchored menus as native NSMenus, so their
geometry and `drawSnapshot` read nothing. Set
`widget.macos.native-context-menus` and `widget.macos.native-anchored-menus` to
false, put `nonnative` on the popup, or run headless, which turns both off.

## PATHS

The binary is `<objdir>/dist/<App>.app/Contents/MacOS/firefox`; `dist/bin` is a
staging copy that fails at startup. The system temp directory is `$TMPDIR`,
where `firefox-probe.py` keeps a failed run's browser log
(`desktop-theme-probing` §6 "Leave the tree clean"). A plain jar.mn entry is a
symlink into the source tree, as on Linux
(§2 "What is live at the next launch" in
[`build-freshness.md`](build-freshness.md)).

## GRABS

An uncropped grab covers the main display alone, in device pixels (2x on
Retina); a crop is in points, and Pillow returns it at 1x, so a Retina crop is
downsampled. Without the Screen Recording permission for the terminal the grab
holds only the wallpaper and still exits 0; check the PNG shows the window
before reading it. Before reading a grab, read §2 "The compositor's output" in
[`measuring-pixels.md`](measuring-pixels.md).
