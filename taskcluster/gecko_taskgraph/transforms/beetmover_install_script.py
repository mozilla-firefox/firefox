# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover-install-script task into an actual task description.
"""

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by

from gecko_taskgraph.transforms.beetmover import craft_release_properties
from gecko_taskgraph.util.scriptworker import (
    generate_beetmover_artifact_map,
    generate_beetmover_upstream_artifacts,
)


class BeetmoverInstallScriptSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    label: str
    scopes: optionally_keyed_by("project", list[str], use_msgspec=True)  # type: ignore


transforms = TransformSequence()


@transforms.add
def remove_name(config, tasks):
    for task in tasks:
        if "name" in task:
            del task["name"]
        yield task


transforms.add_validate(BeetmoverInstallScriptSchema)


@transforms.add
def resolve_scopes(config, tasks):
    for task in tasks:
        # `run-on-projects` only gates automatic scheduling; an explicit try
        # selection (task-config/fuzzy) bypasses it entirely, so the
        # "default" branch here is what staging publishes through.
        resolve_keyed_by(
            task,
            "scopes",
            "beetmover-install-script",
            project=config.params.get("project"),
        )
        yield task


@transforms.add
def make_task_worker(config, tasks):
    for task in tasks:
        platform = task["attributes"]["build_platform"]
        task["worker"] = {
            "release-properties": craft_release_properties(config, task),
            "upstream-artifacts": generate_beetmover_upstream_artifacts(
                config, task, platform
            ),
            "artifact-map": generate_beetmover_artifact_map(
                config, task, platform=platform
            ),
        }
        yield task
