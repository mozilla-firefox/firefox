# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
This transform construct tasks to perform diffs between builds, as
defined in kind.yml
"""

from typing import Optional, Union

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema
from taskgraph.util.taskcluster import get_artifact_path

from gecko_taskgraph.transforms.task import TaskDescriptionSchema


class IndexSearchSchema(Schema):
    index_search: str


class DiffDescriptionSchema(Schema, kw_only=True):
    # Name of the diff task.
    name: str
    # Treeherder tier.
    tier: int
    # Treeherder symbol.
    symbol: str
    # relative path (from config.path) to the file the task was defined in.
    task_from: Optional[str] = None
    # Original and new builds to compare.
    original: Union[str, IndexSearchSchema]
    new: Union[str, IndexSearchSchema]
    # Arguments to pass to diffoscope, used for task-defaults in
    # taskcluster/kinds/diffoscope/kind.yml
    args: Optional[str] = None
    # Extra arguments to pass to diffoscope, that can be set per job.
    extra_args: Optional[str] = None
    # Fail the task when differences are detected.
    fail_on_diff: Optional[bool] = None
    # What artifact to check the differences of. Defaults to target.tar.xz
    # for Linux, target.dmg for Mac, target.zip for Windows, target.apk for
    # Android.
    artifact: Optional[str] = None
    # Whether to unpack first. Diffoscope can normally work without unpacking,
    # but when one needs to --exclude some contents, that doesn't work out well
    # if said content is packed (e.g. in omni.ja).
    unpack: Optional[bool] = None
    # Commands to run before performing the diff.
    pre_diff_commands: Optional[list[str]] = None
    # Only run the task on a set of projects/branches.
    run_on_projects: TaskDescriptionSchema.__annotations__["run_on_projects"] = None
    run_on_repo_type: TaskDescriptionSchema.__annotations__["run_on_repo_type"] = None
    optimization: TaskDescriptionSchema.__annotations__["optimization"] = None


transforms = TransformSequence()
transforms.add_validate(DiffDescriptionSchema)


@transforms.add
def fill_template(config, tasks):
    dummy_tasks = {}

    for task in tasks:
        name = task["name"]

        deps = {}
        urls = {}
        previous_artifact = None
        artifact = task.get("artifact")
        for k in ("original", "new"):
            value = task[k]
            if isinstance(value, str):
                deps[k] = value
                dep_name = k
                os_hint = value
            else:
                index = value["index-search"]
                if index not in dummy_tasks:
                    dummy_tasks[index] = {
                        "label": "index-search-" + index,
                        "description": index,
                        "worker-type": "invalid/always-optimized",
                        "run": {
                            "using": "always-optimized",
                        },
                        "optimization": {
                            "index-search": [index],
                        },
                    }
                    yield dummy_tasks[index]
                deps[index] = "index-search-" + index
                dep_name = index
                os_hint = index.split(".")[-1]
            if artifact:
                pass
            elif "linux" in os_hint:
                artifact = "target.tar.xz"
            elif "macosx" in os_hint:
                artifact = "target.dmg"
            elif "android" in os_hint:
                artifact = "target.apk"
            elif "win" in os_hint:
                artifact = "target.zip"
            else:
                raise Exception(f"Cannot figure out the OS for {value!r}")
            if previous_artifact is not None and previous_artifact != artifact:
                raise Exception("Cannot compare builds from different OSes")
            urls[k] = {
                "artifact-reference": f"<{dep_name}/{get_artifact_path(task, artifact)}>",
            }
            previous_artifact = artifact

        taskdesc = {
            "label": "diff-" + name,
            "description": name,
            "treeherder": {
                "symbol": task["symbol"],
                "platform": "diff/opt",
                "kind": "other",
                "tier": task["tier"],
            },
            "worker-type": "b-linux",
            "worker": {
                "docker-image": {"in-tree": "diffoscope"},
                "artifacts": [
                    {
                        "type": "file",
                        "path": f"/builds/worker/{f}",
                        "name": f"public/{f}",
                    }
                    for f in (
                        "diff.html",
                        "diff.txt",
                    )
                ],
                "env": {
                    "ORIG_URL": urls["original"],
                    "NEW_URL": urls["new"],
                    "DIFFOSCOPE_ARGS": " ".join(
                        task[k] for k in ("args", "extra-args") if k in task
                    ),
                    "PRE_DIFF": "; ".join(task.get("pre-diff-commands", [])),
                },
                "max-run-time": 1800,
            },
            "run": {
                "using": "run-task",
                "checkout": task.get("unpack", False),
                "command": "/builds/worker/bin/get_and_diffoscope{}{}".format(
                    " --unpack" if task.get("unpack") else "",
                    " --fail" if task.get("fail-on-diff") else "",
                ),
            },
            "dependencies": deps,
            "optimization": task.get("optimization"),
            "run-on-repo-type": task.get("run-on-repo-type", ["git", "hg"]),
        }
        if "run-on-projects" in task:
            taskdesc["run-on-projects"] = task["run-on-projects"]

        if artifact.endswith(".dmg"):
            taskdesc.setdefault("fetches", {}).setdefault("toolchain", []).extend([
                "linux64-cctools-port",
                "linux64-libdmg",
            ])

        yield taskdesc
