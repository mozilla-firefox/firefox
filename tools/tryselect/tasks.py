# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import gc
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict

import mozpack.path as mozpath
import taskgraph
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozbuild.util import construct_log_filename
from mozpack.files import FileFinder
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor
from moztest.resolve import TestManifestLoader, TestResolver, get_suite_definition
from taskgraph.generator import TaskGraphGenerator, load_graph_config
from taskgraph.parameters import ParameterMismatch, parameters_loader
from taskgraph.taskgraph import TaskGraph
from taskgraph.util import json
from taskgraph.util.vcs import get_repository

from tryselect.util.project import get_project_topsrcdir

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)


def comm_parameter_overrides(comm_topsrcdir):
    """Parameters required to generate the comm taskgraph locally.

    The comm_* parameters have no defaults (they are normally injected by the
    decision task), so derive head/base from the comm checkout and hardcode the
    repositories for try."""
    repo = get_repository(comm_topsrcdir)
    rev = repo.head_rev
    ref = repo.branch or rev
    src_path = os.path.relpath(comm_topsrcdir, build.topsrcdir).replace(os.sep, "/")
    return {
        "project": "try-comm-central",
        "comm_head_repository": "https://hg.mozilla.org/comm-central",
        "comm_head_rev": rev,
        "comm_head_ref": ref,
        "comm_base_repository": "https://hg.mozilla.org/comm-central",
        "comm_base_rev": rev,
        "comm_base_ref": ref,
        "comm_src_path": f"{src_path}/",
        "message": "",
    }


PARAMETER_MISMATCH = """
ERROR - The parameters being used to generate tasks differ from those expected
by your working copy:

    {}

To fix this, either rebase onto the latest mozilla-central or pass in
-p/--parameters. For more information on how to define parameters, see
the --parameters argument in `./mach taskgraph target --help`.
"""


def invalidate(cache):
    try:
        cmod = os.path.getmtime(cache)
    except OSError as e:
        # File does not exist. We catch OSError rather than use `isfile`
        # because the recommended watchman hook could possibly invalidate the
        # cache in-between the check to `isfile` and the call to `getmtime`
        # below.
        if e.errno == 2:
            return
        raise

    tc_dir = os.path.join(build.topsrcdir, "taskcluster")
    tmod = max(os.path.getmtime(os.path.join(tc_dir, p)) for p, _ in FileFinder(tc_dir))

    if tmod > cmod:
        os.remove(cache)


WATCHMAN_TRIGGER_NAME = "rebuild-taskgraph-cache"

WATCHMAN_HINT = """\
Tip: this ~20s wait happens whenever a file under taskcluster/ changes (e.g.
after pulling firefox-main or switching branches). watchman can rebuild the
taskgraph cache in the background so `mach try` stays fast. Since watchman is
already watching this checkout, enable it with:

    {command}

See https://firefox-source-docs.mozilla.org/tools/try/tasks.html for details.
"""


def suggest_watchman_setup():
    """On an interactive cache miss, nudge the user toward the watchman trigger
    that keeps the taskgraph cache warm in the background.

    Only shown when stdout is a terminal (so the background watchman trigger and
    CI runs stay silent), watchman is installed and already watching the
    checkout, and the trigger is not already registered."""
    if not sys.stdout.isatty():
        return

    watchman = shutil.which("watchman")
    if not watchman:
        return

    try:
        proc = subprocess.run(
            [watchman, "trigger-list", build.topsrcdir],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return

    # Only suggest when watchman is already watching this checkout (a failed
    # `trigger-list` means it is not) and our trigger is not registered yet, so
    # we never provoke inotify-limit issues by watching the firefox checkout here.
    if proc.returncode != 0 or WATCHMAN_TRIGGER_NAME in proc.stdout:
        return

    watchman_json = os.path.join(here, "watchman.json")
    # cmd and PowerShell do not set SHELL.
    if sys.platform == "win32" and "SHELL" not in os.environ:
        command = f'more "{watchman_json}" | watchman -j'
    else:
        command = f'watchman -j < "{mozpath.normsep(watchman_json)}"'
    print(WATCHMAN_HINT.format(command=command))


def write_profile(monitor):
    log_subdir = os.path.join("logs", "try")
    build._ensure_state_subdir_exists(log_subdir)
    profile_path = build._get_state_filename(
        construct_log_filename("profile"), subdir=log_subdir
    )
    with open(profile_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(monitor.as_profile(), f)


def cache_key(attr, params, disable_target_task_filter, target_tasks_method):
    key = attr
    if params and params["project"] not in ("autoland", "mozilla-central"):
        key += f"-{params['project']}"

    if disable_target_task_filter and "full" not in attr:
        key += "-uncommon"

    if target_tasks_method:
        key += f"-target_{target_tasks_method}"

    return key


def add_chunk_patterns(tg):
    for task_name, task in tg.tasks.items():
        chunk_index = -1
        if task_name.endswith("-cf"):
            chunk_index = -2

        chunks = task.task.get("extra", {}).get("chunks", {})
        if isinstance(chunks, int):
            task.chunk_pattern = "{}-*/{}".format(
                "-".join(task_name.split("-")[:chunk_index]), chunks
            )
        else:
            assert isinstance(chunks, dict)
            if chunks.get("total", 1) == 1:
                task.chunk_pattern = task_name
            else:
                task.chunk_pattern = "{}-*".format(
                    "-".join(task_name.split("-")[:chunk_index])
                )
    return tg


def generate_tasks(
    param_spec=None,
    full=False,
    disable_target_task_filter=False,
    target_tasks_method=None,
):
    attr = "full_task_set" if full else "target_task_set"

    overrides = {
        "filters": [
            (
                "try_select_tasks"
                if not disable_target_task_filter
                else "try_select_tasks_uncommon"
            )
        ],
        "try_mode": "try_select",
    }

    # If a separate target_tasks_method was requested, pre-filter the available
    # list of tasks based on that.
    if target_tasks_method:
        overrides["target_tasks_method"] = target_tasks_method
        overrides["filters"].insert(0, "target_tasks_method")

    project_topsrcdir = get_project_topsrcdir(build)
    comm = project_topsrcdir != build.topsrcdir
    if comm:
        overrides.update(comm_parameter_overrides(project_topsrcdir))
        root = os.path.join(project_topsrcdir, "taskcluster")
    else:
        root = os.path.join(build.topsrcdir, "taskcluster")

    params = parameters_loader(param_spec, strict=False, overrides=overrides)
    taskgraph.fast = True

    if comm:
        # The generator only runs the root graph_config's register hook
        # (comm_taskgraph:register). Gecko's extensions (try_select_tasks
        # filter, target task methods, transforms) must be registered first,
        # mirroring the comm decision task in taskcluster/mach_commands.py.
        from gecko_taskgraph import register as register_gecko_taskgraph

        register_gecko_taskgraph(load_graph_config(root))

    generator = TaskGraphGenerator(
        root_dir=root, parameters=params, enable_verifications=False
    )

    cache_dir = os.path.join(
        get_state_dir(specific_to_topsrcdir=True), "cache", "taskgraph"
    )
    key = cache_key(
        attr, generator.parameters, disable_target_task_filter, target_tasks_method
    )
    cache = os.path.join(cache_dir, key)

    invalidate(cache)
    if os.path.isfile(cache):
        with open(cache) as fh:
            return add_chunk_patterns(TaskGraph.from_json(json.load(fh))[1])

    if not os.path.isdir(cache_dir):
        os.makedirs(cache_dir)

    print("Task configuration changed, generating {}".format(attr.replace("_", " ")))
    suggest_watchman_setup()

    cwd = os.getcwd()
    os.chdir(build.topsrcdir)

    def generate(attr):
        try:
            tg = getattr(generator, attr)
        except ParameterMismatch as e:
            print(PARAMETER_MISMATCH.format(e.args[0]))
            sys.exit(1)

        # The generated graph is long-lived and taskgraph collects no garbage
        # while generating it, so a collection triggered while serializing it
        # would walk the whole heap for little gain.
        gc.freeze()

        # write cache
        key = cache_key(
            attr, generator.parameters, disable_target_task_filter, target_tasks_method
        )
        SystemResourceMonitor.begin_marker("Taskgraph", f"write {attr} cache")
        with open(os.path.join(cache_dir, key), "w") as fh:
            json.dump(tg.to_json(), fh)
        SystemResourceMonitor.end_marker("Taskgraph", f"write {attr} cache")
        return add_chunk_patterns(tg)

    monitor = None
    if not SystemResourceMonitor.instance:
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        fd, forward_path = tempfile.mkstemp(prefix="taskgraph-", suffix=".jsonl")
        os.close(fd)
        monitor.accept_forwarded(forward_path)

    try:
        # Cache both full_task_set and target_task_set regardless of whether or
        # not --full was requested. Caching is cheap and can potentially save a
        # lot of time.
        tg_full = generate("full_task_set")
        tg_target = generate("target_task_set")
    finally:
        if monitor:
            monitor.stop()
            os.remove(forward_path)
            write_profile(monitor)

    os.chdir(cwd)
    if full:
        return tg_full
    return tg_target


def filter_tasks_by_worker_type(tasks, params):
    worker_types = params.get("try_task_config", {}).get("worker-types", [])
    if worker_types:
        retVal = {}
        for t in tasks:
            if tasks[t].task["workerType"] in worker_types:
                retVal[t] = tasks[t]
        return retVal
    return tasks


def filter_tasks_by_paths(tasks, paths=[], tag=""):
    resolver = TestResolver.from_environment(cwd=here, loader_cls=TestManifestLoader)

    if paths:
        run_suites, run_tests = resolver.resolve_metadata(paths)
    elif not paths and tag:
        run_tests = list(resolver.resolve_tests(paths=[], tags=tag))

    if not run_tests:
        return {}

    flavors = {(t["flavor"], t.get("subsuite")) for t in run_tests}

    task_regexes = set()
    for flavor, subsuite in flavors:
        _, suite = get_suite_definition(flavor, subsuite, strict=True)
        if "task_regex" not in suite:
            print(
                "warning: no tasks could be resolved from flavor '{}'{}".format(
                    flavor, f" and subsuite '{subsuite}'" if subsuite else ""
                )
            )
            continue

        task_regexes.update(suite["task_regex"])

    def match_task(task):
        return any(re.search(pattern, task) for pattern in task_regexes)

    return {
        task_name: task for task_name, task in tasks.items() if match_task(task_name)
    }


def resolve_tests_by_suite(paths):
    resolver = TestResolver.from_environment(cwd=here, loader_cls=TestManifestLoader)
    _, run_tests = resolver.resolve_metadata(paths)

    suite_to_tests = defaultdict(list)

    # A dictionary containing all the input paths that we haven't yet
    # assigned to a specific test flavor.
    remaining_paths_by_suite = defaultdict(lambda: set(paths))

    for test in run_tests:
        key, _ = get_suite_definition(test["flavor"], test.get("subsuite"), strict=True)

        test_path = test.get("srcdir_relpath")
        if test_path is None:
            continue
        found_path = None
        manifest_relpath = None
        if "manifest_relpath" in test:
            manifest_relpath = mozpath.normpath(test["manifest_relpath"])
        for path in remaining_paths_by_suite[key]:
            if test_path.startswith(path) or manifest_relpath == path:
                found_path = path
                break
        if found_path:
            suite_to_tests[key].append(found_path)
            remaining_paths_by_suite[key].remove(found_path)

    return suite_to_tests
