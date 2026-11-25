#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Capture a before/after pair of every surface in a set of color regimes.

    ./mach python --virtualenv desktop-theme -- matrix.py run SPEC OUTDIR

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
  regimes/. One named like a built-in regime replaces it.
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
is selected, in `context`.
`contrast(subjects, {states, surface, min, nonText})` takes selectors or
elements, not a mix, and returns what `./mach theme contrast` prints as JSON
for them. The probe resolves with the shots, the readings and the context,
which capture-pair.sh writes as OUTDIR/SET/REGIME/{after,before}.json beside
the browser's output in {after,before}.log.

The run checks the paths before it clears anything. OUTDIR is cleared first,
and only when it holds the marker file a previous run wrote. OUTDIR/matrix.json
records each cell's result. On Ctrl-C the run waits for capture-pair.sh to
restore the reverted paths. It exits 0 when every cell captured, 1 when one
failed and 2 on an error.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

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


RESERVED = {"probes", "matrix.json"}


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
        if name in RESERVED:
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


def prepare_outdir(outdir):
    marker = outdir / MARKER
    if outdir.exists() and any(outdir.iterdir()) and not marker.exists():
        raise SpecError(f"{outdir} is not empty and holds no {MARKER}; pick another")
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    marker.touch()


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
    shots.mkdir(parents=True)
    code, output = run_capture_pair(cmd, root, env, shots / "capture-pair.out")
    return {
        "exit": code,
        "shots": sum(
            1
            for line in output.splitlines()
            if re.match(r"SHOT .*[/\\]after-[^/\\]*\.png \d+x\d+ ", line)
        ),
    }


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

    prepare_outdir(outdir)

    scripts = outdir / "probes"
    scripts.mkdir(exist_ok=True)
    probes = {}
    for set_name, source in sources.items():
        probes[set_name] = scripts / f"{set_name}.js"
        probes[set_name].write_text(source)

    summary_path = outdir / "matrix.json"
    summary = {"cells": {}}
    summary["spec"] = str(Path(args.spec).resolve())
    summary["base"] = spec["base"]
    summary["paths"] = spec["paths"]
    summary["baseRev"] = short_rev(root, spec["base"])
    summary["headRev"] = short_rev(root, "HEAD")
    summary["sets"] = {name: s["regimes"] for name, s in spec["sets"].items()}

    failed = 0
    for i, (set_name, regime) in enumerate(cells, 1):
        key = f"{set_name}/{regime}"
        print(f"[{i}/{len(cells)}] {key} ...", end=" ", flush=True)
        result = run_cell(root, spec, outdir, set_name, regime, probes[set_name], env)
        summary["cells"][key] = result
        summary_path.write_text(json.dumps(summary, indent=2))
        cell_dir = outdir / set_name / regime
        if result["exit"] == 0:
            print(f"{result['shots']} shots")
            continue
        failed += 1
        print(f"failed; see {cell_dir / 'capture-pair.out'}")
    summary_path.write_text(json.dumps(summary, indent=2))
    return 1 if failed else 0


def get_parser():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="capture every cell of a spec")
    r.add_argument("spec")
    r.add_argument("outdir")
    return ap


def main():
    args = get_parser().parse_args()
    try:
        return run(args)
    except SpecError as e:
        print(f"matrix.py: error: {e}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("matrix.py: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
