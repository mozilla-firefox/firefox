# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.dependencies import get_primary_dependency

from gecko_taskgraph.util.cached_tasks import add_optimization

transforms = TransformSequence()


@transforms.add
def copy_cached_dep(config, tasks):
    """Ensure this task is replaced anytime the primary dep is."""
    # This transform is a bit of a hack to work around the fact that
    # the `if-dependencies` feature doesn't work with tasks that get
    # optimized by replacement.
    for task in tasks:
        primary_dep = get_primary_dependency(config, task)

        if primary_dep and "cached_task" in primary_dep.attributes:
            add_optimization(
                config,
                task,
                f"{config.kind}.v1",
                task["name"],
                digest=primary_dep.attributes["cached_task"]["digest"],
            )

        yield task
