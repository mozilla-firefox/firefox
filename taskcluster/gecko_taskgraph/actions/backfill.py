# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import logging
import re
import sys
from functools import partial

from taskgraph.util import json
from taskgraph.util.taskcluster import get_task_definition

from .registry import register_callback_action
from .util import (
    combine_task_graph_files,
    create_tasks,
    fetch_graph_and_labels,
    get_decision_task_id,
    get_pushes,
    get_pushes_from_params_input,
    get_pushes_in_gap,
    trigger_action,
)

logger = logging.getLogger(__name__)
SYMBOL_REGEX = re.compile("^(.*)-[a-z0-9]{11}-bk$")
GROUP_SYMBOL_REGEX = re.compile("^(.*)-bk$")
MIN_SLICE_GAP = 7
_PRIORITIES = ["lowest", "very-low", "low", "medium", "high", "very-high", "highest"]
_PERF_SUITES = ("raptor", "talos")

# Allowed browser applications for performance test backfills
# Only Firefox and Geckoview should be backfilled for regression detection
ALLOWED_PERFTEST_BACKFILL_APPS = (
    "firefox",
    "geckoview",
    "fenix",
)


def _is_perf_task(task):
    return (
        task.kind == "perftest" or task.attributes.get("unittest_suite") in _PERF_SUITES
    )


def _lower_priority(current, max_priority):
    try:
        if current is None or _PRIORITIES.index(max_priority) < _PRIORITIES.index(
            current
        ):
            return max_priority
    except ValueError:
        pass
    return current


def input_for_support_action(revision, task, times=1, retrigger=True):
    """Generate input for action to be scheduled.

    Define what label to schedule with 'label'.
    If it is a test task that uses explicit manifests add that information.
    """
    input = {
        "label": task["metadata"]["name"],
        "revision": revision,
        "times": times,
        # We want the backfilled tasks to share the same symbol as the originating task
        "symbol": task["extra"]["treeherder"]["symbol"],
        "retrigger": retrigger,
    }

    # Support tasks that are using manifest based scheduling
    if task["payload"].get("env", {}).get("MOZHARNESS_TEST_PATHS"):
        input["test_manifests"] = json.loads(
            task["payload"]["env"]["MOZHARNESS_TEST_PATHS"]
        )

    return input


@register_callback_action(
    title="Backfill",
    name="backfill",
    permission="backfill",
    symbol="Bk",
    description=("Given a task schedule it on previous pushes in the same project."),
    order=200,
    context=[{}],  # This will be available for all tasks
    schema={
        "type": "object",
        "properties": {
            "depth": {
                "type": "integer",
                "default": 19,
                "minimum": 1,
                "maximum": 25,
                "title": "Depth",
                "description": (
                    "The number of previous pushes before the current "
                    "push to attempt to trigger this task on."
                ),
            },
            "inclusive": {
                "type": "boolean",
                "default": False,
                "title": "Inclusive Range",
                "description": (
                    "If true, the backfill will also retrigger the task "
                    "on the selected push."
                ),
            },
            "times": {
                "type": "integer",
                "default": 1,
                "minimum": 1,
                "maximum": 10,
                "title": "Times",
                "description": (
                    "The number of times to execute each job you are backfilling."
                ),
            },
            "retrigger": {
                "type": "boolean",
                "default": True,
                "title": "Retrigger",
                "description": (
                    "If False, the task won't retrigger on pushes that have already "
                    "ran it."
                ),
            },
            "slices": {
                "type": "integer",
                "default": 0,
                "minimum": 0,
                "maximum": 25,
                "title": "Slices",
                "description": (
                    "Number of cuts to make in the push range. "
                    "0 (default) triggers all pushes (standard backfill). "
                    "1 triggers one push in the middle of the range. "
                    "3 cuts the range into 4 chunks and triggers the 3 boundary pushes."
                ),
            },
        },
        "additionalProperties": False,
    },
    available=lambda parameters: True,
)
def backfill_action(parameters, graph_config, input, task_group_id, task_id):
    """
    This action takes a task ID and schedules it on previous pushes (via support action).
    When 'slices' is 0 (default), standard backfill is used (all pushes).
    When 'slices' > 0, the gap of missing pushes is detected and the mode is chosen automatically.

      - small gaps: standard backfill
      - large gaps: sliced backfill (exact pivot pushes at n/N+1, 2n/N+1, ... for slices=N).

    To execute this action locally follow the documentation here:
    https://taskcluster-taskgraph.readthedocs.io/en/latest/howto/create-actions.html#testing-actions
    """
    task = get_task_definition(task_id)

    # Only backfill allowed browser applications for performance tests
    task_label = task.get("metadata", {}).get("name", "")
    is_browsertime = "browsertime" in task_label

    if is_browsertime and not any(
        app in task_label for app in ALLOWED_PERFTEST_BACKFILL_APPS
    ):
        logger.warning(
            f"Skipping backfill for non-allowed raptor-browsertime task: {task_label}"
        )
        return

    input_for_action = input_for_support_action(
        revision=parameters["head_rev"],
        task=task,
        times=input.get("times", 1),
        retrigger=input.get("retrigger", True),
    )
    slices = input.get("slices", 0)
    if slices == 0:
        strategy = "standard"
        pushes = get_pushes_from_params_input(parameters, input)
    else:
        pushes = get_pushes_in_gap(
            parameters, input_for_action.get("label", ""), graph_config
        )
        if len(pushes) < MIN_SLICE_GAP:
            strategy = "standard"
        else:
            strategy = "sliced"

    planned_pushes = plan_pushes_to_trigger(pushes, strategy, slices)
    failed = False
    logger.info(
        "BACKFILL_STARTED: %s",
        json.dumps({
            "label": input_for_action.get("label", ""),
            "strategy": strategy,
            "slices": slices,
            "scanned_push_count": len(pushes),
            "target_pushes": planned_pushes,
        }),
    )
    for i, push_id in enumerate(planned_pushes, 1):
        try:
            # The Gecko decision task can sometimes fail on a push and we need to handle
            # the exception that this call will produce
            push_decision_task_id = get_decision_task_id(parameters["project"], push_id)
        except Exception:
            logger.warning(f"Could not find decision task for push {push_id}")
            # The decision task may have failed, this is common enough that we
            # don't want to report an error for it.
            continue

        logger.info(
            "BACKFILL_DATA: %s",
            json.dumps({
                "push_id": push_id,
                "decision_task_id": push_decision_task_id,
                "label": input_for_action.get("label", ""),
                "strategy": strategy,
                "slices": slices,
                "push_count": i,
                "total_pushes": len(planned_pushes),
            }),
        )
        try:
            trigger_action(
                action_name="backfill-task",
                # This lets the action know on which push we want to add a new task
                decision_task_id=push_decision_task_id,
                input=input_for_action,
            )
        except Exception:
            logger.exception(f"Failed to trigger action for {push_id}")
            failed = True

    if failed:
        sys.exit(1)


def add_backfill_suffix(regex, symbol, suffix):
    m = regex.match(symbol)
    if m is None:
        symbol += suffix
    return symbol


def is_speedometer_task(label):
    return "shippable" in label and "speedometer3" in label


def _normalize_test_paths(test_paths):
    return {suite: sorted(paths) for suite, paths in (test_paths or {}).items()}


def manifests_scope(task, test_manifests):
    """Return "full" if `test_manifests` matches the manifests this task would
    run on its own push, otherwise "partial".

    Backfilled tasks run the manifests of the originating push, which can
    differ from the chunk scheduled on the target push when chunking shifts.
    """
    own = task.task["payload"].get("env", {}).get("MOZHARNESS_TEST_PATHS")
    if not own:
        return "partial"
    if _normalize_test_paths(json.loads(own)) == _normalize_test_paths(test_manifests):
        return "full"
    return "partial"


def backfill_modifier(task, input, lower_priority=False):
    if lower_priority:
        task.task["priority"] = _lower_priority(task.task.get("priority"), "very-low")

    if task.label != input["label"]:
        return task

    logger.debug(f"Modifying test_manifests for {task.label}")
    times = input.get("times", 1)

    if is_speedometer_task(task.label):
        # Reduce duplicates to 1 from 12. Allow `times` to continue
        # to be used by doing this before the check below.
        task.attributes["task_duplicates"] = 1

    # Set task duplicates based on 'times' value.
    if times > 1:
        task.attributes["task_duplicates"] = times

    # If the original task has defined test paths
    test_manifests = input.get("test_manifests")
    if test_manifests:
        revision = input.get("revision")

        # Compute before MOZHARNESS_TEST_PATHS is overwritten below.
        task.task["tags"]["manifests"] = manifests_scope(task, test_manifests)
        task.attributes["test_manifests"] = test_manifests
        task.task["payload"]["env"]["MOZHARNESS_TEST_PATHS"] = json.dumps(
            test_manifests
        )
        original_label = input.get("original_label", task.label)
        task.task["metadata"]["name"] = original_label
        th_info = task.task["extra"]["treeherder"]
        # Use the symbol of the originating task to preserve the chunk
        # identity even when new_label hijacked a different chunk.
        symbol = input.get("symbol", th_info["symbol"])
        th_info["symbol"] = add_backfill_suffix(
            SYMBOL_REGEX, symbol, f"-{revision[0:11]}-bk"
        )
        if th_info.get("groupSymbol"):
            # Group all backfilled tasks together
            th_info["groupSymbol"] = add_backfill_suffix(
                GROUP_SYMBOL_REGEX, th_info["groupSymbol"], "-bk"
            )
        task.task["tags"]["action"] = "backfill-task"
    return task


def new_label(label, tasks):
    """This is to handle the case when a previous push does not contain a specific task label
    and we try to find a label we can reuse.

    For instance, we try to backfill chunk #3, however, a previous push does not contain such
    chunk, thus, we try to reuse another task/label.
    """
    logger.info(f"Extracting new label for {label}")

    if "-" not in label:
        raise Exception(
            f"Expected '-' was not found in label {label}, cannot extract new label."
        )

    begining_label, ending = label.rsplit("-", 1)

    if ending.isdigit():
        # We assume that the taskgraph has chunk #1 OR unnumbered chunk and we hijack it
        if begining_label in tasks:
            return begining_label
        if begining_label + "-1" in tasks:
            return begining_label + "-1"
        raise Exception(f"New label ({label}) was not found in the task-graph")
    else:
        # Handle the case where an unnumbered task was previously chunked (e.g.,
        # "task-swr" maps to "task-swr-1" in an older revision).
        if label + "-1" in tasks:
            return label + "-1"
        raise Exception(f"{label} was not found in the task-graph")


@register_callback_action(
    name="backfill-task",
    title="Backfill task on a push.",
    permission="backfill",
    symbol="backfill-task",
    description="This action is normally scheduled by the backfill action. "
    "The intent is to schedule a task on previous pushes.",
    order=500,
    context=[],
    schema={
        "type": "object",
        "properties": {
            "label": {"type": "string", "description": "A task label"},
            "revision": {
                "type": "string",
                "description": "Revision of the original push from where we backfill.",
            },
            "symbol": {
                "type": "string",
                "description": "Symbol to be used by the scheduled task.",
            },
            "test_manifests": {
                "type": "array",
                "default": [],
                "description": "An array of test manifest paths",
                "items": {"type": "string"},
            },
            "times": {
                "type": "integer",
                "default": 1,
                "minimum": 1,
                "maximum": 10,
                "title": "Times",
                "description": (
                    "The number of times to execute each job you are backfilling."
                ),
            },
            "retrigger": {
                "type": "boolean",
                "default": True,
                "title": "Retrigger",
                "description": (
                    "If False, the task won't retrigger on pushes that have already "
                    "ran it."
                ),
            },
        },
    },
)
def add_task_with_original_manifests(
    parameters, graph_config, input, task_group_id, task_id
):
    """
    This action is normally scheduled by the backfill action. The intent is to schedule a test
    task with the test manifests from the original task (if available).

    The push in which we want to schedule a new task is defined by the parameters object.

    To execute this action locally follow the documentation here:
    https://taskcluster-taskgraph.readthedocs.io/en/latest/howto/create-actions.html#testing-actions
    """
    # This step takes a lot of time when executed locally
    logger.info("Retreving the full task graph and labels.")
    decision_task_id, full_task_graph, label_to_taskid, _ = fetch_graph_and_labels(
        parameters, graph_config
    )

    label = input.get("label")
    if not input.get("retrigger") and label in label_to_taskid:
        logger.info(
            f"Skipping push with decision task ID {decision_task_id} as it already has this test."
        )
        return

    original_label = label
    if label not in full_task_graph.tasks:
        label = new_label(label, full_task_graph.tasks)
        input["label"] = label
    input["original_label"] = original_label

    to_run = [label]

    # When backfilling a performance test, lower the priority of every task this
    # action schedules (the test and its dependencies) to avoid delaying the
    # tasks of regular pushes.
    lower_priority = _is_perf_task(full_task_graph[label])

    logger.info("Creating tasks...")
    create_tasks(
        graph_config,
        to_run,
        full_task_graph,
        label_to_taskid,
        parameters,
        decision_task_id,
        suffix="0",
        modifier=partial(backfill_modifier, input=input, lower_priority=lower_priority),
    )

    # TODO Implement a way to write out artifacts without assuming there's
    # multiple sets of them so we can stop passing in "suffix".
    combine_task_graph_files(["0"])


@register_callback_action(
    title="Backfill all browsertime",
    name="backfill-all-browsertime",
    permission="backfill",
    symbol="baB",
    description=(
        "Schedule all browsertime tests for the current and previous push in the same project."
    ),
    order=800,
    context=[],  # This will be available for all tasks
    available=lambda parameters: True,
)
def backfill_all_browsertime(parameters, graph_config, input, task_group_id, task_id):
    """
    This action takes a revision and schedules it on previous pushes (via support action).

    To execute this action locally follow the documentation here:
    https://taskcluster-taskgraph.readthedocs.io/en/latest/howto/create-actions.html#testing-actions
    """
    pushes = get_pushes(
        project=parameters["head_repository"],
        end_id=int(parameters["pushlog_id"]),
        depth=2,
    )

    for push_id in pushes:
        try:
            # The Gecko decision task can sometimes fail on a push and we need to handle
            # the exception that this call will produce
            push_decision_task_id = get_decision_task_id(parameters["project"], push_id)
        except Exception:
            logger.warning(f"Could not find decision task for push {push_id}")
            # The decision task may have failed, this is common enough that we
            # don't want to report an error for it.
            continue

        try:
            trigger_action(
                action_name="add-all-browsertime",
                # This lets the action know on which push we want to add a new task
                decision_task_id=push_decision_task_id,
            )
        except Exception:
            logger.exception(f"Failed to trigger action for {push_id}")
            sys.exit(1)


def filter_raptor_jobs(full_task_graph, label_to_taskid, project):
    # Late import to prevent impacting other backfill action tasks
    from ..util.attributes import match_run_on_projects

    to_run = []
    for label, entry in full_task_graph.tasks.items():
        if entry.kind != "test":
            continue
        if entry.task.get("extra", {}).get("suite", "") != "raptor":
            continue
        if not match_run_on_projects(
            {"project": project}, entry.attributes.get("run_on_projects", [])
        ):
            continue
        if "macosx1500" in entry.attributes.get("test_platform", ""):
            continue
        if "browsertime" not in entry.attributes.get("raptor_try_name", ""):
            continue
        if "shippable" not in entry.attributes.get("test_platform", ""):
            continue
        if "android" in entry.attributes.get("test_platform", ""):
            # Bug 1786254 - The backfill bot is scheduling too many tests atm
            continue
        exceptions = ("live", "profiling", "youtube-playback")
        if any(e in entry.attributes.get("raptor_try_name", "") for e in exceptions):
            continue
        # Only run on allowed browser applications.
        raptor_try_name = entry.attributes.get("raptor_try_name", "")
        if not any(app in raptor_try_name for app in ALLOWED_PERFTEST_BACKFILL_APPS):
            continue
        if "firefox" in raptor_try_name and entry.attributes.get(
            "test_platform", ""
        ).endswith("64-shippable-qr/opt"):
            # add the browsertime test
            if label not in label_to_taskid:
                to_run.append(label)
        if "geckoview" in raptor_try_name:
            # add the pageload test
            if label not in label_to_taskid:
                to_run.append(label)
    return to_run


@register_callback_action(
    name="add-all-browsertime",
    title="Add All Browsertime Tests.",
    permission="backfill",
    symbol="aaB",
    description="This action is normally scheduled by the backfill-all-browsertime action. "
    "The intent is to schedule all browsertime tests on a specific pushe.",
    order=900,
    context=[],
)
def add_all_browsertime(parameters, graph_config, input, task_group_id, task_id):
    """
    This action is normally scheduled by the backfill-all-browsertime action. The intent is to
    trigger all browsertime tasks for the current revision.

    The push in which we want to schedule a new task is defined by the parameters object.

    To execute this action locally follow the documentation here:
    https://taskcluster-taskgraph.readthedocs.io/en/latest/howto/create-actions.html#testing-actions
    """
    logger.info("Retreving the full task graph and labels.")
    decision_task_id, full_task_graph, label_to_taskid, _ = fetch_graph_and_labels(
        parameters, graph_config
    )

    to_run = filter_raptor_jobs(full_task_graph, label_to_taskid, parameters["project"])

    create_tasks(
        graph_config,
        to_run,
        full_task_graph,
        label_to_taskid,
        parameters,
        decision_task_id,
    )
    logger.info(f"Scheduled {len(to_run)} raptor tasks (time 1)")


def plan_pushes_to_trigger(
    pushes: list[str],
    strategy: str,
    slices: int,
) -> list[str]:
    """
    Returns push_ids to trigger backfill-task for.
    standard: all pushes.
    sliced: trigger only the exact pivot pushes (n/N+1, 2n/N+1, ... for slices=N).
    """
    if not pushes:
        return []

    if strategy == "standard":
        return pushes

    n = len(pushes)
    pivot_indices = sorted(set(i * n // (slices + 1) for i in range(1, slices + 1)))
    planned: list[str] = []
    for pivot in pivot_indices:
        pid = pushes[pivot]
        planned.append(pid)
    return planned
