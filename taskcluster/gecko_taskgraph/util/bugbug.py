# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import functools
import hashlib
import os
import pathlib
import sys
import threading
import time
from concurrent.futures import Future

import requests
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
RETRY_INTERVAL = 10  # seconds

# Preset confidence thresholds.
CT_LOW = 0.7
CT_MEDIUM = 0.8
CT_HIGH = 0.9

# The confidence levels of the preset thresholds, for which bugbug can return the thresholds to
# use with its current models.
CT_LEVELS = {CT_LOW: "low", CT_MEDIUM: "medium", CT_HIGH: "high"}

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


def _fetch_push_schedules(branch, rev, session, *, write_perfherder_data=True):
    # Noop if we're in test-action-callback
    if create.testing:
        return

    url = BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"
    start = monotonic()

    # On try there is no fallback and pulling is slower, so we allow bugbug more
    # time to compute the results.
    # See https://github.com/mozilla/bugbug/issues/1673.
    timeout = RETRY_TIMEOUT
    if branch == "try":
        timeout += int(timeout / 3)

    attempts = timeout / RETRY_INTERVAL
    i = 0
    while i < attempts:
        r = session.get(url)
        r.raise_for_status()

        if r.status_code != 202:
            break

        time.sleep(RETRY_INTERVAL)
        i += 1
    end = monotonic()

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
    return _fetch_push_schedules(branch, rev, get_session())


def start_push_schedules(branch, rev):
    """Start querying bugbug for the push schedules in a daemon thread.

    Returns a `Future` that callers can either check or ignore; since the
    thread is a daemon, it won't keep the process alive if bugbug is slow.

    The future isn't cached, so that the processes taskgraph forks to load kinds
    don't wait for a thread that isn't copied into them. The thread uses its own
    session, so that those processes don't inherit and reuse its open
    connections, and so that it doesn't share a session with the main thread.
    """
    future = Future()

    def run():
        try:
            future.set_result(
                _fetch_push_schedules(
                    branch, rev, _new_session(), write_perfherder_data=False
                )
            )
        except Exception as e:
            future.set_exception(e)

    threading.Thread(target=run, daemon=True).start()
    return future


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
