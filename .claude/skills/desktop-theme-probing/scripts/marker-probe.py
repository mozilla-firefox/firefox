#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Read which token paints a property per state, with marker values on :root.

Lifts the remote-control cue, sets one distinct rgb marker per --token on the
chrome document's root, locks each --state's pseudo-classes on the element (or
on its --lock-on ancestor), finishes the transitions the lock starts, reads
--property off each --element, maps the value back to the token, and restores
the root's style attribute and the cue. Runs in the chrome context through
probelib.py, headless, on a throwaway profile:

  ./mach python --virtualenv wpt -- marker-probe.py --binary <objdir>/dist/bin/firefox \\
    --property fill --token x-fill --token x-fill-hover \\
    --element '#some-button .toolbarbutton-icon' --lock-on toolbarbutton \\
    --state default --state :hover --state :hover:active

A --token is the custom property's name with or without its leading dashes
(argparse rejects a bare "--x" value, so "x" or "--token=--x"). A --state is
"default" or a run of element-state pseudo-classes (":hover:active"); without
any, the run reads default, :hover and :hover:active. :hover and :active lock
on every flat-tree ancestor of the locked element too, as a real pointer sets
them (contrast.py and pair-walk.py lock the same way); any other pseudo-class
locks on that element only. --lock-on names the ancestor to lock when
--element is the thing read, which matters for the pseudo-classes that do not
climb; --read names the descendant to read when the state is locked on
--element itself. The selectors do not cross shadow roots. Attribute states
([open], [checked]) need a hand-written --async probe. Where forced colors are
`active` (e.g. under a forcedColorsOverride in --chrome-setup), the adjustment
replaces the rgb markers on the properties it forces and the token reads null;
the run then warns on stderr and in the JSON's "warning".

The launch flags, --url, --window-size, --chrome-setup* and --steps are
probelib.py's, as in firefox-probe.py. Exits 1 when an --element matches
nothing, a --read matches no descendant, a --lock-on selector matches no
ancestor, or a lock did not take (InspectorUtils.hasPseudoClassLock is false:
not an element-state pseudo-class, or a typo); the JSON is printed either way.
"""

import argparse
import json
import os
import re
import sys
import tempfile

import probelib

PROG = "marker-probe"

TEMPLATE = r"""
  const tokens = %(tokens)s, elements = %(elements)s, states = %(states)s;
  const prop = %(prop)s, lockOn = %(lock_on)s, read = %(read)s;
  const keepRemoteControl = %(keep_rc)s;
  const root = document.documentElement;
  const saved = root.getAttribute("style");
  const legend = {};
  const failures = [];
  const remoteControl = root.hasAttribute("remotecontrol");
  const restoreRemoteControl = keepRemoteControl ? () => {} : liftRemoteControl([root]);
  tokens.forEach((t, i) => {
    const c = `rgb(${i + 1}, ${i + 1}, ${i + 1})`;
    root.style.setProperty(t, c, "important");
    legend[c] = t;
  });
  const out = {
    regime: {
      forcedColors: matchMedia("(forced-colors)").matches,
      prefersContrast: matchMedia("(prefers-contrast)").matches,
      dark: matchMedia("(prefers-color-scheme: dark)").matches,
      nativeTheme: matchMedia("(-moz-native-theme)").matches,
      remotecontrol: root.hasAttribute("remotecontrol"),
      uidensity: root.getAttribute("uidensity"),
    },
    legend,
    readings: [],
  };
  const frames = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  try {
    await frames();
    for (const sel of elements) {
      const el = document.querySelector(sel);
      if (!el) {
        failures.push(`--element ${sel} matches nothing`);
        out.readings.push({ element: sel, missing: true });
        continue;
      }
      const subject = lockOn ? el.closest(lockOn) : el;
      if (!subject) {
        failures.push(`--lock-on ${lockOn} matches no ancestor of ${sel}`);
        out.readings.push({ element: sel, lockOn, missing: true });
        continue;
      }
      const target = read ? el.querySelector(read) : el;
      if (!target) {
        failures.push(`--read ${read} matches nothing in ${sel}`);
        out.readings.push({ element: sel, read, missing: true });
        continue;
      }
      for (const state of states) {
        const { notLocked, clear } = lockState(subject, state);
        if (notLocked.length) {
          failures.push(`${notLocked.join("")} did not lock on ${sel}`);
        }
        finishTransitions(document);
        await frames();
        const value = getComputedStyle(target).getPropertyValue(prop);
        out.readings.push({
          element: sel, read, state, value, token: legend[value] ?? null,
          ...(notLocked.length ? { notLocked } : {}),
          disabled: subject.hasAttribute("disabled"),
          open: subject.hasAttribute("open"),
          checked: subject.hasAttribute("checked"),
        });
        clear();
      }
    }
  } finally {
    restoreRemoteControl();
    if (saved === null) root.removeAttribute("style"); else root.setAttribute("style", saved);
    out.styleRestored = root.getAttribute("style") === saved;
  }
  if (matchMedia("(forced-colors: active)").matches && out.readings.some(r => "value" in r && r.token === null)) {
    out.warning = "forced colors are active, and the adjustment replaces an rgb marker on a property it forces, so a token: null reading may be the filter; set forced-color-adjust: none on the element read, or mark with system colors in a hand-written probe";
  }
  if (failures.length) out.error = failures.join("; ");
  out.remotecontrolAtLaunch = remoteControl;
  return out;
"""


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    probelib.add_launch_arguments(ap)
    probelib.add_page_arguments(ap)
    ap.add_argument(
        "--token",
        action="append",
        required=True,
        help="custom property to mark, leading dashes optional, repeatable",
    )
    ap.add_argument(
        "--element", action="append", required=True, help="selector to read, repeatable"
    )
    ap.add_argument(
        "--property",
        default="fill",
        help="CSS property name as getPropertyValue takes it (background-color); "
        "a camelCase name is converted",
    )
    ap.add_argument(
        "--state",
        action="append",
        default=None,
        help='"default" or pseudo-classes like ":hover:active", repeatable; '
        "defaults to default, :hover and :hover:active; :hover and :active "
        "also lock on every flat-tree ancestor",
    )
    ap.add_argument(
        "--lock-on",
        default=None,
        help="ancestor selector to lock the pseudo-classes on (el.closest), "
        "for every --element; a miss fails the run, so run elements with "
        "different ancestors separately",
    )
    ap.add_argument(
        "--read",
        default=None,
        help="descendant selector read off each --element while the state is locked on the element itself",
    )
    ap.add_argument(
        "--keep-remotecontrol",
        action="store_true",
        help="leave the remotecontrol attribute on the root, for a change that "
        "edits a [remotecontrol] rule",
    )
    ap.add_argument(
        "--keep-script",
        action="store_true",
        help="write the generated probe body to a file, print its path and keep it",
    )
    args = ap.parse_args()
    probelib.check_page_arguments(ap, args)
    states = args.state or ["default", ":hover", ":hover:active"]
    tokens = [t if t.startswith("--") else "--" + t for t in args.token]
    js = probelib.PRELUDE + TEMPLATE % {
        "tokens": json.dumps(tokens),
        "elements": json.dumps(args.element),
        "states": json.dumps(states),
        "prop": json.dumps(
            re.sub(r"[A-Z]", lambda m: "-" + m[0].lower(), args.property)
        ),
        "lock_on": json.dumps(args.lock_on),
        "read": json.dumps(args.read),
        "keep_rc": json.dumps(args.keep_remotecontrol),
    }
    if args.keep_script:
        fd, path = tempfile.mkstemp(prefix="marker-probe-", suffix=".js")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(js)
        print(f"probe script: {path}", file=sys.stderr)

    def body(client):
        probelib.open_page(client, args, "chrome")
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
            client, js, is_async=True, timeout_ms=args.timeout_ms
        )
        print(
            probelib.dump(
                result if steps_out is None else {"steps": steps_out, "result": result},
                indent=2,
            )
        )
        if isinstance(result, dict) and result.get("warning"):
            print(f"{PROG}: warning: {result['warning']}", file=sys.stderr)
        if isinstance(result, dict) and result.get("error"):
            print(f"{PROG}: {result['error']}", file=sys.stderr)
            return 1
        return 0

    return probelib.run(args, PROG, body)


if __name__ == "__main__":
    sys.exit(main())
