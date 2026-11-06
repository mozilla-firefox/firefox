# The forced-color filter, measured

The detail behind `SKILL.md` §2 "Simulate the other realm's regime" and §3 "What
the forced-color adjustment does to a declaration".

## 1. What each declaration paints

Measured 2026-10-14 on a headless Linux build (159.0a1), chrome context under
§2's override, light scheme, with the parent's `color` set to a literal. A
headless Linux run resolves every system color to its stand-in, so `Canvas` is
white and `CanvasText` black. css_guidelines "The forced-color adjustment"
states the rules these rows follow and the cases it already gives (i.e. a UA
border color per element, the shadow, gradient and `url()` cases, the properties
left unfiltered); the rows below are the measured values it does not list.

| declaration | used value |
|---|---|
| `background-color: #949297` | `Canvas`, invisible against the surface |
| `background-color: light-dark(#B7B6BA, #949297)` | `Canvas` |
| `background-color: rgb(148 146 151 / 50%)` | `Canvas` at 50% |
| `background-color: transparent`, `ButtonFace` | kept |
| `border-color: #949297`, `var(--x)` with `--x: #949297` | `CanvasText` |
| `border-color: color-mix(in srgb, ButtonText 50%, ButtonFace)`, `light-dark(Canvas, CanvasText)`, `contrast-color(Canvas)` | kept |
| `border-color: light-dark(Canvas, #949297)`, `rgb(from CanvasText r g b / 50%)` | `CanvasText` |
| `caret-color: #949297` | `auto`, which paints the forced text color |
| `color: #949297` | the parent's color |
| `color: transparent`, `CanvasText` | kept |

## 2. Putting chrome under forced colors

css_guidelines "Chrome area" says what the override reaches and why the tabs
stay out. Chrome JS reaches the field as `browsingContext.forcedColorsOverride`
(`"none"` or `"active"`):

```js
window.browsingContext.forcedColorsOverride = "active";
// approximates Windows chrome, which is only `requested`:
document.documentElement.style.setProperty("forced-color-adjust", "none");
```

From `firefox-probe.py`, with `ui.useAccessibilityTheme=1` giving chrome
`prefers-contrast` as well:

```sh
firefox-probe.py --setpref ui.useAccessibilityTheme=1 --chrome-setup 'window.browsingContext.forcedColorsOverride = "active"; document.documentElement.style.forcedColorAdjust = "none"'
```

The second statement is for Windows chrome only. `--chrome-setup` excludes
`--chrome-setup-file`, so beside a setup file run the line as a `chrome` step.

Measured on headless Linux, chrome context:

- `(forced-colors)` and `(forced-colors: active)` both turn true;
  `(prefers-contrast)` stays false until `ui.useAccessibilityTheme=1` is set as
  well.
- The filter runs on chrome declarations, and chrome's `@media (forced-colors)`
  rules apply, the tokens' forced-colors layer included.
- The page in the tab keeps `(forced-colors)` false; set `document_color_use=2`
  for it.

`pair-walk.py` takes the override through a `--setup-file` holding those lines;
under the root's `none` its transparent-border check finds nothing, which is
right for Windows chrome. Under the override alone it judges the content-style
filter.

## 3. A `none` subtree can read its neighbor's filtered colors

Under forced colors, an element inside a `forced-color-adjust: none` subtree can
take the filtered colors of an element elsewhere with the same matched rules,
styled earlier in the same pass, and the reverse, in content and in chrome under
the override. An inline `forced-color-adjust: none` does not prevent it. This is
a defect in Stylo's rule cache, bug 2082150; drop this section once it is fixed.

When a `none` subtree paints filtered colors, set a custom property inline on
the element (`el.style.setProperty("--bump", "1")`) and re-read: that restyles
it alone, and a value that comes back authored was the cache, not the
declaration. An extra class that no rule matches does not restyle it.
