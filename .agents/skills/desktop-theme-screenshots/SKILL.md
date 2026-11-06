---
name: desktop-theme-screenshots
description: Before/after screenshots for a bug or review; pixel-diffing a pair to prove a change visually neutral; shooting a hover or open-menu state; sampling a reporter's image; showing a hit area, focus target or stacking order.
---

# Produce before/after visual evidence for a front-end change

Instruments and display rungs: load `desktop-theme-probing` before the first
capture; it defines the host's and the target's platform file that this skill
cites by section. To decide which states
show the change, also load the sibling that owns its subject:
`desktop-theme-colors` for a forced-colors, contrast or color-scheme state,
`desktop-theme-layout` for geometry, `desktop-theme-tokens` for a token value.

## 1. Decide what state makes the change visible, before writing any capture code

An identical pair does not prove the change neutral: the state that shows the
difference may never have been on screen. Work out from the diff which rendered
state differs. A rule that only applies under `[selected]`, `:hover`,
`:focus-visible` or a media query produces byte-identical screenshots in the
default state.

**When the diff changes a color, read the "Colors that resolve per realm"
section of `docs/code-quality/coding-style/css_guidelines.md` before choosing
what to shoot.** The doc lives in the source tree, not in this skill's
directory, and it's kept up to date with how different parts of the code base
interact in non-obvious ways; it may differ from what you remember.

Sort the diff into one of these cases by where its difference shows; the case
decides what you capture, force or log:

- **Visible in the default state.** Capture and move on.
- **Visible only in a state.** Force the state rather than driving the UI to it:
  synthesized keys and mouse events fail quietly, leaving the pair identical
  with nothing in the log. An attribute takes
  `element.toggleAttribute("selected", true)`. An element-state pseudo-class,
  focus included (e.g. `:hover`, `:focus-visible`, `:checked`), takes
  `InspectorUtils.addPseudoClassLock` on the element the selector names, often
  an ancestor of the one you shoot (the lock rule in `desktop-theme-probing` §2
  "Keep the probe honest"). A media-query state takes its pref
  (`desktop-theme-colors` §2). Focus that a widget's own key handling moves
  between items (e.g. in a menu or list) is the exception: a lock neither moves
  focus nor picks the item the keys would, so drive it with `keys` steps and
  print `:focus-visible` beside each reading. In a content page, the probe
  runner's `--steps` `move` step makes `:hover` follow the pointer. `capture.js`
  finishes running transitions before each shot; elsewhere finish them yourself
  (`desktop-theme-probing` §2 "Reach and read the state"). Restore the state if
  later captures depend on it.
- **Too small for the capture to isolate.** A color that moves a few sRGB units
  in any painted property, or a box that moves less than a device pixel, is lost
  to rounding or to `pixel-diff.py`'s threshold (§5). Log the computed value of
  each property the diff changes, on the element it changes, in both halves, and
  hand over the measurement, as for a 1px `color-mix()` border. A hue change
  across a wide area (a translucent shadow's color) can stay visible below the
  threshold: diff it at `--threshold 0`, and attach a `zoom-pair.py` crop when
  the zoom shows it.
- **Visible only for some content or configuration.** A change to what clips,
  overflows, truncates or scrolls shows only where content reaches the boundary
  (e.g. a long label, an icon drawn to its corners, RTL), and a rule under a
  feature pref, a density, a theme or a device pixel ratio shows only there; a
  swap between a snapped and an unsnapped length shows only at some device pixel
  ratios (`desktop-theme-layout` §2 "Device pixels"). Find the instances first
  with an inline old-against-new differential over every candidate
  (`desktop-theme-probing` §4), and shoot those; `desktop-theme-layout` §1 owns
  the configuration sweep. Enumerate the candidates from the changed rule:
  every element its selector matches (`querySelectorAll` with the state
  pseudo-classes stripped, in each document and shadow root that loads the
  sheet), and on each, every edge or property the declaration paints.
- **Not visible in any state.** A change to a hit area, a clip boundary, a
  scroll extent, a focus target or the stacking order moves no pixels. Draw or
  log the invisible thing (§4).

## 2. Build the capture harness

| Surface | Route |
|---|---|
| Chrome, rendered by a suite's fixture | a scratch browser-chrome test pasting `capture.js`; `capture-pair.sh` (§3) runs it |
| Chrome, no fixture needed | `firefox-probe.py --context chrome --async` running `capture.js` |
| Content, such as a web page or an about: page | `firefox-probe.py --steps` with a `drawSnapshot` step; the devtools MCP last |
| Chrome ink inside the browser's rect, such as an outline or border drawn over the page's edge | a compositor grab: `drawWindow` paints chrome over the tab panel's background, never under the composited page, so ink the page covers reads visible (§1 "Chrome and content canvases" in `desktop-theme-probing`'s [`measuring-pixels.md`](../desktop-theme-probing/references/measuring-pixels.md)) |
| An open panel or menupopup | a compositor grab (§2 "The compositor's output" in `desktop-theme-probing`'s [`measuring-pixels.md`](../desktop-theme-probing/references/measuring-pixels.md)) |

`drawWindow` and the chrome window's `drawSnapshot` both leave an open popup out
and draw what lies under it. A popup here is a `<panel>` or `<menupopup>`
element; a dropdown built from ordinary elements in the window captures like
the rest of the chrome. On Linux a compositor grab of a popup needs `--setpref
ui.GtkCSDTransparencyAvailable=1` in both halves: without it `popup.css` paints
the popup square with no shadow margin, and a change to its radius or shadow
yields an identical pair (`platform-linux.md`, section HEADLESS, in
`desktop-theme-probing`).

**`capture.js`.** Paste [`scripts/capture.js`](scripts/capture.js) at the head
of the test or probe and call `await capture(name, element)` once per shot (it
is async; a call left unawaited ends the probe before any `SHOT` line), with
every axis the probe varies (e.g. density, ratio, state) in `name`, since a
repeated name overwrites the earlier image. Paste it rather than load it by
path, and don't retype it: without its `ctx.scale(dpr, dpr)`, `drawWindow` draws
at 1x into a canvas sized for the device pixel ratio, so above a ratio of 1 the
image fills only part of it. It draws the element's border box plus 8 CSS px
with `drawWindow` (`capture(name, element, { pad: 24 })` keeps a wider shadow or
outline in frame), in the CSS pixels of the element's window, after lifting the
root's `remotecontrol` attribute (the harness's stripes over the address bar).
The background is white; for white ink, pass `{ background: "magenta" }` and
check the ink's pixel count. Its `SHOT` line gives the frame's size, `dpr=` and
`origin=` in the window, all in whole CSS px; it throws for an element with no
box or outside the window. Log the element's rect and the computed value of each
property the diff changes beside it (§5).

- **The label and the output directory come from prefs**, `shots.label` and
  `shots.dir`, read at module scope: pass them at launch (`--setpref`, to the
  mochitest harness or the probe runner) or in the profile. A
  `pushPrefEnv` inside a task is too late and silently yields `unlabeled` under
  `PathUtils.tempDir`. `shots.dir` must be absolute and must already exist. The
  same unedited file produces both halves of the pair.

**Test route.** Drop the file in the `browser/` test directory nearest the UI
under test and register it (§2 "Register and run it" in
[`scratch-probe.md`](../desktop-theme-probing/references/scratch-probe.md)).
Reuse whatever renders the surface in the suite that covers it (e.g. a mock
server, a test provider), not a hand-built node that merely matches the
selectors.

**Probe route.** Paste `capture.js` into an `--async` body, which may `await`
and `return`s its result (§2 "The chrome sandbox and `--async`" in
`desktop-theme-probing`'s
[`references/probe-runner.md`](../desktop-theme-probing/references/probe-runner.md)).
The suite's fixture is out of reach: a state the product's own providers cannot
produce on that profile is forced on a real node or built by hand, with the
caption saying so. Open a tab with `gBrowser.addTrustedTab(url)`;
`gBrowser.addTab` throws without a `triggeringPrincipal`. The `SHOT` lines go to
the browser's stdout, so include `globalThis.__shots` in what the probe returns
(`{...readings, shots: globalThis.__shots}`), or pass `--gecko-log` and grep
`SHOT` from it. A string-typed pref that looks like an integer takes single
quotes, as in `--setpref "layout.css.devPixelsPerPx='2'"`.

**Content route.** A `--steps` list run with `--context content --url <page>`
drives the page headless, and a `chrome` step calling
`gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.drawSnapshot(rect,
scale, "white")` composites the content process into an `ImageBitmap`; draw it
on a canvas and write the PNG with `IOUtils.write`. Put the work in an async
function and `return` its promise from the step: Marionette waits for a returned
promise, and without the `return` the step ends before the file is written. In a
`chrome` step `document` is the browser window's, so
`document.createElement("canvas")` gives an HTML canvas. `--help` lists the
step kinds. `capture.js` cannot see the page (§1 "Chrome and content canvases"
in
[`measuring-pixels.md`](../desktop-theme-probing/references/measuring-pixels.md)).
`capture-pair.sh` drives only the test and chrome-probe routes; for a content
page or a compositor grab (which needs `--no-headless` on the display the
host's platform file in `desktop-theme-probing`, section GRABS, allows), run
each half yourself with the by-hand revert in `desktop-theme-probing` §6
between them.

**Devtools MCP route.** Use `screenshot_page` or `screenshot_by_uid`, headless,
on this tree's binary (`desktop-theme-probing` §1's binary check). Before the
first `restart_firefox`, read §1 "Binary and profile", §2 "Prefs" and §5
"Screenshots" (where `saveTo` may write) in
[`devtools-mcp.md`](../desktop-theme-probing/references/devtools-mcp.md). The
before half is the by-hand revert in `desktop-theme-probing` §6 followed by
`restart_firefox`. The screenshots clip to the viewport, and raising
`layout.css.devPixelsPerPx` shrinks the CSS viewport at a
fixed window size, so call `set_viewport_size` after the ratio is in force and
check the frame size against the element's rect: a pair cropped alike looks
consistent.

**On every route the profile is empty and online.** Seed what the surface lists
and undo the launch's other differences per `desktop-theme-probing` §2 "A probe
launch is not a user's profile". Anything fed from the network or the clock
(e.g. suggestions, stories, remote settings, dates) can differ between the
halves; turn those sources off or mock them (a mock server through the
provider's endpoint pref).

## 3. Capture before and after without building twice

`./mach mochitest` installs the test file itself, so one committed change and
one scratch test give both halves.
[`scripts/capture-pair.sh`](scripts/capture-pair.sh) reverts each `<path>` to
`--base` for the before half and restores it afterwards; read its `--help`
before passing any option. Its `--probe` form takes a chrome-context script
pasting `capture.js` in place of a test, for a chrome surface no suite's fixture
renders. One call captures one cell of a regime matrix; run it once per cell,
with that cell's prefs and setup file.

- **Each `<path>` is a file the browser loads.** For a `*.tokens.json` change,
  pass the generated token CSS that `./mach buildtokens` writes into the tree.
- **The change is committed, and each path exists at both ends.** Pair an
  uncommitted change with the by-hand revert in `desktop-theme-probing` §6.
- **A test file reached through several manifests runs once per manifest**, and
  each run overwrites the last one's output, so a pair keyed only on
  `before`/`after` compares the last variant to run. Put the variant in the
  filename, from a marker each manifest sets (`prefs = ["shots.variant=nova"]`,
  read with `Services.prefs.getCharPref("shots.variant", "base")`). The feature
  pref cannot tell them apart when the base manifest's default equals another
  manifest's value. The other variants are a free control (§5).

Run headless: the capture needs rendering, not a window manager or an input
seat. Shoot in the report's regime (`desktop-theme-colors` §1), and in both
Linux native-theme branches where that section says to. A headless run paints
the host's look and feel, not the user's: before shooting a native-theme,
system-color or scrollbar change, read the host's platform file in
`desktop-theme-probing`, section HEADLESS (the host is the machine the run is
on, even when the report names another platform), for the prefs to pin or the
reason to take a real display rung (section DISPLAYS).

## 4. Draw what has no pixels

Each invisible kind gets a marker or a reading, the same in both halves, and
the caption says what the marker means:

- **Hit area or border box: an outline**, `element.style.outline = "2px solid
  red"` with `outlineOffset = "-2px"`. The negative offset traces the box just
  inside its border edge and survives an ancestor with `overflow: hidden`; at
  `0px` it draws outside the border box, where such an ancestor clips it into
  two brackets. Where nothing clips and the border box itself is the thing
  shown, `0px` traces that edge truer; the inset one reads a couple of pixels
  small. A `border` would change the box under measurement and `box-shadow`
  blurs the edge. Outline every peer in the group so the spacing reads.
- **Painted box, for a background the theme never sets: a marker
  `background-color`** on the element.
- **Clip boundary: an oversized marker child.** An absolutely positioned child
  with a solid background, larger than the clipping box on every side, makes
  the clip edge the ink edge, so a moved clip or an `overflow-clip-margin` shows
  as the marker's edge. An element that renders no children, such as an
  `img`, cannot hold the marker: log the clip's geometry and outline that box
  with an inset outline, or shoot the element again with `overflow: visible`
  and compare.
- **Scroll extent: a reading.** Log `scrollWidth`/`scrollHeight` against
  `clientWidth`/`clientHeight` beside an outline of the scrolled content.
- **Focus target: an outline on `document.activeElement`** after the focusing
  action, with its id logged.
- **Stacking order: a reading.** Log `elementFromPoint` over the overlap, or
  give the overlapping boxes distinct marker backgrounds.

Under forced colors the adjustment repaints a literal marker (a red outline
takes the text color, a marker background turns `Canvas`): mark per
`desktop-theme-probing` §4.

## 5. Verify the pair actually differs

`cmp` the two files. Differing byte sizes prove a difference; equal ones prove
nothing. An identical pair means one of three things; say which:

- you captured the wrong state (§1), the usual cause;
- the change genuinely does not affect that surface;
- the run did not pick up the revert.

To tell them apart, log the computed value of each property the diff changes,
and the subject's state, in each half. Values that differ mean the revert took;
a selector that matches (`getMatchingCSSRules`, `desktop-theme-probing` §2
"Reach and read the state") means the state was right. A control pair, a variant
the patch does not apply to (e.g. a different theme, a pref-off manifest),
guards the other direction: if it differs, the capture picks up run-to-run noise
and neither half is trustworthy.

To learn whether the files differ *where you changed something*,
`scripts/pixel-diff.py` prints the count of differing pixels and their bounding
box, confined by `--rect` to the element; a zero count also reports the
differences under `--threshold`, which is §1's "too small" case. `--help`
covers the rect's origin, `--dpr`, `--offset`, `--lines` (what a caption for a
change one or two device pixels wide quotes) and the exit codes; the host's
platform file in `desktop-theme-probing`, section GRABS, gives a grab's scale.
It needs Pillow, which the `wpt` site carries. On Linux the site's first run can
print a `PyGObject` build failure, an optional package, and then succeed, so
ignore the warning and read the exit code:

```sh
./mach python --virtualenv wpt -- .claude/skills/desktop-theme-screenshots/scripts/pixel-diff.py \
  after-<name>.png before-<name>.png --rect <x>,<y>,<w>,<h>
```

Chrome text antialiasing moves between runs, so a window-level pair routinely
differs across the whole toolbar while the surface under test is byte-identical.
`--serve` picks a fresh port per launch, so the address bar's URL differs as
well; load a `file://` URL or `about:blank` when the page needs no origin.
Report the `--rect` count for the element. When the change moves layout (every
row a pixel taller), no fixed rect isolates it and the count covers the whole
view; then the geometry table beside the shots is the evidence, not the count.

## 6. Read a reporter's screenshot by sampling it

Exhaust the live measurements first; open someone else's image only for what
their state alone can answer, and re-render a state you need as a pair (§1–§3).
Where the result depends on an OS backdrop this host cannot paint (the target's
platform file, section COMPOSITING), a local capture shows nothing: sample
the reporter's backdrop and composite the computed layers over it by
arithmetic instead, and say so.
Sample it rather than stare at it, through `./mach python --virtualenv wpt`
(§5 has the Pillow caveats):

```python
from PIL import Image

im = Image.open("attachment.png").convert("RGB")
for y in range(y0, y1):  # walk across the feature in question
    print(y, im.getpixel((x, y)))  # x inside the element under test
```

Walk a line across the boundary or fill in question and read where the color
changes and what it changes to; for a clip, check whether the content's own
pixels continue past the box's edge. Divide by the device pixel ratio before
comparing against CSS pixels you measured yourself. A reporter's image does
not state its ratio: take it from a length you know in CSS pixels, such as a
1px border's run of device rows or the size of an element you measured on your
build in the reporter's density and theme, and check it on a second one.

## 7. Clean up, and hand the images over

Clean up per `desktop-theme-probing` §6. Name the files so the pair is obvious
(`before-<variant>-<subject>.png`), tell the user the directory, and say for
each pair what to look at and what it was taken in: the element state and
whether it was forced or the node hand-built, the color regime and theme,
feature prefs, density, device pixel ratio and platform. A change a few device
pixels wide is invisible in a full-window shot, so attach a zoom beside the
pair: `scripts/zoom-pair.py before.png after.png zoom.png --rect X,Y,W,H`
(`--help`).

Attach each image with the `bug-filing` skill's
`.agents/skills/bug-filing/attach-to-bug.py`, following that skill's dry-run
steps; pass `--file-name` where several variant directories each hold a
`before.png`. Attaching is outward-facing and permanent: confirm with the user
first and say which bug.
