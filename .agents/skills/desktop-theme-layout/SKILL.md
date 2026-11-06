---
name: desktop-theme-layout
description: Front-end CSS and markup issues and changes that move, size or clip a box - overflow, scrollbars, centering, RTL, fractional scaling, font size, density, zoom, popovers, anchors; whether a change moves or resizes anything; a Firefox::Theme or Toolkit::Themes bug, even when the report already names the rule or token.
---

# Debug a front-end CSS or layout misbehavior

Load `desktop-theme-probing` before hand-rolling a probe; it defines the host's
and the target's platform file that this skill cites by section. For a
positioned box, a popover or an anchored box that lands wrong or grows the page,
read [`references/positioning.md`](references/positioning.md): §1 for what the
box resolves against, §2 for the top layer, §3 for anchors.

Also load `desktop-theme-tokens` when a length comes from a design token, and
`desktop-theme-screenshots` when the result needs showing on the bug, or to
sample a reporter's screenshot once the live measurements run out (its §6 "Read
a reporter's screenshot by sampling it").

## 1. Sweep the environment, not just the CSS

**A document in the source tree backs this skill:
`docs/code-quality/coding-style/css_guidelines.md`. It's kept up to date with
how different parts of the code base interact in non-obvious ways; it may differ
from what you remember. Before the sweep, read its "Chrome versus in-content"
and "UI density" sections.** This skill builds on them and does not repeat them;
`desktop-theme-tokens` §5 covers rem-valued tokens.

Sweep these from the chrome context and emit one row per cell:

- **System font size.** `document.documentElement.style.fontSize`, on the root,
  not `body`. Sweep the span of the platforms' chrome roots, about 11px to 15px.
  Section FONT of each platform file (`desktop-theme-probing`) gives the
  target's size and how to read the host's. Popups keep their own font size
  (css_guidelines "Chrome versus in-content"): set it on the popup too, and read
  an item's computed `font-size` back. Before sweeping this axis on an
  in-content page, check whether the page sets its own root font size.
- **Scrollbars.** `ui.useOverlayScrollbars`: 1 overlay, 0 classic; set it at
  launch, since a live change notifies only media queries, and read it back in
  each cell (`references/environment-sweep.md` §3). Each platform's
  default is in its platform file (`desktop-theme-probing`), section
  SCROLLBARS. A classic scrollbar takes width from the box; an overlay one does
  not.
- **Compositing.** Where the target's platform file (`desktop-theme-probing`),
  section COMPOSITING, names a media query that compositing decides, sweep both
  sides of it and read the query back in each cell; that section has the pref
  and how to take the paint.
- **DPI / scaling.** `layout.css.devPixelsPerPx`, a *string* pref; `"-1.0"`
  restores auto. It takes effect headless, at launch or on the running
  instance. Sweep 1.25, 1.5 and 1.75 in both realms, with 1 and 2 as controls,
  and read `devicePixelRatio` back in every cell, since a cell that reads 1
  measured nothing. §2 "Device pixels" says which changes need this axis.
- **UI density.** `browser.uidensity`: 0 normal, 1 compact, 2 touch; live. A
  cell that does not move the box is not evidence the pref was ignored; read the
  computed value of each declaration the changed `[uidensity]` selectors set (a
  `min-height` the content already exceeds moves nothing). Set the pref
  explicitly; under Nova a short window compacts itself otherwise
  (css_guidelines "UI density" gives the thresholds).
  An in-content page has no `uidensity` attribute.
- **Writing direction.** `intl.l10n.pseudo` set to `bidi`; wait until
  `:-moz-locale-dir(rtl)` matches on the root before measuring, and clear it
  afterwards. Read where the text lands from a `Range` over the label's
  contents (`selectNodeContents`, then `getBoundingClientRect()`): a label that
  fills its box has the same element rect whichever edge its text starts from.
  Repeat with text in the opposite direction to the UI (Hebrew in an LTR UI,
  Latin in RTL), since an element with `dir="auto"`, a `<bdi>` or
  `unicode-bidi: plaintext` takes its direction from its text, and its `start`
  and `end` then follow the text, not the UI.
- **Script.** Set the label text to a few writing systems and re-measure.
- **Full-page zoom.** `ZoomManager.setZoomForBrowser(browser, 1.5)` scales the
  content document and leaves chrome alone; read both sides.

Run the sweep with the loop from §1 "The loop" in
[`references/environment-sweep.md`](references/environment-sweep.md). Before
calling a relocation neutral on the strength of a matching table, read §4 "What
the sweep misses" there: changes that resize or reparent descendants while the
box's own rect stays put.

The sweep covers the environment, not the product's own states. List the
attributes the changed selectors key on (e.g. `[open]`, `[checked]`,
`:focus-within`) and measure each state through the entry point the product uses
for it. When a declaration moves under a narrower selector, also measure the
elements the new selector no longer matches, which lose the declaration;
enumerate them with `desktop-theme-probing`'s `references/probe-recipes.md` §8.

The sweep also covers every surface and branch the changed declarations reach.
When a patch redefines or moves a custom property, list the sheets that read it
(`desktop-theme-tokens` §2 has the search) and measure each surface they style,
on every branch behind a pref or platform condition, not only the one the bug
names. Report a move the bug doesn't name as a finding with its measurement,
even one you judge harmless: scoping the change is the author's call. An
unregistered custom property reads back as its unresolved `calc()` text, so
measure a length variable by setting it as a probe element's `width`.

The font axis usually finds a `px` box holding `em` text. Report the threshold:
"the button outgrows its 20px slot from a 13px root font
upwards" names the affected platforms.

A per-monitor difference is rarely a per-monitor *cause*: the compositor's scale
factor is not what chrome CSS sees. Read `devicePixelRatio` and the computed
root `font-size` from a window on each monitor before theorising about physical
DPI or rotation.

## 2. Device pixels

**Before judging a length against the display scale, read css_guidelines "HDPI
support" with "Some properties are rounded to device pixels, but not others",
and "Chrome-only media features go by the stylesheet, not the document" before
measuring `env(hairline)`.**

Do the arithmetic with the ratio a cell reads back, not the pref's value
(css_guidelines "Some properties are rounded to device pixels, but not others"
says why 1.75 runs at 1.7647). In content, a full-page zoom other than 100%
changes the ratio too.

- **Some widths snap and the rest do not.** The same css_guidelines section
  lists them; read `layout.css.outline-offset.snapping` on the build, since its
  default differs by channel. Insets and `box-shadow` spread do not snap either.
- **Sweep the ratios for any length that is not a whole number of device pixels
  at some ratio, and for any swap between a snapped and an unsnapped length.**
  Trading a border for padding, a 1px background box for a border, or a border
  for an outline plus `outline-offset` resizes the box and moves everything
  after it (css_guidelines "Some properties are rounded to device pixels, but
  not others"). No unit is exempt, nor a `round()` to a CSS-pixel step; only an
  `env(hairline)` step lands on device pixels.
- **Measure `env(hairline)` from a `chrome://` or `resource://` sheet.** In any
  other sheet, such as a `data:` or `file://` scratch page or `userChrome.css`,
  it has no value and
  the declaration acts as `unset` (css_guidelines "HDPI support" says what that
  computes to). Before reducing it to a scratch page, read
  [`references/environment-sweep.md`](references/environment-sweep.md) §3 "What
  each axis does to a box", under "DPI / scaling".
- **Fixed-size content centered in a box sits off center wherever (box −
  content) / 2 is not a whole number of device pixels.** That covers an icon,
  image, checkbox or badge centered by flex or grid alignment, auto margins or
  symmetric padding. Read the content's offset inside its box, times
  `devicePixelRatio`, at each ratio. Layout reports the same fractional gap on
  both sides, which looks centered; paint snaps each edge, so a gap of 3.5
  device pixels paints as 3 on one side and 4 on the other. Judge whether the
  offset is whole, not whether the two sides match.
- **No CSS value snaps a position.** An edge sits at the sum of the lengths
  before it, so a snapped width after a fractional offset still lands mid-pixel.
  Reproduce a sub-pixel artifact with a fractional nudge before attributing it
  to the realm, the top layer or a transform.
- **Paint rounds each edge of a background, a border, an overflow clip and a
  `clip-path: inset()` to a device pixel, so sample the pixels along a shared
  edge at each ratio before adding anything to cover it.** Two clips at one
  layout coordinate round to the same device pixel at every offset and ratio, so
  exactly one box paints each pixel along the edge. Shifting both clips by the
  fractional remainder of the shared edge's coordinate in device pixels opens a
  gap, because each fractional inset resolves against its own snapped border
  box.

## 3. Keep the line box and the ink apart

**Judge overflow by what paints.** `normal` leading puts several pixels of air
above the ascender and below the descender, so a 21px line box in a 20px button
shows nothing wrong.

**Find what clips before turning it off.** A clip comes from `overflow` other
than `visible`, `contain: paint` (or `strict`, `content`,
`content-visibility`), `clip-path`, or the UA rule that gives `img`, `canvas`,
`video` and XUL `image` `overflow: clip; overflow-clip-margin: content-box`. An
overflow clip sits at the edge `overflow-clip-margin` names (the padding box by
default) and follows `border-radius` inset to that edge, so a large radius with
padding cuts the corners of a replaced element's content. It also clips a
descendant's decoration, such as a background at a negative inset or its
`box-shadow`, while `getBoundingClientRect` still reports the full box.
`overflow-clip-margin` widens the clip for content and decoration alike under
`overflow: clip` or `contain: paint`, and on a replaced element (e.g. `img`,
`video`, `iframe`) under any `overflow` other than `visible`; a scroll
container ignores it.

**A computed `border-radius` can overstate the rendered corner.** When the radii
along one side of a box add up to more than that side, CSS scales every radius
of the box down by one factor, so for a uniform radius the corner renders at
`min(r, height / 2, width / 2)` while `getComputedStyle` still reads `r`. Print
the box's rect beside every radius you read, and derive the rendered radius from
both. A rounded item inside a rounded container is concentric when the
container's rendered radius equals the item's rendered radius plus the gap from
the item's edge to the container's outer border edge, the border width included
in that gap.

**Diff what paints against what would paint unclipped**: render the region, turn
off every ancestor that clips at that edge (`overflow: visible`, `clip-path:
none` or `contain: none`, whichever applies), render again, and compare the two
pixel buffers. Rows that differ are what the clip removes. Several ancestors
often clip at one edge, and turning off only the nearest shows no differing rows
while the ink stays clipped, so list them first by walking
`flattenedTreeParentNode` up from the clipped box. When the clipped ink is a
border, outline or shadow, read its color in each regime of
`desktop-theme-colors` §6 before calling a branch unaffected: a diff where that
ink is transparent shows nothing, and forced colors or a `prefers-contrast` rule
can make it opaque. Pair it with a positive control, the same comparison in a
configuration that certainly overflows (e.g. a larger font, a larger offset or
outline width). In chrome the capture is one privileged `ctx.drawWindow()` call
returning counts; before trusting its pixels, read `desktop-theme-probing`'s
`references/measuring-pixels.md` §1 "Chrome and content canvases" for what it
does not composite.

**Clamp the container, not the line box.** A `line-height` below `normal`
discards the font's own metrics, and an element with a non-visible `overflow`
(which `text-overflow: ellipsis` requires) clips at its own edge, not the
visible container's. For each script, measure the label's box height against the
nearest ancestor that clips.
