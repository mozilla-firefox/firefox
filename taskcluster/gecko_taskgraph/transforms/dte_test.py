# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform calls to the DTE test suite in GHA.
"""

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.copy import deepcopy
from taskgraph.util.taskcluster import get_artifact_path

transforms = TransformSequence()

# Seconds reserved out of max-run-time for the checkout, the artifact download
# and task teardown, so a stuck GHA run reports a timeout of its own rather than
# being killed by the worker.
POLL_TIMEOUT_PADDING = 600


@transforms.add
def update_env(config, tasks):
    for task in tasks:
        name = task["name"]
        if "win" in name:
            input_key = "win_installer_link"
            artifact = "target.zip"
        elif "linux" in name:
            input_key = "linux_tarball_link"
            artifact = "target.tar.xz"
        elif "macos" in name:
            input_key = "mac_installer_link"
            artifact = "target.dmg"

        task["worker"]["env"] |= {
            "INSTALLER_LINK": {
                "artifact-reference": f"<build/{get_artifact_path(task, artifact)}>"
            },
            "INPUT_KEY": input_key,
            "POLL_TIMEOUT": str(task["worker"]["max-run-time"] - POLL_TIMEOUT_PADDING),
        }
        yield task


@transforms.add
def add_smart_window_tasks(config, tasks):
    for task in tasks:
        # Copy before yielding, since later transforms change the task in place.
        smart_window = deepcopy(task)
        smart_window["name"] = f"smart-window-{task['name']}"
        smart_window["description"] = (
            "Start Desktop Test Eng Smart Window tests via GHA dispatch API call"
        )
        smart_window["treeherder"]["tier"] = 3
        smart_window["treeherder"]["symbol"] = "dte-sw"
        smart_window["worker"]["env"]["WORKFLOW_ID"] = "smart-window.yml"

        yield task
        yield smart_window
