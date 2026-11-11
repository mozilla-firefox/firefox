#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Measure the WCAG contrast of the text an element and its descendants paint,
or with --non-text, of the outlines and borders they paint.

For each --state, locks the pseudo-classes on each --element (InspectorUtils);
:hover and :active also lock on every flat-tree ancestor, as a real pointer
sets them, so the element can be a descendant of the one the rule names. It
then finishes running transitions, then walks the element and its visible
descendants. For each one with text it reads the computed text color and
composites the backgrounds under it: its own and each flat-tree ancestor's
(through slots and shadow hosts), down to the first opaque one, else the canvas
as `Canvas`. The ratio comes from devtools' `colorUtils.calculateContrastRatio`,
the function the accessibility inspector uses, which also blends translucent
text over the composited background. Each entry is judged against WCAG AA:
4.5:1, or 3:1 for large text (24px, or 18.66px at weight 700 and up); --min
overrides both, such as 7 under prefers-contrast.

Text means a direct text-node child, the value of an input or textarea, and the
`value` of a XUL label or description. Generated content, `::placeholder`,
`::selection`, shadows and text decorations are not measured.

A background image or `backdrop-filter` anywhere in the composited chain, or
an ancestor with `opacity` below 1, a `filter`, a `mask-image` or a
`mix-blend-mode`, makes the pixels unknowable from computed style: the entry
is listed under `notJudged` with the reason, and a ratio over an image needs a
pixel sample (`desktop-theme-screenshots`).

--non-text measures the outlines and borders the element and its visible
descendants paint instead, against 3:1 (WCAG 1.4.11) unless --min says
otherwise, so a state locked on a control reaches a ring that a descendant
paints. Each line is blended over the backgrounds it paints on, then compared
with the backgrounds beside it. An outline outside the border box is judged
against its parent's backgrounds and one inside it against the element's own;
one that straddles the edge is judged against the parent's, with the inner half
as `insideRatio`. A border paints over the element's own background unless
background-clip keeps that inside the border; it is judged against the
parent's backgrounds, and where the element paints a background, against that
too as `insideRatio`. `pass` judges `ratio` and `insidePass` the inside; a
boundary can need both. An `outline-style: auto` ring, a border-image, an
inset, outset, groove or ridge border, and a border on an element with native
`appearance` and no author-level border or background come back not judged; a
box-shadow ring and an icon are not measured at all and need a pixel sample.

Launches through probelib.py, headless, on a throwaway
profile, in whatever regime the prefs give (none by default):

  ./mach python --virtualenv wpt contrast.py --binary <objdir>/dist/bin/firefox \\
    --setup-file setup.js --element '<subject>' --surface '<sibling>' \\
    --state default --state :hover

On macOS the binary is <objdir>/dist/*.app/Contents/MacOS/firefox.

--setup-file holds the body of an async function run in the same context
first, to bring the UI into the state under test. It may set
globalThis.contrastSubjects to an array of elements instead of passing
--element. --chrome-setup-file (with --chrome-setup-async) runs in chrome
before the page loads, to install an add-on theme or seed history, and --steps
runs input steps after the load; all three, and the launch flags, work as in
firefox-probe.py.

--element repeats (every match of each is walked) and takes `host >>> inner`
to reach into a shadow root, one `>>>` per boundary. Slotted content stays in the host's light DOM, so name it (or the
host) without `>>>`. --context content probes the page --url loads, in a
system-principal sandbox, which brings InspectorUtils (and so the locks) to it.

The surface is the nearest opaque ancestor. Where a sibling paints it instead,
positioned or stacked in the same grid cell, the walk would composite over an
outer ancestor; --surface names the element to composite once the walk leaves
the subject: its background, then its own ancestors'. The subject's ancestors
below the one it shares with the surface add no background but still count for
opacity, filters and masks. It takes `>>>` like --element, and the first match
serves every subject, so to pick one among identical siblings, give it an id in
--setup-file and name that. Under an add-on theme the walk can end on the
body's background-image (not judged); for the address bar's results, pass
--surface '#urlbar .urlbar-background'.

stdout is the JSON result; the mozversion lines go to stderr, so keep the two
streams apart before parsing. A table of every entry, lowest ratio first, goes
to stderr unless --quiet, followed by the entries not judged (n/j) with their
reason. The root's `remotecontrol` attribute (the harness's striped address bar)
is lifted for the walk and put back. A throw in the setup or the walk exits 1
with its message and stack and prints no JSON. The exit status is also 1 when
the result has an error: no subject, a --surface that matches nothing, or a
state that cannot be locked. It stays 0 when an entry fails its minimum: that is
a finding, not an error. An entry that measured nothing gets a `warning`, and so
does a state whose lock changed no background behind the subject while no
--surface was given.

--print-js prints the probe as an `async () => {...}` function for the devtools
MCP's evaluate_privileged_script and exits.
"""

import argparse
import json
import sys
from pathlib import Path

import probelib

PROG = "contrast"

CORE = r"""
  const selector = %(selector)s, states = %(states)s, surfaceSelector = %(surface)s, minOverride = %(min)s, nonText = %(nonText)s;
  const { require } = ChromeUtils.importESModule("resource://devtools/shared/loader/Loader.sys.mjs");
  const { colorUtils } = require("devtools/shared/css/color");
  const frames = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  const tuple = v => {
    const c = InspectorUtils.colorToRGBA(v);
    return c && [c.r, c.g, c.b, c.a];
  };
  const fmt = t => `rgba(${t.slice(0, 3).map(Math.round).join(", ")}, ${+t[3].toFixed(3)})`;
  const describe = el => {
    let s = el.localName;
    if (el.id) s += "#" + el.id;
    if (typeof el.className == "string" && el.className.trim()) s += "." + el.className.trim().split(/\s+/).join(".");
    return s;
  };
  const queryAll = sel => sel.split(">>>").map(x => x.trim()).reduce(
    (roots, part) => roots.flatMap(r => [...(r.shadowRoot || r).querySelectorAll(part)]), [document]);
  const canvasOf = root => {
    const probe = root.ownerDocument.createElementNS("http://www.w3.org/1999/xhtml", "span");
    root.append(probe);
    probe.style.color = "Canvas";
    const t = tuple(getComputedStyle(probe).color);
    probe.remove();
    return t;
  };
  let subject = null, surfaceEl = null;
  const surfaceAncestors = new Set();
  // The first property on n that changes the pixels of everything it paints.
  const effectOf = (n, cs) => {
    if (parseFloat(cs.opacity) < 1) return `opacity ${cs.opacity} on ${describe(n)}`;
    for (const [prop, none] of [["filter", "none"], ["mix-blend-mode", "normal"], ["mask-image", "none"]]) {
      if (cs.getPropertyValue(prop) != none) return `${prop} on ${describe(n)}`;
    }
    return null;
  };
  // The backgrounds under el, top first, down to the first opaque one, and
  // the first property in el's ancestry that makes the composite unknowable.
  const chainOf = el => {
    const layers = [];
    let blocker = null, opaque = false, jumped = false;
    for (let n = el; n && n.nodeType == 1; n = parentOf(n)) {
      if (surfaceEl && !jumped && n == parentOf(subject)) {
        // The subject's ancestors below the surface's add no background, but their effects still apply.
        for (let a = n; a && !surfaceAncestors.has(a); a = parentOf(a)) blocker ||= effectOf(a, getComputedStyle(a));
        n = surfaceEl;
        jumped = true;
      }
      const cs = getComputedStyle(n);
      blocker ||= effectOf(n, cs);
      if (opaque) continue;
      if (!blocker && cs.backgroundImage != "none") blocker = `background-image on ${describe(n)}`;
      if (!blocker && cs.backdropFilter != "none") blocker = `backdrop-filter on ${describe(n)}`;
      const t = tuple(cs.backgroundColor);
      if (t && t[3] > 0) {
        layers.push({ element: describe(n), value: cs.backgroundColor, t });
        opaque = t[3] >= 1;
      }
    }
    if (!opaque) {
      const root = el.ownerDocument.documentElement;
      layers.push({ element: "(canvas)", value: "Canvas", t: canvasOf(root) });
    }
    return { layers, blocker };
  };
  const composite = layers => {
    let c = layers[layers.length - 1].t;
    for (let i = layers.length - 2; i >= 0; i--) c = colorUtils.blendColors(layers[i].t, c);
    return c;
  };
  const visible = el => {
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && (!el.checkVisibility || el.checkVisibility({ visibilityProperty: true, opacityProperty: true }));
  };
  // The outline and the borders el paints, sides with the same color, width and
  // style merged. `outer` and `inner` say which side of the border edge an
  // outline covers; `reason` marks a line its computed color does not describe,
  // and a line without `color` paints whatever its own color is.
  const SHADED = ["inset", "outset", "groove", "ridge"];
  // Gecko stops theming a widget once author-level CSS styles its border or background.
  const authorStyled = el =>
    [...el.style].some(p => /^(border|background)/.test(p)) ||
    InspectorUtils.getMatchingCSSRules(el).some(r => r.parentStyleSheet?.parsingMode == "author" && [...r.style].some(p => /^(border|background)/.test(p)));
  const linesOf = (el, cs) => {
    const lines = [];
    const outlineWidth = parseFloat(cs.outlineWidth), offset = parseFloat(cs.outlineOffset);
    if (cs.outlineStyle == "auto") {
      lines.push({ part: "outline", reason: "outline-style: auto paints the platform focus ring, not outline-color" });
    } else if (cs.outlineStyle != "none" && outlineWidth > 0) {
      lines.push({ part: "outline", color: cs.outlineColor, width: cs.outlineWidth, outer: offset + outlineWidth > 0, inner: offset < 0 });
    }
    const sides = new Map();
    for (const side of ["top", "right", "bottom", "left"]) {
      const style = cs.getPropertyValue(`border-${side}-style`), width = cs.getPropertyValue(`border-${side}-width`);
      if (style == "none" || style == "hidden" || !(parseFloat(width) > 0)) continue;
      const color = cs.getPropertyValue(`border-${side}-color`), key = `${color} ${width} ${style}`;
      if (!sides.has(key)) sides.set(key, { color, width, style, sides: [] });
      sides.get(key).sides.push(side);
    }
    for (const b of sides.values()) {
      const line = { part: b.sides.length == 4 ? "border" : `border-${b.sides.join("/")}`, width: b.width, border: true };
      if (cs.appearance != "none" && !authorStyled(el)) line.reason = `appearance: ${cs.appearance} paints the widget's own border`;
      else if (cs.borderImageSource != "none") line.reason = "border-image paints instead of border-color";
      else {
        line.color = b.color;
        if (SHADED.includes(b.style)) line.reason = `border-style: ${b.style} paints shades of border-color`;
      }
      lines.push(line);
    }
    return lines;
  };
  const walk = function* (el) {
    yield el;
    for (const c of el.shadowRoot ? [...el.shadowRoot.children, ...el.children] : el.children) yield* walk(c);
  };

  const mq = q => matchMedia(q).matches;
  const out = {
    regime: { forcedColors: mq("(forced-colors)"), prefersContrast: mq("(prefers-contrast)"), dark: mq("(prefers-color-scheme: dark)") },
    subjects: [],
  };
  const subjects = selector ? selector.flatMap(s => queryAll(s)) : (globalThis.contrastSubjects || []);
  if (!subjects.length) out.error = "no subject: " + (selector ? selector.join(", ") : "globalThis.contrastSubjects is empty");
  if (surfaceSelector) {
    surfaceEl = queryAll(surfaceSelector)[0];
    if (!surfaceEl) out.error = "no surface: " + surfaceSelector;
    for (let a = surfaceEl; a; a = parentOf(a)) surfaceAncestors.add(a);
  }
  const restoreRemoteControl = liftRemoteControl(subjects.map(s => s.ownerDocument.documentElement));
  try {
  for (subject of out.error ? [] : subjects) {
    for (const state of states) {
      const entry = { subject: describe(subject), state, entries: [], notJudged: [] };
      const layersKey = () => chainOf(subject).layers.map(l => `${l.element} ${l.value}`).join("|");
      const unlockedLayers = state != "default" && !surfaceEl ? layersKey() : null;
      const { notLocked, clear: clearLocks } = lockState(subject, state);
      if (notLocked.length) {
        entry.error = notLocked.map(pc => `${pc} is not a pseudo-class InspectorUtils can lock`).join("; ");
        clearLocks();
        out.subjects.push(entry);
        continue;
      }
      finishTransitions(subject.ownerDocument);
      await frames();
      const subjectVisible = visible(subject);
      const stateChangedNothing = unlockedLayers !== null && layersKey() == unlockedLayers;
      try {
        for (const el of nonText ? walk(subject) : []) {
          if (!visible(el)) continue;
          const cs = getComputedStyle(el);
          const lines = linesOf(el, cs);
          if (!lines.length) continue;
          const parent = parentOf(el);
          const outside = parent ? chainOf(parent) : { layers: [{ element: "(canvas)", value: "Canvas", t: canvasOf(el.ownerDocument.documentElement) }], blocker: null };
          const own = chainOf(el);
          const effect = effectOf(el, cs);
          const ownBg = tuple(cs.backgroundColor);
          const under = cs.backgroundClip.split(",").pop().trim() == "border-box" ? own : outside;
          const min = minOverride ?? 3;
          for (const line of lines) {
            const label = { element: describe(el), part: line.part };
            const fg = line.color === undefined ? null : tuple(line.color);
            if (line.color !== undefined && (!fg || fg[3] == 0)) continue;
            // What the line paints over and what sits beside it, outer side first.
            const sides = line.border
              ? [{ under, beside: outside }, ...(ownBg && ownBg[3] > 0 ? [{ under, beside: own }] : [])]
              : [...(line.outer ? [{ under: outside, beside: outside }] : []), ...(line.inner ? [{ under: own, beside: own }] : [])];
            const blockerOf = s => (fg[3] < 1 && s.under.blocker) || s.beside.blocker;
            const blocker = line.reason || effect || blockerOf(sides[0]);
            if (blocker) {
              entry.notJudged.push({ ...label, reason: blocker });
              continue;
            }
            const judge = s => {
              const painted = colorUtils.blendColors(fg, composite(s.under.layers));
              const bg = composite(s.beside.layers);
              const ratio = colorUtils.calculateContrastRatio(bg, painted);
              return { painted: fmt(painted), background: fmt(bg), ratio: +ratio.toFixed(2), pass: ratio >= min };
            };
            const first = judge(sides[0]);
            const e = {
              ...label, color: line.color, width: line.width, painted: first.painted, background: first.background,
              layers: sides[0].beside.layers.map(l => `${l.element} ${l.value}`),
              ratio: first.ratio, min, pass: first.pass,
            };
            if (sides[1]) {
              const inside = blockerOf(sides[1]);
              if (inside) {
                e.insideNotJudged = inside;
              } else {
                const second = judge(sides[1]);
                Object.assign(e, { insideBackground: second.background, insideRatio: second.ratio, insidePass: second.pass });
              }
            }
            entry.entries.push(e);
          }
        }
        for (const el of nonText ? [] : walk(subject)) {
          if (!visible(el)) continue;
          const cs = getComputedStyle(el);
          const text = textOf(el, cs);
          if (!text) continue;
          const fg = tuple(text.color);
          if (!fg || fg[3] == 0) continue;
          const { layers, blocker } = chainOf(el);
          const label = { element: describe(el), text: text.text.slice(0, 40) };
          if (blocker) {
            entry.notJudged.push({ ...label, reason: blocker });
            continue;
          }
          const bg = composite(layers);
          const size = parseFloat(cs.fontSize), weight = parseInt(cs.fontWeight);
          const large = size >= 24 || (size >= 18.66 && weight >= 700);
          const min = minOverride ?? (large ? 3 : 4.5);
          const ratio = colorUtils.calculateContrastRatio(bg, fg);
          entry.entries.push({
            ...label, color: text.color, background: fmt(bg),
            layers: layers.map(l => `${l.element} ${l.value}`),
            fontSize: cs.fontSize, fontWeight: cs.fontWeight, large,
            ratio: +ratio.toFixed(2), min, pass: ratio >= min,
          });
        }
      } finally {
        clearLocks();
      }
      if (!entry.entries.length) {
        entry.warning = !subjectVisible ? "measured nothing: the subject is not visible" : nonText
          ? "measured nothing: no visible outline or border" + (entry.notJudged.length ? " that could be judged" : "")
          : "measured nothing: no visible text" + (entry.notJudged.length ? " outside an image, opacity or filter" : "") + "; an icon's ink needs a pixel sample (desktop-theme-screenshots)";
      } else if (stateChangedNothing) {
        entry.warning = `locking ${state} changed no background behind the subject; where a sibling paints the state, pass --surface`;
      }
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


def print_table(result):
    rows = []
    for s in result.get("subjects", []):
        for e in s.get("entries", []):
            rows.append((
                e["ratio"],
                "pass" if e["pass"] else "FAIL",
                f"{e['min']}",
                s["state"],
                e["element"],
                e.get("text", e.get("part")),
                e["color"],
                e["background"]
                + (
                    f" (inside {e['insideRatio']} {'pass' if e['insidePass'] else 'FAIL'})"
                    if "insideRatio" in e
                    else " (inside n/j)"
                    if "insideNotJudged" in e
                    else ""
                ),
            ))
        for e in s.get("notJudged", []):
            rows.append((
                0,
                "n/j",
                "",
                s["state"],
                e["element"],
                e.get("text", e.get("part")),
                e["reason"],
                "",
            ))
    rows.sort(key=lambda r: (r[1] == "n/j", r[0]))
    rows = [("-",) + r[1:] if r[1] == "n/j" else r for r in rows]
    header = ("ratio", "", "min", "state", "element", "text/part", "color", "over")
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(8)]
    for r in [header] + rows:
        print(
            "  ".join(str(v).ljust(w) for v, w in zip(r, widths)).rstrip(),
            file=sys.stderr,
        )


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
        help="selector for the subject(s), repeatable; every match of each is walked",
    )
    ap.add_argument(
        "--state",
        action="append",
        default=None,
        help='"default" or pseudo-classes like ":hover:active", repeatable',
    )
    ap.add_argument(
        "--surface",
        default=None,
        help="selector for the surface behind the subject when a sibling paints it",
    )
    ap.add_argument(
        "--min",
        type=float,
        default=None,
        help="minimum ratio for every entry (default: 4.5, or 3 for large text and --non-text)",
    )
    ap.add_argument(
        "--non-text",
        action="store_true",
        help="measure each subject's outline and borders, not text",
    )
    ap.add_argument(
        "--setup-file", default=None, help="body of an async function run first"
    )
    ap.add_argument("--context", choices=["chrome", "content"], default="chrome")
    ap.add_argument("--quiet", action="store_true", help="no table on stderr")
    ap.add_argument(
        "--print-js",
        action="store_true",
        help="print an async function for the devtools MCP and exit",
    )
    args = ap.parse_args()
    setup = Path(args.setup_file).read_text(encoding="utf-8") if args.setup_file else ""
    core = CORE % {
        "selector": json.dumps(args.element),
        "states": json.dumps(args.state or ["default"]),
        "surface": json.dumps(args.surface),
        "min": json.dumps(args.min),
        "nonText": json.dumps(args.non_text),
    }
    body_js = probelib.PRELUDE + BODY_TEMPLATE % {"setup": setup, "core": core}
    if args.print_js:
        print(FUNCTION_TEMPLATE % body_js)
        return 0
    if not args.binary:
        ap.error("--binary is required unless --print-js")
    probelib.check_page_arguments(ap, args)
    sandbox = "system" if args.context == "content" else "default"

    def body(client):
        probelib.open_page(client, args, args.context, sandbox)
        steps_out = None
        if args.steps:
            steps_out = probelib.run_steps(
                client, args.steps, PROG, sandbox, args.headless, args.grab_any_display
            )
            if steps_out and "error" in steps_out[-1]:
                print(probelib.dump({"steps": steps_out}, indent=2))
                return 1
        result = probelib.evaluate(
            client, body_js, is_async=True, sandbox=sandbox, timeout_ms=args.timeout_ms
        )
        shown = result if steps_out is None else {"steps": steps_out, "result": result}
        print(probelib.dump(shown, indent=2))
        if not args.quiet:
            print_table(result)
        errors = [result["error"]] if result.get("error") else []
        for entry in result.get("subjects", []):
            label = f"{entry['subject']} {entry['state']}"
            if entry.get("error"):
                errors.append(f"{label}: {entry['error']}")
            elif entry.get("warning"):
                print(f"{PROG}: warning: {label}: {entry['warning']}", file=sys.stderr)
        for e in errors:
            print(f"{PROG}: error: {e}", file=sys.stderr)
        return 1 if errors else 0

    return probelib.run(args, PROG, body)


if __name__ == "__main__":
    sys.exit(main())
