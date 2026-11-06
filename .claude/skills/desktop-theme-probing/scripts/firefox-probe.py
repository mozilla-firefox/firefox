#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
r"""Run JS in a real Firefox build, in the chrome or the content context, and print the result as JSON.

Run it with the checkout's mach so the in-tree marionette_driver resolves:

    ./mach python --virtualenv wpt <skill>/scripts/firefox-probe.py \
        --binary <objdir>/dist/bin/firefox \
        --url https://example.com/ -e 'return {title: document.title}'

    ./mach python --virtualenv wpt <skill>/scripts/firefox-probe.py \
        --binary <objdir>/dist/bin/firefox \
        --context chrome -e 'return {state: window.windowState, hidden: document.hidden}'

On macOS the binary is <objdir>/dist/<App>.app/Contents/MacOS/firefox.

Context:
  content (default)  the snippet runs in the page --url loaded (about:blank
                     without one), --settle ms after the load (default 2000),
                     in a Marionette sandbox over the page's global (bare
                     `document`, `window`) with Xray vision: a custom element's
                     own methods and expandos read undefined; reach them
                     through `el.wrappedJSObject`.
                     `--sandbox system` evaluates with a system principal (for
                     `InspectorUtils`); the Xrays apply there too.
  chrome             the snippet runs against browser.xhtml. --url loads the
                     page in the initial tab first and the snippet still runs
                     in chrome. `Cu.now()` is undefined there; take timing off
                     `performance.now()`.

Modes:
  sync (default)  evaluate the snippet once, print the returned value
  --async         the snippet is the body of an async function: it may `await`
                  and `return`s its result; a rejection exits 1 with its
                  message and stack, as a sync throw does. A snippet that reads
                  a resolve callback (`arguments[arguments.length - 1]`) gets
                  one instead and must call it.
  --sample-ms     evaluate the snippet at each offset (ms) after --setup, print a
                  table; its first `pre` row gives the baseline from before
                  --setup ran

--setup runs once in the selected context right before the snippet, after any
--steps (window.minimize()), and needs a snippet;
--chrome-setup (or --chrome-setup-file) runs in chrome context before the load,
for what content cannot do (seeding Places or form history, resizing the window,
installing an add-on). Each evaluation gets
a FRESH sandbox unless you pass --keep-sandbox, so a declaration made in a
setup or an earlier call is invisible to the next; reach across by way of
`window`, a pref or the DOM.

`--steps FILE` runs a JSON list of input steps after the load and before the
snippet, for a hover, click or drag the page cannot script itself: [{"click":
"#open"}, {"pause": 500}, {"move": "#target"}, {"grab": "hover"}, {"pause":
100}, {"grab": "hover-2"}, {"move": [5, 5]}, {"grab": "left"}] "move"/"click"
take a CSS selector (its center; a target off screen is scrolled into view
first, one already in view is left alone, since the scroll repaints; the step
fails when the center hits neither the target nor a descendant, as with an
inline box around floats, which has no height, and with any element inside an
open <panel> or <menupopup>, which the window document's elementFromPoint does
not reach; lock :hover on that with InspectorUtils.addPseudoClassLock in a
"chrome" step instead), "<frame selector> >>> <inner
selector>" for an element inside an iframe (a contrast row), or [x, y] CSS px in
the viewport, and go through WebDriver pointer actions, so :hover follows.
"keys" types a string through WebDriver key actions into whatever has focus (one
key_down/key_up per character; a WebDriver key code such as "\ue007" for Enter
is one character). "wheel" scrolls with a real WebDriver wheel action, the path
a user's scroll takes (window.scrollTo() in a script step takes another):
{"wheel": {"at": [x, y], "dy": 300, "dx": 0, "duration": 250}}, dx/dy in CSS px
(positive scrolls down/right), duration in ms (optional); "at" takes move's
target forms, but a selector target off screen is scrolled into view first, so a
sequence of scrolls takes coordinates. "grab" shoots the screen with
scripts/screen-grab.py (Pillow, under this interpreter) into <name>.png beside
the steps file; a headless run refuses it. On Linux it grabs the X root window
of a private Xvfb display (references/platform-linux.md, section DISPLAYS) and
refuses any other X server unless --grab-any-display is given; a grab takes
about 150 ms, so a one-frame flash is below its floor. On macOS and Windows it
grabs the logged-in desktop; read screen-grab.py's docstring for their
coordinate limits. It records mozInnerScreenX/Y, the content origin on that
screen in CSS pixels, with "dpr" and the PNG's "size": pass the origin unscaled
as the --offset of desktop-theme-screenshots' pixel-diff.py together with --dpr,
which scales both. "script" evaluates JS and keeps its return value; the step is
a JSON string, so a newline inside a JS string literal is written `\\n`, or the
step fails with "string literal contains an unescaped line break". "chrome"
evaluates JS in chrome context between steps and keeps its return value, for a
browser action that must land between two steps: a zoom before a grab (`return
(gBrowser.selectedBrowser.browsingContext.fullZoom = 1.2)`), or selecting the
first tab again after a click opened another (`gBrowser.selectedTab =
gBrowser.tabs[0]; return gBrowser.tabs.length`), since the new tab is the one on
screen while the steps still drive the first. A call that starts async work
without returning its promise returns undefined and is not waited on:
`PanelUI.show()` is one, so open the app menu with `return new Promise(r => {
PanelUI.panel.addEventListener('popupshown', r, {once: true}); PanelUI.show();
})`. Both step kinds run as a plain
function body: a returned Promise is awaited, but `await` is a SyntaxError, so
chain `.then()`. A step's return value lands in its entry's "result"; "value"
echoes the step's source. The output is then {"steps": [...], "result":
<snippet>}; the snippet may be omitted when --steps is given. With --sample-ms
the output is {"steps": [...]} followed by the table.

The launch flags (--setpref, --port, --gecko-log, --no-headless, --timeout-ms)
and the exit codes are probelib.py's; a failed step stops the steps, prints the
output and exits 1.

mach eats a bare --help, so put the -- before the script path:
`./mach python --virtualenv wpt -- <skill>/scripts/firefox-probe.py --help`.
"""

import argparse
import sys
import time
import urllib.parse
import urllib.request

import probelib

PROG = "firefox-probe"


def print_table(rows):
    header = ("at_ms", "t_ms", "value")
    widths = [max(len(r[i]) for r in rows + [header]) for i in range(3)]
    for row in [header] + rows:
        print("  ".join(row[i].ljust(widths[i]) for i in range(3)).rstrip())


def main():
    parser = argparse.ArgumentParser(
        description="Run JS in a real Firefox build, in the chrome or the content context.",
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    probelib.add_launch_arguments(parser)
    probelib.add_page_arguments(parser)
    parser.add_argument(
        "--context",
        choices=("chrome", "content"),
        default="content",
        help="where the snippet runs (default content)",
    )
    parser.add_argument(
        "--serve",
        action="store_true",
        help="load a file:// --url over http://127.0.0.1 from a server for its "
        "directory, for a page that reads its own origin or loads a subresource "
        "that must be same-origin",
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument(
        "-e", "--script", help="JS to evaluate (needs an explicit `return`)"
    )
    source.add_argument("--script-file", help="file containing the JS to evaluate")
    parser.add_argument(
        "--async",
        dest="is_async",
        action="store_true",
        help="the JS is the body of an async function: it may await and returns "
        "its result; a rejection exits 1",
    )
    parser.add_argument(
        "--sample-ms",
        help="comma-separated offsets (ms) at which to re-evaluate the snippet, "
        "printed as a table with a `pre` baseline row; not usable with --async",
    )
    setup = parser.add_mutually_exclusive_group()
    setup.add_argument(
        "--setup",
        help="JS run once in the selected context right before the snippet, after "
        "any --steps; needs a snippet (e.g. window.minimize())",
    )
    setup.add_argument("--setup-file", help="file containing the setup JS")
    parser.add_argument(
        "--sandbox",
        default="default",
        help='sandbox tag; "system" gets a system principal (default: "default")',
    )
    parser.add_argument(
        "--keep-sandbox",
        action="store_true",
        help="reuse the sandbox across evaluations; the default is a fresh one each time",
    )
    args = parser.parse_args()
    has_snippet = bool(args.script or args.script_file)
    if not (has_snippet or args.steps):
        parser.error("one of -e, --script-file or --steps is required")
    if args.sample_ms and args.is_async:
        parser.error("--sample-ms and --async are mutually exclusive")
    if args.sample_ms and not has_snippet:
        parser.error("--sample-ms needs a snippet (-e or --script-file)")
    if (args.setup or args.setup_file) and not has_snippet:
        parser.error(
            "--setup runs right before the snippet, so it needs -e or --script-file"
        )
    probelib.check_page_arguments(parser, args)

    script = probelib.read_source(args.script, args.script_file)
    if script and not args.is_async and probelib.CALLBACK.search(script):
        parser.error("the snippet reads a resolve callback, which only --async passes")
    setup_script = probelib.read_source(args.setup, args.setup_file)
    offsets = None
    if args.sample_ms:
        offsets = [int(x) for x in args.sample_ms.replace(" ", "").split(",") if x]
    if args.serve:
        url = args.url
        if not url or not url.startswith("file://"):
            sys.exit(f"{PROG}: --serve needs a file:// --url")
        parts = urllib.parse.urlparse(url)
        url = probelib.serve_file(urllib.request.url2pathname(parts.path))
        if parts.query:
            url += f"?{parts.query}"
        if parts.fragment:
            url += f"#{parts.fragment}"
        args.url = url

    def evaluate(client, js, is_async=False):
        return probelib.evaluate(
            client,
            js,
            is_async,
            sandbox=args.sandbox,
            new_sandbox=not args.keep_sandbox,
            timeout_ms=args.timeout_ms,
        )

    def run_setup(client):
        result = evaluate(client, setup_script)
        if result is not None:
            print(f"setup returned: {probelib.dump(result)}", file=sys.stderr)

    def body(client):
        probelib.open_page(
            client, args, args.context, args.sandbox, not args.keep_sandbox
        )

        steps_out = None
        if args.steps:
            steps_out = probelib.run_steps(
                client,
                args.steps,
                PROG,
                args.sandbox,
                args.headless,
                args.grab_any_display,
            )
        steps_failed = bool(steps_out) and "error" in steps_out[-1]

        if offsets is not None:
            rows = [("pre", "0", probelib.dump(evaluate(client, script)))]
            if setup_script:
                run_setup(client)
            start = time.monotonic()
            for offset in offsets:
                remaining = offset / 1000.0 - (time.monotonic() - start)
                if remaining > 0:
                    time.sleep(remaining)
                elapsed = int((time.monotonic() - start) * 1000)
                value = probelib.dump(evaluate(client, script))
                rows.append((str(offset), str(elapsed), value))
            if steps_out is not None:
                print(probelib.dump({"steps": steps_out}, indent=2))
            print_table(rows)
            return 1 if steps_failed else 0

        result = None
        if script is not None:
            if setup_script:
                run_setup(client)
            result = evaluate(client, script, args.is_async)
        if steps_out is not None:
            result = {"steps": steps_out, "result": result}
        print(probelib.dump(result, indent=2))
        return 1 if steps_failed else 0

    return probelib.run(args, PROG, body)


if __name__ == "__main__":
    sys.exit(main())
