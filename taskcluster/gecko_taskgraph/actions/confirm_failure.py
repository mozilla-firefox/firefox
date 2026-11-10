# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import logging
from functools import partial

from taskgraph.util import json
from taskgraph.util.taskcluster import get_artifact, get_task_definition, list_artifacts

from ..util.constants import TEST_KINDS
from .registry import register_callback_action
from .retrigger import retrigger_action
from .util import add_args_to_command, create_tasks, fetch_graph_and_labels

logger = logging.getLogger(__name__)


def get_failures(task_id, task_definition):
    """Returns a dict containing properties containing a list of
    directories containing test failures and a separate list of
    individual test failures from the errorsummary.log artifact for
    the task.

    Find test path to pass to the task in
    MOZHARNESS_TEST_PATHS.  If no appropriate test path can be
    determined, nothing is returned.
    """

    def fix_wpt_name(test):
        # TODO: find other cases to handle
        if ".any." in test:
            test = "%s.any.js" % test.split(".any.")[0]
        if ".window.html" in test:
            test = test.replace(".window.html", ".window.js")

        if test.startswith("/_mozilla"):
            test = "testing/web-platform/mozilla/tests" + test[len("/_mozilla") :]
        else:
            test = "testing/web-platform/tests/" + test.strip("/")
        # some wpt tests have params, those are not supported
        test = test.split("?")[0]

        return test

    # collect dirs that don't have a specific manifest
    dirs = []
    tests = []

    artifacts = list_artifacts(task_id)
    for artifact in artifacts:
        if "name" not in artifact or not artifact["name"].endswith("errorsummary.log"):
            continue

        stream = get_artifact(task_id, artifact["name"])
        if not stream:
            continue

        # We handle the stream as raw bytes because it may contain invalid
        # UTF-8 characters in portions other than those containing the error
        # messages we're looking for.
        for line in stream.read().split(b"\n"):
            if not line.strip():
                continue

            l = json.loads(line)
            if "group_results" in l.keys() and l["status"] != "OK":
                dirs.append(l["group_results"].group())

            elif "test" in l.keys():
                if not l["test"]:
                    print("Warning: no testname in errorsummary line: %s" % l)
                    continue

                test_path = l["test"].split(" ")[0]
                found_path = False

                # tests with url params (wpt), will get confused here
                if "?" not in test_path:
                    test_path = test_path.split(":")[-1]

                # edge case where a crash on shutdown has a "test" name == group name
                if (
                    test_path.endswith(".toml")
                    or test_path.endswith(".ini")
                    or test_path.endswith(".list")
                ):
                    # TODO: consider running just the manifest
                    continue

                # edge cases with missing test names
                if (
                    test_path is None
                    or test_path == "None"
                    or "SimpleTest" in test_path
                ):
                    continue

                if "signature" in l.keys():
                    # dealing with a crash
                    found_path = True
                    if "web-platform" in task_definition["extra"]["suite"]:
                        test_path = fix_wpt_name(test_path)
                else:
                    if "status" not in l and "expected" not in l:
                        continue

                    if l["status"] != l["expected"]:
                        if l["status"] not in l.get("known_intermittent", []):
                            found_path = True
                            if "web-platform" in task_definition["extra"]["suite"]:
                                test_path = fix_wpt_name(test_path)

                if found_path and test_path:
                    fpath = test_path.replace("\\", "/")
                    tval = {"path": fpath, "group": l["group"]}
                    # only store one failure per test
                    if not [t for t in tests if t["path"] == fpath]:
                        tests.append(tval)

                # only run the failing test not both test + dir
                if l["group"] in dirs:
                    dirs.remove(l["group"])

            # TODO: 10 is too much; how to get only NEW failures?
            if len(tests) > 10:
                break

    dirs = [{"path": "", "group": d} for d in list(set(dirs))]
    return {"dirs": dirs, "tests": tests}


def get_repeat_args(task_definition, failure_group):
    task_name = task_definition["metadata"]["name"]
    repeatable_task = False
    if (
        "crashtest" in task_name
        or "mochitest" in task_name
        or "reftest" in task_name
        or "xpcshell" in task_name
        or "web-platform" in task_name
        and "jsreftest" not in task_name
    ):
        repeatable_task = True

    repeat_args = ""
    if not repeatable_task:
        return repeat_args

    if failure_group == "dirs":
        # execute 3 total loops
        repeat_args = ["--repeat=2"] if repeatable_task else []
    elif failure_group == "tests":
        # execute 5 total loops
        repeat_args = ["--repeat=4"] if repeatable_task else []

    return repeat_args


def confirm_modifier(task, input):
    if task.label != input["label"]:
        return task

    logger.debug(f"Modifying paths for {task.label}")

    # If the original task has defined test paths
    suite = input.get("suite")
    test_path = input.get("test_path")
    test_group = input.get("test_group")
    if test_path or test_group:
        repeat_args = input.get("repeat_args")

        if repeat_args:
            task.task["payload"]["command"] = add_args_to_command(
                task.task["payload"]["command"], extra_args=repeat_args
            )

        # TODO: do we need this attribute?
        task.attributes["test_path"] = test_path

        task.task["payload"]["env"]["MOZHARNESS_TEST_PATHS"] = json.dumps(
            {suite: [test_group]}, sort_keys=True
        )
        task.task["payload"]["env"]["MOZHARNESS_CONFIRM_PATHS"] = json.dumps(
            {suite: [test_path]}, sort_keys=True
        )
        task.task["payload"]["env"]["MOZLOG_DUMP_ALL_TESTS"] = "1"

        task.task["metadata"]["name"] = task.label
        task.task["tags"]["action"] = "confirm-failure"
        task.task["tags"]["manifests"] = "partial"
    return task


@register_callback_action(
    name="confirm-failures",
    title="Confirm failures in job",
    symbol="cf",
    description="Re-run Tests for original manifest, directories or tests for failing tests.",
    order=150,
    context=[{"kind": kind} for kind in TEST_KINDS],
    schema={
        "type": "object",
        "properties": {
            "label": {"type": "string", "description": "A task label"},
            "suite": {"type": "string", "description": "Test suite"},
            "test_path": {"type": "string", "description": "A full path to test"},
            "test_group": {
                "type": "string",
                "description": "A full path to group name",
            },
            "repeat_args": {
                "type": "string",
                "description": "args to pass to test harness",
            },
        },
        "additionalProperties": False,
    },
)
def confirm_failures(parameters, graph_config, input, task_group_id, task_id):
    task_definition = get_task_definition(task_id)
    decision_task_id, full_task_graph, label_to_taskid, _ = fetch_graph_and_labels(
        parameters, graph_config
    )

    # create -cf label; ideally make this a common function
    task_definition["metadata"]["name"].split("-")
    cfname = "%s-cf" % task_definition["metadata"]["name"]

    if cfname not in full_task_graph.tasks:
        raise Exception(f"{cfname} was not found in the task-graph")

    to_run = [cfname]

    suite = task_definition["extra"]["suite"]
    if "-coverage" in suite:
        suite = suite[: suite.index("-coverage")]
    if "-qr" in suite:
        suite = suite[: suite.index("-qr")]
    failures = get_failures(task_id, task_definition)

    if failures["dirs"] == [] and failures["tests"] == []:
        logger.info("need to retrigger task as no specific test failures found")
        retrigger_action(parameters, graph_config, input, decision_task_id, task_id)
        return

    # for each unique failure, create a new confirm failure job
    for failure_group in failures:
        for failure_path in failures[failure_group]:
            repeat_args = get_repeat_args(task_definition, failure_group)

            input = {
                "label": cfname,
                "suite": suite,
                "test_path": failure_path["path"],
                "test_group": failure_path["group"],
                "repeat_args": repeat_args,
            }

            logger.info("confirm_failures: %s" % failures)
            create_tasks(
                graph_config,
                to_run,
                full_task_graph,
                label_to_taskid,
                parameters,
                decision_task_id,
                modifier=partial(confirm_modifier, input=input),
            )
