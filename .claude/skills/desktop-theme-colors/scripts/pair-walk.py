#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""List the foregrounds that leave their background's system-color pair.

For each --state, locks the pseudo-classes on the --element through
InspectorUtils, which chrome and a system-principal page (e.g. about:preferences)
expose and an ordinary web page does not; :hover and :active also lock on every flat-tree
ancestor, as a real pointer sets them, as contrast.py does. It finishes the
transitions the lock starts, then walks the element and its visible descendants.
For each one it finds the surface behind it (the nearest ancestor-or-self with
a background, else the canvas as Canvas), maps that background to the system
colors it equals, resolved inside the subject so its color-scheme applies, and
checks the element's text color (`-webkit-text-fill-color`, which paints the
glyphs over `color`), an image's fill, an SVG's stroke, and its
border and outline colors against the foregrounds that pair allows. A value
outside the pair is a finding. A surface that is translucent or equals no system
color is listed as not judged, since a pair check means nothing there.

Text means a direct text-node child, the value of an input or textarea, and the
`value` of a XUL label or description. Other generated content (`::before`,
`::after`, `::marker`), `::placeholder`, `::selection`, text decorations,
shadows, the caret and the pixels of a raster image or canvas are not checked.

`-moz-context-properties` does not read back from getComputedStyle, so every
image (an img, a background-image, a list-style-image) has its fill checked; an
image that ignores fill, such as a favicon, can show up as a false finding.

Launches through desktop-theme-probing's probelib.py, headless, on a throwaway
profile, in the high contrast regime unless you pass other prefs:

  ./mach python --virtualenv wpt pair-walk.py --binary <objdir>/dist/bin/firefox \\
    --setup-file open-urlbar.js --element '.urlbarView-row[type=history]' \\
    --state default --state :hover

On macOS the binary is <objdir>/dist/*.app/Contents/MacOS/firefox.

Off Windows those prefs give chrome prefers-contrast, not forced colors; a
--setup-file with `window.browsingContext.forcedColorsOverride = "active";`
puts the window's chrome under the filter, and adding
`document.documentElement.style.setProperty("forced-color-adjust", "none");`
approximates Windows chrome, where the filter does not run.

--setup-file holds the body of an async function run in the same context
first, to bring the UI into the state under test (populate a view, open a
panel, toggle an attribute state). It may set globalThis.pairWalkSubjects to
an array of elements instead of passing --element. The launch flags, --url,
--window-size, --chrome-setup* and --steps work as in firefox-probe.py.

--element takes `host >>> inner` to reach into a shadow root, one `>>>` per
boundary.

The walk judges colors only: it does not read `opacity`, so a state that fades
instead of switching pairs walks clean. Read it computed.

stdout is the JSON result; the mozversion lines go to stderr, so keep the two
streams apart (`2>/dev/null`, or `2>file`) before parsing. A throw in the setup
or the walk exits 1 with its message and stack and prints no JSON. The exit
status is also 1 when the result has an error: no subject, a --surface that
matches nothing, or a state that cannot be locked (any pseudo-class where
InspectorUtils is missing, as on an ordinary web page in --context content, or a
name InspectorUtils does not know); and when an entry
judged nothing (`checked: 0`, with a `warning`): no foreground sat on a surface
that equals a system color, which in Linux or macOS chrome usually means the
chrome is not under forced colors (the setup above).

A border whose authored color is transparent but which paints under forced
colors is a finding (a spacer border reverts to the element color; the authored
colors are read with `forced-color-adjust: none` set temporarily on each
element). A border equal to its own surface is listed under `invisibleBorders`,
not judged against the pair.

--print-js prints the probe as an `async () => {...}` function for the devtools
MCP's evaluate_privileged_script (or evaluate_script in content, where a state
other than default needs InspectorUtils, so a system-principal page) and exits;
pass the setup to it the same way.

The surface is the nearest ancestor with a background. Where a positioned
sibling paints it instead, the walk climbs past it to an outer ancestor;
--surface names the element to use once the walk leaves the subject. It takes
`>>>` like --element.
"""

import argparse
import json
import sys
from pathlib import Path

# probelib.py sits beside the probe launcher in desktop-theme-probing.
sys.path.insert(
    0, str(Path(__file__).resolve().parents[2] / "desktop-theme-probing" / "scripts")
)

import probelib  # noqa: E402

PROG = "pair-walk"

HC_PREFS = ["ui.useAccessibilityTheme=1", "browser.display.document_color_use=0"]

CORE = r"""
  const selectors = %(selector)s, states = %(states)s, surfaceSelector = %(surface)s;
  const PAIRS = {
    Canvas: { text: ["CanvasText", "LinkText", "VisitedText", "ActiveText", "GrayText"], border: ["CanvasText", "ButtonBorder", "GrayText"] },
    Field: { text: ["FieldText", "GrayText"], border: ["FieldText", "ButtonBorder", "GrayText"] },
    ButtonFace: { text: ["ButtonText", "GrayText"], border: ["ButtonText", "ButtonBorder", "GrayText"] },
    SelectedItem: { text: ["SelectedItemText"], border: ["SelectedItemText"] },
    Highlight: { text: ["HighlightText"], border: ["HighlightText"] },
    AccentColor: { text: ["AccentColorText"], border: ["AccentColorText"] },
    Mark: { text: ["MarkText"], border: ["MarkText"] },
  };
  const frames = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  const parseRgb = v => {
    const m = v.match(/^rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)$/);
    return m && { rgb: [+m[1], +m[2], +m[3]].map(Math.round).join(","), a: m[4] === undefined ? 1 : +m[4] };
  };
  const parse = v => {
    let m = parseRgb(v);
    if (m) return m;
    m = v.match(/^color\(srgb ([\d.e-]+) ([\d.e-]+) ([\d.e-]+)(?: \/ ([\d.]+))?\)$/);
    if (m) return { rgb: [m[1], m[2], m[3]].map(x => Math.round(x * 255)).join(","), a: m[4] === undefined ? 1 : +m[4] };
    // A canvas fillStyle serializes any other color form (e.g. oklch(), lab(), color(display-p3 …)) as sRGB.
    if (!CSS.supports("color", v)) return null;
    canvasCtx.fillStyle = v;
    const s = canvasCtx.fillStyle;
    m = s.match(/^#(..)(..)(..)$/);
    return m ? { rgb: m.slice(1).map(x => parseInt(x, 16)).join(","), a: 1 } : parseRgb(s);
  };
  const canvasCtx = document.createElementNS("http://www.w3.org/1999/xhtml", "canvas").getContext("2d");
  // Resolved inside `anchor`, since a color-scheme on the way down changes what Canvas and CanvasText resolve to.
  const sysColors = anchor => {
    const probe = anchor.ownerDocument.createElementNS("http://www.w3.org/1999/xhtml", "span");
    (anchor.shadowRoot || anchor).append(probe);
    const names = new Set(Object.entries(PAIRS).flatMap(([bg, p]) => [bg, ...p.text, ...p.border]));
    const out = {};
    for (const n of names) {
      probe.style.color = n;
      out[n] = parse(getComputedStyle(probe).color)?.rgb;
    }
    probe.remove();
    return out;
  };
  const describe = el => {
    let s = el.localName;
    if (el.id) s += "#" + el.id;
    if (typeof el.className == "string" && el.className.trim()) s += "." + el.className.trim().split(/\s+/).join(".");
    return s;
  };
  const queryAll = sel => sel.split(">>>").map(x => x.trim()).reduce(
    (roots, part) => roots.flatMap(r => [...(r.shadowRoot || r).querySelectorAll(part)]), [document]);
  let subject = null, surfaceEl = null;
  const canvasRgb = new Map();
  const surfaceOf = el => {
    for (let n = el; n && n.nodeType == 1; n = parentOf(n)) {
      if (surfaceEl && n == parentOf(subject)) {
        const bg = getComputedStyle(surfaceEl).backgroundColor;
        return { el: surfaceEl, value: bg, ...parse(bg) };
      }
      const bg = getComputedStyle(n).backgroundColor;
      const p = parse(bg);
      if (p && p.a > 0) return { el: n, value: bg, ...p };
    }
    // No ancestor paints a background, so the canvas shows through.
    const root = el.ownerDocument.documentElement;
    if (!canvasRgb.has(root)) canvasRgb.set(root, sysColors(root).Canvas);
    return { el: root, canvas: true, value: "Canvas", rgb: canvasRgb.get(root), a: 1 };
  };
  const nameOf = surface => surface.canvas ? "(canvas)" : describe(surface.el);
  const visible = el => {
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && (!el.checkVisibility || el.checkVisibility({ visibilityProperty: true, opacityProperty: true }));
  };
  // The border colors the stylesheets asked for, read with the forced color adjustment off.
  const authoredBorders = el => {
    const attr = el.getAttribute("style");
    el.style.setProperty("forced-color-adjust", "none", "important");
    const cs = getComputedStyle(el), out = {};
    for (const side of ["top", "right", "bottom", "left"]) out[side] = cs.getPropertyValue(`border-${side}-color`);
    if (attr === null) el.removeAttribute("style");
    else el.setAttribute("style", attr);
    return out;
  };
  const walk = function* (el) {
    yield el;
    for (const c of el.shadowRoot ? [...el.shadowRoot.children, ...el.children] : el.children) yield* walk(c);
  };

  const mq = q => matchMedia(q).matches;
  const out = {
    regime: { forcedColors: mq("(forced-colors)"), forcedColorsActive: mq("(forced-colors: active)"), prefersContrast: mq("(prefers-contrast)"), dark: mq("(prefers-color-scheme: dark)") },
    subjects: [],
  };
  if (!out.regime.prefersContrast && !out.regime.forcedColorsActive) {
    out.warning = "neither prefers-contrast nor forced colors: only a background authored as a system color can be judged";
  }
  const subjects = selectors ? selectors.flatMap(queryAll) : (globalThis.pairWalkSubjects || []);
  if (!subjects.length) out.error = "no subject: " + (selectors ? selectors.join(", ") : "globalThis.pairWalkSubjects is empty");
  if (surfaceSelector) {
    surfaceEl = queryAll(surfaceSelector)[0];
    if (!surfaceEl) out.error = "no surface: " + surfaceSelector;
  }
  const restoreRemoteControl = liftRemoteControl(subjects.map(s => s.ownerDocument.documentElement));
  try {
  for (subject of out.error ? [] : subjects) {
    const sys = sysColors(subject);
    const keywordsFor = rgb => Object.keys(PAIRS).filter(k => sys[k] == rgb);
    const allowedKind = (kind, surface) => {
      const kws = keywordsFor(surface.rgb);
      const names = [...new Set(kws.flatMap(k => PAIRS[k][kind]))];
      return { kws, names, rgbs: new Set(names.map(n => sys[n])) };
    };
    for (const state of states) {
      const entry = { subject: describe(subject), state, surface: null, findings: [], notJudged: [], invisibleBorders: [], checked: 0 };
      let clearLocks = () => {};
      if (state != "default") {
        if (typeof InspectorUtils == "undefined") {
          entry.error = "pseudo-class locks need the chrome context";
          out.subjects.push(entry);
          continue;
        }
        const lock = lockState(subject, state);
        clearLocks = lock.clear;
        if (lock.notLocked.length) {
          entry.error = lock.notLocked.map(pc => `${pc} is not a pseudo-class InspectorUtils can lock`).join("; ");
          clearLocks();
          out.subjects.push(entry);
          continue;
        }
      }
      finishTransitions(subject.ownerDocument);
      await frames();
      try {
        const own = surfaceOf(subject);
        entry.surface = { element: nameOf(own), value: own.value, keywords: own.a == 1 ? keywordsFor(own.rgb) : [] };
        const skipped = new Set();
        for (const el of walk(subject)) {
          if (!visible(el)) continue;
          const surface = surfaceOf(el);
          if (surface.a < 1 || !keywordsFor(surface.rgb).length) {
            if (!skipped.has(surface.el)) {
              skipped.add(surface.el);
              entry.notJudged.push({ surface: nameOf(surface), value: surface.value, reason: surface.a < 1 ? "translucent" : "not a system color" });
            }
            continue;
          }
          const cs = getComputedStyle(el);
          const checks = [];
          const authored = out.regime.forcedColorsActive ? authoredBorders(el) : null;
          const text = textOf(el, cs);
          if (text) checks.push(["-webkit-text-fill-color", text.color, "text"]);
          const svg = el.namespaceURI == "http://www.w3.org/2000/svg";
          // -moz-context-properties does not read back from getComputedStyle, so any image counts as using fill.
          const image = el.localName == "img" || cs.backgroundImage != "none" || cs.listStyleImage != "none";
          if ((svg || image) && parse(cs.fill)) checks.push(["fill", cs.fill, "text"]);
          if (svg && parse(cs.stroke)) checks.push(["stroke", cs.stroke, "text"]);
          for (const side of ["top", "right", "bottom", "left"]) {
            if (parseFloat(cs.getPropertyValue(`border-${side}-width`)) > 0 && !["none", "hidden"].includes(cs.getPropertyValue(`border-${side}-style`))) {
              checks.push([`border-${side}-color`, cs.getPropertyValue(`border-${side}-color`), "border", authored && authored[side]]);
            }
          }
          if (cs.outlineStyle != "none" && parseFloat(cs.outlineWidth) > 0) checks.push(["outline-color", cs.outlineColor, "border"]);
          for (const [prop, value, kind, authoredValue] of checks) {
            const p = parse(value);
            if (!p || p.a == 0) continue;
            entry.checked++;
            const was = authoredValue && parse(authoredValue);
            if (kind == "border" && was && was.a == 0) {
              entry.findings.push({
                element: describe(el), prop, value, authored: authoredValue,
                problem: "transparent as authored, painted under forced colors (a spacer border reverts to the element color)",
                surface: nameOf(surface), surfaceKeywords: keywordsFor(surface.rgb),
              });
              continue;
            }
            if (kind == "border" && p.a == 1 && p.rgb == surface.rgb) {
              entry.invisibleBorders.push({ element: describe(el), prop, value, surface: nameOf(surface) });
              continue;
            }
            const allowed = allowedKind(kind, surface);
            if (p.a == 1 && allowed.rgbs.has(p.rgb)) continue;
            entry.findings.push({
              element: describe(el), prop, value,
              equals: Object.keys(sys).filter(n => sys[n] == p.rgb && p.a == 1),
              surface: nameOf(surface), surfaceKeywords: allowed.kws, allowed: allowed.names,
            });
          }
        }
      } finally {
        clearLocks();
      }
      if (!entry.checked) entry.warning = "checked nothing: no foreground sat on a surface that equals a system color";
      out.subjects.push(entry);
    }
  }
  } finally {
    restoreRemoteControl();
  }
  return out;
"""

BODY_TEMPLATE = r"""
  await (async () => {
%(setup)s
  })();
%(core)s
"""

FUNCTION_TEMPLATE = "async () => {%s}"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    probelib.add_launch_arguments(ap, binary_required=False)
    probelib.add_page_arguments(ap)
    ap.add_argument(
        "--element",
        action="append",
        default=None,
        help="selector for the subject(s), repeatable; every match is walked",
    )
    ap.add_argument(
        "--state",
        action="append",
        default=None,
        help='"default" or pseudo-classes like ":hover:active", repeatable; '
        ":hover and :active also lock on every flat-tree ancestor",
    )
    ap.add_argument(
        "--surface",
        default=None,
        help="selector for the surface behind the subject when a positioned sibling paints it",
    )
    ap.add_argument(
        "--setup-file", default=None, help="body of an async function run first"
    )
    ap.add_argument("--context", choices=["chrome", "content"], default="chrome")
    ap.add_argument(
        "--no-hc-prefs",
        action="store_true",
        help="do not add ui.useAccessibilityTheme=1 and document_color_use=0 "
        "ahead of the --setpref values",
    )
    ap.add_argument(
        "--print-js",
        action="store_true",
        help="print an async function for the devtools MCP and exit",
    )
    args = ap.parse_args()
    states = args.state or ["default", ":hover"]
    setup = Path(args.setup_file).read_text(encoding="utf-8") if args.setup_file else ""
    core = CORE % {
        "selector": json.dumps(args.element),
        "states": json.dumps(states),
        "surface": json.dumps(args.surface),
    }
    body_js = probelib.PRELUDE + BODY_TEMPLATE % {"setup": setup, "core": core}
    if args.print_js:
        print(FUNCTION_TEMPLATE % body_js)
        return 0
    if not args.binary:
        ap.error("--binary is required unless --print-js")
    probelib.check_page_arguments(ap, args)
    if not args.no_hc_prefs:
        args.setpref = HC_PREFS + args.setpref

    def body(client):
        probelib.open_page(client, args, args.context)
        steps_out = None
        if args.steps:
            steps_out = probelib.run_steps(
                client,
                args.steps,
                PROG,
                headless=args.headless,
                grab_any_display=args.grab_any_display,
            )
            if steps_out and "error" in steps_out[-1]:
                print(probelib.dump({"steps": steps_out}, indent=2))
                return 1
        result = probelib.evaluate(
            client, body_js, is_async=True, timeout_ms=args.timeout_ms
        )
        shown = result if steps_out is None else {"steps": steps_out, "result": result}
        print(probelib.dump(shown, indent=2))
        errors = [result["error"]] if result.get("error") else []
        for entry in result.get("subjects", []):
            label = f"{entry['subject']} {entry['state']}"
            if entry.get("error"):
                errors.append(f"{label}: {entry['error']}")
            elif not entry["checked"]:
                errors.append(f"{label}: {entry['warning']}")
        for e in errors:
            print(f"{PROG}: error: {e}", file=sys.stderr)
        return 1 if errors else 0

    return probelib.run(args, PROG, body)


if __name__ == "__main__":
    sys.exit(main())
