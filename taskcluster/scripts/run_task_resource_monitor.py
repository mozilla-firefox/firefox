# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Startup hook for run-task-git, run in-process via RUN_TASK_STARTUP_HOOK, and
imported by run-task-hg.

Starts monitoring resource usage if RUN_TASK_RESOURCE_USAGE is set to the path
of the profile to write when run-task exits. That file is left empty if
resource usage can't be monitored. The profile is named after
RUN_TASK_RESOURCE_USAGE_NAME, if set.

Monitors started by processes spawned by the task forward their events,
markers and phases to this one, and Perfherder data is reported here on their
behalf.
"""

import atexit
import functools
import json
import os
import sys
import tempfile

from __main__ import print_line

IS_MACOSX = sys.platform == "darwin"
IS_WINDOWS = os.name == "nt"


def add_psutil_to_path():
    # Where generic-worker tasks get psutil from, for the platform of this
    # python, which may differ from the machine's under emulation.
    if IS_WINDOWS:
        import sysconfig

        deps_platform = sysconfig.get_platform()
    else:
        import platform

        os_name = "macosx" if IS_MACOSX else "linux"
        deps_platform = f"{os_name}-{platform.machine()}"
    deps_dir = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "resource-monitor-deps",
        deps_platform,
    )
    if os.path.isdir(deps_dir):
        sys.path.insert(1, deps_dir)
        print_line(
            b"setup",
            b"using psutil for %s from %s\n"
            % (deps_platform.encode("utf-8"), deps_dir.encode("utf-8")),
        )
    else:
        print_line(
            b"setup",
            b"no psutil provided for %s, using the system's\n"
            % deps_platform.encode("utf-8"),
        )


def write_empty_profile(profile_path):
    # The profile is an artifact of the task, which generic-worker fails if
    # it is missing.
    with open(profile_path, "w"):
        pass


def start_resource_monitor():
    profile_path = os.environ.pop("RUN_TASK_RESOURCE_USAGE", None)
    if not profile_path:
        return None
    profile_path = os.path.abspath(profile_path)

    monitor = start_monitoring(profile_path)
    if not monitor:
        write_empty_profile(profile_path)
    return monitor


def start_monitoring(profile_path):
    add_psutil_to_path()

    try:
        from mozsystemmonitor.basemonitor import (
            FORWARD_ENV,
            BaseResourceMonitor,
            have_psutil,
        )
    except Exception as e:
        print_line(
            b"setup",
            b"not monitoring resource usage: %s\n" % str(e).encode("utf-8"),
        )
        return None

    if not have_psutil:
        print_line(b"setup", b"not monitoring resource usage: no psutil\n")
        return None

    metadata = {"product": os.environ.pop("RUN_TASK_RESOURCE_USAGE_NAME", "run-task")}
    if "TASKCLUSTER_WORKER_TYPE" in os.environ:
        metadata["device"] = os.environ["TASKCLUSTER_WORKER_TYPE"]
    if "TASK_ID" in os.environ:
        metadata["appBuildID"] = os.environ["TASK_ID"]
    if "GECKO_HEAD_REPOSITORY" in os.environ and "GECKO_HEAD_REV" in os.environ:
        repo = os.environ["GECKO_HEAD_REPOSITORY"]
        if repo.endswith(".git"):
            repo = repo[: -len(".git")]
        kind = "commit" if os.environ.get("GECKO_REPOSITORY_TYPE") == "git" else "rev"
        metadata["sourceURL"] = f"{repo}/{kind}/{os.environ['GECKO_HEAD_REV']}"

    monitor = BaseResourceMonitor(poll_interval=0.1, metadata=metadata)
    monitor.start()
    if monitor.start_time is None:
        print_line(b"setup", b"not monitoring resource usage: psutil failed\n")
        return None

    fd, forward_path = tempfile.mkstemp(prefix="resource-monitor-", suffix=".jsonl")
    os.close(fd)
    monitor.accept_forwarded(forward_path)
    os.environ[FORWARD_ENV] = forward_path

    monitor.start_streaming(profile_path)
    atexit.register(stop_resource_monitor, monitor, profile_path, forward_path)
    print_line(
        b"setup",
        b"monitoring resource usage to %s\n" % profile_path.encode("utf-8"),
    )
    return monitor


def stop_resource_monitor(monitor, profile_path, forward_path):
    try:
        monitor.stop()
        os.unlink(forward_path)

        for request in monitor.perfherder_requests:
            name, extra_options, start, end, upload_dir, source = request
            data = monitor.perfherder_data(name, extra_options, start, end, source)
            print_line(
                b"resources",
                b"PERFHERDER_DATA: %s\n" % json.dumps(data).encode("utf-8"),
            )
            if upload_dir:
                os.makedirs(upload_dir, exist_ok=True)
                path = os.path.join(upload_dir, "perfherder-data-resource-usage.json")
                with open(path, "w", encoding="utf-8") as fh:
                    json.dump(data, fh)

        # Replace the streamed profile only once the full one is written.
        tmp_path = profile_path + ".tmp"
        with open(tmp_path, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(monitor.as_profile(), fh, separators=(",", ":"))
        os.replace(tmp_path, profile_path)
    except Exception as e:
        print_line(
            b"resources",
            b"failed to record resource usage: %s\n" % str(e).encode("utf-8"),
        )


def record_phase(monitor, func_name, phase_name):
    """Wrap run-task's `func_name` so each call is recorded as a phase.

    `phase_name` is called with the function's arguments and returns the name
    of the phase, or None to not record the call.
    """
    run_task = sys.modules["__main__"]
    func = getattr(run_task, func_name, None)
    if func is None:
        print_line(
            b"setup",
            b"not recording phases for %s: not found\n" % func_name.encode("utf-8"),
        )
        return

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        name = phase_name(*args, **kwargs)
        if name is None:
            return func(*args, **kwargs)
        monitor.begin_phase(name)
        try:
            return func(*args, **kwargs)
        finally:
            monitor.finish_phase(name)

    setattr(run_task, func_name, wrapper)


def main():
    monitor = start_resource_monitor()
    if not monitor:
        return

    # This runs before run-task's main(). None of these functions are called
    # here: they are replaced in run-task's module by wrappers that record
    # each call as a phase when run-task's main() later calls them.
    record_phase(
        monitor,
        "vcs_checkout_from_args",
        lambda options, *args, **kwargs: (
            f"vcs-checkout ({options['project']})" if options["checkout"] else None
        ),
    )
    record_phase(monitor, "fetch_artifacts", lambda *args, **kwargs: "fetch-artifacts")
    record_phase(
        monitor, "install_pip_requirements", lambda *args, **kwargs: "pip-requirements"
    )
    record_phase(
        monitor,
        "run_command",
        lambda prefix, *args, **kwargs: "task" if prefix == b"task" else None,
    )


if __name__ == "<run_path>":
    main()
