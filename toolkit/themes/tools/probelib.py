# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Launch a Firefox build under Marionette for the probe scripts.

Not a command. `contrast.py` and `pair_walk.py` beside it import it, and so do
`desktop-theme-probing`'s `firefox-probe.py` and `marker-probe.py`:
`add_launch_arguments()` gives a parser the launch flags, and
`run(args, prog, body)` launches the binary, verifies the prefs, calls
`body(client)` and tears the browser down, returning the process exit code.
`add_page_arguments()` adds the flags that bring the window to the state under
test (--url, --window-size, --settle, --chrome-setup*, --steps), `open_page()`
and `run_steps()` act on them, and `evaluate()` runs a snippet, wrapping an
async body as `--async` describes. `PRELUDE` is JS the state-locking probes
share.

Exit codes: 0 on success; 1 when `body` raised (a JS error or timeout is
printed without the driver's traceback); 2 when `--binary` is not an executable
file, the browser did not come up, `--port` is taken (the holder is named, never
killed) or a `--setpref` did not take; 3 when the run otherwise succeeded but
quitting the browser or removing its profile failed; 128 plus the signal number
when SIGINT, SIGTERM or SIGHUP stopped the run, after the browser was torn down.

`--setpref name=value` is named and typed as mochitest's (mozprofile's
`Preferences.cast`): true/false and integers by shape, a single-quoted value as
the string inside the quotes, anything else as a string.
A user.js value of another type than the pref's own is dropped without a
word, and the application rewrites some prefs at startup, so every pref is read
back after startup and a mismatch is exit 2 with a message naming which
happened, or a warning under `--allow-dropped-pref`.
"""

import argparse
import functools
import gc
import http.server
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.parse


def find_topsrcdir(start):
    directory = os.path.abspath(start)
    while True:
        if os.path.exists(os.path.join(directory, "mach")):
            return directory
        parent = os.path.dirname(directory)
        if parent == directory:
            return None
        directory = parent


def import_marionette(prog):
    try:
        from marionette_driver.marionette import Marionette

        return Marionette
    except ImportError:
        pass
    for base in (os.getcwd(), __file__):
        top = find_topsrcdir(base)
        if not top:
            continue
        candidate = os.path.join(top, "testing", "marionette", "client")
        if os.path.isdir(os.path.join(candidate, "marionette_driver")):
            sys.path.insert(0, candidate)
            try:
                from marionette_driver.marionette import Marionette

                return Marionette
            except ImportError:
                sys.path.pop(0)
    sys.exit(
        f"{prog}: cannot import marionette_driver. Run this script as "
        "`./mach python --virtualenv wpt <path>` from a Firefox checkout."
    )


def port_holders(port):
    """Who is listening on `port`, as a (holders, unavailable) pair.

    `holders` is a list of (pid, description). A non-empty `unavailable` means
    the lookup could not run, which is a different answer from nobody
    listening. Never kill what this finds: the host is shared.
    """
    if sys.platform.startswith("linux"):
        argv = ["ss", "-ltnp"]
    elif sys.platform == "darwin":
        argv = ["lsof", "-nP", f"-iTCP:{port}", "-sTCP:LISTEN"]
    elif sys.platform == "win32":
        argv = ["netstat", "-ano"]
    else:
        return [], f"no listener lookup is implemented for {sys.platform}"
    try:
        out = subprocess.run(
            argv, capture_output=True, text=True, timeout=10, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError) as e:
        return [], f"{argv[0]} could not be run ({e})"

    holders = []
    for line in out.splitlines():
        fields = line.split()
        if sys.platform.startswith("linux"):
            if len(fields) < 4 or not fields[3].endswith(f":{port}"):
                continue
            pids = re.findall(r"pid=(\d+)", line)
            for pid in pids:
                holders.append((pid, linux_cmdline(pid)))
            if not pids:
                holders.append(("?", "<another user's process; pid not visible>"))
        elif sys.platform == "darwin":
            if len(fields) < 2 or not fields[1].isdigit():
                continue
            holders.append((fields[1], fields[0]))
        elif len(fields) >= 5 and fields[0] == "TCP":
            # netstat localizes the state column, so tell a listener by its
            # unbound foreign address.
            if fields[1].endswith(f":{port}") and fields[2] in ("0.0.0.0:0", "[::]:0"):
                holders.append((fields[4], "<name unavailable>"))
    return holders, ""


def port_in_use(port):
    """Whether 127.0.0.1:`port` refuses a bind, which catches a holder the lookup cannot see."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(("127.0.0.1", port))
        except OSError:
            return True
    return False


def linux_cmdline(pid):
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            return f.read().replace(b"\0", b" ").decode(errors="replace").strip()
    except OSError:
        return "<unreadable>"


def report_startup_failure(prog, port, err):
    where = f"127.0.0.1:{port}" if port else "an auto-picked port"
    print(f"{prog}: Marionette could not come up on {where}", file=sys.stderr)
    print(f"  underlying error: {err}", file=sys.stderr)
    if not port:
        print("  read the gecko log below for the startup error.", file=sys.stderr)
        return
    holders, unavailable = port_holders(port)
    if holders:
        print(f"  port {port} is held by:", file=sys.stderr)
        for pid, description in holders:
            print(f"    pid {pid}: {description}", file=sys.stderr)
        print(
            "  This host is shared. Do NOT kill it: wait it out, or drop --port "
            "to auto-pick a free one.",
            file=sys.stderr,
        )
    elif unavailable:
        print(f"  cannot tell who holds the port: {unavailable}", file=sys.stderr)
    elif port_in_use(port):
        print(
            f"  port {port} is in use, but the listener lookup cannot see its owner "
            "(another user's process?). This host is shared. Do NOT kill anything: "
            "wait it out, or drop --port to auto-pick a free one.",
            file=sys.stderr,
        )
    else:
        print(
            "  Nothing is listening there, so this is a startup failure and not "
            "a port conflict; read the gecko log.",
            file=sys.stderr,
        )


def display_warning():
    """Why --no-headless is a contended run, in the terms of the host platform."""
    if sys.platform in ("darwin", "win32"):
        return (
            "--no-headless opens a window on the logged-in desktop; this platform has "
            "no isolated display, so the run contends with whatever else is on screen."
        )
    if os.environ.get("WAYLAND_DISPLAY"):
        return (
            "--no-headless with WAYLAND_DISPLAY set opens a window on that compositor, "
            "the live desktop included; a virtual X display sets only DISPLAY, which "
            "Firefox does not prefer."
        )
    if os.environ.get("DISPLAY") and "xvfb-run" in os.environ.get("XAUTHORITY", ""):
        return f"--no-headless uses DISPLAY={os.environ['DISPLAY']}, a virtual display from xvfb-run."
    if os.environ.get("DISPLAY"):
        return (
            f"--no-headless will use DISPLAY={os.environ['DISPLAY']}, which is the live "
            "desktop unless this is a virtual display you allocated."
        )
    return (
        "--no-headless has no display to open a window on (neither DISPLAY nor "
        "WAYLAND_DISPLAY is set); expect the launch to fail."
    )


def dump(value, indent=None):
    """`value` as JSON text, degrading only an unserializable leaf to its repr."""
    return json.dumps(value, indent=indent, sort_keys=True, default=repr)


def serve_file(path):
    """Serve the directory of `path` over http://127.0.0.1 on a free port for the
    rest of the process, and return the page's URL."""

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass

    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0),
        functools.partial(Quiet, directory=os.path.dirname(path)),
    )
    threading.Thread(target=server.serve_forever, daemon=True).start()
    name = urllib.parse.quote(os.path.basename(path))
    return f"http://127.0.0.1:{server.server_address[1]}/{name}"


PREF_TYPES = {32: "str", 64: "int", 128: "bool"}

# The profile is thrown away, so a shutdown blocker that hangs (as on a binary
# built at another revision than the tree's JS) would only hold the run until the
# 60 s AsyncShutdown watchdog crashes the browser.
LAUNCH_PREFS = {"toolkit.asyncshutdown.crash_timeout": 5000}


def parse_prefs(items, prog):
    """Type --setpref values as mochitest does, through mozprofile."""
    from mozprofile.prefs import Preferences

    prefs = {}
    for item in items:
        name, sep, raw = item.partition("=")
        if not sep:
            sys.exit(f"{prog}: --setpref takes name=value, got {item!r}")
        prefs[name] = Preferences.cast(raw)
    return prefs


# `Services` and getStringPref are missing on the oldest releases
# release-bisect.py reaches, so both are looked up with a fallback.
READ_PREFS = """
const [prefs] = arguments;
const P = typeof Services != "undefined" ? Services.prefs
  : Components.classes["@mozilla.org/preferences-service;1"]
      .getService(Components.interfaces.nsIPrefBranch);
const getString = P.getStringPref ? n => P.getStringPref(n) : n => P.getCharPref(n);
return Object.entries(prefs).map(([name, want]) => {
  const type = P.getPrefType(name);
  let got;
  try {
    got = type == P.PREF_STRING ? getString(name)
      : type == P.PREF_INT ? P.getIntPref(name)
      : type == P.PREF_BOOL ? P.getBoolPref(name)
      : undefined;
  } catch (e) {}
  return [name, type, got === undefined ? null : got];
});
"""


def dropped_prefs(client, prefs):
    """The --setpref entries that did not survive startup, as (name, reason) pairs."""
    if not prefs:
        return []
    dropped = []
    for name, ptype, got in client.execute_script(READ_PREFS, script_args=[prefs]):
        want = prefs[name]
        if type(got) is type(want) and got == want:
            continue
        if got is None:
            reason = "it has no value after startup"
        elif type(got) is not type(want):
            kind = PREF_TYPES.get(ptype, "unknown")
            quoted = " in single quotes" if kind == "str" else ""
            reason = f"the pref is {kind}-typed, so pass a {kind} value{quoted}"
        else:
            reason = (
                f"it read back {got!r}, so the application rewrote it at startup; "
                "set it on the running instance from a chrome-context snippet instead"
            )
        dropped.append((name, reason))
    return dropped


def add_launch_arguments(parser, gecko_log_help=None, binary_required=True):
    parser.add_argument(
        "--binary",
        required=binary_required,
        help="path to the firefox binary: dist/bin/firefox, "
        "dist/*.app/Contents/MacOS/firefox on macOS, dist/bin/firefox.exe on Windows",
    )
    parser.add_argument(
        "--headless",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="run headless (default). --no-headless uses the real widget backend, "
        "on the live desktop unless the display is an isolated one you allocated "
        "(references/platform-<os>.md for the host OS, section DISPLAYS).",
    )
    parser.add_argument(
        "--timeout-ms", type=int, default=30000, help="script timeout (default 30000)"
    )
    parser.add_argument(
        "--startup-timeout",
        type=int,
        default=180,
        help="seconds to wait for Marionette (default 180)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=0,
        help="Marionette port; 0 (default) auto-picks a free one",
    )
    parser.add_argument(
        "--gecko-log",
        help=gecko_log_help
        or "keep the browser log here (default: a temp file, removed on success)",
    )
    parser.add_argument(
        "--setpref",
        action="append",
        default=[],
        help="name=value pref, repeatable, typed as mochitest "
        "types it: true/false and integers by shape, so single-quote a string "
        "value that looks like one (layout.css.devPixelsPerPx='2')",
    )
    parser.add_argument(
        "--allow-dropped-pref",
        action="store_true",
        help="warn, and do not exit 2, when a --setpref did not take (wrong type, "
        "or rewritten by the application at startup)",
    )


def add_page_arguments(parser):
    """The flags that bring the window into the state under test before the snippet."""
    parser.add_argument(
        "--url",
        help="page to load in the initial tab (content: default about:blank); a "
        "chrome snippet still runs in chrome",
    )
    parser.add_argument(
        "--window-size",
        help="WIDTHxHEIGHT (or WIDTH,HEIGHT), e.g. 1000x560, applied before the load",
    )
    parser.add_argument(
        "--settle",
        type=int,
        help="ms to wait after the load before evaluating (default 2000 in content, 0 in chrome)",
    )
    chrome_setup = parser.add_mutually_exclusive_group()
    chrome_setup.add_argument(
        "--chrome-setup", help="JS run in chrome context before the load"
    )
    chrome_setup.add_argument(
        "--chrome-setup-file", help="file containing the chrome setup JS"
    )
    parser.add_argument(
        "--chrome-setup-async",
        action="store_true",
        help="a bare flag: the chrome setup is a body that may await, wrapped as "
        "firefox-probe.py --async wraps its snippet",
    )
    parser.add_argument(
        "--steps",
        help="JSON file: input steps run after the load (firefox-probe.py's docstring)",
    )
    parser.add_argument(
        "--grab-any-display",
        action="store_true",
        help="Linux: let a grab step shoot an X display whose server is not Xvfb",
    )


def check_page_arguments(parser, args):
    if not args.steps:
        return
    try:
        steps = load_steps(args.steps)
    except (OSError, ValueError) as e:
        parser.error(f"--steps {args.steps}: {e}")
    if args.headless and any("grab" in step for step in steps):
        parser.error(
            "a grab step needs --no-headless (references/platform-<os>.md for the host OS, section DISPLAYS)"
        )


def read_source(inline, path):
    if path:
        with open(path, encoding="utf-8") as f:
            return f.read()
    return inline


CALLBACK = re.compile(r"arguments\[\s*arguments\.length\s*-\s*1\s*\]")

# Marionette awaits a promise that a sync script returns and reports its
# rejection as a JavaScriptError built from String(e), so a non-Error rejection
# gets a message first.
ASYNC_HEAD = "return (async () => {\n"
ASYNC_TAIL = """
})().catch(e => {
  if (e && typeof e == "object" && "stack" in e) throw e;
  let text;
  try { text = JSON.stringify(e); } catch (_) {}
  throw new Error(`rejected with ${text === undefined ? String(e) : text}`);
});"""


def evaluate(
    client, js, is_async=False, sandbox="default", new_sandbox=True, timeout_ms=None
):
    """Run `js` in the client's current context and return its value.

    An async body may `await` and `return`s its result; a rejection raises
    JavascriptException with its message and stack, as a sync throw does. A body
    that reads a resolve callback (`arguments[arguments.length - 1]`) gets one
    and must call it."""
    run = client.execute_script
    if is_async:
        if CALLBACK.search(js):
            run = client.execute_async_script
        else:
            js = ASYNC_HEAD + js + ASYNC_TAIL
    return run(js, new_sandbox=new_sandbox, sandbox=sandbox, script_timeout=timeout_ms)


def open_page(client, args, context, sandbox="default", new_sandbox=True):
    """Apply --window-size and the chrome setup, load --url, switch to
    `context` and wait --settle ms."""
    if args.window_size:
        width, _, height = args.window_size.replace(",", "x").partition("x")
        client.set_window_rect(x=0, y=0, width=int(width), height=int(height))
    setup = read_source(args.chrome_setup, args.chrome_setup_file)
    if setup:
        client.set_context("chrome")
        result = evaluate(
            client,
            setup,
            args.chrome_setup_async,
            sandbox,
            new_sandbox,
            args.timeout_ms,
        )
        if result is not None:
            print(f"chrome setup returned: {dump(result)}", file=sys.stderr)
    url = args.url or ("about:blank" if context == "content" else None)
    if url:
        client.set_context("content")
        client.navigate(url)
    client.set_context(context)
    settle = args.settle
    if settle is None:
        settle = 2000 if context == "content" else 0
    if settle:
        time.sleep(settle / 1000.0)


def load_steps(steps_path):
    with open(steps_path, encoding="utf-8") as f:
        return json.load(f)


def run_steps(
    client, steps_path, prog, sandbox="default", headless=True, grab_any_display=False
):
    """Perform the JSON step list (firefox-probe.py's docstring); return one entry per step."""
    steps = load_steps(steps_path)
    out_dir = os.path.dirname(os.path.abspath(steps_path))
    grab_script = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "screen-grab.py"
    )
    out = []

    def point(target):
        if isinstance(target, str):
            return client.execute_script(
                """
                let [frameSel, innerSel] = arguments[0].split(">>>").map(s => s.trim());
                let doc = document, ox = 0, oy = 0, frame = null;
                if (innerSel) {
                  frame = document.querySelector(frameSel);
                  if (!frame) throw new Error(`no element matches ${frameSel}`);
                  const fb = frame.getBoundingClientRect();
                  ox = fb.x + frame.clientLeft; oy = fb.y + frame.clientTop;
                  doc = frame.contentDocument;
                } else {
                  innerSel = frameSel;
                }
                const el = doc.querySelector(innerSel);
                if (!el) throw new Error(`no element matches ${innerSel}`);
                // Scroll only a target that is off screen: scrollIntoView on a
                // target already in view still repaints and can dissolve the
                // stale paint a grab is meant to catch.
                const inView = (r, w) => r.x + r.width / 2 >= 0 && r.x + r.width / 2 <= w.innerWidth &&
                  r.y + r.height / 2 >= 0 && r.y + r.height / 2 <= w.innerHeight;
                if (frame && !inView(frame.getBoundingClientRect(), window)) {
                  frame.scrollIntoView({block: "nearest", inline: "nearest"});
                }
                let b = el.getBoundingClientRect();
                if (!inView(b, doc.defaultView)) {
                  el.scrollIntoView({block: "nearest", inline: "nearest"});
                  b = el.getBoundingClientRect();
                }
                if (innerSel !== frameSel) {
                  const fb = frame.getBoundingClientRect();
                  ox = fb.x + frame.clientLeft; oy = fb.y + frame.clientTop;
                }
                const cx = b.x + b.width / 2, cy = b.y + b.height / 2;
                const hit = doc.elementFromPoint(cx, cy);
                if (!hit || (hit !== el && !el.contains(hit))) {
                  const name = n => n ? n.localName + (n.id ? "#" + n.id : "") : "nothing";
                  throw new Error(`the center of ${innerSel} (${b.width}x${b.height} at ${Math.round(cx)},${Math.round(cy)}) hits ${name(hit)}, not the target or a descendant; target an element with a box of its own, or pass [x, y]`);
                }
                return [Math.round(ox + cx), Math.round(oy + cy)];
                """,
                [target],
            )
        return [int(target[0]), int(target[1])]

    def pointer():
        return client.actions.sequence(
            "pointer", "probe-pointer", {"pointerType": "mouse"}
        )

    def perform(kind, value, entry):
        if kind == "move":
            entry["at"] = point(value)
            pointer().pointer_move(*entry["at"]).perform()
        elif kind == "click":
            entry["at"] = point(value)
            pointer().pointer_move(*entry["at"]).click().perform()
        elif kind == "wheel":
            entry["at"] = point(value["at"])
            client.actions.sequence("wheel", "probe-wheel").scroll(
                *entry["at"],
                value.get("dx", 0),
                value.get("dy", 0),
                duration=value.get("duration"),
            ).perform()
        elif kind == "pause":
            time.sleep(value / 1000.0)
        elif kind == "grab":
            if headless:
                raise RuntimeError(
                    "grab needs --no-headless: a headless window is not on any "
                    "screen, so the grab would shoot whatever display is there"
                )
            path = os.path.join(out_dir, f"{value}.png")
            if os.path.exists(path):
                raise FileExistsError(
                    f"{path} exists from an earlier run; give this run its own directory"
                )
            entry["png"] = path
            entry["origin"] = client.execute_script(
                "return [mozInnerScreenX, mozInnerScreenY];"
            )
            entry["dpr"] = client.execute_script("return devicePixelRatio;")
            size = subprocess.run(
                [sys.executable, grab_script, path]
                + (["--any-display"] if grab_any_display else []),
                check=True,
                stdout=subprocess.PIPE,
                text=True,
            ).stdout.split()
            entry["size"] = [int(v) for v in size]
        elif kind == "keys":
            seq = client.actions.sequence("key", "probe-keys")
            for ch in value:
                seq.key_down(ch).key_up(ch)
            seq.perform()
        elif kind == "script":
            entry["result"] = client.execute_script(value, sandbox=sandbox)
        elif kind == "chrome":
            with client.using_context("chrome"):
                entry["result"] = client.execute_script(value)
        else:
            raise ValueError(f"unknown step {kind!r}")

    for step in steps:
        ((kind, value),) = step.items()
        entry = {"step": kind, "value": value, "t": round(time.time() * 1000)}
        out.append(entry)
        try:
            perform(kind, value, entry)
        except Exception as e:  # noqa: BLE001
            # A failed step (a move outside the viewport) must not lose the
            # grabs already taken and their origins.
            entry["error"] = f"{type(e).__name__}: {e}"
            print(
                f"{prog}: step {len(out)} ({kind}) failed: {entry['error']}",
                file=sys.stderr,
            )
            break
    return out


# JS shared by the probes that lock a state and read it: contrast.py,
# pair_walk.py and marker-probe.py. Paste it ahead of a probe body.
PRELUDE = r"""
  const parentOf = n => n.assignedSlot || n.parentElement || n.getRootNode().host;
  // Locks `state` ("default", or pseudo-classes such as ":hover:active") on
  // subject. :hover and :active also lock on every flat-tree ancestor, as a
  // real pointer sets them, so the subject can be a descendant of the element
  // a rule names; any other pseudo-class locks on the subject only. Returns
  // the pseudo-classes that did not lock and a function clearing the locks.
  const lockState = (subject, state) => {
    const locked = new Set([subject]);
    InspectorUtils.clearPseudoClassLocks(subject);
    const pcs = state == "default" ? [] : state.split(":").filter(Boolean).map(pc => ":" + pc);
    for (const pc of pcs) {
      const chain = [subject];
      if (pc == ":hover" || pc == ":active") {
        for (let a = parentOf(subject); a; a = parentOf(a)) chain.push(a);
      }
      for (const el of chain) {
        InspectorUtils.addPseudoClassLock(el, pc);
        locked.add(el);
      }
    }
    return {
      notLocked: pcs.filter(pc => !InspectorUtils.hasPseudoClassLock(subject, pc)),
      clear: () => { for (const el of locked) InspectorUtils.clearPseudoClassLocks(el); },
    };
  };
  // A locked state starts its transitions; finish them so the read sees the
  // end values. document.getAnimations() leaves out shadow trees, so visit each.
  const finishTransitions = doc => {
    const scopes = [doc];
    for (let i = 0; i < scopes.length; i++) {
      for (const a of scopes[i].getAnimations()) {
        if (doc.defaultView.CSSTransition.isInstance(a)) a.finish();
      }
      for (const el of scopes[i].querySelectorAll("*")) {
        if (el.openOrClosedShadowRoot) scopes.push(el.openOrClosedShadowRoot);
      }
    }
  };
  // Lifts the harness's remote-control cue (stripes over the address bar) off
  // each root element; returns the function that puts it back.
  const liftRemoteControl = roots => {
    const saved = new Map();
    for (const root of roots) {
      if (!saved.has(root)) {
        saved.set(root, root.getAttribute("remotecontrol"));
        root.removeAttribute("remotecontrol");
      }
    }
    return () => {
      for (const [root, value] of saved) {
        if (value !== null) root.setAttribute("remotecontrol", value);
      }
    };
  };
  const NO_TEXT_INPUTS = ["checkbox", "radio", "range", "color", "image", "hidden"];
  // The text el paints and its -webkit-text-fill-color (which paints the
  // glyphs over `color`), or null when it has no text the walk can see.
  const textOf = (el, cs) => {
    if (el.namespaceURI == "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul" && el.matches(":is(label, description)[value]")) {
      return { text: el.getAttribute("value"), color: getComputedStyle(el, "::before").webkitTextFillColor };
    }
    if (el.localName == "textarea" || (el.localName == "input" && !NO_TEXT_INPUTS.includes(el.type))) {
      return el.value ? { text: el.value, color: cs.webkitTextFillColor } : null;
    }
    const text = [...el.childNodes].filter(n => n.nodeType == 3).map(n => n.textContent).join("").trim();
    return text ? { text, color: cs.webkitTextFillColor } : null;
  };
"""


STALE_BUILD_SIGNATURES = ("Missing chrome or resource URL",)


def report_stale_build(prog, log_path):
    """Print the first stale-build signature in the browser log, if any: a
    startup that dies on one surfaces only as a Marionette timeout."""
    try:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if any(s in line for s in STALE_BUILD_SIGNATURES):
                    print(
                        f"{prog}: stale build? the browser log says: {line.strip()}"
                        " (desktop-theme-probing references/build-freshness.md §1)",
                        file=sys.stderr,
                    )
                    return
    except OSError:
        pass


class Stopped(SystemExit):
    """SIGINT, SIGTERM or SIGHUP arrived; raised so that run()'s teardown runs."""

    def __init__(self, signum):
        super().__init__(128 + signum)
        self.signum = signum


class StopSignals:
    """Turn SIGINT, SIGTERM and SIGHUP into `Stopped` for the life of a run.

    On Linux and Windows the browser runs in its own process group, so an outer
    `timeout` or a harness kill reaches only this process; on macOS it shares
    this process's group, so a signal sent to the group reaches both. Without a
    handler, SIGTERM and SIGHUP end this process before the teardown runs, and
    the browser and its profile outlive the run. Once the teardown starts, a
    signal is only recorded, so it cannot interrupt it."""

    def __init__(self, prog):
        self.prog = prog
        self.signum = None
        self.tearing_down = False
        self.previous = {}
        for name in ("SIGINT", "SIGTERM", "SIGHUP"):
            sig = getattr(signal, name, None)
            if sig is None or signal.getsignal(sig) is signal.SIG_IGN:
                continue
            self.previous[sig] = signal.signal(sig, self.handle)

    def handle(self, signum, frame):
        if self.signum is not None:
            return
        self.signum = signum
        print(
            f"{self.prog}: stopped by {signal.Signals(signum).name}; tearing the browser down",
            file=sys.stderr,
        )
        if not self.tearing_down:
            raise Stopped(signum)

    def restore(self):
        for sig, handler in self.previous.items():
            signal.signal(sig, handler)


def check_binary(prog, binary):
    """An error message when `binary` cannot be launched, else None."""
    if not os.path.exists(binary):
        return f"{prog}: no such binary: {binary}"
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        hint = ""
        if binary.rstrip("/").endswith(".app"):
            hint = f"; on macOS pass {binary.rstrip('/')}/Contents/MacOS/firefox"
        return f"{prog}: not an executable file: {binary}{hint}"
    return None


def run(args, prog, body):
    """Launch per `args`, call `body(client)` and return the exit code, which
    is body's return value when it returns one."""
    Marionette = import_marionette(prog)
    # Raised when the binary will not run (RunnerNotStartedError) or has no
    # application.ini beside it (mozversion's VersionError).
    launch_errors = [OSError]
    try:
        from mozrunner.errors import RunnerNotStartedError

        launch_errors.append(RunnerNotStartedError)
    except ImportError:
        RunnerNotStartedError = None
    try:
        from mozversion.errors import VersionError

        launch_errors.append(VersionError)
    except ImportError:
        pass
    launch_errors = tuple(launch_errors)
    if (
        sys.platform == "win32"
        and not args.binary.lower().endswith(".exe")
        and os.path.exists(args.binary + ".exe")
    ):
        args.binary += ".exe"
    error = check_binary(prog, args.binary)
    if error:
        print(error, file=sys.stderr)
        return 2
    prefs = parse_prefs(args.setpref, prog)
    if not args.headless:
        print(f"{prog}: {display_warning()}", file=sys.stderr)

    keep_log = bool(args.gecko_log)
    log_path = args.gecko_log
    if not log_path:
        fd, log_path = tempfile.mkstemp(prefix=f"{prog}-gecko-", suffix=".log")
        os.close(fd)

    # The constructor launches the browser, and a failure or a signal while it
    # waits for Marionette leaves no client to tear down; keep the instance as
    # soon as it exists instead.
    launched = []

    class ProbeMarionette(Marionette):
        def start_binary(self, timeout):
            launched.append(self)
            super().start_binary(timeout)

    def launch_and_run():
        # Marionette.check_port_available returns from a finally and so discards
        # the error it documents, and raise_for_port then accepts another
        # browser's Marionette on that port as this one's. Bind it here first,
        # so a busy --port fails at once instead of wedging in NewSession.
        if args.port:
            probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # On Windows SO_REUSEADDR lets the bind succeed over a live listener.
            if sys.platform != "win32":
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                probe.bind(("127.0.0.1", args.port))
            except OSError as e:
                report_startup_failure(prog, args.port, e)
                return 2
            finally:
                probe.close()

        try:
            client = ProbeMarionette(
                bin=args.binary,
                headless=args.headless,
                gecko_log=log_path,
                port=args.port,
                startup_timeout=args.startup_timeout,
                socket_timeout=args.timeout_ms / 1000.0 + 60,
                prefs={**LAUNCH_PREFS, **prefs},
            )
        except launch_errors as e:
            report_startup_failure(prog, args.port, e)
            report_stale_build(prog, log_path)
            return 2

        client.start_session()
        client.timeout.script = args.timeout_ms / 1000.0
        client.set_context("chrome")

        exit_code = 0
        for name, reason in dropped_prefs(client, prefs):
            print(f"{prog}: --setpref {name} did not take: {reason}", file=sys.stderr)
            if not args.allow_dropped_pref:
                exit_code = 2
        if exit_code:
            return exit_code

        return body(client) or 0

    stop = StopSignals(prog)
    exit_code = 0
    try:
        exit_code = launch_and_run()
    except Stopped:
        pass
    except Exception as e:  # noqa: BLE001
        print(f"{prog}: {type(e).__name__}: {e}", file=sys.stderr)
        # A JS error or script timeout is the answer, not a probe bug, and the
        # driver's traceback buries the JS text it just printed.
        if type(e).__name__ not in ("JavascriptException", "ScriptTimeoutException"):
            traceback.print_exc()
            report_stale_build(prog, log_path)
        exit_code = 1
    finally:
        stop.tearing_down = True
        if launched:
            client = launched[0]
            teardown_failed = False
            try:
                # Without a session (start_session failed) quit() cannot ask the
                # browser to shut down; cleanup() below kills it instead.
                if client.session is not None:
                    client.quit()
            except Exception as e:  # noqa: BLE001
                print(f"{prog}: quitting the browser failed: {e!r}", file=sys.stderr)
                teardown_failed = True
            try:
                if not client.cleanup_ran:
                    client.cleanup()
            except Exception as e:  # noqa: BLE001
                if RunnerNotStartedError and isinstance(e, RunnerNotStartedError):
                    # A binary that never ran: cleanup() removed the profile,
                    # then tripped over the missing process.
                    client.cleanup_ran = True
                else:
                    print(f"{prog}: teardown failed: {e!r}", file=sys.stderr)
                    teardown_failed = True
            if teardown_failed:
                exit_code = exit_code or 3
        if stop.signum is not None:
            exit_code = 128 + stop.signum
        stop.restore()
        # The driver's log handler keeps the gecko log open until it is
        # collected, and Windows cannot remove an open file.
        launched.clear()
        client = None
        gc.collect()
        if (exit_code or keep_log) and os.path.exists(log_path):
            print(f"{prog}: browser log kept at {log_path}", file=sys.stderr)
        elif os.path.exists(log_path):
            try:
                os.remove(log_path)
            except OSError:
                pass
    return exit_code
