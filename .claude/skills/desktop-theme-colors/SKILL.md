---
name: desktop-theme-colors
description: "CSS issues, changes and reviews involving color - high contrast, forced colors, prefers-contrast, dark mode, system colors; a border, shadow, focus ring or icon that vanishes; states like hover/selected/focus that don't look distinct; a color right in chrome but wrong in an about: page or vice versa; a Firefox::Theme or Toolkit::Themes bug, even when the report already names the rule or token."
allowed-tools:
  - Bash(./mach python --virtualenv wpt .agents/skills/desktop-theme-colors/scripts/pair-walk.py:*)
  - Bash(./mach python --virtualenv wpt .claude/skills/desktop-theme-colors/scripts/pair-walk.py:*)
  - Bash(./mach python --virtualenv wpt -- .agents/skills/desktop-theme-colors/scripts/pair-walk.py:*)
  - Bash(./mach python --virtualenv wpt -- .claude/skills/desktop-theme-colors/scripts/pair-walk.py:*)
  - Bash(./mach python --virtualenv wpt .agents/skills/desktop-theme-colors/scripts/contrast.py:*)
  - Bash(./mach python --virtualenv wpt .claude/skills/desktop-theme-colors/scripts/contrast.py:*)
  - Bash(./mach python --virtualenv wpt -- .agents/skills/desktop-theme-colors/scripts/contrast.py:*)
  - Bash(./mach python --virtualenv wpt -- .claude/skills/desktop-theme-colors/scripts/contrast.py:*)
---

# Settle what a color paints

Each of the four regimes in css_guidelines "High contrast mode" resolves
differently in a chrome window than in an `about:` page, and two differ by
platform. Name which regime each side is in before calling chrome and the page
inconsistent. Load `desktop-theme-probing` before the first live launch (a
`token-resolve.py` run is not one); it defines the platform files (the probing
machine's and the target's) that this skill cites by section.

**Read the used value off the live element** with the instruments in
`desktop-theme-probing` §1. Also apply the checks in
`accessible/docs/HCMCSSChecklist.md` on foreground/background pairs, matching
interaction states, borders, visual effects and SVG colors.

Also load `desktop-theme-tokens` when the color comes from a design token, and
`desktop-theme-screenshots` when the result needs showing on the bug.

## 1. Name the regime, in both realms, before naming the bug

**Two documents in the source tree back this skill:
`docs/code-quality/coding-style/css_guidelines.md` and
`toolkit/themes/shared/design-system/docs/README.design-tokens.stories.md`.
They're kept up to date with how different parts of the code base interact in
non-obvious ways; they may differ from what you remember. Before the probe, read
css_guidelines "Colors that resolve per realm" and "High contrast mode" with its
"Content area" and "Chrome area" subsections, and the README's "Chrome native OS
theme".** When the color comes from a theme, built-in or add-on, also read
"Theme support": a WebExtension theme writes its mapped colors inline on the
root, where only an `!important` rule or a redefinition on a descendant beats
them, while the default and in-app themes paint from the app's CSS. Trace the
winning declaration before choosing a manifest fix over a CSS one. This
skill builds on those sections and does not repeat them.

Then run one probe per realm:

```js
const mq = q => matchMedia(q).matches;
return {
  forcedColors: mq("(forced-colors)"),
  forcedColorsActive: mq("(forced-colors: active)"),
  prefersContrast: mq("(prefers-contrast)"),
  contrastLevel: ["more", "less", "custom"].find(v => mq(`(prefers-contrast: ${v})`)),
  dark: mq("(prefers-color-scheme: dark)"),
};
```

That is the `firefox-probe.py -e` shape; `desktop-theme-probing` §1 has the
others. Read every field: css_guidelines "High contrast mode" says why the two
forced-colors queries and the contrast grades can disagree.

**Reproduce in the regime the report names.** A report's preconditions (e.g. a
theme, a pref) and its platforms set the regime, and the default on the machine
you probe on is often a different one. Linux defaults to the native theme
(`browser.theme.native-theme`); Windows and macOS default to the pref off, and a
built-in theme never uses the native theme. The README's "Chrome native OS
theme" table gives the layer each case paints. A Linux report may come from
either branch, so reproduce both where the changed rule differs between them:
run `desktop-theme-tokens`' `token-resolve.py --effective` on the tokens the
rule reads and compare the `default` and `-moz-native-theme` rows, and check the
rule itself for a `(-moz-native-theme)` condition. For a report from another
platform than the one you probe on, read that platform's file
(`desktop-theme-probing`), section CONTRAST, before choosing a regime. Set the
report's regime up through §6's rows before the first reading, and print the
theme and `(-moz-native-theme)` beside the values.

**Design a chrome color probe around `desktop-theme-probing` §4's marker values
or the rule matching in its §2 "Reach and read the state" from the start.** A
value read names the rule that paints a state only when every candidate rule
computes to a distinct string, and `getComputedStyle` substitutes `var()`
references, so two rules resolving to one color read alike; on Linux and macOS a
chrome probe reads the default or `prefers-contrast` numbers, often equal in
every state, until it puts chrome under forced colors (§2).

## 2. Simulate the other realm's regime

Reach either realm's regime on any platform:

- `ui.useAccessibilityTheme`: int, a LookAndFeel override. 1 turns
  `prefers-contrast` on in both realms. (`ui.prefersContrast` does not exist.)
- `browser.display.document_color_use`: whether the page *also* gets forced
  colors (values and per-platform defaults: css_guidelines "High contrast
  mode"); a Linux or macOS run sets 0 to reach the Windows regime.
- `window.browsingContext.forcedColorsOverride = "active"`, from chrome JS:
  css_guidelines "Chrome area" says what it reaches and how to approximate
  Windows chrome, which is only `requested`, by setting `forced-color-adjust:
  none` on the chrome root; `prefers-contrast` stays out of it. `(forced-colors:
  active)` matches under the override but not in Windows chrome, which matches
  `(forced-colors: requested)` and the bare `(forced-colors)`, so check which
  form a rule uses before trusting a simulated pass. The override covers one
  window: set it on each window's own `browsingContext` and reset each to
  `"none"` afterward. Before passing a window to `pair-walk.py`, read §2
  "Putting chrome under forced colors" in `references/forced-color-filter.md`.

All three take effect live. Set `ui.useAccessibilityTheme` at startup to load
the system colors under it from the beginning; flip `document_color_use` mid-run
to move the page between regimes without touching chrome.

The simulation reproduces the regime, not Windows' color set: only under
`XP_WIN` does an active accessibility theme force the light system colors
(`layout/style/PreferenceSheet.cpp`). Pin `ui.systemUsesDarkTheme=0` when the
measurement stands in for Windows.

**Read it as a differential** (`desktop-theme-probing` §4) of the custom
properties the chrome element and the in-page element share, read with
`getComputedStyle(el).getPropertyValue(name)`. A row where chrome reads a system
color and the page reads a literal or a `light-dark()` of literals has named the
defect. A custom property reads back with its `var()` references substituted
but `light-dark()` and `color-mix()` as written; the color it resolves to in the
current scheme is on a property that uses it, such as `borderTopColor`.

## 3. What the forced-color adjustment does to a declaration

**Before judging a declaration under forced colors, read css_guidelines "The
forced-color adjustment".**
The filter runs only in the `active` state, in `tweak_when_ignoring_colors`
(`servo/components/style/properties/cascade.rs`), on the longhands marked
`ignored_when_colors_disabled` in `properties/longhands.toml`. css_guidelines
"The forced-color adjustment" lists what it keeps, what a rejected value
becomes, which properties it leaves alone, and how it judges a `var()`, a
`color-mix()` and a relative color. A ring or separator drawn as a shadow or a
gradient vanishes. `references/forced-color-filter.md` §1
"What each declaration paints" has the measured rows, and css_guidelines "The
forced-color adjustment" the UA border color per element.

**A transparent border used as a spacer paints** where the document is `active`
(a page, or chrome under §2's override), so a toolbar report is not this case.
Give it a system color matching the surface behind it under
`@media (forced-colors: active)`, as css_guidelines prescribes, after measuring
that surface in the light, dark and stand-in sets (§6). Where the surface
varies so that no one system color matches it, or the border has no reason to
exist in any mode, move the gap into padding (inside the hit area) or margin
(between hit areas; not where adjacent hit areas must touch), which changes how
it snaps (`desktop-theme-layout` §2 "Device pixels") and needs that skill's §1
sweep. Say which fix you chose and why.

**`forced-color-adjust: none` switches off the filter, the text backplate of the
block that carries it, and the replacement of author `::selection` and
`::target-text` colors, and nothing else.**
`@media (forced-colors)` rules and the tokens' forced-colors layers still apply
under it, and Windows chrome has no filter for it to undo. A `none` subtree
that still paints filtered colors can be the style cache
(`references/forced-color-filter.md` §3 "A `none` subtree can read its
neighbor's filtered colors").

**An icon follows the regime only through `currentColor` or a system color in
its `fill` or `stroke`, its own or one context paint hands it from the host**
(`-moz-context-properties`; an SVG's `fill="context-stroke"` takes the host's
`stroke`). Trace a `currentColor` fill through the element's `color` to
its declaring rule (`getMatchingCSSRules` up the ancestors,
`desktop-theme-probing` §2 "Reach and read the state"), and resolve a `-moz-*`
system color as §1 "Two names can be one pair" in
`references/system-color-pairs.md` says.

## 4. Pair the system colors per state and per selector

Pairing applies wherever system colors paint, filtered or not: content under
forced colors, Windows chrome under `requested`, and `prefers-contrast`. The
pairs, as `PAIRS` in `scripts/pair-walk.py` lists them:

| background | text | border |
|---|---|---|
| `Canvas` | `CanvasText`, `LinkText`, `VisitedText`, `ActiveText`, `GrayText` | `CanvasText`, `ButtonBorder`, `GrayText` |
| `Field` | `FieldText`, `GrayText` | `FieldText`, `ButtonBorder`, `GrayText` |
| `ButtonFace` | `ButtonText`, `GrayText` | `ButtonText`, `ButtonBorder`, `GrayText` |
| `SelectedItem`, `Highlight`, `AccentColor`, `Mark` | its `…Text` | its `…Text` |

Two states with correct pairs can still paint the same and look like one state.
Compare each state's pair with every other state's on the same element, by
resolved value (two names can be one color: `references/system-color-pairs.md`
§1); whichever state the patch changed is a finding unless the design asks for
it.

**A state shown only by opacity needs its own system color under forced
colors.** Forced colors leave `opacity` alone, so a disabled control faded over
`ButtonText` paints a color outside the user's palette. First establish what the
state means by reading the code that sets its class or attribute: a token named
for one state, consumed for another, is itself the finding. Then look for a
token for that state that already varies by regime (`desktop-theme-tokens`'
`token-resolve.py --effective` prints its value in each). A derived token keeps
the root's value under an element that redefines its base: check per the
derived-token rule in `desktop-theme-tokens` §4 "Where a token's value belongs"
before consuming one outside the rule that already reads it, or judging whether
a consumer could. A token that passes can be consumed unconditionally, so
consumers need no `@media (forced-colors)`. Only where no token covers the state
does the consumer sheet give it, under forced colors, its pair at full opacity:
`GrayText` for disabled content, its icon's `fill` included.

Before running `scripts/pair-walk.py`, read §3 "Per descendant, with
pair-walk.py" in `references/system-color-pairs.md` for its setup; in chrome on
Linux and macOS that setup includes the override from §2 above, and the walk
exits 1 when an entry judged nothing. Before judging a state, read its §2 "Per
state" and §4 "Per selector": a diff-only read misses the state the patch left
to an existing rule and the descendants a state repaints. Enumerate the elements
a narrowed selector drops with `desktop-theme-probing`'s
`references/probe-recipes.md` §8 "Measure what a narrowed selector drops".

**Judge forced colors by pair membership, and `prefers-contrast` alone by ratio
where a literal paints.** Under forced colors the user's palette only promises
contrast within a pair, and a local simulation does not resolve to a Windows
theme's values, so a ratio can only illustrate a finding. Under
`prefers-contrast` alone the `tokens-prefers-contrast` layer mixes system colors
with tokens that resolve to literals; measure the ratio wherever a literal
paints, against 7:1, where `Gecko_MediaFeatures_PrefersContrast` grades `more`.
In the default regime, a theme's colors included, judge a text pair against WCAG
AA: 4.5:1, or 3:1 for large text, and a focus ring or separator against its
surface at 3:1 (`--non-text`). Measure with `scripts/contrast.py`, never by
hand, and name the palette it was measured on. `--help` has the flags, the state
locks, and the cases it reports not judged, which need a pixel sample. Where a
sibling paints a state (e.g. a tab's `.tab-background` behind its label), a
state entry without `--surface` reads the unchanged background and passes; the
only sign is a warning on stderr, so treat that entry as not judged until you
rerun it with `--surface`.

## 5. Traps when reading a value back

- **Your measurement is not a resist-fingerprinting user's** (css_guidelines
  "Colors that resolve per realm"); run §6's resist-fingerprinting row.
- **Read `outline-style` before `outline-color`** (css_guidelines "Focus
  rings"): an element no author rule gives an outline style reads the token's
  color and paints the accent (`SelectedItem` and `CanvasText` in high
  contrast). Set the style with the color: `outline: var(--focus-outline)`.
- **A `::part()` rule from the outer tree beats the shadow tree's own rule**
  (css_guidelines "Overriding across a shadow boundary") unless the shadow rule
  is `!important`. Read a part's value with both sheets in place.
- **Before removing a declaration as redundant with a shared rule, list the
  rules that set the property on the element** with the declaration in place
  (`desktop-theme-probing` §2 "Reach and read the state", "prove the rule
  applies"): the shared rule may lose to another rule there. When it loses,
  removing the declaration changes the computed value, and a pixel diff clears
  that only for the consumers captured. In a shared sheet, read what the sheet
  itself feeds the property into and capture a consumer that keeps it.
- **A computed outline or inset shadow can be covered.** A container's inset
  outline paints over its in-flow children but under a positioned child with
  `z-index` auto or non-negative,
  whether or not the container itself is positioned, and an inset `box-shadow`
  paints under any child that has a background. Read it off pixels, through a
  compositor grab where the child is a page (`desktop-theme-screenshots` §2).

## 6. Judge the fix in every regime, then restore the prefs

Check that each cell moves the way it should (`desktop-theme-probing` §5).
`document_color_use` is `browser.display.document_color_use`:

| regime | how to reach it |
|---|---|
| default | both prefs at their defaults, read back (§1); also pin and read back the prefs the host's platform file (`desktop-theme-probing`), section HEADLESS, lists for the default regime; before restarting into it, read §2 "Prefs" in `desktop-theme-probing`'s `references/devtools-mcp.md` |
| prefers-contrast, no forced colors | `ui.useAccessibilityTheme=1`, `document_color_use=1`; Windows chrome is `requested` under these prefs, so take this cell for chrome on Linux or macOS |
| forced colors (the Windows regime) | `ui.useAccessibilityTheme=1`, `document_color_use=0`; Windows chrome is `requested` under these prefs; elsewhere add §2's override and root `forced-color-adjust: none` to approximate it, which reports `active` |
| forced colors, content only | `ui.useAccessibilityTheme=0`, `document_color_use=2`; the page still reports `prefers-contrast` |
| forced colors, chrome only | §2's override; this row separates forced colors from `prefers-contrast`. Under it, and under the full forced-colors row, chrome swaps any theme for the default theme (css_guidelines "Theme support"), so a theme cell measures the default theme |
| prefers-contrast `custom` or `less`, content only | `document_color_use=2`, `browser.display.foreground_color` at 4.5:1 to 7:1 (`custom`) or below (`less`) on `browser.display.background_color` |
| stand-ins, content only | `ui.use_standins_for_native_colors=true`; on a host where a headless run already paints the stand-ins, run it on a real widget backend; before picking the display, see the host's platform file (`desktop-theme-probing`), section HEADLESS |
| resist fingerprinting | `privacy.resistFingerprinting=true`: the stand-ins, and no `prefers-contrast` in content |
| light and dark | `ui.systemUsesDarkTheme=0` or `1`, pinned both ways since a headless run and a real display can default to different schemes (the host's platform file in `desktop-theme-probing`, section HEADLESS), read back with `(prefers-color-scheme: dark)`, plus the in-app row's pref where the theme should follow it; set `browser.theme.content-theme=0` and `browser.theme.toolbar-theme=0` only on a running instance, since `LightweightThemeConsumer` rewrites both at most theme updates, so a `--setpref` value is gone by the first reading; read both back after the window applies its theme (css_guidelines "Colors that resolve per realm" has which updates write them and the value each theme gets) |
| chrome and web content in different schemes | `ui.systemUsesDarkTheme` sets the chrome's scheme; `layout.css.prefers-color-scheme.content-override` (about:preferences' website appearance setting) sets the websites', with the value css_guidelines "Colors that resolve per realm" gives for the scheme you want; the second also drives `-moz-content-prefers-color-scheme` in chrome sheets; both take as `--setpref` at launch |
| lightweight or system theme | `lwtheme` on the chrome root without `theme-in-app`, live, for the token layers; a real add-on theme for the colors its manifest writes inline on the root (`desktop-theme-tokens` §3), installed as `desktop-theme-probing`'s `references/probe-recipes.md` §3 "Install a theme in the probe profile" says |
| built-in in-app theme | an `inApp` id from `browser/themes/BuiltInThemeConfig.sys.mjs`, enabled as `desktop-theme-probing`'s `references/probe-recipes.md` §3 "Install a theme in the probe profile" says; it takes the in-app layer on every platform (`desktop-theme-tokens`' `references/token-layers.md` §2 "Which layer each theme kind paints") |
| system accent color | `ui.accentcolor=#rrggbb` and `ui.accentcolortext=#rrggbb`; without them the accent depends on the host and the display (the host's platform file in `desktop-theme-probing`, section HEADLESS) |
| in-app light/dark pair on Linux | `browser.theme.native-theme=false` |
| native theme on Windows or macOS | `browser.theme.native-theme=true`, with the default theme: a built-in in-app theme takes the in-app layer whatever the pref says |

When the fix is in a shared sheet, run every row in both realms, the healthy one
included; for a token change, a realm where no sheet reads a changed token is
out. A change inside a conditional block (such as `@media (forced-colors)` or a
`-moz-pref()` block) needs before and after only in the rows where the condition
holds, plus one other row to confirm it does not apply there; moving a
declaration out of one needs every row, and each difference is a visible change
to report. A fix that changes only lengths needs only the rows that change
whether the affected ink paints (§3: forced colors paints a transparent border
or outline and drops a shadow) or what a changed length computes to (a
conditional block in the sheets that style the element). Add the light and dark
side of each row when the surface uses `light-dark()`, and the density and scale
axes of `desktop-theme-layout` §1 when the fix touches a length.

On Linux and macOS, take chrome's forced-colors answer from the chrome-only row,
and a Windows palette's from a Windows try run.

Restore every pref, set any forced-colors override back to `"none"`, and delete
any scratch probe (`desktop-theme-probing` §6).
