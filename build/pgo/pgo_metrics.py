# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Timings and profile data of a PGO profile run, reported to Perfherder and
recorded as markers in the task's resource usage profile."""

import json
import os
import subprocess
import time
from contextlib import contextmanager

# Durations of the profile run's stages.
STAGES = {}

HIGHER = {"lowerIsBetter": False}


def record_marker(name, start, end, text):
    """Record a marker spanning the given time.monotonic() timestamps in the
    resource usage profile of the task, when run-task monitors it."""
    try:
        from mozsystemmonitor.resourcemonitor import SystemResourceMonitor
    except ImportError:
        return
    SystemResourceMonitor.forward_marker(
        name, start, end, {"type": "Text", "text": text}
    )


@contextmanager
def stage(name):
    start = time.monotonic()
    try:
        yield
    finally:
        end = time.monotonic()
        STAGES[name] = end - start
        record_marker("PGO", start, end, name)


def covered_functions(llvm_profdata, profdata):
    """Return the names of the functions the training run executed."""
    output = subprocess.check_output(
        [llvm_profdata, "show", "--covered", profdata], text=True, timeout=600
    )
    return [line for line in output.splitlines() if line]


def profile_summary(llvm_profdata, profdata):
    """Return the counts printed by `llvm-profdata show`, e.g. "Total
    functions", and the number of functions the training run executed, or an
    empty dict if it fails."""
    try:
        output = subprocess.check_output(
            [llvm_profdata, "show", profdata], text=True, timeout=600
        )
        executed = len(covered_functions(llvm_profdata, profdata))
    except (OSError, subprocess.SubprocessError) as e:
        print(f"Could not read the profile summary: {e}")
        return {}
    summary = {"Executed functions": executed}
    for line in output.splitlines():
        name, sep, value = line.partition(":")
        if sep and value.strip().isdigit():
            summary[name.strip()] = int(value)
    return summary


def suite(name, value, subtests=(), **options):
    """Return a Perfherder suite, without alerting, of (name, value) or
    (name, value, options) subtests. options, e.g. unit or lowerIsBetter,
    apply to the suite and its subtests."""
    options = {"lowerIsBetter": True, **options}
    extra_options = []
    if "TASKCLUSTER_INSTANCE_TYPE" in os.environ:
        extra_options.append(os.environ["TASKCLUSTER_INSTANCE_TYPE"])
    return {
        "name": name,
        "value": value,
        "shouldAlert": False,
        "extraOptions": extra_options,
        **options,
        "subtests": [
            {"name": n, "value": v, **options, **(o[0] if o else {})}
            for n, v, *o in subtests
        ],
    }


def write_perfherder(suites, path):
    """Print the PERFHERDER_DATA of the given suites and write it to path."""
    data = {"framework": {"name": "build_metrics"}, "suites": suites}
    print(f"PERFHERDER_DATA: {json.dumps(data, sort_keys=True)}", flush=True)
    with open(path, "w") as f:
        json.dump(data, f)


def report_perfherder(
    llvm_profdata, profraw_files, profdata, upload_path, extra_suites=()
):
    """Report the stages and the profile data, with extra_suites, to
    Perfherder. This is best effort, so as not to fail a profile run that
    produced a profile."""
    try:
        _report_perfherder(
            llvm_profdata, profraw_files, profdata, upload_path, extra_suites
        )
    except Exception as e:
        print(f"Could not report the profile run to Perfherder: {e}")


def _report_perfherder(
    llvm_profdata, profraw_files, profdata, upload_path, extra_suites
):
    summary = profile_summary(llvm_profdata, profdata)
    suites = [
        suite(
            "pgo profile run", sum(STAGES.values()), sorted(STAGES.items()), unit="s"
        ),
        *extra_suites,
        suite(
            "pgo profile data",
            os.path.getsize(profdata),
            [
                ("profraw files", len(profraw_files)),
                ("profraw bytes", sum(os.path.getsize(f) for f in profraw_files)),
            ]
            + (
                [("Executed functions", summary["Executed functions"], HIGHER)]
                if "Executed functions" in summary
                else []
            )
            + [
                (name, summary[name])
                for name in (
                    "Total functions",
                    "Total number of blocks",
                    "Total count",
                    "Maximum function count",
                )
                if name in summary
            ],
        ),
    ]
    write_perfherder(
        suites, os.path.join(upload_path, "perfherder-data-pgo-profile.json")
    )
