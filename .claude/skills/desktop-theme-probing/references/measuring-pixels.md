# What each pixel-sampling canvas holds

## 1. Chrome and content canvases

`ctx.drawWindow()` on the chrome window captures chrome correctly but does not
composite the content process's rendering: translating content coordinates by
`tab.linkedBrowser.getBoundingClientRect()` and sampling there returns the
content area's own background (`--tabpanel-background-color`), not what the page
painted, which reads as "the element paints nothing". One image with both the
chrome and the page's pixels takes a compositor grab (§2). The reverse misleads
too: whatever chrome paints inside the browser's rect (e.g. an outline, a
shadow, a pseudo-element) lands over that background in `drawWindow`, never
under the page, so it reads as visible even where the composited page covers it.
Whether chrome ink over the content area shows takes a compositor grab.

For pixels inside an `about:` page, `SpecialPowers.snapshotRect(content, rect,
bg)` from a content task draws the page's own rendering; the surrounding chrome
is absent from that capture, so a measurement spanning a chrome box and a
content box needs two captures.

Either wrong canvas returns a uniform sampled column.

**At a ratio other than 1, size the canvas in device pixels.** Scale the context
by `devicePixelRatio`, draw whole CSS pixels and give the canvas `Math.ceil(n *
devicePixelRatio)` rows. A one-row canvas that draws `1 / devicePixelRatio` CSS
px reads back `0,0,0,0` without an error (measured at 1.5). Start the strip at
`Math.round(x * devicePixelRatio) / devicePixelRatio`; a strip that starts
between device pixels is resampled and blends its neighbours.

## 2. The compositor's output

Both canvases re-render the frame tree on request, so neither shows a stale
paint or a flash in chrome. For the compositor's output, take a screen grab with
`firefox-probe.py --no-headless --steps` and a `grab` step after the gesture, on
the display the host's platform file, section GRABS, allows; that section also
says how the grab's coordinates and scale map to the screen's. Read it with
`pixel-diff.py --rect --offset --dpr` (`desktop-theme-screenshots` §5).

Before writing the `grab` step, read §1 "Flags
and output" in [`probe-runner.md`](probe-runner.md); before picking the display,
read §1 "Pick the display rung by what the measurement needs" in
[`running-a-probe.md`](running-a-probe.md).
