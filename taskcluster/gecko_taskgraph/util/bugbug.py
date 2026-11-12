# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import functools
import hashlib
import os
import pathlib
import subprocess
import sys
import time

import requests
from mozsystemmonitor.resourcemonitor import FORWARD_ENV, SystemResourceMonitor
from taskgraph import create
from taskgraph.util import json
from taskgraph.util.taskcluster import requests_retry_session

try:
    # TODO(py3): use time.monotonic()
    from time import monotonic
except ImportError:
    from time import time as monotonic

BUGBUG_BASE_URL = "https://bugbug.moz.tools"
RETRY_TIMEOUT = 9 * 60  # seconds
RETRY_INTERVAL = 2  # seconds

# Names the file a query started by start_push_schedules writes its result to,
# for push_schedules to wait for rather than querying bugbug itself. Being in
# the environment, it reaches the processes kinds are loaded in, however they
# are started.
PUSH_SCHEDULES_ENV = "GECKO_BUGBUG_PUSH_SCHEDULES"
RESULT_POLL_INTERVAL = 0.2  # seconds

# Preset confidence thresholds.
CT_VERY_LOW = 0.3
CT_LOW = 0.7
CT_MEDIUM = 0.8
CT_HIGH = 0.9

# The confidence levels of the preset thresholds, for which bugbug can return the thresholds to
# use with its current models.
CT_LEVELS = {
    CT_VERY_LOW: "very_low",
    CT_LOW: "low",
    CT_MEDIUM: "medium",
    CT_HIGH: "high",
}

GROUP_TRANSLATIONS = {
    "testing/web-platform/tests": "",
    "testing/web-platform/mozilla/tests": "/_mozilla",
}


def translate_group(group):
    for prefix, value in GROUP_TRANSLATIONS.items():
        if group.startswith(prefix):
            return group.replace(prefix, value)

    return group


def get_confidence_threshold(data, kind, confidence_threshold):
    """Return the threshold to use for the "tasks" or "groups" (`kind`) of a bugbug reply.

    The scale of the confidences depends on bugbug's models, so bugbug returns the thresholds
    matching the preset ones (computed when training its models) in "confidence_thresholds".
    Other thresholds, or preset ones when bugbug didn't return any, are used as they are.
    """
    level = CT_LEVELS.get(confidence_threshold)
    if level is None:
        return confidence_threshold

    return (
        data
        .get("confidence_thresholds", {})
        .get(kind, {})
        .get(level, confidence_threshold)
    )


class BugbugTimeoutException(Exception):
    pass


def _new_session():
    s = requests.Session()
    s.headers.update({"X-API-KEY": "gecko-taskgraph"})
    return requests_retry_session(retries=5, session=s)


@functools.cache
def get_session():
    return _new_session()


# Taskgraph forks a process per kind, and connections pooled in the parent's
# session would be shared with, and between, those processes.
if hasattr(os, "register_at_fork"):
    os.register_at_fork(after_in_child=get_session.cache_clear)


def _perfherder_artifact_path(base_path, perfherder_data):
    base_dir = base_path.parent
    stem = base_path.stem
    sequence = int(time.monotonic() * 1000)
    payload = json.dumps(perfherder_data, sort_keys=True).encode("utf-8")
    digest = hashlib.sha1(payload).hexdigest()[:8]

    return base_dir / f"{stem}-{sequence}-{digest}.json"


def _write_perfherder_data(lower_is_better):
    if os.environ.get("MOZ_AUTOMATION", "0") == "1":
        perfherder_data = {
            "framework": {"name": "build_metrics"},
            "suites": [
                {
                    "name": suite,
                    "value": value,
                    "lowerIsBetter": True,
                    "shouldAlert": False,
                    "subtests": [],
                }
                for suite, value in lower_is_better.items()
            ],
        }
        print(f"PERFHERDER_DATA: {json.dumps(perfherder_data)}", file=sys.stderr)
        perfherder_path = os.environ.get("MOZ_PERFHERDER_UPLOAD")
        decision_upload_dir = os.environ.get("MOZ_UPLOAD_DIR")
        if perfherder_path:
            upload_path = pathlib.Path(perfherder_path)
        elif decision_upload_dir:
            upload_path = (
                pathlib.Path(decision_upload_dir) / "perfherder-data-bugbug.json"
            )
        else:
            return

        upload_path.parent.mkdir(parents=True, exist_ok=True)
        target = _perfherder_artifact_path(upload_path, perfherder_data)
        with target.open("w", encoding="utf-8") as f:
            json.dump(perfherder_data, f)


def _get_when_ready(url, session, deadline):
    """Query ``url`` until bugbug stops answering 202 (result still being
    computed) or ``deadline`` is reached. Returns the last response and the
    number of retries."""
    i = 0
    while True:
        r = session.get(url)
        r.raise_for_status()

        if r.status_code != 202 or monotonic() + RETRY_INTERVAL > deadline:
            break

        time.sleep(RETRY_INTERVAL)
        i += 1
    return r, i


def _push_schedules_url(branch, rev):
    return BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"


def _push_schedules_timeout(branch):
    # On try there is no fallback and pulling is slower, so we allow bugbug more
    # time to compute the results.
    # See https://github.com/mozilla/bugbug/issues/1673.
    timeout = RETRY_TIMEOUT
    if branch == "try":
        timeout += int(timeout / 3)
    return timeout


def _write_push_schedules_result(path, result):
    tmp_path = f"{path}.tmp"
    with open(tmp_path, "w") as fh:
        json.dump(result, fh)
    os.replace(tmp_path, path)


def _read_push_schedules_result(path):
    try:
        with open(path) as fh:
            return json.load(fh)
    except FileNotFoundError:
        return None


def read_push_schedules_data(path, branch, rev):
    """Return the data a query started by start_push_schedules wrote to `path`,
    or None if it isn't there (yet), failed, or is for another push."""
    result = _read_push_schedules_result(path)
    if result is None or (result["branch"], result["rev"]) != (branch, rev):
        return None
    return result.get("data")


def _wait_for_push_schedules_result(path, branch, rev):
    """Wait for the result a query started by start_push_schedules writes to
    `path`, and return it, or None if it doesn't come or is for another push."""
    start = monotonic()
    # The query gives up after the same timeout, counted from when it started,
    # which is before we did; leave time for its last request.
    deadline = start + _push_schedules_timeout(branch) + 60
    result = None
    try:
        while result is None and monotonic() < deadline:
            result = _read_push_schedules_result(path)
            if result is None:
                time.sleep(RESULT_POLL_INTERVAL)
    finally:
        SystemResourceMonitor.record_marker(
            "bugbug wait",
            start,
            monotonic(),
            {"type": "Text", "text": _push_schedules_url(branch, rev)},
        )
    if result is None or (result["branch"], result["rev"]) != (branch, rev):
        return None
    return result


def _fetch_push_schedules(branch, rev, session, *, write_perfherder_data=True):
    # Noop if we're in test-action-callback
    if create.testing:
        return

    url = _push_schedules_url(branch, rev)
    start = monotonic()

    deadline = start + _push_schedules_timeout(branch)
    try:
        r, i = _get_when_ready(url, session, deadline)
    finally:
        end = monotonic()
        SystemResourceMonitor.forward_marker(
            "bugbug", start, end, {"type": "Text", "text": url}
        )

    if write_perfherder_data:
        _write_perfherder_data(
            lower_is_better={
                "bugbug_push_schedules_time": end - start,
                "bugbug_push_schedules_retries": i,
            }
        )

    data = r.json()
    if r.status_code == 202:
        raise BugbugTimeoutException(f"Timed out waiting for result from '{url}'")

    if "groups" in data:
        data["groups"] = {translate_group(k): v for k, v in data["groups"].items()}

    if "config_groups" in data:
        data["config_groups"] = {
            translate_group(k): v for k, v in data["config_groups"].items()
        }

    return data


@functools.cache
def push_schedules(branch, rev):
    if path := os.environ.get(PUSH_SCHEDULES_ENV):
        result = _wait_for_push_schedules_result(path, branch, rev)
        if result is not None:
            if "data" in result:
                return result["data"]
            if "timeout" in result:
                raise BugbugTimeoutException(result["timeout"])
    return _fetch_push_schedules(branch, rev, get_session())


@functools.cache
def push_perf_risk(branch, rev):
    """Return bugbug's performance regression risk prediction for the push
    of ``rev``, which scores each of its commits."""
    url = BUGBUG_BASE_URL + f"/perfregressionpredictor/predict/push/{branch}/{rev}"
    deadline = monotonic() + RETRY_TIMEOUT
    r, _ = _get_when_ready(url, get_session(), deadline)
    if r.status_code == 202:
        raise BugbugTimeoutException(f"Timed out waiting for result from '{url}'")
    return r.json()


def start_push_schedules(branch, rev, result_path):
    """Start querying bugbug for the push schedules in a child process, which
    writes the result to `result_path`, for push_schedules to read when
    PUSH_SCHEDULES_ENV names it. Returns the `subprocess.Popen` object.

    This doesn't use a thread because taskgraph forks processes to load kinds,
    and a fork while the thread holds a lock (e.g. the one of sys.stderr, when
    logging) leaves the lock held forever in the forked process.
    """
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(sys.path))
    monitor = SystemResourceMonitor.instance
    if monitor and monitor.children_forward_path:
        env[FORWARD_ENV] = monitor.children_forward_path
    return subprocess.Popen(
        [sys.executable, "-m", __name__, branch, rev, result_path],
        env=env,
        stdin=subprocess.DEVNULL,
    )


def _run_push_schedules_query(branch, rev, result_path):
    result = {"branch": branch, "rev": rev}
    try:
        result["data"] = _fetch_push_schedules(branch, rev, _new_session())
    except BugbugTimeoutException as e:
        result["timeout"] = str(e)
    except Exception as e:
        result["error"] = repr(e)
    try:
        _write_push_schedules_result(result_path, result)
    except OSError:
        # The decision task may have finished and cleaned up already.
        pass


@functools.cache
def patch_schedules(base_rev, patch_content, mode="quick"):
    """Query BugBug API with a patch to get test recommendations.

    This is used by `./mach test --auto` to get test recommendations for local changes.

    Args:
        base_rev (str): The base revision hash.
        patch_content (str): The patch content with commit metadata.
        mode (str): The mode of test selection, which determines the confidence
            threshold. One of 'extensive', 'moderate', or 'quick'.
    Returns:
        dict: A dictionary with containing test recommendations filtered by
            confidence threshold.

    Raises:
        BugbugTimeoutException: If the API times out.
    """

    import hashlib
    import re

    # This ensures consistent hashing across multiple runs with identical
    # changes by stripping the date before hashing.
    filtered_content = re.sub(r"^Date: .*$", "", patch_content, flags=re.MULTILINE)
    patch_hash = hashlib.md5(filtered_content.encode("utf-8")).hexdigest()

    url = BUGBUG_BASE_URL + f"/patch/{base_rev}/{patch_hash}/schedules"

    session = get_session()

    r = session.post(
        url,
        data=patch_content.encode("utf-8"),
        headers={"Content-Type": "text/plain"},
    )
    r.raise_for_status()

    timeout = RETRY_TIMEOUT
    attempts = timeout / RETRY_INTERVAL
    i = 0
    while i < attempts:
        if r.status_code != 202:
            break

        time.sleep(RETRY_INTERVAL)
        r = session.get(url)
        r.raise_for_status()
        i += 1

    data = r.json()
    if r.status_code == 202:
        raise BugbugTimeoutException(f"Timed out waiting for result from '{url}'")

    if mode == "extensive":
        confidence_threshold = CT_LOW
    elif mode == "moderate":
        confidence_threshold = CT_MEDIUM
    elif mode == "quick":
        confidence_threshold = CT_HIGH
    else:
        raise ValueError(
            f"Invalid mode: '{mode}'; expected one of 'extensive', 'moderate', 'quick'"
        )

    return {k: v for k, v in data["groups"].items() if v >= confidence_threshold}


if __name__ == "__main__":
    _run_push_schedules_query(*sys.argv[1:])
