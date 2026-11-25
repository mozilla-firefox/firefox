#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Capture a before/after pair of every surface in a set of color regimes, and
draw images comparing them.

    ./mach python --virtualenv desktop-theme -- matrix.py run SPEC OUTDIR [--only SET/REGIME]...
    ./mach python --virtualenv desktop-theme -- matrix.py render OUTDIR [--notes FILE] [--threshold N]

Run from the checkout root. The spec is a JSON file naming the surfaces to
shoot; this one shoots the navigation toolbar and the reload button's hover
state in every built-in regime:

    {
      "sets": {
        "toolbar": {
          "shots": {
            "navbar": {"element": "#nav-bar"},
            "hover": {"element": "#reload-button", "lock": ":hover", "pad": 8}
          }
        }
      }
    }

The built-in regimes are light-nova, dark-nova, light-classic, dark-classic,
the same four with high contrast (hc-light-nova and so on), native-light-nova
(the native theme), alpenglow-nova (the Alpenglow add-on theme) and
forced-light-nova. Nova and classic are browser.nova.enabled on and off; high
contrast is ui.useAccessibilityTheme=1. forced-light-nova approximates Windows
high contrast chrome off Windows: high contrast plus the forced-colors
override, with `forced-color-adjust: none` on the root so that no color is
rewritten, as in Windows' `requested` state. It still reports `active`, so a
bare `(forced-colors)` query matches as on Windows, while `(forced-colors:
active)` and `(forced-colors: requested)` answer the other way round.

Every cell runs headless on Linux, which reads no GTK settings. Its system
colors are fixed stand-ins rather than a GTK palette, so in native-light-nova
some pairs match: -moz-headerbarinactive and -moz-headerbarinactivetext both
read white. Its system fonts are sans-serif at 16px, so the chrome's root is
16px and a rem length equals its px original: a change from px to rem shows in
no regime. To see one, give a smaller system font in `prefs` or in a regime of
`defineRegimes`: "ui.font.message-box=sans-serif" and
"ui.font.message-box.size='12'" for the root, and the same pair for
ui.font.menu for menus. The quotes make the size a string pref, which it has
to be.

`shots` maps a shot name to the `element` selector to capture (`>>>` steps
into the shadow root of the element before it, as in "moz-button#x >>>
button") and optionally:

- `lock`: a pseudo-class such as ":hover", or a list of them; :hover and
  :active also lock on every ancestor.
- `attrs`: attributes to set on the element for the shot, such as
  {"aria-pressed": "true"}; true sets a boolean attribute (to ""), false or
  null removes one.
- `props`: CSS properties to read besides color, background-color,
  background-image, border-color, outline, box-shadow, fill, stroke, opacity
  and visibility.
- `inactive: true` to shoot with the browser window inactive, which no lock
  can do: the run opens and focuses a second browser window, checks that
  :-moz-window-inactive matches, and closes the window after the shot.
- `descendants: false` to read the element alone.
- `contrast: true` to record `contrast(element)` in that state.
- `pad`: the CSS px around the element (default 8).

For each shot in turn, the run sets the attributes, makes the window inactive
if asked, locks the state, finishes its transitions, and holds every running
animation still with `holdAnimations(document)`: it finishes one that ends and
pauses one that repeats forever at its start, so both halves capture the same
frame, and lists their names in `readings[name].heldAnimations`. It records in
`readings[name].styles` the properties of the element (under "") and of each
rendered element in its flat tree but closed popups (under a path such as
"#back-button > image.toolbarbutton-icon"). It then captures the element,
clears the lock, plays the paused animations again and restores the attributes.
A shot that fails records `readings[name].error` and the cell goes on with the
next. A set gives either `shots` or a `script`, and optionally `regimes`, the
regimes it runs in, a `windowSize` (default 1280x800) and a `steps` file of
firefox-probe.py input steps run before the shots. The spec can also give:

- `base`: the commit the before half takes the paths from (default HEAD~1).
- `paths`: the files capture-pair.sh reverts for the before half, relative to
  the checkout root. By default the run takes each file `git diff --name-only
  base HEAD` lists as modified that capture-pair.sh --check-paths finds linked
  into the objdir, and prints which it took. It refuses when none is, or when
  one is copied into the objdir, which costs three `./mach build faster` runs a
  cell; list such a file in `paths` to accept that. Either way it refuses a
  checkout with uncommitted changes, which both halves would load.
- `prefs`: prefs for every cell, such as "browser.uidensity=1"; a regime's
  prefs win over these.
- `regimes`: the regimes every set runs in unless it lists its own (default
  all, the spec's own included).
- `defineRegimes`: regimes of its own, each a name mapped to `prefs`, an
  optional `extends` (the built-in regime to take the prefs and setup of, then
  add these prefs to; default the one it is named like, else light-nova) and
  an optional `chromeSetup`, a chrome script that declares
  `async function setupRegime()` (such as installing an add-on theme), as in
  regimes/. One named like a built-in regime replaces it. A setup that
  throws an Error saying "regime did not take" gets the cell its retry.
- `prelude`: a file of JS pasted ahead of every set's script or shots, for
  helpers the sets share.
- `windowSize` for every set, and `timeoutMs` for each half (default 120000).

Prelude, script, setup and steps files are relative to the spec's directory.

A `script` handles what `shots` cannot, such as opening a second window. Each
cell, a set in one regime, is one capture-pair.sh --probe run into
OUTDIR/SET/REGIME/. Its probe is capture.js, then probelib's PRELUDE, `REGIME`,
`SET`, empty `readings` and `context` objects, `contrast()`, matrix-shot.js's
`shot(name, element, options)` (taking a shot's keys as options) and
`holdAnimations()`, and the spec's prelude, then the set's script (or the loop
made from its `shots`) as the body of an async function of its own, so its
declarations shadow those names and a `return` ends only the script. The
script reaches each state and either calls `shot()` or awaits `capture(name,
element)` and records what it measured under the same name in
`readings[name]`, as an object or a single value. Record a value that
describes the cell rather than one the change should move, such as which tab
is selected, in `context`: `render` lists it per cell and compares nothing in it.
`contrast(subjects, {states, surface, min, nonText})` takes selectors or
elements, not a mix, and returns what `./mach theme contrast` prints as JSON
for them. The probe resolves with the shots, the readings and the context,
which capture-pair.sh writes as OUTDIR/SET/REGIME/{after,before}.json beside
the browser's output in {after,before}.log.

A cell that fails is retried once, keeping the first attempt's output in
OUTDIR/SET/REGIME/attempt-1.out, unless a retry would repeat the failure: a JS
error, a timeout, or no shot captured. Each cell prints the shots that failed
and their errors, whether the cell failed or not. When a set's first cell fails
with a JS error or a timeout in a regime without a chrome setup, the run skips
the set's other cells. A cell that does not launch (a refused path, a failed
build, no binary, a browser that never comes up) stops the run. On Ctrl-C the
run waits for capture-pair.sh to restore the reverted paths.

The run checks the paths before it clears anything. OUTDIR is cleared first,
and only when it holds the marker file a previous run wrote, so keep a --notes
file elsewhere; --only reruns the cells matching a SET/REGIME glob and clears
only theirs. OUTDIR/matrix.json records each cell's result, with a stamp of the
revisions, paths, set and regime it ran with; --only names the cells it keeps
whose stamp no longer matches. It exits 0 when every cell captured, 1 when one
failed and 2 on an error.

`render` pairs after-NAME.png with before-NAME.png in each cell, counts the
pixels that differ by more than --threshold (default 0: both halves run one
build headless, so a pixel that differs is the change's) and writes
OUTDIR/SET/REGIME/diff-NAME.png where any do; with a threshold, it also counts
the pixels that differ by less, and a shot with only those still goes on the
pixels list. It turns the two halves' readings into notes:
a style property that changed, naming the elements it changed on; an element
rendered in one half only; any other value that changed; a size that changed;
an error; a contrast ratio that changed or fails in both halves; and contrast
measuring nothing in both. A row is one shot of one set across its regimes,
and it goes on the first list that fits: a reading changed in some regime; a
shot failed, or its contrast measured nothing, the same way in both halves, or
one half did not finish; only pixels differ; a contrast entry fails in both
halves. A row shows each
regime that is not identical, folding a regime where no pixel differs and the
notes match an earlier one into that one, and names the regimes that are
identical and those it could not compare. Each list that has rows becomes a
PNG sheet, OUTDIR/LIST.png for LIST changed, error, pixels or failing, split
into LIST-1.png, LIST-2.png and so on where one image would exceed
Phabricator's 8 MiB upload limit. A sheet draws each shown regime under a
numbered caption naming the shot and the regimes it stands for: before, after
and difference side by side, or stacked when side by side would be wider
than 1600 px, and below them the three cropped to the differing pixels and
enlarged without smoothing, or the whole capture enlarged when those cover
half its width or more and it is small enough, laid out the same way. OUTDIR/notes.md, the Markdown to post the sheets with, gives
the notes of each numbered regime grouped by sheet, then the rows identical
in every regime compared, the cells that failed, did not run or have a stale
stamp, and each cell's context; a failed cell still adds the shots it read.
`render` prints one line per row (its list, the shot, and per regime shown the
pixels that differ and the first note), one per identical shot and one per
cell with context. --notes is a JSON file mapping SET/NAME or SET/REGIME/NAME to a note shown on that row or
regime, or beside an identical shot. `render` reports the keys naming a regime
that is identical, which it does not show, and those matching no shot.

OUTDIR/rows.json holds the data the sheets and notes.md are drawn from:

- `title`; `failedCells`, `notRunCells` and `staleCells`, lists of
  SET/REGIME.
- `identical`: {`shot`: SET/NAME, `missing`: the regimes not compared,
  `note`}.
- `context`: SET/REGIME mapped to {`before`, `after`}.
- `rows`, in list order: {`set`, `name`, `status` (changed, error, pixels or
  failing), `note`, `count` (the most pixels differing in one regime),
  `same` and `missing` (regimes identical and not compared), `regimes`}.
- Each of `regimes`: {`regime`, `sameIn` (the regimes folded into it),
  `status`, `notes` (what changed), `errors`, `failing`, `count`, `below`
  (pixels differing by no more than --threshold), `maxDelta` (the largest
  channel difference), `bbox`, `notRun` (the half that did not finish, or
  null), `heldAnimations`, `note`, and the paths relative to OUTDIR `before`,
  `after`, `diff` and `zoom` ({`before`, `after`, `diff`, `scale`} or null)}.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import sheet

HERE = Path(__file__).resolve().parent
MARKER = ".desktop-theme-matrix"
EXIT_ERROR = 2


class SpecError(Exception):
    pass


def topsrcdir():
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=False,
    )
    if out.returncode:
        raise SpecError("run from a checkout")
    return Path(out.stdout.strip())


def short_rev(root, rev):
    return subprocess.run(
        ["git", "rev-parse", "--short", rev],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()


def builtin_regimes():
    regimes = {}
    for hc in (False, True):
        for nova in (True, False):
            for dark in (False, True):
                name = "-".join(
                    ["hc"] * hc
                    + ["dark" if dark else "light", "nova" if nova else "classic"]
                )
                regimes[name] = regime_prefs(nova=nova, dark=dark, hc=hc)
    regimes["native-light-nova"] = regime_prefs(native=True)
    regimes["alpenglow-nova"] = regime_prefs(setup="alpenglow.js")
    regimes["forced-light-nova"] = regime_prefs(hc=True, setup="forced-colors.js")
    return regimes


def regime_prefs(native=False, nova=True, dark=False, hc=False, setup=None):
    return {
        "prefs": [
            f"browser.theme.native-theme={str(native).lower()}",
            f"browser.nova.enabled={str(nova).lower()}",
            f"ui.systemUsesDarkTheme={int(dark)}",
            f"ui.useAccessibilityTheme={int(hc)}",
        ],
        "chromeSetup": setup and HERE / "regimes" / setup,
    }


def strings(value, what):
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise SpecError(f"{what} is a list of strings")
    return list(value)


def pref_list(value, what):
    prefs = strings(value, what)
    for pref in prefs:
        if not re.fullmatch(r"[^=\s]+=.*", pref):
            raise SpecError(f"{what}: {pref!r} is not name=value")
    return prefs


def window_size(value, what):
    if not isinstance(value, str) or not re.fullmatch(r"\d+x\d+", value):
        raise SpecError(f"{what}: windowSize is a string such as 1280x800")
    return value


RESERVED = {"probes", "matrix.json", "rows.json", "notes.md"}


def mapping(value, what):
    """Check a spec object mapping names, which become file and directory
    names in OUTDIR, to objects."""
    if not isinstance(value, dict):
        raise SpecError(f"{what} maps names to objects")
    for name, entry in value.items():
        if not isinstance(entry, dict):
            raise SpecError(f"{what}: {name} is not an object")
        if not name or name.startswith(".") or set(name) & set('/\\:*?"<>|'):
            raise SpecError(
                f"{what}: {name!r} names files, so no leading . and none of"
                ' / \\ : * ? " < > |'
            )
        if name in RESERVED or is_sheet(name):
            raise SpecError(f"{what}: {name!r} is a name the run writes in OUTDIR")
    return value


def shots_body(set_name, shots):
    """The set script for a set given as `shots`."""
    mapping(shots, f"set {set_name} shots")
    if not shots:
        raise SpecError(f"set {set_name}: shots is empty")
    for name, shot in shots.items():
        where = f"set {set_name} shot {name}"
        if not isinstance(shot.get("element"), str):
            raise SpecError(f"{where} needs an element selector")
        extra = set(shot) - {
            "element",
            "lock",
            "attrs",
            "props",
            "descendants",
            "contrast",
            "inactive",
            "pad",
        }
        if extra:
            raise SpecError(f"{where}: unknown keys {', '.join(sorted(extra))}")
        locks = shot.get("lock", [])
        locks = [locks] if isinstance(locks, str) else locks
        if not isinstance(locks, list) or not all(
            isinstance(lock, str) and re.fullmatch(r":[a-z][a-z-]*", lock)
            for lock in locks
        ):
            raise SpecError(
                f"{where}: a lock is a pseudo-class such as ':hover'; set an "
                "attribute with attrs"
            )
        attrs = shot.get("attrs", {})
        if not isinstance(attrs, dict) or not all(
            isinstance(v, (str, bool)) or v is None for v in attrs.values()
        ):
            raise SpecError(
                f"{where}: attrs maps attribute names to a string, true, false or null"
            )
        strings(shot.get("props", []), f"{where} props")
        pad = shot.get("pad", 0)
        if not isinstance(pad, (int, float)) or isinstance(pad, bool):
            raise SpecError(f"{where}: pad is a number of CSS px")
        for key in ("contrast", "descendants", "inactive"):
            if not isinstance(shot.get(key, False), bool):
                raise SpecError(f"{where}: {key} is true or false")
    return (
        f"const SHOTS = {json.dumps(shots, indent=2)};\n"
        "for (const [name, options] of Object.entries(SHOTS)) {\n"
        "  await shot(name, options.element, options);\n"
        "}\n"
    )


def changed_files(root, base):
    out = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "--diff-filter=M", base, "HEAD"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if out.returncode:
        raise SpecError(f"git diff {base} HEAD: {out.stderr.strip()}")
    return out.stdout.splitlines()


def check_paths(root, base, paths, env):
    """What capture-pair.sh --check-paths says of each path: {"linked":
    [path, ...], "copied": [path, ...], "refused": [(path, reason), ...]}."""
    found = {"linked": [], "copied": [], "refused": []}
    if not paths:
        return found
    check = subprocess.run(
        ["bash", str(HERE / "capture-pair.sh"), "--check-paths", "--base", base]
        + paths,
        cwd=root,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    if check.returncode:
        raise SpecError(f"capture-pair.sh --check-paths: {check.stderr.strip()}")
    for line in check.stdout.splitlines():
        kind, _, rest = line.partition(" ")
        if kind == "refused":
            path, _, reason = rest.partition(": ")
            found[kind].append((path, reason))
        elif kind in found:
            found[kind].append(rest)
    return found


def load_spec(path):
    try:
        text = Path(path).read_text(encoding="utf-8")
        spec = json.loads(text)
    except (OSError, ValueError) as e:
        raise SpecError(f"{path}: {e}")
    if not isinstance(spec, dict):
        raise SpecError(f"{path}: the spec is a JSON object")
    base_dir = Path(path).resolve().parent
    allowed = {
        "paths",
        "base",
        "windowSize",
        "timeoutMs",
        "prefs",
        "regimes",
        "defineRegimes",
        "prelude",
        "sets",
    }
    unknown = set(spec) - allowed
    if unknown:
        raise SpecError(f"unknown spec keys: {', '.join(sorted(unknown))}")
    if not spec.get("sets"):
        raise SpecError("the spec needs 'sets'")
    paths = None
    if "paths" in spec:
        paths = strings(spec["paths"], "paths")
        if not paths:
            raise SpecError("'paths' is empty; leave it out to take the changed files")
    if "base" in spec and not isinstance(spec["base"], str):
        raise SpecError("base is a string")
    default_size = window_size(spec.get("windowSize", "1280x800"), "spec")
    timeout = spec.get("timeoutMs", 120000)
    if not isinstance(timeout, int) or isinstance(timeout, bool) or timeout <= 0:
        raise SpecError("timeoutMs is a positive number of ms")

    def file(value, what):
        if not isinstance(value, str):
            raise SpecError(f"{what}: a file name is a string")
        p = (base_dir / value).resolve()
        if not p.is_file():
            raise SpecError(f"{what}: no such file: {p}")
        return p

    builtins = builtin_regimes()
    regimes = dict(builtins)
    for name, regime in mapping(spec.get("defineRegimes", {}), "defineRegimes").items():
        extra = set(regime) - {"prefs", "extends", "chromeSetup"}
        if extra:
            raise SpecError(f"regime {name}: unknown keys {', '.join(sorted(extra))}")
        extends = regime.get("extends", name if name in builtins else "light-nova")
        start = builtins.get(extends) if isinstance(extends, str) else None
        if not start:
            raise SpecError(f"regime {name}: no built-in regime {extends}")
        regimes[name] = {
            "prefs": start["prefs"]
            + pref_list(regime.get("prefs", []), f"regime {name} prefs"),
            "chromeSetup": file(regime["chromeSetup"], f"regime {name}")
            if "chromeSetup" in regime
            else start["chromeSetup"],
        }

    def regime_list(value, what):
        names = strings(value, what)
        missing = [r for r in names if r not in regimes]
        if missing:
            raise SpecError(f"{what}: no regime {', '.join(missing)}")
        if not names:
            raise SpecError(f"{what} is empty")
        twice = sorted({r for r in names if names.count(r) > 1})
        if twice:
            raise SpecError(f"{what}: {', '.join(twice)} listed twice")
        return names

    default_regimes = list(regimes)
    if "regimes" in spec:
        default_regimes = regime_list(spec["regimes"], "regimes")
    sets = {}
    for name, s in mapping(spec["sets"], "sets").items():
        extra = set(s) - {"script", "shots", "regimes", "windowSize", "steps"}
        if extra:
            raise SpecError(f"set {name}: unknown keys {', '.join(sorted(extra))}")
        if ("script" in s) == ("shots" in s):
            raise SpecError(f"set {name} needs either a script or shots")
        if "script" in s:
            body = file(s["script"], f"set {name}").read_text(encoding="utf-8")
        else:
            body = shots_body(name, s["shots"])
        sets[name] = {
            "body": body,
            "regimes": regime_list(s["regimes"], f"set {name} regimes")
            if "regimes" in s
            else default_regimes,
            "windowSize": window_size(s["windowSize"], f"set {name}")
            if "windowSize" in s
            else default_size,
            "steps": file(s["steps"], f"set {name} steps") if "steps" in s else None,
        }
    return {
        "paths": paths,
        "base": spec.get("base", "HEAD~1"),
        "timeoutMs": timeout,
        "prelude": file(spec["prelude"], "prelude").read_text(encoding="utf-8")
        if "prelude" in spec
        else "",
        "prefs": pref_list(spec.get("prefs", []), "prefs"),
        "regimes": regimes,
        "sets": sets,
    }


def tools(root):
    sys.path.insert(0, str(root / "toolkit" / "themes" / "tools"))


def probe_source(root, set_body, prelude):
    tools(root)
    import contrast
    import probelib

    return (
        (HERE / "capture.js").read_text(encoding="utf-8")
        + probelib.PRELUDE
        + '\nconst REGIME = Services.prefs.getCharPref("shots.regime");\n'
        + 'const SET = Services.prefs.getCharPref("shots.set");\n'
        + "const readings = {};\nconst context = {};\n\n"
        + contrast.page_function()
        + "\n"
        + (HERE / "matrix-shot.js").read_text(encoding="utf-8")
        + "\n"
        + prelude
        + "\nawait (async () => {\n"
        + set_body
        + "\n})();\nreturn { shots: globalThis.__shots, readings, context };\n"
    )


def stamp(spec, set_name, regime_name, probe, head_rev, base_rev):
    """What a cell's result depends on, hashed: the revisions, the paths, the
    probe and the set and regime definitions."""
    s, regime = spec["sets"][set_name], spec["regimes"][regime_name]
    files = [regime["chromeSetup"], s["steps"]]
    data = {
        "revs": [head_rev, base_rev],
        "paths": spec["paths"],
        "prefs": spec["prefs"] + regime["prefs"],
        "files": [f.read_text(encoding="utf-8") if f else None for f in files],
        "probe": probe,
        "windowSize": s["windowSize"],
    }
    return hashlib.sha1(json.dumps(data, sort_keys=True).encode()).hexdigest()[:12]


def objdir(root):
    out = subprocess.run(
        [str(root / "mach"), "environment"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    lines = out.stdout.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("object directory:") and i + 1 < len(lines):
            return lines[i + 1].strip()
    return None


def prepare_outdir(outdir, only):
    marker = outdir / MARKER
    if outdir.exists() and any(outdir.iterdir()) and not marker.exists():
        raise SpecError(f"{outdir} is not empty and holds no {MARKER}; pick another")
    if not only and outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    marker.touch()


def fails_for_good(shots):
    """Whether a half timed out or failed with a JS error other than a
    regime's setup saying the regime did not take."""
    for half in ("after", "before"):
        log = shots / f"{half}.log"
        if log.exists():
            text = log.read_text(errors="replace")
            if "ScriptTimeoutException" in text:
                return True
            if "JavascriptException" in text and "regime did not take" not in text:
                return True
    return False


def launched(shots):
    """Whether the after half got a browser session."""
    log = shots / "after.log"
    if not log.exists():
        return False
    text = log.read_text(errors="replace")
    return not any(
        f"firefox-probe: {s}" in text
        for s in ("no such binary:", "Marionette could not come up", "stale build")
    )


def run_capture_pair(cmd, root, env, out_path):
    """Run capture-pair.sh with its output in out_path. On Ctrl-C, which the
    child gets too, wait for it to restore the reverted paths, then re-raise."""
    with open(out_path, "w") as out:
        proc = subprocess.Popen(
            cmd, cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT, text=True
        )
        interrupted = False
        while True:
            try:
                proc.wait()
                break
            except KeyboardInterrupt:
                interrupted = True
                print(
                    "\nwaiting for capture-pair.sh to restore the reverted paths",
                    file=sys.stderr,
                )
    if interrupted:
        raise KeyboardInterrupt
    return proc.returncode, out_path.read_text(errors="replace")


def run_cell(root, spec, outdir, set_name, regime_name, probe, env):
    s = spec["sets"][set_name]
    regime = spec["regimes"][regime_name]
    shots = outdir / set_name / regime_name
    cmd = ["bash", str(HERE / "capture-pair.sh"), "--probe", "--base", spec["base"]]
    cmd += ["--timeout-ms", str(spec["timeoutMs"])]
    cmd += ["--window-size", s["windowSize"]]
    if regime["chromeSetup"]:
        setup = outdir / "probes" / f"setup-{regime_name}.js"
        setup.write_text(
            regime["chromeSetup"].read_text(encoding="utf-8")
            + "\nreturn setupRegime();\n"
        )
        cmd += [
            "--chrome-setup-file",
            setup.as_posix(),
            "--chrome-setup-async",
        ]
    if s["steps"]:
        cmd += ["--steps", s["steps"].as_posix()]
    for pref in [
        *spec["prefs"],
        *regime["prefs"],
        # Quoted, so that a name such as 125 or true stays a string pref.
        f"shots.regime='{regime_name}'",
        f"shots.set='{set_name}'",
    ]:
        cmd += ["--setpref", pref]
    cmd += [probe.as_posix(), *spec["paths"], shots.as_posix()]
    first_attempt = None
    for attempt in (1, 2):
        shutil.rmtree(shots, ignore_errors=True)
        shots.mkdir(parents=True)
        if first_attempt:
            (shots / "attempt-1.out").write_text(first_attempt)
        code, output = run_capture_pair(cmd, root, env, shots / "capture-pair.out")
        failed_for_good = code != 0 and fails_for_good(shots)
        result = {
            "exit": code,
            "shots": sum(
                1
                for line in output.splitlines()
                if re.match(r"SHOT .*[/\\]after-[^/\\]*\.png \d+x\d+ ", line)
            ),
            "attempts": attempt,
            # A failure every cell repeats: capture-pair.sh stopped before
            # launching (a refusal, a failed build, no binary), or the browser
            # never came up.
            "launched": launched(shots),
            "failsForGood": failed_for_good,
        }
        if (
            code == 0
            or not result["launched"]
            or failed_for_good
            or "no SHOT lines" in output
        ):
            break
        first_attempt = "".join(
            f"==> {f.name} <==\n{f.read_text(errors='replace')}\n"
            for f in sorted(shots.glob("*.out")) + sorted(shots.glob("*.log"))
        )
    return result, output.strip().splitlines()[-1:]


def resolve_paths(root, spec, env):
    """Check the spec's paths, or pick them from the files the change modified,
    before anything is cleared or launched."""
    dirty = subprocess.run(
        ["git", "diff", "--name-only", "HEAD"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    ).stdout.splitlines()
    if dirty:
        raise SpecError(
            "uncommitted changes, which both halves would load: "
            f"{', '.join(dirty)}; commit or stash them (SKILL.md §3)"
        )
    given = spec["paths"]
    found = check_paths(
        root, spec["base"], given or changed_files(root, spec["base"]), env
    )
    if given and found["refused"]:
        raise SpecError("; ".join(r for _, r in found["refused"]))
    if given:
        return given
    if found["refused"]:
        left = ", ".join(path for path, _ in found["refused"])
        print(f"capture-pair.sh refuses, so left alone: {left}")
    if found["copied"]:
        print(f"copied into the objdir: {', '.join(found['copied'])}")
        raise SpecError(
            "a copied file costs three `./mach build faster` runs a cell; "
            "list the files to revert in paths to accept that"
        )
    if not found["linked"]:
        raise SpecError(
            f"no file modified between {spec['base']} and HEAD is linked into "
            "the objdir; give paths"
        )
    print(f"paths: {', '.join(found['linked'])}")
    return found["linked"]


def run(args):
    root = topsrcdir()
    spec = load_spec(args.spec)
    outdir = Path(args.outdir).resolve()
    cells = [
        (set_name, regime)
        for set_name, s in spec["sets"].items()
        for regime in s["regimes"]
    ]
    if args.only:
        cells = [
            c for c in cells if any(fnmatch.fnmatch("/".join(c), p) for p in args.only)
        ]
        if not cells:
            raise SpecError("--only matches no cell")

    env = dict(os.environ)
    if "OBJDIR" not in env:
        found = objdir(root)
        if found:
            env["OBJDIR"] = found
    spec["paths"] = resolve_paths(root, spec, env)
    sources = {
        set_name: probe_source(root, s["body"], spec["prelude"])
        for set_name, s in spec["sets"].items()
    }

    prepare_outdir(outdir, args.only)

    scripts = outdir / "probes"
    scripts.mkdir(exist_ok=True)
    probes = {}
    for set_name, source in sources.items():
        probes[set_name] = scripts / f"{set_name}.js"
        probes[set_name].write_text(source)

    summary_path = outdir / "matrix.json"
    summary = {"cells": {}}
    if args.only and summary_path.exists():
        try:
            summary = json.loads(summary_path.read_text())
        except ValueError as e:
            raise SpecError(
                f"{summary_path} is not valid JSON ({e}); rerun without --only"
            )
    summary["spec"] = str(Path(args.spec).resolve())
    summary["base"] = spec["base"]
    summary["paths"] = spec["paths"]
    summary["baseRev"] = short_rev(root, spec["base"])
    summary["headRev"] = short_rev(root, "HEAD")
    summary["sets"] = {name: s["regimes"] for name, s in spec["sets"].items()}
    stamps = {
        f"{set_name}/{regime}": stamp(
            spec,
            set_name,
            regime,
            sources[set_name],
            summary["headRev"],
            summary["baseRev"],
        )
        for set_name, s in spec["sets"].items()
        for regime in s["regimes"]
    }
    summary["stamps"] = stamps
    rerun = {"/".join(c) for c in cells}
    stale = [
        key
        for key, cell in summary["cells"].items()
        if key not in rerun and cell.get("stamp") != stamps.get(key)
    ]
    if stale:
        print(
            "kept from an earlier run with another revision, paths or spec: "
            + ", ".join(sorted(stale))
        )

    failed = 0
    skipped_sets = set()
    first_cell = {}
    for i, (set_name, regime) in enumerate(cells, 1):
        key = f"{set_name}/{regime}"
        print(f"[{i}/{len(cells)}] {key} ...", end=" ", flush=True)
        if set_name in skipped_sets:
            summary["cells"].pop(key, None)
            shutil.rmtree(outdir / set_name / regime, ignore_errors=True)
            print("skipped")
            continue
        result, last = run_cell(
            root, spec, outdir, set_name, regime, probes[set_name], env
        )
        result["stamp"] = stamps[key]
        summary["cells"][key] = result
        summary_path.write_text(json.dumps(summary, indent=2))
        is_first = first_cell.setdefault(set_name, key) == key
        cell_dir = outdir / set_name / regime
        if result["exit"] == 0:
            errors = shot_errors(cell_dir)
            print(
                f"{result['shots']} shots"
                + (f", {len(errors)} failed: {'; '.join(errors)}" if errors else "")
            )
            continue
        if not result["launched"]:
            print("did not launch")
            print(f"capture-pair.sh: {''.join(last)}", file=sys.stderr)
            return EXIT_ERROR
        failed += 1
        print(f"failed; see {cell_dir / 'capture-pair.out'}")
        for line in shot_errors(cell_dir):
            print(f"  {line}")
        setup = spec["regimes"][regime]["chromeSetup"]
        others = any(c[0] == set_name for c in cells[i:])
        if is_first and result["failsForGood"] and not setup and others:
            skipped_sets.add(set_name)
            print(f"{set_name}'s first cell failed for good; skipping its other cells")
    summary_path.write_text(json.dumps(summary, indent=2))
    return 1 if failed else 0


def read_result(path):
    """The JSON a probe resolved with, from the {after,before}.json that
    capture-pair.sh writes; or None."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if isinstance(value, dict) and "steps" in value and "result" in value:
        value = value["result"]
    return value if isinstance(value, dict) else None


def shot_errors(cell):
    """Each shot's error in a cell, as "NAME: ERROR" lines, naming the half
    when only one has it."""
    errors = {}
    for half in ("after", "before"):
        readings = (read_result(cell / f"{half}.json") or {}).get("readings") or {}
        for name, value in readings.items():
            if isinstance(value, dict) and value.get("error"):
                errors.setdefault((name, value["error"]), []).append(half)
    return [
        f"{name}: {error}" + (f" ({halves[0]} half)" if len(halves) == 1 else "")
        for (name, error), halves in errors.items()
    ]


def is_contrast(value):
    return isinstance(value, dict) and "subjects" in value and "regime" in value


class Entries(list):
    """The contrast entries one subject has for one part and text, in walk
    order."""


def contrast_where(entry):
    """The part and the text or element a contrast entry measures."""
    return " ".join(
        str(x)
        for x in (entry.get("part"), entry.get("text") or entry.get("element"))
        if x
    )


def flatten(value, prefix=""):
    """Map dotted keys to the scalar values under them and to the empty
    containers, or to the Entries of a contrast result."""
    if isinstance(value, (dict, list)) and not value and prefix:
        return {prefix: value}
    if is_contrast(value):
        out = {}
        if value.get("error"):
            out[f"{prefix} error".strip()] = {"error": value["error"]}
        seen = {}
        for subject in value["subjects"]:
            label = f"{prefix} {subject['subject']}".strip()
            if subject["state"] != "default":
                label += f" {subject['state']}"
            seen[label] = seen.get(label, 0) + 1
            if seen[label] > 1:
                label += f" [{seen[label]}]"
            if subject.get("error"):
                out[f"{label} error"] = {"error": subject["error"]}
            if subject.get("warning"):
                out[f"{label} warning"] = subject["warning"]
            for entry in subject.get("entries", []):
                where = contrast_where(entry)
                item = {
                    "ratio": entry.get("ratio"),
                    "min": entry.get("min"),
                    "pass": entry.get("pass"),
                }
                if "insideRatio" in entry:
                    item["inside"] = {
                        "ratio": entry["insideRatio"],
                        "min": entry.get("min"),
                        "pass": entry.get("insidePass"),
                    }
                elif "insideNotJudged" in entry:
                    item["insideNotJudged"] = entry["insideNotJudged"]
                key = f"{label} ({where})" if where else label
                out.setdefault(key, Entries()).append(item)
            for entry in subject.get("notJudged", []):
                where = contrast_where(entry)
                key = f"{label} not judged" + (f" ({where})" if where else "")
                out.setdefault(key, []).append(entry.get("reason"))
        return out
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            out.update(flatten(v, f"{prefix}.{k}" if prefix else str(k)))
        return out
    if isinstance(value, list):
        out = {}
        for i, v in enumerate(value):
            out.update(flatten(v, f"{prefix}[{i}]"))
        return out
    return {prefix: value}


def align(key, before, after):
    """Pair two halves' Entries under one key: equal entries first, then the
    rest in walk order, so one entry more in a half shifts no other pair. A
    pair is numbered by its entry's place in the before half's walk, and an
    entry only after counts on from the end of that half."""
    before = list(enumerate(before))
    end = len(before)
    after = list(after)
    pairs = []
    for i, item in list(before):
        if item in after:
            before.remove((i, item))
            after.remove(item)
            pairs.append((i, item, item))
    n = min(len(before), len(after))
    pairs += [(i, b, a) for (i, b), a in zip(before[:n], after[:n])]
    pairs += [(i, b, None) for i, b in before[n:]]
    pairs += [(end + j, None, a) for j, a in enumerate(after[n:])]
    pairs.sort(key=lambda p: p[0])
    return [(key if i == 0 else f"{key} #{i + 1}", b, a) for i, b, a in pairs]


def ratio_notes(key, vb, va, changed, failing):
    fields = ("ratio", "min", "pass")
    if any(vb[f] != va[f] for f in fields):
        if vb["ratio"] != va["ratio"]:
            note = f"{key}: contrast {vb['ratio']} → {va['ratio']}"
        elif vb["min"] != va["min"]:
            note = f"{key}: contrast {va['ratio']}, minimum {vb['min']} → {va['min']}"
        else:
            # contrast() rounds the ratio it reports but judges the exact one.
            note = f"{key}: contrast about {va['ratio']}"
        if not va["pass"]:
            note += f", below {va['min']}"
        elif not vb["pass"]:
            note += f", was below {vb['min']}"
        changed.append(note)
    elif not va["pass"]:
        failing.append(
            f"{key}: contrast {va['ratio']}, below {va['min']} before and after"
        )


def compare(before, after):
    """Return the notes on what changed between two halves' readings of one
    shot, on errors or contrast measuring nothing the same in both, and on
    contrast entries failing in both."""
    before, after = as_readings(before), as_readings(after)
    changed = []
    if isinstance(before.get("styles"), dict) and isinstance(after.get("styles"), dict):
        changed = compare_styles(before.pop("styles"), after.pop("styles"))
    b, a = flatten(before), flatten(after)
    errors, failing = [], []
    for key in [*b, *(k for k in a if k not in b)]:
        vb, va = b.get(key, Entries()), a.get(key, Entries())
        if isinstance(vb, Entries) and isinstance(va, Entries):
            for sub, eb, ea in align(key, vb, va):
                if eb is None or ea is None:
                    changed.append(f"{sub}: only {'after' if eb is None else 'before'}")
                    continue
                ratio_notes(sub, eb, ea, changed, failing)
                ib, ia = eb.get("inside"), ea.get("inside")
                if ib and ia:
                    ratio_notes(f"{sub} inside", ib, ia, changed, failing)
                elif ib or ia:
                    changed.append(f"{sub} inside: only {'after' if ia else 'before'}")
                nb, na = eb.get("insideNotJudged"), ea.get("insideNotJudged")
                if nb != na:
                    changed.append(
                        f"{sub} inside not judged: {shown(nb)} → {shown(na)}"
                    )
        elif key not in a:
            changed.append(f"{key} only before: {shown(vb)}")
        elif key not in b:
            changed.append(f"{key} only after: {shown(va)}")
        elif isinstance(va, dict) and "error" in va and va == vb:
            errors.append(f"{key}: {va['error']}, before and after")
        elif (key == "error" or key.endswith(".error")) and va == vb:
            errors.append(
                f"{va}, before and after"
                if key == "error"
                else f"{key}: {va}, before and after"
            )
        elif (
            key.endswith(" warning")
            and str(va).startswith("measured nothing")
            and va == vb
        ):
            errors.append(f"{key[: -len(' warning')]}: {va}, before and after")
        elif vb != va:
            changed.append(f"{key}: {shown(vb)} → {shown(va)}")
    return changed, errors, failing


def shown(value):
    if isinstance(value, dict) and "error" in value:
        return value["error"]
    return json.dumps(value, ensure_ascii=False)


def as_readings(value):
    """A shot's readings as a dict, with a script's bare value under "value"."""
    if isinstance(value, dict):
        return dict(value)
    return {} if value is None else {"value": value}


SHOWN_ELEMENTS = 6


def elements(keys):
    names = [key or "the element" for key in keys]
    more = len(names) - SHOWN_ELEMENTS
    if more > 1:
        names = names[:SHOWN_ELEMENTS] + [f"{more} more"]
    return ", ".join(names)


def compare_styles(before, after):
    """Notes on the computed styles `shot()` read in two halves, one per
    property and change, naming the elements it changed on."""
    groups = {}
    only = {"before": [], "after": []}
    for kb, ka in pair_styles(before, after):
        if ka is None or kb is None:
            only["before" if ka is None else "after"].append(kb or ka)
            continue
        vb, va = before[kb], after[ka]
        name = kb if kb == ka else f"{kb} (after: {ka})"
        for prop in [*vb, *(p for p in va if p not in vb)]:
            if vb.get(prop) != va.get(prop):
                change = (prop, vb.get(prop), va.get(prop))
                groups.setdefault(change, []).append(name)
    notes = [
        f"{prop} {shown_style(old)} → {shown_style(new)}: {elements(keys)}"
        for (prop, old, new), keys in groups.items()
    ]
    for half, keys in only.items():
        if keys:
            notes.append(f"rendered only {half}: {elements(keys)}")
    return notes


def pair_styles(before, after):
    """Pair the elements of two halves' styles, keyed as readStyles() keys
    them, within each family of keys that differ only in a sibling index
    such as [2]: same key and styles first, then, when the family has a
    sibling more in one half, same styles, then the rest in order, so a
    sibling that appears or disappears shifts no other pair.
    Return (before key or None, after key or None) pairs."""
    families = {}
    for i, styles in enumerate((before, after)):
        for key in styles:
            family = re.sub(r"\[\d+\]", "", key)
            families.setdefault(family, ([], []))[i].append(key)
    pairs = []
    for bk, ak in families.values():
        matches = [lambda kb, ka: kb == ka and before[kb] == after[ka]]
        if len(bk) != len(ak):
            matches.append(lambda kb, ka: before[kb] == after[ka])
        for match in matches:
            for kb in list(bk):
                ka = next((ka for ka in ak if match(kb, ka)), None)
                if ka is not None:
                    bk.remove(kb)
                    ak.remove(ka)
                    pairs.append((kb, ka))
        n = min(len(bk), len(ak))
        pairs += list(zip(bk[:n], ak[:n]))
        pairs += [(kb, None) for kb in bk[n:]]
        pairs += [(None, ka) for ka in ak[n:]]
    return pairs


def shown_style(value):
    return "(none)" if value is None or value == "" else value


STATUS_ORDER = ["changed", "error", "pixels", "failing", "same"]


def zoom(cell, name, bbox, size):
    """Write before, after and the difference cropped and enlarged by
    sheet.zoom_box(); return the paths, or None when it picks no crop."""
    from PIL import Image

    picked = sheet.zoom_box(bbox, size)
    if not picked:
        return None
    box, scale = picked
    out = {}
    for half in ("before", "after", "diff"):
        src = cell / f"{half}-{name}.png"
        dest = cell / f"zoom-{half}-{name}.png"
        with Image.open(src) as im:
            sheet.enlarge(im, box, scale).save(dest)
        out[half] = link(dest, cell)
    out["scale"] = scale
    return out


def link(path, cell):
    """The path of a cell's file relative to OUTDIR."""
    return path.relative_to(cell.parent.parent).as_posix()


def compare_cell(cell, threshold, pixel_diff):
    """Compare each shot of one cell; return {name: regime entry}."""
    halves = {}
    unfinished = []
    for half in ("after", "before"):
        result = read_result(cell / f"{half}.json")
        if result is None:
            unfinished.append(half)
        halves[half] = (result or {}).get("readings") or {}
    # capture-pair.sh runs the after half first and stops when it fails.
    not_run = unfinished[0] if unfinished else None
    names = sorted(
        {
            p.name.split("-", 1)[1][: -len(".png")]
            for p in cell.glob("*.png")
            if p.name.startswith(("after-", "before-"))
        }
        | set(halves["after"])
        | set(halves["before"])
    )
    out = {}
    for name in names:
        after, before = cell / f"after-{name}.png", cell / f"before-{name}.png"
        diff = cell / f"diff-{name}.png"
        zooms = [
            cell / f"zoom-{half}-{name}.png" for half in ("before", "after", "diff")
        ]
        for stale in [diff, *zooms]:
            stale.unlink(missing_ok=True)
        entry = {
            "count": 0,
            "below": 0,
            "maxDelta": 0,
            "bbox": None,
            "zoom": None,
            "notes": [],
            "failing": [],
            "notRun": not_run,
        }
        if not_run:
            # One half's images only, so nothing to compare them with.
            pass
        elif after.exists() and before.exists():
            im, other = pixel_diff.load(str(after)), pixel_diff.load(str(before))
            if im.size != other.size:
                entry["notes"].append(
                    "size {}x{} → {}x{} device px".format(*other.size, *im.size)
                )
            mask, channel_max = pixel_diff.diff_mask(im, other, threshold)
            entry["count"] = mask.histogram()[255]
            entry["below"] = sum(channel_max.histogram()[1 : threshold + 1])
            entry["maxDelta"] = channel_max.getextrema()[1]
            entry["bbox"] = mask.getbbox()
            if entry["count"]:
                pixel_diff.write_diff_image(str(diff), im, mask, None)
                entry["zoom"] = zoom(cell, name, entry["bbox"], mask.size)
        elif after.exists() or before.exists():
            entry["notes"].append(
                f"captured only {'after' if after.exists() else 'before'}"
            )
        if not_run:
            ran = halves["after"].get(name)
            error = ran.get("error") if isinstance(ran, dict) else None
            changed, failing = [], []
            unfinished_note = f"the {not_run} half did not finish"
            errors = [f"{error}; {unfinished_note}" if error else unfinished_note]
        else:
            changed, errors, failing = compare(
                halves["before"].get(name), halves["after"].get(name)
            )
        entry["notes"] += changed
        entry["errors"] = errors
        entry["failing"] = failing
        if entry["notes"]:
            entry["status"] = "changed"
        elif errors:
            entry["status"] = "error"
        elif entry["count"] or entry["below"]:
            entry["status"] = "pixels"
        elif failing:
            entry["status"] = "failing"
        else:
            entry["status"] = "same"
        for half, path in (("before", before), ("after", after), ("diff", diff)):
            entry[half] = link(path, cell) if path.exists() else None
        held = set()
        for readings in halves.values():
            if isinstance(readings.get(name), dict):
                held.update(readings[name].get("heldAnimations") or [])
        entry["heldAnimations"] = sorted(held)
        out[name] = entry
    return out


def not_compared(missing):
    return f"; not compared in {', '.join(missing)}" if missing else ""


def pixel_summary(e):
    if e["count"]:
        return f"{e['count']} px"
    if e["below"]:
        return f"{e['below']} px by at most {e['maxDelta']}"
    return ""


def row_line(row):
    """One line of `render`'s output for a row: status, shot, and per shown
    regime the regimes it stands for, the pixels that differ and the first
    note."""
    parts = []
    for e in row["regimes"]:
        notes = e["notes"] + e["errors"] + e["failing"]
        text = ", ".join([e["regime"], *e["sameIn"]])
        details = [d for d in (pixel_summary(e), notes and notes[0]) if d]
        if len(notes) > 1:
            details.append(f"{len(notes) - 1} more notes")
        parts.append(text + (f" ({'; '.join(details)})" if details else ""))
    return (
        f"{row['status']:8} {row['set']}/{row['name']}: "
        + " | ".join(parts)
        + not_compared(row["missing"])
    )


LISTS = [
    ("changed", "A reading changed"),
    (
        "error",
        "The shot failed, or its contrast measured nothing, the same way before and after",
    ),
    ("pixels", "Only pixels differ"),
    ("failing", "Only the same contrast failure before and after"),
]


def regimes_text(e):
    return ", ".join([e["regime"], *e["sameIn"]])


def entry_summary(e):
    if e["notRun"]:
        return f"not compared: the {e['notRun']} half did not finish"
    if e["below"] and not e["count"]:
        return (
            f"{e['below']} pixels differ, by at most {e['maxDelta']}, "
            "under the threshold"
        )
    if not (e["before"] or e["after"]):
        return "not captured"
    if not e["count"]:
        return "no pixel differs"
    text = f"{e['count']} pixels differ"
    if e["bbox"]:
        text += ", from {},{} to {},{}".format(*e["bbox"])
    return text


def entry_block(outdir, number, row, e, fonts):
    """One sheet row: the caption, then before, after and the difference, then
    the zoomed crops of the three when there are any."""
    from PIL import Image

    def load(rel):
        if not rel:
            return None
        with Image.open(outdir / rel) as im:
            return im.convert("RGB")

    z = e["zoom"]
    return sheet.pair_block(
        f"{number}. {row['set']}/{row['name']}: {regimes_text(e)}",
        entry_summary(e),
        [load(e["before"]), load(e["after"]), load(e["diff"])],
        fonts,
        zoomed=(
            ([load(z[half]) for half in ("before", "after", "diff")], z["scale"])
            if z
            else None
        ),
        missing_diff=missing_diff(e),
    )


def md_entry(number, e):
    regime_note = f" {e['note']}" if e["note"] else ""
    lines = [f"- **{number}.** {regimes_text(e)}: {entry_summary(e)}.{regime_note}"]
    for text in e["notes"] + e["errors"] + e["failing"]:
        lines.append(f"  - {text}")
    if e["heldAnimations"]:
        lines.append(
            "  - animations held still in both halves: "
            + ", ".join(e["heldAnimations"])
        )
    return lines


def missing_diff(e):
    """What the Difference panel says when there is no difference image."""
    if e["notRun"]:
        return "not compared"
    if e["below"]:
        return "under the threshold"
    if e["before"] and e["after"]:
        return "no pixel differs"
    return "nothing to compare"


def is_sheet(name):
    """Whether `render` writes a sheet of this name in OUTDIR."""
    return bool(
        re.fullmatch(r"({})(-\d+)?\.png".format("|".join(k for k, _ in LISTS)), name)
    )


def draw_and_note(outdir, data):
    """Write a sheet per non-empty list and OUTDIR/notes.md; return the sheet
    paths."""
    fonts = sheet.fonts()
    for old in outdir.glob("*.png"):
        if is_sheet(old.name):
            old.unlink()
    md = [f"Screenshot matrix, {data['title']}."]
    cells = [
        ("Cells that failed", data["failedCells"]),
        ("Cells not run", data["notRunCells"]),
        ("Cells from another revision, paths or spec", data["staleCells"]),
    ]
    sheets = []
    number = 0
    for key, label in LISTS:
        rows = [r for r in data["rows"] if r["status"] == key]
        if not rows:
            continue
        blocks, section = [], []
        for row in rows:
            section.append("")
            extra = [row["note"]] if row["note"] else []
            if row["same"]:
                extra.append(f"Identical in {', '.join(row['same'])}.")
            if row["missing"]:
                extra.append(f"Not compared in {', '.join(row['missing'])}.")
            heading = f"**{row['set']}/{row['name']}**"
            section.append(heading + (": " + " ".join(extra) if extra else ""))
            section.append("")
            for e in row["regimes"]:
                number += 1
                section += md_entry(number, e)
                blocks.append(entry_block(outdir, number, row, e, fonts))
        paths = sheet.write_sheets(
            outdir / f"{key}.png", f"{label}: {data['title']}", blocks, fonts
        )
        sheets += paths
        md += [
            "",
            f"## {label}",
            "",
            ("Sheets: " if len(paths) > 1 else "Sheet: ")
            + ", ".join(p.name for p in paths),
        ]
        md += section
    if data["identical"]:
        md += ["", "## Identical in every regime", ""]
        for item in data["identical"]:
            text = f"- {item['shot']}"
            if item["missing"]:
                text += f"; not compared in {', '.join(item['missing'])}"
            if item["note"]:
                text += f": {item['note']}"
            md.append(text)
    if any(cells for _, cells in cells):
        md += ["", "## Cells", ""]
        md += [f"- {what}: {', '.join(c)}" for what, c in cells if c]
    if data["context"]:
        md += ["", "## Context", ""]
        for cell, halves in data["context"].items():
            md.append(
                f"- {cell}: before {json.dumps(halves['before'])}, "
                f"after {json.dumps(halves['after'])}"
            )
    (outdir / "notes.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    return sheets


def render(args):
    outdir = Path(args.outdir).resolve()
    summary_path = outdir / "matrix.json"
    if not (outdir / MARKER).exists() or not summary_path.exists():
        raise SpecError(f"{outdir} holds no matrix run")
    if not 0 <= args.threshold <= 255:
        raise SpecError(f"--threshold {args.threshold} is not between 0 and 255")
    summary = json.loads(summary_path.read_text())
    notes = {}
    if args.notes:
        try:
            notes = json.loads(Path(args.notes).read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise SpecError(f"{args.notes}: {e}")
        if not isinstance(notes, dict) or not all(
            isinstance(v, str) for v in notes.values()
        ):
            raise SpecError(f"{args.notes}: notes map SET/NAME keys to strings")
    tools(topsrcdir())
    import pixel_diff

    rows = []
    failed_cells = []
    not_run_cells = []
    stale_cells = []
    identical = []
    context = {}
    used_notes = set()
    same_keys = set()

    def note(key):
        used_notes.add(key)
        return notes.get(key)

    stamps = summary.get("stamps", {})
    for set_name, regimes in summary["sets"].items():
        by_name = {}
        for regime in regimes:
            key = f"{set_name}/{regime}"
            cell = summary["cells"].get(key)
            if not cell:
                not_run_cells.append(key)
                continue
            if cell.get("stamp") != stamps.get(key):
                stale_cells.append(key)
            if cell["exit"] != 0:
                failed_cells.append(key)
            # A failed cell still has the readings of the shots that failed
            # in both halves, such as a selector matching nothing.
            cell_dir = outdir / set_name / regime
            cell_context = {
                half: (read_result(cell_dir / f"{half}.json") or {}).get("context")
                for half in ("before", "after")
            }
            if any(cell_context.values()):
                context[key] = cell_context
            for name, entry in compare_cell(
                cell_dir, args.threshold, pixel_diff
            ).items():
                entry["regime"] = regime
                by_name.setdefault(name, []).append(entry)
        for name, entries in by_name.items():
            status = min((e["status"] for e in entries), key=STATUS_ORDER.index)
            compared = {e["regime"] for e in entries}
            missing = [r for r in regimes if r not in compared]
            same_keys.update(
                f"{set_name}/{e['regime']}/{name}"
                for e in entries
                if e["status"] == "same"
            )
            if status == "same":
                identical.append({
                    "shot": f"{set_name}/{name}",
                    "missing": missing,
                    "note": note(f"{set_name}/{name}"),
                })
                continue
            # A regime showing the same notes as an earlier one, with no
            # pixel differing in either, folds into that one's sameIn.
            shown_regimes = {}
            for e in (e for e in entries if e["status"] != "same"):
                e["note"] = note(f"{set_name}/{e['regime']}/{name}")
                key = (
                    e["status"],
                    tuple(e["notes"] + e["errors"] + e["failing"]),
                    e["note"],
                )
                pixels = e["count"] or e["below"]
                if not pixels and key in shown_regimes:
                    shown_regimes[key]["sameIn"].append(e["regime"])
                else:
                    e["sameIn"] = []
                    shown_regimes[id(e) if pixels else key] = e
            rows.append({
                "set": set_name,
                "name": name,
                "status": status,
                "note": note(f"{set_name}/{name}"),
                "count": max(e["count"] for e in entries),
                "regimes": list(shown_regimes.values()),
                "same": [e["regime"] for e in entries if e["status"] == "same"],
                "missing": missing,
            })
    rows.sort(key=lambda r: (STATUS_ORDER.index(r["status"]), -r["count"]))
    data = {
        "title": f"{Path(summary['spec']).name}: {summary.get('baseRev') or summary['base']}"
        f" → {summary.get('headRev') or 'HEAD'}",
        "failedCells": failed_cells,
        "notRunCells": not_run_cells,
        "staleCells": stale_cells,
        "identical": identical,
        "rows": rows,
        "context": context,
    }
    (outdir / "rows.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    sheets = draw_and_note(outdir, data)
    counts = {s: sum(r["status"] == s for r in rows) for s in STATUS_ORDER}
    print(
        f"wrote {', '.join(p.name for p in sheets) or 'no sheet'}, notes.md and "
        f"rows.json in {outdir}: {counts['changed']} changed, "
        f"{counts['error']} failing or measuring nothing the same way in both halves or unfinished in one, "
        f"{counts['pixels']} pixels only, "
        f"{counts['failing']} with only a contrast failure in both halves, "
        f"{len(identical)} identical"
        + (f"; cells that failed: {', '.join(failed_cells)}" if failed_cells else "")
        + (f"; cells not run: {', '.join(not_run_cells)}" if not_run_cells else "")
        + (f"; stale cells: {', '.join(stale_cells)}" if stale_cells else "")
    )
    for row in rows:
        print(row_line(row))
    for item in identical:
        print(
            f"same     {item['shot']}"
            + not_compared(item["missing"])
            + (f": {item['note']}" if item["note"] else "")
        )
    for key, halves in context.items():
        print(
            f"context  {key}: "
            + "; ".join(f"{half} {json.dumps(v)}" for half, v in halves.items())
        )
    unused = set(notes) - used_notes
    if unused & same_keys:
        print(
            "notes on identical regimes, not shown: "
            + ", ".join(sorted(unused & same_keys)),
            file=sys.stderr,
        )
    if unused - same_keys:
        print(
            f"notes matching no shot: {', '.join(sorted(unused - same_keys))}",
            file=sys.stderr,
        )
    return 0


def get_parser():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="capture every cell of a spec")
    r.add_argument("spec")
    r.add_argument("outdir")
    r.add_argument(
        "--only",
        action="append",
        metavar="SET/REGIME",
        help="rerun only the cells matching this glob (repeatable)",
    )
    p = sub.add_parser("render", help="draw the comparison sheets for a run")
    p.add_argument("outdir")
    p.add_argument("--notes", metavar="FILE", help="JSON notes keyed by SET/NAME")
    p.add_argument(
        "--threshold",
        type=int,
        default=0,
        help="count a pixel when a channel differs by more than this (default 0)",
    )
    return ap


def main():
    args = get_parser().parse_args()
    try:
        return run(args) if args.command == "run" else render(args)
    except SpecError as e:
        print(f"matrix.py: error: {e}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("matrix.py: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
