# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Run raptor twice on one worker, against two builds or two browsers.

A perf test task lands on whichever worker happens to be free, so separate
profiling runs usually end up on different machines. This module runs both
tests on one machine and keeps their artifacts in separate subdirectories of
the blob upload dir and of the mozharness log dir (`base/` and `new/`).

For two Firefox builds, only the installer and breakpad symbols differ. For a
Windows Firefox versus Chromium-as-Release comparison, the task also fetches
the Chromium toolchain and runs each browser with its own profiling arguments.

Perfherder ingests both runs' results. A run with its own installer gets an
`ab-<side>` extra option, so its results go into a separate series from those
of the push that the task runs on.

AB tasks run `raptor_ab_script.py` in place of `raptor_script.py`, with the
same arguments. It calls `run_ab_profiles`, which runs `raptor_script.py` once
per side as described by the environment below.

    RAPTOR_AB_CONFIG  JSON with an order and base/new run descriptions. A run
                      may override arguments, installer_url, symbols_url, and
                      task_label; omitted values use this task's own values.
"""

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

SYMBOLS_ARTIFACT = "target.crashreporter-symbols.zip"
DOWNLOAD_ATTEMPTS = 5
DOWNLOAD_RETRY_DELAY = 30


def log(message):
    print(f"raptor-ab: {message}", flush=True)


def download(url, dest):
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        log(f"downloading {url} -> {dest} (attempt {attempt})")
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                with open(dest, "wb") as fh:
                    shutil.copyfileobj(response, fh)
        except Exception as e:
            log(f"download failed: {e}")
            if attempt == DOWNLOAD_ATTEMPTS:
                raise
            time.sleep(DOWNLOAD_RETRY_DELAY)
        else:
            log(f"downloaded {dest.stat().st_size} bytes")
            return


def run_raptor(run, side):
    """Run one side with its mozharness arguments and installer."""
    env = os.environ.copy()
    extra_config = json.loads(env.get("EXTRA_MOZHARNESS_CONFIG", "{}"))
    # mozharness deletes the log files it finds in its log dir when it starts.
    extra_config["log_dir"] = os.path.join(extra_config.get("log_dir", "logs"), side)
    if run.get("installer_url"):
        extra_config["installer_url"] = run["installer_url"]
    env["EXTRA_MOZHARNESS_CONFIG"] = json.dumps(extra_config, sort_keys=True)

    arguments = run.get("arguments")
    if arguments is None:
        arguments = sys.argv[1:]
    else:
        fetches_dir = env["MOZ_FETCHES_DIR"]
        arguments = [arg.replace("$MOZ_FETCHES_DIR", fetches_dir) for arg in arguments]
    if run.get("installer_url"):
        arguments = [*arguments, f"--perfherder-extra-option=ab-{side}"]
    if run.get("task_label"):
        env["RAPTOR_TASK_LABEL"] = run["task_label"]
    raptor_script = os.path.join(os.path.dirname(sys.argv[0]), "raptor_script.py")
    command = [sys.executable, "-u", raptor_script, *arguments]
    log(f"running {side}: {' '.join(command)}")

    return subprocess.call(command, env=env)


def collect_artifacts(blob_upload_dir, destination):
    """Set a run's artifacts aside; the next run clobbers the work dir."""
    try:
        if not blob_upload_dir.is_dir():
            log(f"no artifacts at {blob_upload_dir}")
            return
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(blob_upload_dir), str(destination))
        log(f"moved artifacts to {destination}")
    except OSError as e:
        # Never let this hide why the run itself failed.
        log(f"could not collect artifacts: {e}")


def run_ab_profiles():
    config = json.loads(os.environ["RAPTOR_AB_CONFIG"])
    runs = config["runs"]
    order = config["order"]
    if sorted(order) != ["base", "new"] or set(runs) != {"base", "new"}:
        log(f"invalid AB run configuration: {config!r}")
        return 1
    extra_config = json.loads(os.environ.get("EXTRA_MOZHARNESS_CONFIG", "{}"))
    current_installer_url = extra_config.get("installer_url")
    if not current_installer_url:
        log("EXTRA_MOZHARNESS_CONFIG has no installer_url")
        return 1

    fetches_dir = Path(os.environ["MOZ_FETCHES_DIR"]).resolve()
    symbols_in_place = fetches_dir / SYMBOLS_ARTIFACT
    if not symbols_in_place.is_file():
        log(f"{symbols_in_place} is missing")
        return 1
    swap_symbols = any(run.get("symbols_url") for run in runs.values())
    if swap_symbols:
        symbols_stash = fetches_dir / "ab-symbols"
        symbols_stash.mkdir(exist_ok=True)
        os.replace(symbols_in_place, symbols_stash / "current.zip")
        for side, run in runs.items():
            if run.get("symbols_url"):
                download(run["symbols_url"], symbols_stash / f"{side}.zip")

    blob_upload_dir = Path("build/blobber_upload_dir").resolve()
    staging_dir = Path("ab-results").resolve()

    exit_code = 0
    completed = []
    for side in order:
        run = runs[side]
        log(f"===== {side}: {run['description']}")
        if swap_symbols:
            symbol_name = f"{side}.zip" if run.get("symbols_url") else "current.zip"
            os.replace(symbols_stash / symbol_name, symbols_in_place)
        try:
            exit_code = run_raptor(run, side)
        finally:
            if swap_symbols and symbols_in_place.is_file():
                os.replace(symbols_in_place, symbols_stash / symbol_name)
            collect_artifacts(blob_upload_dir, staging_dir / side)
        log(f"===== {side} finished with exit code {exit_code}")
        if exit_code != 0:
            log(f"skipping the remaining run(s) because {side} failed")
            break
        completed.append(side)

    if swap_symbols and (symbols_stash / "current.zip").is_file():
        os.replace(symbols_stash / "current.zip", symbols_in_place)

    # Reassemble what the worker uploads as public/test_info. The runs clobber
    # the work dir, so this has to happen after the last one.
    blob_upload_dir.mkdir(parents=True, exist_ok=True)
    for side in order:
        if (staging_dir / side).is_dir():
            shutil.move(str(staging_dir / side), str(blob_upload_dir / side))

    summary = {
        "order": order,
        "completed": completed,
        "runs": {
            side: {
                "browser": runs[side]["browser"],
                "installer_url": (
                    runs[side].get("installer_url", current_installer_url)
                    if runs[side]["browser"] == "firefox"
                    else None
                ),
                "description": runs[side]["description"],
            }
            for side in order
        },
    }
    (blob_upload_dir / "ab-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8"
    )
    log(f"summary: {json.dumps(summary, sort_keys=True)}")

    return exit_code
