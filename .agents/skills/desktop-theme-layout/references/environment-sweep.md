# The environment sweep

The sweep is one script that walks UI density, device pixel ratio and root font
size as nested loops and prints one tab-separated row per cell. Run it through
the devtools MCP's `evaluate_privileged_script` as `async () => { … }`, or as a
`firefox-probe.py --context chrome --async` snippet in the shape
`desktop-theme-probing` §1 gives.

## 1. The loop

A dppx change rescales the CSS viewport, so the loop holds the window at one CSS
size per cell; a sweep taken without that resize diffs as window-size noise.

```js
const SELECTOR = "…"; // the box under test
const W = 1000, H = 700;
const win = Services.wm.getMostRecentBrowserWindow();
const doc = win.document;
const root = doc.documentElement;
const el = doc.querySelector(SELECTOR);
const sleep = ms => new Promise(r => win.setTimeout(r, ms));
async function settle() {
  for (let i = 0; i < 30; i++) {
    win.resizeTo(W, H);
    await sleep(150);
    if (win.outerWidth == W && win.outerHeight == H) {
      await sleep(150);
      return "";
    }
  }
  return "UNSETTLED ";
}
const name = e =>
  (e && (String(e.className).split(" ")[0] || e.localName)) || "null";
const rect = e => {
  let b = e.getBoundingClientRect();
  return [b.left, b.top, b.right, b.bottom].map(n => n.toFixed(2)).join(",");
};
const rows = ["density\tdppx\tdpr\tfont\tinner\tbox\thit:mid\thit:end"];
for (const density of [0, 1, 2]) {
  Services.prefs.setIntPref("browser.uidensity", density);
  for (const dppx of [1, 1.25, 1.5, 1.75, 2]) {
    Services.prefs.setCharPref("layout.css.devPixelsPerPx", String(dppx));
    for (const fs of [11, 12, 13, 15, 18]) {
      root.style.fontSize = fs + "px";
      const unsettled = await settle();
      const b = el.getBoundingClientRect();
      const midY = (b.top + b.bottom) / 2;
      const rtl = win.getComputedStyle(el).direction == "rtl";
      rows.push([density + ":" + (root.getAttribute("uidensity") || "normal"),
        dppx, win.devicePixelRatio.toFixed(4), fs,
        unsettled + win.innerWidth + "x" + win.innerHeight, rect(el),
        name(doc.elementFromPoint((b.left + b.right) / 2, midY)),
        name(doc.elementFromPoint(rtl ? b.left + 2 : b.right - 2, midY))
      ].join("\t"));
    }
  }
}
root.style.fontSize = "";
Services.prefs.clearUserPref("browser.uidensity");
Services.prefs.setCharPref("layout.css.devPixelsPerPx", "-1.0");
return rows.join("\n");
```

Return the rows as one tab-separated string rather than a nested object; the
loop is 75 cells and takes about 25 seconds headless. Over the MCP a loop this
long outlives the BiDi command timeout; §4 "Command timeout" in
`desktop-theme-probing`'s `references/devtools-mcp.md` has the detached form.

## 2. The columns

- `density` is the pref value and the root's `uidensity` attribute, which
  differ when the browser overrides the pref (css_guidelines "UI density").
- `dpr` is `devicePixelRatio` read back, which can differ from the requested
  `dppx`: 1.75 runs as 1.7647 (SKILL.md §2 "Device pixels").
- `inner` is `innerWidth x innerHeight` after the per-cell resize, prefixed
  `UNSETTLED` when the window never reached the requested outer size. Keep the
  check on `outerWidth`/`outerHeight`, which land on whole CSS pixels; the inner
  size comes out fractional at 1.7647, so a check on it never settles there.
  Check that two tables' cells line up on it before diffing them.
- `box` is the element's border-box rect to two decimals.
- `hit:mid` and `hit:end` name the element `elementFromPoint` finds at the
  box's center and two pixels inside its inline-end edge, the left edge in RTL.
  Add a point for each edge the change under test could have moved, and a column
  for `(child.left - box.left) * win.devicePixelRatio` when the box centers
  fixed-size content.

## 3. What each axis does to a box

### System font size

`document.documentElement.style.fontSize` also takes a relative value
(`"1.2em"`) to scale around whatever the system hands you. Chrome's `rem` tokens
inherit from the root, so a `body` override misses them. Besides the font
keywords css_guidelines "Chrome versus in-content" names for popups, `caption`
resets `font-size` too.

### Scrollbars

A live change of `ui.useOverlayScrollbars` triggers no reflow. Read it back
with `win.matchMedia("(-moz-overlay-scrollbars)").matches` in each cell.

### DPI / scaling

A `1px` border against `1px` padding matches at dppx 1 and 2, but the border is
0.8px at 1.25, 0.667px at 1.5 and 0.567px at 1.75 (ratio 1.7647), so the box
grows by 0.4px, 0.67px and 0.87px across two borders.

`env(hairline)` resolves only in the sheets css_guidelines "Chrome-only media
features go by the stylesheet, not the document" lists, and acts as `unset`
anywhere else ("HDPI support" there says what each declaration then reads). A
`data:` sheet stays outside the list even inside `browser.xhtml`. Measure from a
`chrome://` or `resource://` sheet, or give the `env()` a fallback.

### Writing direction

A box built from `left`/`right`, or from a one-sided padding, fails only in
RTL. Setting `dir="rtl"` on the chrome root is the shortcut when the strings
must stay English, but `:-moz-locale-dir()` matches on a document state bit
rather than on the element's `dir`, so a `dir` flip leaves every
`:-moz-locale-dir(rtl)` rule unmatched, and it cannot reproduce a label that
only overflows once translated.

### Script

`line-height: normal` is whatever the font's ascent and descent ask for, so it
varies by script and by the face the platform picks (on one Linux host Arabic
ran 50% taller than Latin).

### Full-page zoom

`ZoomManager.setZoomForBrowser` is in `toolkit/content/viewZoomOverlay.js`. A
widget rendered in both chrome and a page measures differently in the two at
any zoom but 1.

## 4. What the sweep misses

**An element's own rect cannot show that it stopped establishing a containing
block or a stacking context.** When a rule that made it a containing block
(`positioning.md` §1 lists the properties) stops applying, its border box is
unchanged while its absolutely positioned and fixed descendants reparent to
another containing block. Losing `z-index`, `opacity` below 1 or `isolation`
changes the paint order the same way. Every cell of the table matches. Measure
the descendants' rects, and compare the `hit:` columns against the same points
before the change to name the covering element.

**A size constraint bounds whichever box `box-sizing` names**, the content box
by default and the border box on a XUL element, whose UA sheet sets
`border-box`, so read the computed value. Moving padding (or a border) of `P`
per side from a parent onto its child turns the parent's height from `max(minH,
child) + 2P` into `max(minH, child + 2P)`, which is up to `2P` shorter wherever
`minH` exceeds the child's content height. That can be every cell at small
fonts, so add the moved padding to the `min-height` (`minH + 2P`). The same
holds for `height`, `max-height`, their inline-axis counterparts and
`flex-basis`; under `box-sizing: border-box` the move is neutral.

**An alignment change on a flex or grid container can resize its children while
the container's rect stays put.** A child with no explicit size stretches by
default (across a flex line, or across its grid area on both axes unless it is a
replaced element); any other `align-items`, `justify-items`, `place-*`,
`align-self` or `justify-self` value sizes it to its content, and its hit area
and text-selection area shrink with it, as they do when `align-content` or
`justify-content` stops stretching wrapped flex lines or `auto` grid tracks.
When a patch changes any of these, measure every child with no explicit size on
that axis, and hit-test two pixels inside each edge of the container.
