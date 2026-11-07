# Look and feel

`LookAndFeel` (`widget/LookAndFeel.h`) is how Gecko reads the platform's
appearance settings: the system colors, the system fonts, and integer and float
metrics such as the caret blink time, the scrollbar style or whether the system
uses a dark theme. CSS system colors and system font keywords resolve through it,
and so do media features such as `prefers-contrast`, `prefers-reduced-motion`
and the chrome-only `-moz-overlay-scrollbars` and `-moz-windows-mica`. Each
toolkit reads the native values, and a content process uses the values the
parent process sent it.

[Colors and High Contrast Mode](/accessible/ColorsAndHighContrastMode.md) says
how Firefox picks the colors a page renders with, and the
[CSS guidelines](/code-quality/coding-style/css_guidelines.md#high-contrast-mode)
say how forced colors and `prefers-contrast` resolve in chrome and in content.
This page covers overriding look-and-feel values and what a headless run reads.

## Overriding values with prefs

Every look-and-feel value has a pref, listed in the `sIntPrefs`, `sFloatPrefs`,
`sColorPrefs` and `sFontPrefs` tables in `nsXPLookAndFeel.cpp`. When one is
set, its value replaces the platform's on every platform, so a test or a
developer can reach a configuration the machine doesn't have. These are the
overrides front-end work uses most:

| Pref | Value | What it changes |
| --- | --- | --- |
| `ui.useAccessibilityTheme` | 1 | `prefers-contrast`, in chrome and in content |
| `ui.systemUsesDarkTheme` | 0 or 1 | the system color scheme and `(-moz-system-dark-theme)` |
| `ui.useOverlayScrollbars` | 1 overlay, 0 classic | the scrollbar style and `(-moz-overlay-scrollbars)` |
| `ui.prefersReducedMotion` | 1 | `prefers-reduced-motion: reduce` |
| `ui.accentcolor`, `ui.accentcolortext` | a CSS color | `AccentColor` and `AccentColorText` |
| `ui.windowsMica` | 1 | `(-moz-windows-mica)` |
| `ui.windowsAccentColorInTitlebar` | 0 or 1 | `(-moz-windows-accent-color-in-titlebar)` |
| `ui.GtkCSDAvailable` | 0 or 1 | `(-moz-gtk-csd-available)` |
| `ui.GtkCSDTransparencyAvailable` | 0 or 1 | `(-moz-gtk-csd-transparency-available)` |

Whether chrome or content follows the system color scheme depends on the theme
prefs; [Colors that resolve per
realm](/code-quality/coding-style/css_guidelines.md#colors-that-resolve-per-realm)
gives the order.

Each kind of pref has its own type:

- An integer value takes an integer pref. A boolean or string pref of the same
  name is ignored, so `ui.useAccessibilityTheme` set to `true` has no effect.
- A float value takes an integer pref in hundredths: `ui.textScaleFactor` set
  to 150 is a factor of 1.5.
- A color pref takes a CSS color. A pref of the same name with `.dark` appended
  applies to the dark color scheme, and the plain one applies to both schemes
  when that is absent.
- A font pref, `ui.font.<keyword>` (e.g. `ui.font.menu`), takes a family name,
  with optional `.size`, `.weight` and `.italic` prefs beside it. `.size` (in
  px) and `.weight` must be string prefs such as `"20"` and `"700"`; an integer
  pref is ignored. `.italic` is a boolean pref.

A color override changes the system color, which chrome always uses. A document
in a tab gets a stand-in instead of the system color in some cases
(`LookAndFeel::ShouldUseStandins`, bug 1861362), and the override doesn't reach
it there.

A change to one of these prefs takes effect in a running browser
(`nsXPLookAndFeel::OnPrefChanged`). `kMediaQueryPrefs` lists the other prefs
that feed look and feel, each with what its change recomputes.

## Headless

A headless run (`--headless` or `MOZ_HEADLESS=1`) reads the same look and feel
as a windowed one where the platform settings are reachable without a window.

On macOS and Windows a headless run reads the real system: the host's system
colors, fonts, dark mode, contrast setting and scrollbar style. A headless run
there is not a fixed environment, so a test that depends on one of these
values has to set the override for it.

Under GTK a headless run opens no display, so there are no GTK settings to read
and look and feel keeps fixed values:

- Every system color is its stand-in
  (`nsXPLookAndFeel::GetStandinForNativeColor`), including `AccentColor`, which
  is rgb(0, 96, 223).
- The system fonts are `sans-serif` at 16px.
- The system color scheme is light, and the contrast and reduced-motion settings
  are off.
- `(-moz-gtk-csd-available)` and `(-moz-gtk-csd-transparency-available)` are
  false (bug 2090594).
- Scrollbars follow `widget.gtk.overlay-scrollbars.enabled`, as in a windowed
  run.

The overrides above apply in a headless run as in any other, so a value a
headless run lacks can be set with its pref.
