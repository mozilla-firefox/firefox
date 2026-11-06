# Running a snippet through firefox-probe.py

Read `--help` for every flag, step kind and output shape, and `probelib.py`'s
docstring for the launch flags and exit codes; this file holds what they do not
say.

## 1. Flags and output

- **The pref read-back checks the pref, not what it controls**, so read that
  back too: for `layout.css.devPixelsPerPx`, `devicePixelRatio`. A pref the
  product rewrites at startup reads back as dropped whatever its type, and
  needs setting on the running instance instead.
- **The result is the JSON on stdout.** The `mozversion` `INFO` lines, the
  runner's own messages and a `glean.dispatcher` panic (`Global Glean object not
  initialized`, mach's telemetry, harmless) all go to stderr, so send stderr to
  its own file (`>probe.json 2>probe.err`) and parse stdout whole. A result can
  be a scalar (e.g. `42`, `"text"`, `true`, `null`) printed on one line, so a
  filter that starts at the first line beginning with `{` or `[` prints nothing
  for it. A call that builds the virtualenv (the first in a tree, and sometimes
  the first after a checkout or `./mach artifact install`) prints "Creating the
  'wpt' site" and a harmless PyGObject install failure to stdout, after the
  result. Build it with a call of its own before the first probe, `timeout 300
  ./mach python --virtualenv wpt -c pass`, and the probes after it print the
  result alone.
- **Return plain values**: a cyclic object prints nothing and exits 1 with
  `Cyclic object value`.
- **`--window-size WIDTHxHEIGHT` sets the outer window**, so the content
  viewport comes out smaller by the height of the toolbars above it; read the
  content's `innerHeight` back rather than computing it.
- **`--chrome-setup-async` takes the same body as `--async`** (§2). Whatever
  the chrome setup returns prints to stderr as `chrome setup returned: …`.
- **Steps act in the `--context` the probe runs in**, so under `chrome` they
  click and type into browser UI. A `grab` refuses to overwrite a PNG an
  earlier run left, so give each run its own output directory; a grab writes
  into the directory that holds the `--steps` file.
- **An author sheet the probe loads itself never matches `-moz-pref()`**, a
  `data:` one included (css_guidelines "Chrome-only media features go by the
  stylesheet, not the document" lists the sheets that do), so check a
  chrome-only media query through the computed value the shipped rule sets.

`firefox-probe.py` needs no running server. It runs on a throwaway profile, so a
pref it sets dies with the run. It sets `toolkit.asyncshutdown.crash_timeout` to
5000 there, so a shutdown blocker that hangs, as on a binary built at another
revision than the tree's JS, ends the run in seconds with a minidump line in the
browser log instead of holding it 60 s. A SIGINT, SIGTERM or SIGHUP (e.g. an
outer `timeout`, a harness kill) still tears the browser down and removes its
profile; a SIGKILL cannot, so give an outer `timeout` no `-s KILL`.

The `grab` step captures the compositor's
output; before choosing it over a canvas, read §2 "The compositor's output" in
[`measuring-pixels.md`](measuring-pixels.md).
The script runs headless by default; before passing `--no-headless`, read §1
"Pick the display rung by what the measurement needs" in
[`running-a-probe.md`](running-a-probe.md): a non-headless run opens on the
user's desktop unless the host has an isolated display (its platform file,
section DISPLAYS).

## 2. The chrome sandbox and `--async`

- **The snippet runs in Marionette's chrome sandbox, not in the window.**
  `globalThis` is not `window`. `ChromeUtils.importESModule` works there, and
  its `Failed to load` means no module lives at that URL: many browser modules
  moved from `resource:///modules/` to `moz-src:///browser/…`
  (`CustomizableUI.sys.mjs` among them), so take the URL from an existing
  importer, or use the window's global (`window.CustomizableUI`).
- `firefox-probe.py --async` takes a bare body that may `await` and `return`s
  its result; a throw or rejection exits 1 with its message and stack. On a
  `ScriptTimeoutException`, pass `--gecko-log` and read the browser log (it
  holds NUL bytes, so search it with `rg -a` or `grep -a`) before assuming the
  workload was slow; a wedge that looks like a slow loop is usually a
  `TypeError` on the first iteration.

The run's window opens on about:blank (`SKILL.md` §2 "A probe launch is not a
user's profile"). `--url` loads a page in the initial tab before a chrome
snippet runs, and `--serve` serves a `--url file:///abs/path` over http. To load
one mid-run, call `win.openTrustedLinkIn("data:text/html,x", "current")` and
poll until `win.gBrowser.currentURI.spec` starts with `data:`.

The runner's content sandbox has Xray vision over a web page, under
`--sandbox system` too, so a custom element's own methods and expandos read
`undefined` there (a `TypeError: … is not a function`); reach them through
`el.wrappedJSObject`. On a page with a system principal
(`document.nodePrincipal.isSystemPrincipal`, as on many about: pages) no Xray
applies under `--sandbox system`: `el.wrappedJSObject` is `undefined` and the
element's own members read directly. The MCP's `evaluate_script` runs in the
page's realm and needs no waiver.
