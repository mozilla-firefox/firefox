# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json
import logging
import os
import shutil
import sys
import time
from collections import defaultdict

import yaml
from redo import retry
from taskgraph import create
from taskgraph.create import create_tasks

# TODO: Let standalone taskgraph generate parameters instead of calling internals
from taskgraph.decision import (
    _determine_more_accurate_base_ref,
    _determine_more_accurate_base_rev,
    _get_env_prefix,
)
from taskgraph.generator import TaskGraphGenerator
from taskgraph.parameters import Parameters
from taskgraph.taskgraph import TaskGraph
from taskgraph.util.python_path import find_object
from taskgraph.util.taskcluster import get_artifact
from taskgraph.util.vcs import get_repository
from taskgraph.util.yaml import load_yaml

from . import GECKO
from .actions import render_actions_json
from .files_changed import get_changed_files
from .parameters import get_app_version, get_version
from .try_option_syntax import parse_message
from .util.backstop import ANDROID_PERFTEST_BACKSTOP_INDEX, BACKSTOP_INDEX, is_backstop
from .util.bugbug import push_schedules
from .util.chunking import resolver
from .util.hg import get_hg_commit_message, get_hg_revision_branch, get_hg_revision_info
from .util.partials import populate_release_history
from .util.taskcluster import insert_index
from .util.taskgraph import find_decision_task, find_existing_tasks_from_previous_kinds

logger = logging.getLogger(__name__)

ARTIFACTS_DIR = "artifacts"

# For each project, this gives a set of parameters specific to the project.
# See `taskcluster/docs/parameters.rst` for information on parameters.
PER_PROJECT_PARAMETERS = {
    "try": {
        "enable_always_target": True,
        "target_tasks_method": "try_tasks",
        "release_type": "nightly",
    },
    "kaios-try": {
        "target_tasks_method": "try_tasks",
    },
    "ash": {
        "target_tasks_method": "default",
    },
    "cedar": {
        "target_tasks_method": "default",
    },
    "holly": {
        "enable_always_target": True,
        "target_tasks_method": "holly_tasks",
    },
    "oak": {
        "target_tasks_method": "default",
        "release_type": "nightly-oak",
    },
    "graphics": {
        "target_tasks_method": "graphics_tasks",
    },
    "autoland": {
        "optimize_strategies": "gecko_taskgraph.optimize:project.autoland",
        "target_tasks_method": "autoland_tasks",
        "test_manifest_loader": "bugbug",  # Remove this line to disable "manifest scheduling".
    },
    "mozilla-central": {
        "target_tasks_method": "mozilla_central_tasks",
        "release_type": "nightly",
    },
    "mozilla-beta": {
        "target_tasks_method": "mozilla_beta_tasks",
        "release_type": "beta",
    },
    "mozilla-release": {
        "target_tasks_method": "mozilla_release_tasks",
        "release_type": "release",
    },
    "mozilla-esr128": {
        "target_tasks_method": "mozilla_esr128_tasks",
        "release_type": "esr128",
    },
    "pine": {
        "target_tasks_method": "pine_tasks",
        "release_type": "nightly-pine",
    },
    "cypress": {
        "target_tasks_method": "cypress_tasks",
    },
    "larch": {
        "target_tasks_method": "larch_tasks",
        "release_type": "nightly-larch",
    },
    "kaios": {
        "target_tasks_method": "kaios_tasks",
    },
    "toolchains": {
        "target_tasks_method": "mozilla_central_tasks",
    },
    # the default parameters are used for projects that do not match above.
    "default": {
        "target_tasks_method": "default",
    },
}


def full_task_graph_to_runnable_jobs(full_task_json):
    runnable_jobs = {}
    for label, node in full_task_json.items():
        if not ("extra" in node["task"] and "treeherder" in node["task"]["extra"]):
            continue

        th = node["task"]["extra"]["treeherder"]
        runnable_jobs[label] = {"symbol": th["symbol"]}

        for i in ("groupName", "groupSymbol", "collection"):
            if i in th:
                runnable_jobs[label][i] = th[i]
        if th.get("machine", {}).get("platform"):
            runnable_jobs[label]["platform"] = th["machine"]["platform"]
    return runnable_jobs


def full_task_graph_to_manifests_by_task(full_task_json):
    manifests_by_task = defaultdict(list)
    for label, node in full_task_json.items():
        manifests = node["attributes"].get("test_manifests")
        if not manifests:
            continue

        manifests_by_task[label].extend(manifests)
    return manifests_by_task


def try_syntax_from_message(message):
    """
    Parse the try syntax out of a commit message, returning '' if none is
    found.
    """
    try_idx = message.find("try:")
    if try_idx == -1:
        return ""
    return message[try_idx:].split("\n", 1)[0]


def taskgraph_decision(options, parameters=None):
    """
    Run the decision task.  This function implements `mach taskgraph decision`,
    and is responsible for

     * processing decision task command-line options into parameters
     * running task-graph generation exactly the same way the other `mach
       taskgraph` commands do
     * generating a set of artifacts to memorialize the graph
     * calling TaskCluster APIs to create the graph
    """

    parameters = parameters or (
        lambda graph_config: get_decision_parameters(graph_config, options)
    )

    decision_task_id = os.environ["TASK_ID"]

    # create a TaskGraphGenerator instance
    tgg = TaskGraphGenerator(
        root_dir=options.get("root"),
        parameters=parameters,
        decision_task_id=decision_task_id,
        write_artifacts=True,
    )

    if not create.testing:
        # set additional index paths for the decision task
        set_decision_indexes(decision_task_id, tgg.parameters, tgg.graph_config)

    # write out the parameters used to generate this graph
    write_artifact("parameters.yml", dict(**tgg.parameters))

    # write out the public/actions.json file
    write_artifact(
        "actions.json",
        render_actions_json(tgg.parameters, tgg.graph_config, decision_task_id),
    )

    # write out the full graph for reference
    full_task_json = tgg.full_task_graph.to_json()
    write_artifact("full-task-graph.json", full_task_json)

    # write out the public/runnable-jobs.json file
    write_artifact(
        "runnable-jobs.json", full_task_graph_to_runnable_jobs(full_task_json)
    )

    # write out the public/manifests-by-task.json file
    write_artifact(
        "manifests-by-task.json.gz",
        full_task_graph_to_manifests_by_task(full_task_json),
    )

    # write out the public/tests-by-manifest.json file
    write_artifact("tests-by-manifest.json.gz", resolver.tests_by_manifest)

    # this is just a test to check whether the from_json() function is working
    _, _ = TaskGraph.from_json(full_task_json)

    # write out the target task set to allow reproducing this as input
    write_artifact("target-tasks.json", list(tgg.target_task_set.tasks.keys()))

    # write out the optimized task graph to describe what will actually happen,
    # and the map of labels to taskids
    write_artifact("task-graph.json", tgg.morphed_task_graph.to_json())
    write_artifact("label-to-taskid.json", tgg.label_to_taskid)

    # write bugbug scheduling information if it was invoked
    if len(push_schedules) > 0:
        write_artifact("bugbug-push-schedules.json", push_schedules.popitem()[1])

    # cache run-task, misc/fetch-content & robustcheckout.py
    scripts_root_dir = os.path.join(GECKO, "taskcluster/scripts")
    run_task_file_path = os.path.join(scripts_root_dir, "run-task")
    fetch_content_file_path = os.path.join(
        GECKO,
        "third_party",
        "python",
        "taskcluster_taskgraph",
        "taskgraph",
        "run-task",
        "fetch-content",
    )
    robustcheckout_path = os.path.join(
        GECKO,
        "testing/mozharness/external_tools/robustcheckout.py",
    )
    shutil.copy2(run_task_file_path, ARTIFACTS_DIR)
    shutil.copy2(fetch_content_file_path, ARTIFACTS_DIR)
    shutil.copy2(robustcheckout_path, ARTIFACTS_DIR)

    # actually create the graph
    create_tasks(
        tgg.graph_config,
        tgg.morphed_task_graph,
        tgg.label_to_taskid,
        tgg.parameters,
        decision_task_id=decision_task_id,
    )


def get_decision_parameters(graph_config, options):
    """
    Load parameters from the command-line options for 'taskgraph decision'.
    This also applies per-project parameters, based on the given project.

    """
    product_dir = graph_config["product-dir"]

    parameters = {
        n: options[n]
        for n in [
            "base_repository",
            "base_ref",
            "base_rev",
            "head_repository",
            "head_rev",
            "head_ref",
            "head_tag",
            "project",
            "pushlog_id",
            "pushdate",
            "owner",
            "level",
            "repository_type",
            "target_tasks_method",
            "tasks_for",
        ]
        if n in options
    }

    commit_message = get_hg_commit_message(os.path.join(GECKO, product_dir))

    repo_path = os.getcwd()
    repo = get_repository(repo_path)
    parameters["base_ref"] = _determine_more_accurate_base_ref(
        repo,
        candidate_base_ref=options.get("base_ref"),
        head_ref=options.get("head_ref"),
        base_rev=options.get("base_rev"),
    )

    parameters["base_rev"] = _determine_more_accurate_base_rev(
        repo,
        base_ref=parameters["base_ref"],
        candidate_base_rev=options.get("base_rev"),
        head_rev=options.get("head_rev"),
        env_prefix=_get_env_prefix(graph_config),
    )

    if head_git_rev := get_hg_revision_info(
        GECKO, revision=parameters["head_rev"], info="extras.git_commit"
    ):
        parameters["head_git_rev"] = head_git_rev

    # Define default filter list, as most configurations shouldn't need
    # custom filters.
    parameters["filters"] = [
        "target_tasks_method",
    ]
    parameters["enable_always_target"] = ["docker-image"]
    parameters["existing_tasks"] = {}
    parameters["do_not_optimize"] = []
    parameters["build_number"] = 1
    parameters["version"] = get_version(product_dir)
    parameters["app_version"] = get_app_version(product_dir)
    parameters["message"] = try_syntax_from_message(commit_message)
    parameters["hg_branch"] = get_hg_revision_branch(
        GECKO, revision=parameters["head_rev"]
    )
    parameters["files_changed"] = sorted(
        get_changed_files(parameters["head_repository"], parameters["head_rev"])
    )
    parameters["next_version"] = None
    parameters["optimize_strategies"] = None
    parameters["optimize_target_tasks"] = True
    parameters["phabricator_diff"] = None
    parameters["release_type"] = ""
    parameters["release_eta"] = ""
    parameters["release_enable_partner_repack"] = False
    parameters["release_enable_partner_attribution"] = False
    parameters["release_partners"] = []
    parameters["release_partner_config"] = {}
    parameters["release_partner_build_number"] = 1
    parameters["release_enable_emefree"] = False
    parameters["release_product"] = None
    parameters["required_signoffs"] = []
    parameters["signoff_urls"] = {}
    parameters["test_manifest_loader"] = "default"
    parameters["try_mode"] = None
    parameters["try_task_config"] = {}
    parameters["try_options"] = None

    # owner must be an email, but sometimes (e.g., for ffxbld) it is not, in which
    # case, fake it
    if "@" not in parameters["owner"]:
        parameters["owner"] += "@noreply.mozilla.org"

    # use the pushdate as build_date if given, else use current time
    parameters["build_date"] = parameters["pushdate"] or int(time.time())
    # moz_build_date is the build identifier based on build_date
    parameters["moz_build_date"] = time.strftime(
        "%Y%m%d%H%M%S", time.gmtime(parameters["build_date"])
    )

    project = parameters["project"]
    try:
        parameters.update(PER_PROJECT_PARAMETERS[project])
    except KeyError:
        logger.warning(
            f"using default project parameters; add {project} to "
            f"PER_PROJECT_PARAMETERS in {__file__} to customize behavior "
            "for this project"
        )
        parameters.update(PER_PROJECT_PARAMETERS["default"])

    # `target_tasks_method` has higher precedence than `project` parameters
    if options.get("target_tasks_method"):
        parameters["target_tasks_method"] = options["target_tasks_method"]

    # ..but can be overridden by the commit message: if it contains the special
    # string "DONTBUILD" and this is an on-push decision task, then use the
    # special 'nothing' target task method.
    if "DONTBUILD" in commit_message and options["tasks_for"] == "hg-push":
        parameters["target_tasks_method"] = "nothing"

    if options.get("include_push_tasks"):
        get_existing_tasks(options.get("rebuild_kinds", []), parameters, graph_config)

    # If the target method is nightly, we should build partials. This means
    # knowing what has been released previously.
    # An empty release_history is fine, it just means no partials will be built
    parameters.setdefault("release_history", dict())
    if "nightly" in parameters.get("target_tasks_method", ""):
        parameters["release_history"] = populate_release_history("Firefox", project)

    if options.get("try_task_config_file"):
        task_config_file = os.path.abspath(options.get("try_task_config_file"))
    else:
        # if try_task_config.json is present, load it
        task_config_file = os.path.join(os.getcwd(), "try_task_config.json")

    # load try settings
    if "try" in project and options["tasks_for"] == "hg-push":
        set_try_config(parameters, task_config_file)

    if options.get("optimize_target_tasks") is not None:
        parameters["optimize_target_tasks"] = options["optimize_target_tasks"]

    # Determine if this should be a backstop push.
    parameters["backstop"] = is_backstop(parameters)

    # For the android perf tasks, run them 50% less often
    parameters["android_perftest_backstop"] = is_backstop(
        parameters,
        push_interval=30,
        time_interval=60 * 6,
        backstop_strategy="android_perftest_backstop",
    )

    if "decision-parameters" in graph_config["taskgraph"]:
        find_object(graph_config["taskgraph"]["decision-parameters"])(
            graph_config, parameters
        )

    result = Parameters(**parameters)
    result.check()
    return result


def get_existing_tasks(rebuild_kinds, parameters, graph_config):
    """
    Find the decision task corresponding to the on-push graph, and return
    a mapping of labels to task-ids from it. This will skip the kinds specificed
    by `rebuild_kinds`.
    """
    try:
        decision_task = retry(
            find_decision_task,
            args=(parameters, graph_config),
            attempts=4,
            sleeptime=5 * 60,
        )
    except Exception:
        logger.exception("Didn't find existing push task.")
        sys.exit(1)
    _, task_graph = TaskGraph.from_json(
        get_artifact(decision_task, "public/full-task-graph.json")
    )
    parameters["existing_tasks"] = find_existing_tasks_from_previous_kinds(
        task_graph, [decision_task], rebuild_kinds
    )


def set_try_config(parameters, task_config_file):
    if os.path.isfile(task_config_file):
        logger.info(f"using try tasks from {task_config_file}")
        with open(task_config_file) as fh:
            task_config = json.load(fh)
        task_config_version = task_config.pop("version", 1)
        if task_config_version == 1:
            parameters["try_mode"] = "try_task_config"
            parameters["try_task_config"] = task_config
        elif task_config_version == 2:
            parameters.update(task_config["parameters"])
            parameters["try_mode"] = "try_task_config"
        else:
            raise Exception(
                f"Unknown `try_task_config.json` version: {task_config_version}"
            )

    if "try:" in parameters["message"]:
        parameters["try_mode"] = "try_option_syntax"
        parameters.update(parse_message(parameters["message"]))
    else:
        parameters["try_options"] = None


def set_decision_indexes(decision_task_id, params, graph_config):
    index_paths = []
    if params["android_perftest_backstop"]:
        index_paths.insert(0, ANDROID_PERFTEST_BACKSTOP_INDEX)
    if params["backstop"]:
        # When two Decision tasks run at nearly the same time, it's possible
        # they both end up being backstops if the second checks the backstop
        # index before the first inserts it. Insert this index first to reduce
        # the chances of that happening.
        index_paths.insert(0, BACKSTOP_INDEX)

    subs = params.copy()
    subs["trust-domain"] = graph_config["trust-domain"]

    for index_path in index_paths:
        insert_index(index_path.format(**subs), decision_task_id, use_proxy=True)


def write_artifact(filename, data):
    logger.info(f"writing artifact file `{filename}`")
    if not os.path.isdir(ARTIFACTS_DIR):
        os.mkdir(ARTIFACTS_DIR)
    path = os.path.join(ARTIFACTS_DIR, filename)
    if filename.endswith(".yml"):
        with open(path, "w") as f:
            yaml.safe_dump(data, f, allow_unicode=True, default_flow_style=False)
    elif filename.endswith(".json"):
        with open(path, "w") as f:
            json.dump(data, f, separators=(",", ": "))
    elif filename.endswith(".json.gz"):
        import gzip

        with gzip.open(path, "wb") as f:
            f.write(json.dumps(data).encode("utf-8"))
    else:
        raise TypeError(f"Don't know how to write to {filename}")


def read_artifact(filename):
    path = os.path.join(ARTIFACTS_DIR, filename)
    if filename.endswith(".yml"):
        return load_yaml(path, filename)
    if filename.endswith(".json"):
        with open(path) as f:
            return json.load(f)
    if filename.endswith(".json.gz"):
        import gzip

        with gzip.open(path, "rb") as f:
            return json.load(f.decode("utf-8"))
    else:
        raise TypeError(f"Don't know how to read {filename}")


def rename_artifact(src, dest):
    os.rename(os.path.join(ARTIFACTS_DIR, src), os.path.join(ARTIFACTS_DIR, dest))
