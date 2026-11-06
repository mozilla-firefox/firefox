# Positioning: the containing block, the top layer, anchors

Anchor positioning is changing; verify each of these against the build in
front of you.

## 1. What a positioned box resolves against

- **An absolutely positioned box resolves against the padding edge of its
  nearest ancestor whose `position` is not `static`**, and against the initial
  containing block (the viewport-sized rectangle at the canvas origin) when
  there is none. Percentages in `inset`, `width`, `height` and `max-height`
  resolve against that box, not against the parent element.
- **A `transform` or an individual `translate`, `rotate` or `scale`,
  `transform-style: preserve-3d`, `offset-path`, `filter`, `backdrop-filter`,
  `perspective`, `contain: layout` or `paint` (and so `strict`, `content` and
  `content-visibility: auto` or `hidden`), or a `will-change` naming one of
  them, makes the ancestor the containing block for absolute and fixed
  descendants alike.** A fixed descendant of a transformed toolbar or panel
  positions and scrolls with that ancestor, not the viewport, while its computed
  `position` still reads `fixed`. `container-type` does not do this, so a size
  container leaves a fixed descendant on the viewport. Before explaining a
  misplaced fixed box, walk its ancestors for these properties.
- **`position: fixed` takes the box out of the page's scrollable overflow and
  resolves its percentages against the viewport**, but the element can no longer
  be scrolled into view. Clamping to the viewport can leave nothing visible
  where the thing the box belongs to is off-screen.

## 2. The top layer

- **A top-layer element with `position: absolute` resolves against the initial
  containing block and contributes to the document's scrollable overflow.** The
  UA sheet makes `[popover]` and a modal `dialog` `position: fixed`, which stays
  out of it (§1), but an anchored popover usually overrides that with
  `absolute`, so the popover can grow the page and add a scrollbar.
- **Decorative boxes count; ink does not.** An absolutely positioned background
  at a negative inset contributes scrollable overflow, so it can grow the page
  on its own. `box-shadow` and `outline` do not. A box pushed past its
  ancestor's border box by a negative margin or inset contributes scrollable
  overflow the same way.
- **The UA sheet gives a popover `overflow: auto`, so it clips decoration that
  reaches outside it.** SKILL.md §3 "Keep the line box and the ink apart" has
  the mechanism.

## 3. Anchor positioning

- **`max-height: 100%` on an anchored element is not "the viewport".** With
  `position-area`, percentages resolve against the position-area region, which
  for a box in a scroll container spans the container's scrollable overflow, so
  on a page that already scrolls the clamp can reach past the viewport; a
  `position: fixed` box's region stays inside the viewport. Measure what `100%`
  resolved to before relying on it.
- **Anchor positioning compensates for scroll with a translation, not a
  re-layout.** Scrolling slides the box but never gives it more room, until its
  placement stops fitting; then scrolling triggers a reflow that tries
  `position-try-fallbacks` again.
- **`position-try-fallbacks` fires when the box's size exceeds the position
  area, which a `max-height` clamp prevents.** Overflow from `min-height`
  beating the clamp does count; content spilling out of a fixed-size box does
  not. A scrollable containing block makes an option reaching far outside the
  viewport count as fitting, since the area follows its scrollable overflow
  (above). The test compares sizes only, so where an option lands the box does
  not reject it. The property can be supported, applied and present on the
  computed style while never engaging, so check the computed `position-area`
  rather than the fallback property.
- **An absolutely positioned box that overflows its position area slides back
  inside the original containing block** once every fallback has failed. Whether
  its size is auto or explicit, a box larger than its position area but no
  larger than the containing block on both axes is shifted to fit the containing
  block, so a box placed below or beside its anchor can end up covering the
  anchor; a box larger than the containing block on either axis is shifted on
  neither, start-aligns and overflows. A box no larger than its area never
  slides, so a reduction with a small fixed size fails to reproduce, and capping
  the box at the area (`100%` resolves against it) under `box-sizing:
  border-box` keeps it in place. No `align-self` value opts out in Gecko, not
  even the `unsafe` forms (bug 2072587).
- **`position-area`'s `span-*` values include the anchor's own row or column.**
  `span-block-end` runs from the anchor's block-*start* edge to the container's
  block end, so a box meant to sit below the anchor and not over it wants plain
  `block-end`.
- **An anchor must be laid out before the box anchored to it.** For a top-layer
  element that compares top-layer order; otherwise the anchor must sit inside
  the positioned element's containing block (§1), or that containing block must
  be the initial containing block. A top-layer popover can anchor to an element
  in the page, but an absolutely positioned *child* of that popover cannot: its
  containing block is the popover, and the anchor is not inside it.
- **`inline-size: stretch` is behind
  `layout.css.stretch-size-keyword.enabled`**, off by default, so `CSS.supports`
  returns false on a normal build; `-moz-available` works. To fill a position
  area minus the box's own margins, `inset-inline: 0` with `justify-self:
  stretch` works and is unprefixed.
- **`@position-try` accepts only insets, margins, sizing, self-alignment,
  `position-anchor` and `position-area`.**
