# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
"""


import logging

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.dependencies import get_primary_dependency
from taskgraph.util.treeherder import inherit_treeherder_from_dep

logger = logging.getLogger(__name__)

transforms = TransformSequence()


@transforms.add
def fill_template(config, tasks):
    for task in tasks:
        assert "snap-upstream-test-" in task.get("label")

        test_type = task.get("attributes")["snap_test_type"]
        test_release = task.get("attributes")["snap_test_release"]
        task["label"] = task.get("label").replace(
            "-test-", "-test-" + test_type + "-" + test_release + "-"
        )

        dep = get_primary_dependency(config, task)
        assert dep

        inherit_treeherder_from_dep(task, dep)

        th_group = dep.task["extra"]["treeherder"]["groupSymbol"].replace("B", "Sel")
        th_symbol = (
            f"{test_type}-{test_release}-{dep.task['extra']['treeherder']['symbol']}"
        )
        task["treeherder"]["symbol"] = f"{th_group}({th_symbol})"

        timeout = 10
        if dep.attributes.get("build_type") != "opt":
            timeout = 60
            task["worker"]["env"]["BUILD_IS_DEBUG"] = "1"

        task["worker"]["env"]["TEST_TIMEOUT"] = f"{timeout}"

        yield task
