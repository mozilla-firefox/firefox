# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gc
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from contextlib import contextmanager
from pathlib import Path

import taskgraph
import yaml
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor
from redo import retry
from taskgraph import create
from taskgraph.create import create_tasks
from taskgraph.generator import TaskGraphGenerator
from taskgraph.main import format_kind_graph_mermaid
from taskgraph.parameters import Parameters
from taskgraph.taskgraph import TaskGraph
from taskgraph.util import json
from taskgraph.util.python_path import find_object
from taskgraph.util.taskcluster import get_artifact
from taskgraph.util.vcs import get_repository
from taskgraph.util.yaml import load_yaml

from . import GECKO
from .actions import render_actions_json
from .files_changed import get_changed_files
from .parameters import get_app_version, get_version
from .util.backstop import ANDROID_PERFTEST_BACKSTOP_INDEX, BACKSTOP_INDEX, is_backstop
from .util.bugbug import push_schedules, start_push_schedules
from .util.hg import get_hg_revision_metadata
from .util.partials import populate_release_history
from .util.taskcluster import insert_index
from .util.taskgraph import find_decision_task, find_existing_tasks_from_previous_kinds

logger = logging.getLogger(__name__)

ARTIFACTS_DIR = os.environ.get("MOZ_UPLOAD_DIR", "artifacts")
GIT_BACKING_REPO = "https://github.com/mozilla-releng/git-backing"

# Age, in days, of the oldest changesets assumed to already be present in a
# downstream task's store (i.e. served by the CDN clone bundle). The source
# bundle artifact carries everything newer, so this must comfortably exceed the
# CDN clone bundle regeneration interval. A larger value only grows a still-small
# artifact; if the base is missing, robustcheckout falls back to a remote pull.
SOURCE_BUNDLE_MAX_AGE_DAYS = 2

# Number of most-recent changesets (by revision number) over which to evaluate
# the age predicate when computing the bundle base. Reading each changeset's
# date decompresses its changelog entry, so restricting the scan to a recent
# window keeps bundle generation ~O(window) instead of O(history) (~34s ->
# ~3s on mozilla-unified). Must comfortably exceed the changesets landed in
# SOURCE_BUNDLE_MAX_AGE_DAYS; if too small it only causes some receivers to fall
# back to a remote pull, never breakage.
SOURCE_BUNDLE_RECENT_REVS = 20000

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
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/autoland",
        "optimize_strategies": "gecko_taskgraph.optimize:project.autoland",
        "target_tasks_method": "autoland_tasks",
        "test_manifest_loader": "bugbug",  # Remove this line to disable "manifest scheduling".
    },
    "mozilla-central": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/main",
        "target_tasks_method": "mozilla_central_tasks",
        "release_type": "nightly",
    },
    "mozilla-beta": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/beta",
        "optimize_strategies": "gecko_taskgraph.optimize:project.beta",
        "target_tasks_method": "mozilla_beta_tasks",
        "release_type": "beta",
    },
    "mozilla-release": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/release",
        "target_tasks_method": "mozilla_release_tasks",
        "release_type": "release",
    },
    "mozilla-esr140": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/esr140",
        "target_tasks_method": "mozilla_esr140_tasks",
        "release_type": "esr140",
    },
    "mozilla-esr153": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "head_git_ref": "refs/heads/esr153",
        "target_tasks_method": "mozilla_esr153_tasks",
        "release_type": "esr153",
    },
    "pine": {
        "target_tasks_method": "pine_tasks",
        "release_type": "nightly-pine",
    },
    "maple": {
        # Prevent it from running everything - For now just use "Add New Jobs"
        "target_tasks_method": "nothing",
        "release_type": "release",
    },
    "cypress": {
        "target_tasks_method": "cypress_tasks",
        "release_type": "nightly-cypress",
    },
    "larch": {
        "target_tasks_method": "larch_tasks",
        "release_type": "nightly-larch",
    },
    "kaios": {
        "target_tasks_method": "kaios_tasks",
    },
    "toolchains": {
        "head_git_repository": "https://github.com/mozilla-firefox/firefox",
        "target_tasks_method": "mozilla_central_tasks",
    },
    # git projects
    "firefox": {
        # TODO We'll eventually need to split this out based on tasks_for and
        # branch, but for now just use the pull request target_tasks_method.
        "target_tasks_method": "firefox_pull_request_tasks",
    },
    "firefox-dev": {
        "enable_always_target": True,
        "target_tasks_method": "try_tasks",
        "release_type": "nightly",
    },
    "firefox-dev-secure": {
        "enable_always_target": True,
        "target_tasks_method": "try_tasks",
        "release_type": "nightly",
    },
    "staging-firefox": {
        "target_tasks_method": "firefox_pull_request_tasks",
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


def build_decision_perfherder_data(trust_domain, params_time, taskgraph_time):
    suite = {
        "name": "decision",
        "value": params_time + taskgraph_time,
        "lowerIsBetter": True,
        "subtests": [
            {"name": "parameters", "value": params_time, "lowerIsBetter": True},
            {"name": "taskgraph", "value": taskgraph_time, "lowerIsBetter": True},
        ],
    }
    if trust_domain == "gecko":
        suite["monitor"] = True
        suite["alertNotifyEmails"] = ["release+gecko-decision-alerts@mozilla.com"]
    else:
        suite["shouldAlert"] = False
    return {
        "framework": {"name": "build_metrics"},
        "suites": [suite],
    }


def taskgraph_decision(options, parameters):
    """
    Run the decision task.  This function implements `mach taskgraph decision`,
    and is responsible for

     * running task-graph generation exactly the same way the other `mach
       taskgraph` commands do
     * generating a set of artifacts to memorialize the graph
     * calling TaskCluster APIs to create the graph

    The ``parameters`` argument must be a pre-resolved ``Parameters`` object.
    """

    decision_task_id = os.environ["TASK_ID"]

    # Start querying bugbug early, so its results are ready sooner for the
    # optimizers, and so they are stored even on pushes that don't use them
    # (e.g. backstop pushes).
    bugbug_future = None
    if not create.testing and parameters["project"] == "autoland":
        bugbug_future = start_push_schedules(
            parameters["project"], parameters["head_rev"]
        )

    with source_bundle(parameters["head_rev"], parameters["repository_type"]):
        # create a TaskGraphGenerator instance
        tgg = TaskGraphGenerator(
            root_dir=options.get("root"),
            parameters=parameters,
            decision_task_id=decision_task_id,
            write_artifacts=True,
            enable_verifications=options.get("verify", True),
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
        full_task_graph = tgg.full_task_graph
        # The generated graph is long-lived and taskgraph collects no garbage
        # while generating it, so a collection triggered while serializing it
        # would walk the whole heap for little gain.
        gc.freeze()
        full_task_json = full_task_graph.to_json()
        write_artifact("full-task-graph.json", full_task_json)

        # write out kind graph
        write_artifact("kind-graph.mm", format_kind_graph_mermaid(tgg.kind_graph))

        # write out the public/runnable-jobs.json file
        write_artifact(
            "runnable-jobs.json", full_task_graph_to_runnable_jobs(full_task_json)
        )

        # write out the public/manifests-by-task.json file
        write_artifact(
            "manifests-by-task.json.gz",
            full_task_graph_to_manifests_by_task(full_task_json),
        )

        # `tests-by-manifest.json.gz` was previously written out here
        # it was moved to `loader/test.py` because its contents now depend on
        # data generated in a subprocess which we do not have access to here
        # see https://bugzilla.mozilla.org/show_bug.cgi?id=1989038 for additional
        # details

        # write out the target task set to allow reproducing this as input
        write_artifact("target-tasks.json", list(tgg.target_task_set.tasks.keys()))

        # write out the optimized task graph to describe what will actually happen,
        # and the map of labels to taskids
        write_artifact("task-graph.json", tgg.morphed_task_graph.to_json())
        write_artifact("label-to-taskid.json", tgg.label_to_taskid)

        # upload run-task, fetch-content, robustcheckout.py and more as artifacts
        scripts_dir = Path(GECKO, "taskcluster", "scripts")
        taskgraph_dir = Path(taskgraph.__file__).parent
        to_copy = {
            scripts_dir / "run-task": f"{ARTIFACTS_DIR}/run-task-hg",
            scripts_dir / "tester" / "test-linux.sh": ARTIFACTS_DIR,
            taskgraph_dir / "run-task" / "fetch-content": ARTIFACTS_DIR,
            taskgraph_dir / "run-task" / "run-task": f"{ARTIFACTS_DIR}/run-task-git",
            scripts_dir / "robustcheckout.py": ARTIFACTS_DIR,
            scripts_dir / "run-task-setup.py": ARTIFACTS_DIR,
            scripts_dir / "run_task_resource_monitor.py": ARTIFACTS_DIR,
            Path(GECKO, "testing", "mozbase", "mozsystemmonitor")
            / "mozsystemmonitor"
            / "resourcemonitor.py": ARTIFACTS_DIR,
        }
        for target, dest in to_copy.items():
            shutil.copy(target, dest)

        # actually create the graph
        create_tasks(
            tgg.graph_config,
            tgg.morphed_task_graph,
            tgg.label_to_taskid,
            tgg.parameters,
            decision_task_id=decision_task_id,
        )

        # write bugbug scheduling information if it was invoked or is ready
        if push_schedules.cache_info().currsize > 0:
            write_artifact(
                "bugbug-push-schedules.json",
                push_schedules(tgg.parameters["project"], tgg.parameters["head_rev"]),
            )
        elif bugbug_future and bugbug_future.done() and not bugbug_future.exception():
            write_artifact("bugbug-push-schedules.json", bugbug_future.result())
        else:
            logger.info(
                "Bugbug results not available, not writing bugbug-push-schedules.json"
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

    repo_path = os.getcwd()
    repo = get_repository(repo_path)

    # Set some vcs specific parameters
    if parameters["repository_type"] == "hg":
        metadata = get_hg_revision_metadata(GECKO, parameters["head_rev"])
        commit_message = metadata["desc"]
        parameters["head_git_repository"] = GIT_BACKING_REPO
        if head_git_rev := metadata["extras"].get("git_commit"):
            parameters["head_git_rev"] = head_git_rev
        parameters["hg_branch"] = metadata["branch"]

        parameters["files_changed"] = sorted(
            get_changed_files(parameters["head_repository"], parameters["head_rev"])
        )

    elif parameters["repository_type"] == "git":
        try:
            commit_message = repo.get_commit_message()
        except UnicodeDecodeError:
            commit_message = ""
        # `files_changed` is derived further down, once parameter overrides had a
        # chance to correct `base_rev`.
        parameters["hg_branch"] = None

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
    parameters["test_manifest_loader"] = "default"
    parameters["try_mode"] = None
    parameters["try_task_config"] = {}

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

    if parameters.get("tasks_for", "").startswith("github-pull-request"):
        parameters["optimize_strategies"] = (
            "gecko_taskgraph.optimize:project.pull_request"
        )

    # `target_tasks_method` has higher precedence than `project` parameters
    if options.get("target_tasks_method"):
        parameters["target_tasks_method"] = options["target_tasks_method"]

    if options.get("include_push_tasks"):
        get_existing_tasks(options.get("rebuild_kinds", []), parameters, graph_config)

    # If the target method is nightly, we should build partials. This means
    # knowing what has been released previously.
    # An empty release_history is fine, it just means no partials will be built
    parameters.setdefault("release_history", dict())
    if "nightly" in parameters.get("target_tasks_method", ""):
        parameters["release_history"] = populate_release_history("Firefox", project)

    if options.get("optimize_target_tasks") is not None:
        parameters["optimize_target_tasks"] = options["optimize_target_tasks"]

    # If the commit message contains "DONTBUILD" and this is an on-push
    # decision task, mark it so the morph phase can drop all tasks and so
    # that is_backstop knows not to count this push as a backstop.
    parameters["dontbuild"] = (
        "DONTBUILD" in commit_message and options["tasks_for"] == "hg-push"
    )

    # "SHIPPING" is set in the commit message by Lando when a release manager
    # lands an uplift they intend to ship, and raises the priority of the tasks
    # on the beta and ESR branches. See `task-priority` in `taskcluster/config.yml`.
    parameters["shipping"] = (
        "SHIPPING" in commit_message and options["tasks_for"] == "hg-push"
    )

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

    # load extra parameters from file or note if able
    if options.get("allow_parameter_override"):
        note_ref = "refs/notes/decision-parameters"
        if options.get("try_task_config_file"):
            task_config_file = os.path.abspath(options.get("try_task_config_file"))
        else:
            task_config_file = os.path.join(os.getcwd(), "try_task_config.json")

        if os.path.isfile(task_config_file):
            set_try_config(parameters, task_config_file)
            parameters["try_mode"] = "try_task_config"

        elif note_params := get_note(repo, note_ref, parameters["head_repository"]):
            try:
                note_params = json.loads(note_params)
                logger.info(
                    f"Overriding parameters from {note_ref}:\n{json.dumps(note_params, indent=2)}"
                )
                parameters.update(note_params)
                parameters["try_mode"] = "try_task_config"
            except ValueError as e:
                raise Exception(f"Failed to parse {note_ref} as JSON: {e}") from e

    # Github reports a null base revision for a push that creates a branch, which would
    # make the diff cover the whole tree. `mach try` records the real base in the
    # parameter overrides applied above, so only derive `files_changed` now.
    if parameters["repository_type"] == "git" and "files_changed" not in parameters:
        parameters["files_changed"] = get_git_files_changed(repo, parameters)

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


def get_note(repo, note_ref, remote):
    text = f"fetch {remote} {note_ref}"
    SystemResourceMonitor.begin_marker("git", text)
    try:
        return repo.get_note(note_ref, remote)
    finally:
        SystemResourceMonitor.end_marker("git", text)


def get_git_files_changed(repo, parameters):
    base_rev = parameters["base_rev"]
    if base_rev != repo.NULL_REVISION and repo.is_shallow:
        text = f"fetch --depth=1 {parameters['base_repository']} {base_rev}"
        SystemResourceMonitor.begin_marker("git", text)
        try:
            repo.run("fetch", "--depth=1", parameters["base_repository"], base_rev)
        except subprocess.CalledProcessError:
            logger.warning(
                f"Could not fetch base revision {base_rev}, "
                "treating the whole tree as changed."
            )
            base_rev = repo.NULL_REVISION
        finally:
            SystemResourceMonitor.end_marker("git", text)

    return repo.get_changed_files(rev=parameters["head_rev"], base=base_rev)


def set_try_config(parameters, task_config_file):
    logger.info(f"using try tasks from {task_config_file}")
    with open(task_config_file) as fh:
        task_config = json.load(fh)
    task_config_version = task_config.pop("version", 1)
    if task_config_version == 1:
        parameters["try_task_config"] = task_config
    elif task_config_version == 2:
        parameters.update(task_config["parameters"])
    else:
        raise Exception(
            f"Unknown `try_task_config.json` version: {task_config_version}"
        )


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
        insert_index(index_path.format(**subs), decision_task_id)


@contextmanager
def source_bundle(head_rev, repository_type):
    """Write a Mercurial bundle of recent changesets as a public artifact.

    Downstream build and source-test tasks can apply this bundle to obtain the
    head revision without an expensive ``getbundle`` against hg.mozilla.org. The
    bundle only covers changesets newer than ``SOURCE_BUNDLE_MAX_AGE_DAYS``,
    which is the delta a task would otherwise pull on top of the CDN clone
    bundle.

    The bundle is generated in the background while the body of the ``with``
    block runs, and waited on when it exits. If the body raises, generation is
    aborted and any partial bundle is removed.

    Only produced for Mercurial checkouts; git checkouts are served elsewhere.
    """
    if repository_type != "hg":
        logger.info("source checkout is not Mercurial; skipping source bundle")
        yield
        return

    if not os.path.isdir(ARTIFACTS_DIR):
        os.mkdir(ARTIFACTS_DIR)

    path = os.path.join(ARTIFACTS_DIR, "checkout.bundle")
    # Assume downstream stores already hold every public changeset older than
    # SOURCE_BUNDLE_MAX_AGE_DAYS (served by the CDN clone bundle); bundle only
    # what is newer, up to the head revision. `not date('-N')` matches changesets
    # older than N days; drafts (e.g. try commits) are excluded from the base so
    # they are always carried. The date predicate is confined to the last
    # SOURCE_BUNDLE_RECENT_REVS changesets (older ones are always older than the
    # window) to avoid decompressing every changelog entry in history.
    base = (
        f"ancestors({head_rev}) and public() and not "
        f"(last(all(), {SOURCE_BUNDLE_RECENT_REVS}) "
        f"and date('-{SOURCE_BUNDLE_MAX_AGE_DAYS}'))"
    )
    logger.info(f"writing source bundle artifact `{path}` in the background")
    output = tempfile.TemporaryFile()
    proc = None
    error = None
    try:
        # Output is captured so it doesn't interleave with graph generation logs.
        proc = subprocess.Popen(
            [
                "hg",
                "--cwd",
                GECKO,
                "bundle",
                "--type",
                "gzip-v2",
                "--rev",
                head_rev,
                "--base",
                base,
                path,
            ],
            stdin=subprocess.DEVNULL,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
    except OSError as e:
        error = e

    succeeded = False
    try:
        yield
        succeeded = True
    finally:
        with output:
            if proc:
                if not succeeded:
                    proc.kill()
                start = time.monotonic()
                returncode = proc.wait()
                if succeeded:
                    output.seek(0)
                    hg_output = output.read().decode("utf-8", "replace").strip()
                    if hg_output:
                        logger.info(hg_output)
                    logger.info(
                        f"waited {time.monotonic() - start:.1f}s for source bundle"
                    )
                    if returncode:
                        error = f"hg bundle exited with status {returncode}"

        if not succeeded or error:
            if succeeded:
                # A missing bundle is non-fatal: downstream tasks fall back to
                # pulling from the remote. Don't fail the decision task over it.
                logger.warning(f"failed to write source bundle artifact: {error}")
            if os.path.exists(path):
                os.remove(path)


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
            json.dump(data, f)
    elif filename.endswith(".json.gz"):
        import gzip

        with gzip.open(path, "wb") as f:
            f.write(json.dumps(data).encode("utf-8"))
    else:
        with open(path, "w") as f:
            f.write(data)


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
            return json.load(f)
    else:
        raise TypeError(f"Don't know how to read {filename}")


def rename_artifact(src, dest):
    os.rename(os.path.join(ARTIFACTS_DIR, src), os.path.join(ARTIFACTS_DIR, dest))
