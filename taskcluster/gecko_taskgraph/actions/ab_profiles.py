# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json
import logging
import re
from functools import partial

from taskcluster.exceptions import TaskclusterRestFailure
from taskgraph.util.taskcluster import (
    get_artifact,
    get_artifact_url,
    get_task_definition,
    list_artifacts,
    status_task,
)
from taskgraph.util.treeherder import join_symbol, split_symbol

from ..util.perftest import use_raptor_ab_script
from ..util.taskgraph import find_decision_task
from .registry import register_callback_action
from .util import create_tasks, fetch_graph_and_labels

logger = logging.getLogger(__name__)

# What the native profilers look for in MOZ_FETCHES_DIR, on every platform.
SYMBOLS_ARTIFACT_NAME = "target.crashreporter-symbols.zip"

ARTIFACT_REFERENCE = re.compile(r"^<([^/>]+)/(.+)>$")
TASK_REFERENCE = re.compile(r"^<([^/>]+)>$")
FULL_REVISION = re.compile(r"^[0-9a-f]{40}$")
SCHEDULER_ID = re.compile(r"^(.+)-level-(\d+)$")


def build_references(task):
    """Where the task gets the build under test and its symbols from.

    Both are task-references in the task's own definition, so reading them
    avoids hardcoding artifact paths that differ per platform: target.zip on
    Windows, target.tar.xz on Linux, target.dmg on macOS.

    Returns the name of the dependency that provides them, the installer's
    artifact path and the symbols' artifact path.
    """
    env = task.task["payload"]["env"]

    extra_config = json.loads(env["EXTRA_MOZHARNESS_CONFIG"]["artifact-reference"])
    match = ARTIFACT_REFERENCE.match(extra_config["installer_url"])
    if not match:
        raise Exception(
            f"Cannot tell which task provides {task.label}'s installer: "
            f"{extra_config['installer_url']!r}. This action needs the installer "
            "to come from a task in this push."
        )
    installer_upstream, installer_artifact = match.groups()

    fetches = json.loads(env["MOZ_FETCHES"]["task-reference"])
    symbols = [f for f in fetches if f["artifact"].endswith(SYMBOLS_ARTIFACT_NAME)]
    if not symbols:
        raise Exception(
            f"{task.label} does not fetch {SYMBOLS_ARTIFACT_NAME}, so its profiles "
            "would not be symbolicated."
        )
    match = TASK_REFERENCE.match(symbols[0]["task"])
    if not match:
        raise Exception(
            f"Cannot tell which task provides {task.label}'s symbols: "
            f"{symbols[0]['task']!r}."
        )
    symbols_upstream = match.group(1)

    if installer_upstream != symbols_upstream:
        raise Exception(
            f"{task.label} takes its installer from {installer_upstream} but its "
            f"symbols from {symbols_upstream}. This action assumes one task "
            "provides both, which holds on desktop but not on Android."
        )

    return installer_upstream, installer_artifact, symbols[0]["artifact"]


def resolve_base_build(input, graph_config, build_label):
    """Return the task id of the build to compare against."""
    if input["base_build_task_id"]:
        return input["base_build_task_id"]

    base_project = input["base_project"]
    base_revision = input["base_revision"]
    if not FULL_REVISION.match(base_revision):
        raise Exception(
            f"The base revision {base_revision!r} is not a full 40-character "
            "revision hash."
        )
    head_rev_param = "{}head_rev".format(graph_config["project-repo-param-prefix"])
    base_parameters = {"project": base_project, head_rev_param: base_revision}
    try:
        decision_task_id = find_decision_task(base_parameters, graph_config)
        label_to_taskid = get_artifact(decision_task_id, "public/label-to-taskid.json")
    except TaskclusterRestFailure as e:
        raise Exception(
            f"Could not find a decision task for revision {base_revision} "
            f"on {base_project}. Check the revision and the project."
        ) from e
    if build_label not in label_to_taskid:
        raise Exception(
            f"The decision task for revision {base_revision} on {base_project} "
            f"has no {build_label} task. This action needs both builds to exist "
            "already. If an action task added the base build, pass its task id "
            "as the base build task id instead."
        )
    return label_to_taskid[build_label]


def require_trust_level(task_id, parameters, graph_config):
    """Refuse a base build from a less trusted tree than this push's.

    This task runs the base build, so a try build must not run on autoland.
    The reverse, try comparing against an autoland build, is fine.
    """
    scheduler_id = get_task_definition(task_id).get("schedulerId", "")
    match = SCHEDULER_ID.match(scheduler_id)
    level = int(parameters["level"])
    if (
        not match
        or match.group(1) != graph_config["trust-domain"]
        or int(match.group(2)) < level
    ):
        raise Exception(
            f"The base build ({task_id}) was scheduled by {scheduler_id!r}, but "
            f"this push is level {level}. The base build has to come from a "
            "tree with the same or a higher trust level."
        )


def require_completed(task_id, description):
    try:
        status = status_task(task_id)
    except TaskclusterRestFailure as e:
        raise Exception(
            f"Could not find the {description} build task {task_id}."
        ) from e
    if not status:
        # `mach taskgraph test-action-callback` doesn't talk to the queue.
        return
    state = status.get("state")
    if state != "completed":
        raise Exception(
            f"The {description} build ({task_id}) is {state}, not completed. "
            "This action needs both builds to be finished before it can run."
        )


def require_artifacts(task_id, description, artifacts):
    """Check now that the build's artifacts still exist.

    The task fetches the base build by URL, so otherwise it would only notice
    when it downloads them, which can be after a whole run.
    """
    available = {artifact["name"] for artifact in list_artifacts(task_id)}
    missing = sorted(set(artifacts) - available)
    if missing:
        raise Exception(
            f"The {description} build ({task_id}) has no {', '.join(missing)}. "
            "The artifacts may have expired, in which case the build has to be "
            "pushed again."
        )


def ab_profiles_modifier(task, input):
    """Turn a profiling task into a two-run task."""
    if task.label != input["label"]:
        return task

    payload = task.task["payload"]
    payload["env"]["RAPTOR_AB_CONFIG"] = json.dumps(input["config"])
    payload["command"] = use_raptor_ab_script(payload["command"])

    # These tasks run the test twice, so they take about twice as long.
    payload["maxRunTime"] *= 2

    task.task["metadata"]["name"] += "-ab"
    treeherder = task.task["extra"]["treeherder"]
    group, symbol = split_symbol(treeherder["symbol"])
    treeherder["symbol"] = join_symbol(group, f"{symbol.removesuffix('-p')}-ab")

    # A retrigger has to copy this task's definition, because recreating the
    # task from the task graph would lose its AB command and configuration. The
    # "action" tag selects the retrigger action that copies the definition; the
    # "retrigger" tag would select the one that recreates the task.
    tags = task.task.setdefault("tags", {})
    tags.pop("retrigger", None)
    tags["action"] = "ab-profiles-task"

    description = task.task["metadata"]["description"]
    config = input["config"]
    prefix = (
        f"AB profiles, base: {config['runs']['base']['description']}, "
        f"new: {config['runs']['new']['description']}, "
        f"run order: {','.join(config['order'])}. "
    )
    if isinstance(description, dict) and "task-reference" in description:
        description["task-reference"] = prefix + description["task-reference"]
    else:
        task.task["metadata"]["description"] = prefix + description

    return task


@register_callback_action(
    title="AB profiles",
    name="ab-profiles",
    symbol="ab-prof",
    description=(
        "Run this Speedometer profiling task for two Firefox builds in one "
        "task on the same machine, so that machine differences affect the "
        "comparison less. Both builds must exist and be finished. The base "
        "build must come from a tree with the same or a higher trust level, so "
        "try can compare against autoland but not the other way around."
    ),
    order=210,
    context=[{"test-type": "raptor"}],
    schema={
        "type": "object",
        "properties": {
            "base_revision": {
                "type": "string",
                "default": "",
                "title": "Base revision",
                "description": (
                    "Full 40-character revision of the push whose build should "
                    "be compared against this one. Ignored if a base build task "
                    "id is given."
                ),
            },
            "base_project": {
                "type": "string",
                "default": "try",
                "title": "Base project",
                "description": "Project (branch) the base revision was pushed to.",
            },
            "base_build_task_id": {
                "type": "string",
                "default": "",
                "title": "Base build task id",
                "description": (
                    "Task id of the build to compare against. Overrides the base "
                    "revision, for cases where the build cannot be looked up from "
                    "a push (a build from an action task on another repository, "
                    "say)."
                ),
            },
            "order": {
                "type": "string",
                "enum": ["base,new", "new,base"],
                "default": "base,new",
                "title": "Run order",
                "description": (
                    "Which side to run first. The machine is warmer for the "
                    "second run, so triggering the action twice with both orders "
                    "and comparing the pairs separates that effect out."
                ),
            },
        },
        "additionalProperties": False,
    },
)
def ab_profiles_action(parameters, graph_config, input, task_group_id, task_id):
    # Treeherder fills the schema's defaults in for us, but a caller going
    # straight at the hook doesn't have to, so don't rely on them being there.
    input = {
        "base_revision": "",
        "base_project": "try",
        "base_build_task_id": "",
        "order": "base,new",
        **(input or {}),
    }
    if not input["base_revision"] and not input["base_build_task_id"]:
        raise Exception("Either a base revision or a base build task id is required.")

    decision_task_id, full_task_graph, label_to_taskid, _ = fetch_graph_and_labels(
        parameters, graph_config
    )

    label = get_task_definition(task_id)["metadata"]["name"]
    task = full_task_graph.tasks.get(label)
    if not task or not task.attributes.get("supports_ab_profiles"):
        raise Exception(
            f"{label} does not support AB profiles. Select a Speedometer native "
            "profiling task instead."
        )
    upstream, installer_artifact, symbols_artifact = build_references(task)
    build_label = task.dependencies[upstream]

    if build_label not in label_to_taskid:
        raise Exception(
            f"This push has no {build_label} task. The Firefox build must "
            "exist before this action runs."
        )
    require_completed(label_to_taskid[build_label], "new")

    base_build_task_id = resolve_base_build(input, graph_config, build_label)
    require_completed(base_build_task_id, "base")
    require_trust_level(base_build_task_id, parameters, graph_config)
    require_artifacts(
        base_build_task_id, "base", [installer_artifact, symbols_artifact]
    )
    if input["base_build_task_id"]:
        base_description = f"task {base_build_task_id}"
    else:
        base_description = f"{input['base_revision'][:12]} ({input['base_project']})"
    base_run = {
        "browser": "firefox",
        "description": base_description,
        "installer_url": get_artifact_url(base_build_task_id, installer_artifact),
        "symbols_url": get_artifact_url(base_build_task_id, symbols_artifact),
    }
    new_run = {
        "browser": "firefox",
        "description": f"{parameters['head_rev'][:12]} ({parameters['project']})",
    }

    input_for_action = dict(
        label=label,
        config={
            "order": input["order"].split(","),
            "runs": {"base": base_run, "new": new_run},
        },
    )
    logger.info(f"Creating {label} with {input_for_action}")

    create_tasks(
        graph_config,
        [label],
        full_task_graph,
        label_to_taskid,
        parameters,
        decision_task_id,
        modifier=partial(ab_profiles_modifier, input=input_for_action),
    )
