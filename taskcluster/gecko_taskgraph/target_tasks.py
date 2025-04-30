# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import itertools
import logging
import os
import re
from datetime import datetime, timedelta

import requests
from redo import retry
from taskgraph import create
from taskgraph.target_tasks import register_target_task
from taskgraph.util.attributes import attrmatch
from taskgraph.util.parameterization import resolve_timestamps
from taskgraph.util.taskcluster import (
    find_task_id,
    get_artifact,
    get_task_definition,
    parse_time,
)
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph import GECKO, try_option_syntax
from gecko_taskgraph.util.attributes import (
    is_try,
    match_run_on_hg_branches,
    match_run_on_projects,
)
from gecko_taskgraph.util.hg import find_hg_revision_push_info, get_hg_commit_message
from gecko_taskgraph.util.platforms import platform_family
from gecko_taskgraph.util.taskcluster import find_task, insert_index

logger = logging.getLogger(__name__)


# Some tasks show up in the target task set, but are possibly special cases,
# uncommon tasks, or tasks running against limited hardware set that they
# should only be selectable with --full.
UNCOMMON_TRY_TASK_LABELS = [
    # Platforms and/or Build types
    r"build-.*-gcp",  # Bug 1631990
    r"mingwclang",  # Bug 1631990
    r"valgrind",  # Bug 1631990
    # Android tasks
    r"android-geckoview-docs",
    r"android-hw",
    # Windows tasks
    r"windows11-64-2009-hw-ref",
    r"windows11-64-24h2-hw-ref",
    r"windows10-aarch64-qr",
    # Linux tasks
    r"linux-",  # hide all linux32 tasks by default - bug 1599197
    r"linux1804-32",  # hide linux32 tests - bug 1599197
    # Test tasks
    r"web-platform-tests.*backlog",  # hide wpt jobs that are not implemented yet - bug 1572820
    r"-ccov",
    r"-profiling-",  # talos/raptor profiling jobs are run too often
    r"-32-.*-webgpu",  # webgpu gets little benefit from these tests.
    r"-asan-.*-webgpu",
    r"-tsan-.*-webgpu",
    # Hide shippable versions of tests we have opt versions of because the non-shippable
    # versions are faster to run. This is mostly perf tests.
    r"-shippable(?!.*(awsy|browsertime|marionette-headless|mochitest-devtools-chrome-fis|raptor|talos|web-platform-tests-wdspec-headless|mochitest-plain-headless))",  # noqa - too long
    r"nightly-simulation",
    # Can't actually run on try
    r"notarization",
]


def index_exists(index_path, reason=""):
    print(f"Looking for existing index {index_path} {reason}...")
    try:
        task_id = find_task_id(index_path)
        print(f"Index {index_path} exists: taskId {task_id}")
        return True
    except KeyError:
        print(f"Index {index_path} doesn't exist.")
        return False


def filter_out_shipping_phase(task, parameters):
    return (
        # nightly still here because of geckodriver
        not task.attributes.get("nightly")
        and task.attributes.get("shipping_phase") in (None, "build")
    )


def filter_out_devedition(task, parameters):
    return not task.attributes.get("shipping_product") == "devedition"


def filter_out_cron(task, parameters):
    """
    Filter out tasks that run via cron.
    """
    return not task.attributes.get("cron")


def filter_for_project(task, parameters):
    """Filter tasks by project.  Optionally enable nightlies."""
    run_on_projects = set(task.attributes.get("run_on_projects", []))
    return match_run_on_projects(parameters["project"], run_on_projects)


def filter_for_hg_branch(task, parameters):
    """Filter tasks by hg branch.
    If `run_on_hg_branch` is not defined, then task runs on all branches"""
    run_on_hg_branches = set(task.attributes.get("run_on_hg_branches", ["all"]))
    return match_run_on_hg_branches(parameters["hg_branch"], run_on_hg_branches)


def filter_on_platforms(task, platforms):
    """Filter tasks on the given platform"""
    platform = task.attributes.get("build_platform")
    return platform in platforms


def filter_by_uncommon_try_tasks(task, optional_filters=None):
    """Filters tasks that should not be commonly run on try.

    Args:
        task (str): String representing the task name.
        optional_filters (list, optional):
            Additional filters to apply to task filtering.

    Returns:
        (Boolean): True if task does not match any known filters.
            False otherwise.
    """
    filters = UNCOMMON_TRY_TASK_LABELS
    if optional_filters:
        filters = itertools.chain(filters, optional_filters)

    return not any(re.search(pattern, task) for pattern in filters)


def filter_by_regex(task_label, regexes, mode="include"):
    """Filters tasks according to a list of pre-compiled reguar expressions.

    If mode is "include", a task label must match any regex to pass.
    If it is "exclude", a task label must _not_ match any regex to pass.
    """
    if not regexes:
        return True

    assert mode in ["include", "exclude"]

    any_match = any(r.search(task_label) for r in regexes)
    if any_match:
        return mode == "include"
    return mode != "include"


def filter_release_tasks(task, parameters):
    platform = task.attributes.get("build_platform")
    if platform in (
        "linux",
        "linux64",
        "linux64-aarch64",
        "macosx64",
        "win32",
        "win64",
        "win64-aarch64",
    ):
        if task.attributes["kind"] == "l10n":
            # This is on-change l10n
            return True
        if (
            task.attributes["build_type"] == "opt"
            and task.attributes.get("unittest_suite") != "talos"
            and task.attributes.get("unittest_suite") != "raptor"
        ):
            return False

    if task.attributes.get("shipping_phase") not in (None, "build"):
        return False

    """ No debug on release, keep on ESR with 4 week cycles, release
    will not be too different from central, but ESR will live for a long time.

    From June 2019 -> June 2020, we found 1 unique regression on ESR debug
    and 5 unique regressions on beta/release.  Keeping spidermonkey and linux
    debug finds all but 1 unique regressions (windows found on try) for beta/release.

    ...but debug-only failures started showing up on ESR (esr-91, esr-102) so
    desktop debug tests were added back for beta.
    """
    build_type = task.attributes.get("build_type", "")
    build_platform = task.attributes.get("build_platform", "")
    test_platform = task.attributes.get("test_platform", "")

    if parameters["release_type"].startswith("esr") or (
        parameters["release_type"] == "beta" and "android" not in build_platform
    ):
        return True

    # code below here is intended to reduce release debug tasks
    if task.kind == "hazard" or "toolchain" in build_platform:
        # keep hazard and toolchain builds around
        return True

    if build_type == "debug":
        if "linux" not in build_platform:
            # filter out windows/mac/android
            return False
        if task.kind not in ["spidermonkey"] and "-qr" in test_platform:
            # filter out linux-qr tests, leave spidermonkey
            return False
        if "64" not in build_platform:
            # filter out linux32 builds
            return False

    # webrender-android-*-debug doesn't have attributes to find 'debug', using task.label.
    if task.kind == "webrender" and "debug" in task.label:
        return False
    return True


def filter_out_missing_signoffs(task, parameters):
    for signoff in parameters["required_signoffs"]:
        if signoff not in parameters["signoff_urls"] and signoff in task.attributes.get(
            "required_signoffs", []
        ):
            return False
    return True


def filter_tests_without_manifests(task, parameters):
    """Remove test tasks that have an empty 'test_manifests' attribute.

    This situation can arise when the test loader (e.g bugbug) decided there
    weren't any important manifests to run for the given push. We filter tasks
    out here rather than in the transforms so that the full task graph is still
    aware that the task exists (which is needed by the backfill action).
    """
    if (
        task.kind == "test"
        and "test_manifests" in task.attributes
        and not task.attributes["test_manifests"]
    ):
        return False
    return True


def standard_filter(task, parameters):
    return all(
        filter_func(task, parameters)
        for filter_func in (
            filter_out_cron,
            filter_for_project,
            filter_for_hg_branch,
            filter_tests_without_manifests,
        )
    )


def accept_raptor_android_build(platform):
    """Helper function for selecting the correct android raptor builds."""
    if "android" not in platform:
        return False
    if "shippable" not in platform:
        return False
    if "p5" in platform and "aarch64" in platform:
        return False
    if "p6" in platform and "aarch64" in platform:
        return True
    if "s24" in platform and "aarch64" in platform:
        return True
    if "a55" in platform and "aarch64" in platform:
        return True
    return False


def accept_raptor_desktop_build(platform):
    """Helper function for selecting correct desktop raptor builds."""
    if "android" in platform:
        return False
    # ignore all windows 7 perf jobs scheduled automatically
    if "windows7" in platform or "windows10-32" in platform:
        return False
    # Completely ignore all non-shippable platforms
    if "shippable" in platform:
        return True
    return False


def accept_awsy_task(try_name, platform):
    if accept_raptor_desktop_build(platform):
        if "windows" in platform and "windows11-64" not in platform:
            return False
        if "dmd" in try_name:
            return False
        if "awsy-base" in try_name:
            return True
        if "awsy-tp6" in try_name:
            return True
    return False


def filter_unsupported_artifact_builds(task, parameters):
    try_config = parameters.get("try_task_config", {})
    if not try_config.get("use-artifact-builds", False):
        return True

    supports_artifact_builds = task.attributes.get("supports-artifact-builds", True)
    return supports_artifact_builds


def filter_out_shippable(task):
    return not task.attributes.get("shippable", False)


def _try_task_config(full_task_graph, parameters, graph_config):
    requested_tasks = parameters["try_task_config"]["tasks"]
    pattern_tasks = [x for x in requested_tasks if x.endswith("-*")]
    tasks = list(set(requested_tasks) - set(pattern_tasks))
    matched_tasks = []
    missing = set()
    for pattern in pattern_tasks:
        found = [
            t
            for t in full_task_graph.graph.nodes
            if t.split(pattern.replace("*", ""))[-1].isnumeric()
        ]
        if found:
            matched_tasks.extend(found)
        else:
            missing.add(pattern)

        if "MOZHARNESS_TEST_PATHS" in parameters["try_task_config"].get("env", {}):
            matched_tasks = [x for x in matched_tasks if x.endswith("-1")]

        if "MOZHARNESS_TEST_TAG" in parameters["try_task_config"].get("env", {}):
            matched_tasks = [x for x in matched_tasks if x.endswith("-1")]

    selected_tasks = set(tasks) | set(matched_tasks)
    missing.update(selected_tasks - set(full_task_graph.tasks))

    if missing:
        missing_str = "\n  ".join(sorted(missing))
        logger.warning(
            f"The following tasks were requested but do not exist in the full task graph and will be skipped:\n  {missing_str}"
        )
    return list(selected_tasks - missing)


def _try_option_syntax(full_task_graph, parameters, graph_config):
    """Generate a list of target tasks based on try syntax in
    parameters['message'] and, for context, the full task graph."""
    options = try_option_syntax.TryOptionSyntax(
        parameters, full_task_graph, graph_config
    )
    target_tasks_labels = [
        t.label
        for t in full_task_graph.tasks.values()
        if options.task_matches(t)
        and filter_by_uncommon_try_tasks(t.label)
        and filter_unsupported_artifact_builds(t, parameters)
    ]

    attributes = {
        k: getattr(options, k)
        for k in [
            "no_retry",
            "tag",
        ]
    }

    for l in target_tasks_labels:
        task = full_task_graph[l]
        if "unittest_suite" in task.attributes:
            task.attributes["task_duplicates"] = options.trigger_tests

    for l in target_tasks_labels:
        task = full_task_graph[l]
        # If the developer wants test jobs to be rebuilt N times we add that value here
        if options.trigger_tests > 1 and "unittest_suite" in task.attributes:
            task.attributes["task_duplicates"] = options.trigger_tests

        # If the developer wants test talos jobs to be rebuilt N times we add that value here
        if (
            options.talos_trigger_tests > 1
            and task.attributes.get("unittest_suite") == "talos"
        ):
            task.attributes["task_duplicates"] = options.talos_trigger_tests

        # If the developer wants test raptor jobs to be rebuilt N times we add that value here
        if (
            options.raptor_trigger_tests
            and options.raptor_trigger_tests > 1
            and task.attributes.get("unittest_suite") == "raptor"
        ):
            task.attributes["task_duplicates"] = options.raptor_trigger_tests

        task.attributes.update(attributes)

    # Add notifications here as well
    if options.notifications:
        for task in full_task_graph:
            owner = parameters.get("owner")
            routes = task.task.setdefault("routes", [])
            if options.notifications == "all":
                routes.append(f"notify.email.{owner}.on-any")
            elif options.notifications == "failure":
                routes.append(f"notify.email.{owner}.on-failed")
                routes.append(f"notify.email.{owner}.on-exception")

    return target_tasks_labels


@register_target_task("try_tasks")
def target_tasks_try(full_task_graph, parameters, graph_config):
    try_mode = parameters["try_mode"]
    if try_mode == "try_task_config":
        return _try_task_config(full_task_graph, parameters, graph_config)
    if try_mode == "try_option_syntax":
        return _try_option_syntax(full_task_graph, parameters, graph_config)
    # With no try mode, we schedule nothing, allowing the user to add tasks
    # later via treeherder.
    return []


@register_target_task("default")
def target_tasks_default(full_task_graph, parameters, graph_config):
    """Target the tasks which have indicated they should be run on this project
    via the `run_on_projects` attributes."""
    return [
        l
        for l, t in full_task_graph.tasks.items()
        if standard_filter(t, parameters)
        and filter_out_shipping_phase(t, parameters)
        and filter_out_devedition(t, parameters)
    ]


@register_target_task("autoland_tasks")
def target_tasks_autoland(full_task_graph, parameters, graph_config):
    """In addition to doing the filtering by project that the 'default'
    filter does, also remove any tests running against shippable builds
    for non-backstop pushes."""
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        if task.kind != "test":
            return True

        if parameters["backstop"]:
            return True

        build_type = task.attributes.get("build_type")

        if not build_type or build_type != "opt" or filter_out_shippable(task):
            return True

        return False

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("mozilla_central_tasks")
def target_tasks_mozilla_central(full_task_graph, parameters, graph_config):
    """In addition to doing the filtering by project that the 'default'
    filter does, also remove any tests running against regular (aka not shippable,
    asan, etc.) opt builds."""
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        if task.kind != "test":
            return True

        build_platform = task.attributes.get("build_platform")
        build_type = task.attributes.get("build_type")
        shippable = task.attributes.get("shippable", False)

        if not build_platform or not build_type:
            return True

        family = platform_family(build_platform)
        # We need to know whether this test is against a "regular" opt build
        # (which is to say, not shippable, asan, tsan, or any other opt build
        # with other properties). There's no positive test for this, so we have to
        # do it somewhat hackily. Android doesn't have variants other than shippable
        # so it is pretty straightforward to check for. Other platforms have many
        # variants, but none of the regular opt builds we're looking for have a "-"
        # in their platform name, so this works (for now).
        is_regular_opt = (
            family == "android" and not shippable
        ) or "-" not in build_platform

        if build_type != "opt" or not is_regular_opt:
            return True

        return False

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("graphics_tasks")
def target_tasks_graphics(full_task_graph, parameters, graph_config):
    """In addition to doing the filtering by project that the 'default'
    filter does, also remove artifact builds because we have csets on
    the graphics branch that aren't on the candidate branches of artifact
    builds"""
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        if task.attributes["kind"] == "artifact-build":
            return False
        return True

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("mozilla_beta_tasks")
def target_tasks_mozilla_beta(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a promotable beta or release build
    of desktop, plus android CI. The candidates build process involves a pipeline
    of builds and signing, but does not include beetmover or balrog jobs."""

    return [
        l
        for l, t in full_task_graph.tasks.items()
        if filter_release_tasks(t, parameters) and standard_filter(t, parameters)
    ]


@register_target_task("mozilla_release_tasks")
def target_tasks_mozilla_release(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a promotable beta or release build
    of desktop, plus android CI. The candidates build process involves a pipeline
    of builds and signing, but does not include beetmover or balrog jobs."""

    return [
        l
        for l, t in full_task_graph.tasks.items()
        if filter_release_tasks(t, parameters) and standard_filter(t, parameters)
    ]


@register_target_task("mozilla_esr128_tasks")
def target_tasks_mozilla_esr128(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a promotable beta or release build
    of desktop, without android CI. The candidates build process involves a pipeline
    of builds and signing, but does not include beetmover or balrog jobs."""

    def filter(task):
        if not filter_release_tasks(task, parameters):
            return False

        if not standard_filter(task, parameters):
            return False

        platform = task.attributes.get("build_platform")

        # Android is not built on esr.
        if platform and "android" in platform:
            return False

        return True

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("promote_desktop")
def target_tasks_promote_desktop(full_task_graph, parameters, graph_config):
    """Select the superset of tasks required to promote a beta or release build
    of a desktop product. This should include all non-android
    mozilla_{beta,release} tasks, plus l10n, beetmover, balrog, etc."""

    def filter(task):
        if task.attributes.get("shipping_product") != parameters["release_product"]:
            return False

        # 'secondary' balrog/update verify/final verify tasks only run for RCs
        if parameters.get("release_type") != "release-rc":
            if "secondary" in task.kind:
                return False

        if not filter_out_missing_signoffs(task, parameters):
            return False

        if task.attributes.get("shipping_phase") == "promote":
            return True

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("push_desktop")
def target_tasks_push_desktop(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to push a build of desktop to cdns.
    Previous build deps will be optimized out via action task."""
    filtered_for_candidates = target_tasks_promote_desktop(
        full_task_graph,
        parameters,
        graph_config,
    )

    def filter(task):
        if not filter_out_missing_signoffs(task, parameters):
            return False
        # Include promotion tasks; these will be optimized out
        if task.label in filtered_for_candidates:
            return True

        if (
            task.attributes.get("shipping_product") == parameters["release_product"]
            and task.attributes.get("shipping_phase") == "push"
        ):
            return True

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("ship_desktop")
def target_tasks_ship_desktop(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to ship desktop.
    Previous build deps will be optimized out via action task."""
    is_rc = parameters.get("release_type") == "release-rc"
    if is_rc:
        # ship_firefox_rc runs after `promote` rather than `push`; include
        # all promote tasks.
        filtered_for_candidates = target_tasks_promote_desktop(
            full_task_graph,
            parameters,
            graph_config,
        )
    else:
        # ship_firefox runs after `push`; include all push tasks.
        filtered_for_candidates = target_tasks_push_desktop(
            full_task_graph,
            parameters,
            graph_config,
        )

    def filter(task):
        if not filter_out_missing_signoffs(task, parameters):
            return False
        # Include promotion tasks; these will be optimized out
        if task.label in filtered_for_candidates:
            return True

        if (
            task.attributes.get("shipping_product") != parameters["release_product"]
            or task.attributes.get("shipping_phase") != "ship"
        ):
            return False

        if "secondary" in task.kind:
            return is_rc
        return not is_rc

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("cypress_tasks")
def target_tasks_cypress(full_task_graph, parameters, graph_config):
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        # bug 1899403: no need for android tasks
        if "android" in task.attributes.get("build_platform", ""):
            return False
        return True

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("pine_tasks")
def target_tasks_pine(full_task_graph, parameters, graph_config):
    """Bug 1879960 - no reftests or wpt needed"""
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        if "android" in task.attributes.get("build_platform", ""):
            return False
        suite = task.attributes.get("unittest_suite", "")
        if "reftest" in suite or "web-platform" in suite:
            return False
        return True

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("larch_tasks")
def target_tasks_larch(full_task_graph, parameters, graph_config):
    """Bug 1879213 - only run necessary tasks on larch"""
    filtered_for_project = target_tasks_default(
        full_task_graph, parameters, graph_config
    )

    def filter(task):
        # no localized builds, no android
        if (
            "l10n" in task.kind
            or "msix" in task.kind
            or "android" in task.attributes.get("build_platform", "")
            or (task.kind == "test" and "msix" in task.label)
        ):
            return False
        # otherwise reduce tests only
        if task.kind != "test":
            return True
        return "browser-chrome" in task.label or "xpcshell" in task.label

    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@register_target_task("kaios_tasks")
def target_tasks_kaios(full_task_graph, parameters, graph_config):
    """The set of tasks to run for kaios integration"""

    def filter(task):
        # We disable everything in central, and adjust downstream.
        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("custom-car_perf_testing")
def target_tasks_custom_car_perf_testing(full_task_graph, parameters, graph_config):
    """Select tasks required for running daily performance tests for custom chromium-as-release."""

    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        if attributes.get("unittest_suite") != "raptor":
            return False

        try_name = attributes.get("raptor_try_name")

        if "network-bench" in try_name:
            return False

        # Desktop and Android selection for CaR
        if accept_raptor_desktop_build(platform):
            if "browsertime" in try_name and "custom-car" in try_name:
                # Bug 1898514: avoid tp6m or non-essential tp6 jobs in cron
                if "tp6" in try_name and "essential" not in try_name:
                    return False
                # Bug 1928416
                # For ARM coverage, this will only run on M2 machines at the moment.
                if "jetstream2" in try_name:
                    # Bug 1947649 - Disable js2 on 1400 mac for custom-car due to near perma
                    if "m-car" in try_name and "1400" in platform:
                        return False
                    return True
                return True
        elif accept_raptor_android_build(platform):
            if "browsertime" in try_name and "cstm-car-m" in try_name:
                if "-nofis" not in try_name:
                    return False
                if "hw-s24" in platform and "speedometer3" not in try_name:
                    return False
                if "jetstream2" in try_name:
                    return True
                # Bug 1954124 - Don't run JS3 + Android on a cron yet.
                if "jetstream3" in try_name:
                    return False
                # Bug 1898514 - Avoid tp6m or non-essential tp6 jobs in cron on non-a55 platform
                # Bug 1961831 - Disable pageload tests temporarily during provider switch
                if "tp6m" in try_name:
                    return False
                # Bug 1945165 - Disable ebay-kleinanzeigen on cstm-car-m because of permafail
                if (
                    "ebay-kleinanzeigen" in try_name
                    and "ebay-kleinanzeigen-search" not in try_name
                ):
                    return False
                return True
        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("general_perf_testing")
def target_tasks_general_perf_testing(full_task_graph, parameters, graph_config):
    """
    Select tasks required for running performance tests 3 times a week.
    """

    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        if attributes.get("unittest_suite") != "raptor":
            return False

        try_name = attributes.get("raptor_try_name")

        if "tp6-bench" in try_name:
            return False

        if "tp7" in try_name:
            return False

        # Bug 1867669 - Temporarily disable all live site tests
        if "live" in try_name and "sheriffed" not in try_name:
            return False

        if "network-bench" in try_name:
            return False

        # Desktop selection
        if accept_raptor_desktop_build(platform):
            # Select some browsertime tasks as desktop smoke-tests
            if "responsiveness" in try_name and "chrome" in try_name:
                # Disabled chrome responsiveness tests temporarily in bug 1898351
                # due to frequent failures
                return False
            if "browsertime" in try_name:
                if "chrome" in try_name:
                    if "tp6" in try_name and "essential" not in try_name:
                        return False
                    return True
                # chromium-as-release has its own cron
                if "custom-car" in try_name:
                    return False
                if "-live" in try_name:
                    return True
                if "-fis" in try_name:
                    return False
                if "linux" in platform:
                    if "speedometer" in try_name:
                        return True
                if "safari" and "benchmark" in try_name:
                    # Bug 1954202 Safari + JS3 seems to be perma failing on CI.
                    if "jetstream3" in try_name and "safari-tp" not in try_name:
                        return False
                    if "jetstream2" in try_name and "safari" in try_name:
                        return False
                    return True
        # Android selection
        elif accept_raptor_android_build(platform):
            if "hw-s24" in platform and "speedometer3" not in try_name:
                return False
            if "chrome-m" in try_name and "-nofis" not in try_name:
                return False
            # Bug 1929960 - Enable all chrome-m tp6m tests on a55 only
            if "chrome-m" in try_name and "tp6m" in try_name and "hw-a55" in platform:
                # Bug 1954923 - Disable ebay-kleinanzeigen
                if (
                    "ebay-kleinanzeigen" in try_name
                    and "search" not in try_name
                    and "nofis" in try_name
                ):
                    return False
                return True
            if "chrome-m" in try_name and (
                ("ebay" in try_name and "live" not in try_name)
                or (
                    "live" in try_name
                    and ("facebook" in try_name or "dailymail" in try_name)
                )
            ):
                return False
            # Ignore all fennec tests here, we run those weekly
            if "fennec" in try_name:
                return False
            # Select live site tests
            if "-live" in try_name:
                return True
            # Select fenix resource usage tests
            if "fenix" in try_name:
                if "-power" in try_name:
                    return True

            if "geckoview" in try_name:
                return False
            # Select browsertime-specific tests
            if "browsertime" in try_name:
                # Don't run android CaR sp tests as we already have a cron for this.
                if "m-car" in try_name:
                    return False
                if "jetstream2" in try_name:
                    return True
                # Bug 1954124 - Don't run JS3 + Android on a cron yet.
                if "jetstream3" in try_name:
                    return False
                if "fenix" in try_name:
                    return False
                if "speedometer" in try_name:
                    return True
                if "motionmark" in try_name and "1-3" in try_name:
                    if "chrome-m" in try_name:
                        return True
        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("geckoview-perftest")
def target_tasks_geckoview_perftest(full_task_graph, parameters, graph_config):
    """
    Select tasks required for running geckoview tests 2 times a week.
    """

    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        if attributes.get("unittest_suite") != "raptor":
            return False

        if accept_raptor_android_build(platform):
            try_name = attributes.get("raptor_try_name")
            if "geckoview" in try_name and "browsertime" in try_name:
                if "hw-s24" in platform and "speedometer" not in try_name:
                    return False
                if "live" in try_name and "cnn-amp" not in try_name:
                    return False
                return True

        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


def make_desktop_nightly_filter(platforms):
    """Returns a filter that gets all nightly tasks on the given platform."""

    def filter(task, parameters):
        return all(
            [
                filter_on_platforms(task, platforms),
                filter_for_project(task, parameters),
                task.attributes.get("shippable", False),
                # Tests and nightly only builds don't have `shipping_product` set
                task.attributes.get("shipping_product")
                in {None, "firefox", "thunderbird"},
                task.kind not in {"l10n"},  # no on-change l10n
            ]
        )

    return filter


@register_target_task("sp-perftests")
def target_tasks_speedometer_tests(full_task_graph, parameters, graph_config):
    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        if attributes.get("unittest_suite") != "raptor":
            return False

        try_name = attributes.get("raptor_try_name")
        if accept_raptor_desktop_build(platform):
            if (
                "browsertime" in try_name
                and "speedometer" in try_name
                and "chrome" in try_name
            ):
                return True
        if accept_raptor_android_build(platform):
            if "hw-s24" in platform and "speedometer3" not in try_name:
                return False
            if (
                "browsertime" in try_name
                and "speedometer" in try_name
                and "chrome-m" in try_name
            ):
                if "-nofis" not in try_name:
                    return False
                return True

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("nightly_linux")
def target_tasks_nightly_linux(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of linux. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter(
        {"linux64-shippable", "linux-shippable", "linux64-aarch64-shippable"}
    )
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("nightly_macosx")
def target_tasks_nightly_macosx(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of macosx. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter({"macosx64-shippable"})
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("nightly_win32")
def target_tasks_nightly_win32(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of win32 and win64.
    The nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter({"win32-shippable"})
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("nightly_win64")
def target_tasks_nightly_win64(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of win32 and win64.
    The nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter({"win64-shippable"})
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("nightly_win64_aarch64")
def target_tasks_nightly_win64_aarch64(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of win32 and win64.
    The nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter({"win64-aarch64-shippable"})
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("nightly_asan")
def target_tasks_nightly_asan(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of asan. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    filter = make_desktop_nightly_filter(
        {"linux64-asan-reporter-shippable", "win64-asan-reporter-shippable"}
    )
    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("daily_releases")
def target_tasks_daily_releases(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to identify if we should release.
    If we determine that we should the task will communicate to ship-it to
    schedule the release itself."""

    def filter(task):
        return task.kind in ["maybe-release"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("nightly_desktop")
def target_tasks_nightly_desktop(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of linux, mac,
    windows."""
    for platform in ("desktop", "all"):
        index_path = (
            f"{graph_config['trust-domain']}.v2.{parameters['project']}.revision."
            f"{parameters['head_rev']}.taskgraph.decision-nightly-{platform}"
        )
        if os.environ.get("MOZ_AUTOMATION") and retry(
            index_exists,
            args=(index_path,),
            kwargs={
                "reason": "to avoid triggering multiple nightlies off the same revision",
            },
        ):
            return []

    # Tasks that aren't platform specific
    release_filter = make_desktop_nightly_filter({None})
    release_tasks = [
        l for l, t in full_task_graph.tasks.items() if release_filter(t, parameters)
    ]
    # Avoid duplicate tasks.
    return list(
        set(target_tasks_nightly_win32(full_task_graph, parameters, graph_config))
        | set(target_tasks_nightly_win64(full_task_graph, parameters, graph_config))
        | set(
            target_tasks_nightly_win64_aarch64(
                full_task_graph, parameters, graph_config
            )
        )
        | set(target_tasks_nightly_macosx(full_task_graph, parameters, graph_config))
        | set(target_tasks_nightly_linux(full_task_graph, parameters, graph_config))
        | set(target_tasks_nightly_asan(full_task_graph, parameters, graph_config))
        | set(release_tasks)
    )


@register_target_task("nightly_all")
def target_tasks_nightly_all(full_task_graph, parameters, graph_config):
    """Select the set of tasks required for a nightly build of firefox desktop and android"""
    index_path = (
        f"{graph_config['trust-domain']}.v2.{parameters['project']}.revision."
        f"{parameters['head_rev']}.taskgraph.decision-nightly-all"
    )
    if os.environ.get("MOZ_AUTOMATION") and retry(
        index_exists,
        args=(index_path,),
        kwargs={
            "reason": "to avoid triggering multiple nightlies off the same revision",
        },
    ):
        return []

    return list(
        set(target_tasks_nightly_desktop(full_task_graph, parameters, graph_config))
        | set(target_tasks_nightly_android(full_task_graph, parameters, graph_config))
    )


# Run Searchfox analysis once daily.
@register_target_task("searchfox_index")
def target_tasks_searchfox(full_task_graph, parameters, graph_config):
    """Select tasks required for indexing Firefox for Searchfox web site each day"""
    index_path = (
        f"{graph_config['trust-domain']}.v2.{parameters['project']}.revision."
        f"{parameters['head_rev']}.searchfox-index"
    )
    if os.environ.get("MOZ_AUTOMATION"):
        print(
            f"Looking for existing index {index_path} to avoid triggering redundant indexing off the same revision..."
        )
        try:
            task = find_task(index_path)
            print(f"Index {index_path} exists: taskId {task['taskId']}")
        except requests.exceptions.HTTPError as e:
            if e.response.status_code != 404:
                raise
            print(f"Index {index_path} doesn't exist.")
        else:
            # Find the earlier expiration time of existing tasks
            taskdef = get_task_definition(task["taskId"])
            task_graph = get_artifact(task["taskId"], "public/task-graph.json")
            if task_graph:
                base_time = parse_time(taskdef["created"])
                first_expiry = min(
                    resolve_timestamps(base_time, t["task"]["expires"])
                    for t in task_graph.values()
                )
                expiry = parse_time(first_expiry)
                if expiry > datetime.utcnow() + timedelta(days=7):
                    print("Skipping index tasks")
                    return []
        if not create.testing:
            insert_index(index_path, os.environ["TASK_ID"], use_proxy=True)

    return [
        "searchfox-linux64-searchfox/debug",
        "searchfox-macosx64-searchfox/debug",
        "searchfox-macosx64-aarch64-searchfox/debug",
        "searchfox-win64-searchfox/debug",
        "searchfox-android-aarch64-searchfox/debug",
        "searchfox-ios-searchfox/debug",
        "source-test-file-metadata-bugzilla-components",
        "source-test-file-metadata-test-info-all",
        "source-test-wpt-metadata-summary",
    ]


# Run build linux64-plain-clang-trunk/opt on mozilla-central/beta with perf tests
@register_target_task("linux64_clang_trunk_perf")
def target_tasks_build_linux64_clang_trunk_perf(
    full_task_graph, parameters, graph_config
):
    """Select tasks required to run perf test on linux64 build with clang trunk"""

    # Only keep tasks generated from platform `linux1804-64-clang-trunk-qr/opt`
    def filter(task_label):
        if "linux1804-64-clang-trunk-qr/opt" in task_label and "live" not in task_label:
            return True
        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t.label)]


# Run Updatebot's cron job 4 times daily.
@register_target_task("updatebot_cron")
def target_tasks_updatebot_cron(full_task_graph, parameters, graph_config):
    """Select tasks required to run Updatebot's cron job"""
    return ["updatebot-cron"]


@register_target_task("customv8_update")
def target_tasks_customv8_update(full_task_graph, parameters, graph_config):
    """Select tasks required for building latest d8/v8 version."""
    return ["toolchain-linux64-custom-v8"]


@register_target_task("file_update")
def target_tasks_file_update(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to perform nightly in-tree file updates"""

    def filter(task):
        # For now any task in the repo-update kind is ok
        return task.kind in ["repo-update"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("l10n_bump")
def target_tasks_l10n_bump(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to perform l10n bumping."""

    def filter(task):
        # For now any task in the repo-update kind is ok
        return task.kind in ["l10n-bump"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("merge_automation")
def target_tasks_merge_automation(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to perform repository merges."""

    def filter(task):
        # For now any task in the repo-update kind is ok
        return task.kind in ["merge-automation"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("scriptworker_canary")
def target_tasks_scriptworker_canary(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to run scriptworker canaries."""

    def filter(task):
        # For now any task in the repo-update kind is ok
        return task.kind in ["scriptworker-canary"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("cron_bouncer_check")
def target_tasks_bouncer_check(full_task_graph, parameters, graph_config):
    """Select the set of tasks required to perform bouncer version verification."""

    def filter(task):
        if not filter_for_project(task, parameters):
            return False
        # For now any task in the repo-update kind is ok
        return task.kind in ["cron-bouncer-check"]

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


def _filter_by_release_project(parameters):
    project_by_release = {
        "nightly": "mozilla-central",
        "beta": "mozilla-beta",
        "release": "mozilla-release",
        "esr128": "mozilla-esr128",
    }
    target_project = project_by_release.get(parameters["release_type"])
    if target_project is None:
        raise Exception("Unknown or unspecified release type in simulation run.")

    def filter_for_target_project(task):
        """Filter tasks by project.  Optionally enable nightlies."""
        run_on_projects = set(task.attributes.get("run_on_projects", []))
        return match_run_on_projects(target_project, run_on_projects)

    return filter_for_target_project


def filter_out_android_on_esr(parameters, task):
    return not parameters["release_type"].startswith(
        "esr"
    ) or "android" not in task.attributes.get("build_platform", "")


@register_target_task("staging_release_builds")
def target_tasks_staging_release(full_task_graph, parameters, graph_config):
    """
    Select all builds that are part of releases.
    """
    filter_for_target_project = _filter_by_release_project(parameters)

    return [
        l
        for l, t in full_task_graph.tasks.items()
        if t.attributes.get("shipping_product")
        and filter_out_android_on_esr(parameters, t)
        and filter_for_target_project(t)
        and t.attributes.get("shipping_phase") == "build"
    ]


@register_target_task("release_simulation")
def target_tasks_release_simulation(full_task_graph, parameters, graph_config):
    """
    Select tasks that would run on push on a release branch.
    """
    filter_for_target_project = _filter_by_release_project(parameters)

    return [
        l
        for l, t in full_task_graph.tasks.items()
        if filter_release_tasks(t, parameters)
        and filter_out_cron(t, parameters)
        and filter_for_target_project(t)
        and filter_out_android_on_esr(parameters, t)
    ]


@register_target_task("codereview")
def target_tasks_codereview(full_task_graph, parameters, graph_config):
    """Select all code review tasks needed to produce a report"""

    def filter(task):
        # Ending tasks
        if task.kind in ["code-review"]:
            return True

        # Analyzer tasks
        if task.attributes.get("code-review") is True:
            return True

        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("nothing")
def target_tasks_nothing(full_task_graph, parameters, graph_config):
    """Select nothing, for DONTBUILD pushes"""
    return []


@register_target_task("daily_beta_perf")
def target_tasks_daily_beta_perf(full_task_graph, parameters, graph_config):
    """
    Select performance tests on the beta branch to be run daily
    """
    index_path = (
        f"{graph_config['trust-domain']}.v2.{parameters['project']}.revision."
        f"{parameters['head_rev']}.taskgraph.decision-daily-beta-perf"
    )
    if os.environ.get("MOZ_AUTOMATION") and retry(
        index_exists,
        args=(index_path,),
        kwargs={
            "reason": "to avoid triggering multiple daily beta perftests off of the same revision",
        },
    ):
        return []

    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        try_name = attributes.get("raptor_try_name") or task.label

        unittest_suite = attributes.get("unittest_suite")
        if unittest_suite not in ("raptor", "awsy", "talos"):
            return False
        if not platform:
            return False

        # Select beta tasks for awsy
        if "awsy" in try_name:
            if accept_awsy_task(try_name, platform):
                return True
            return False

        # Select beta tasks for talos
        if "talos" == unittest_suite:
            if accept_raptor_desktop_build(platform):
                if "windows11-64" in platform:
                    if "xperf" in try_name:
                        return True
                    return False
                if ("mac" in platform or "windows" in platform) and "g3" in try_name:
                    return False
                if "-swr" in try_name:
                    if "dromaeo" in try_name:
                        return False
                    if "perf-reftest-singletons" in try_name:
                        return False
                    if "realworldweb" in try_name:
                        return False
                if any(
                    x in try_name
                    for x in ("prof", "ipc", "gli", "sessionrestore", "tabswitch")
                ):
                    return False
                return True
            return False

        if accept_raptor_desktop_build(platform):
            if "browsertime" and "firefox" in try_name:
                if "profiling" in try_name:
                    return False
                if "bytecode" in try_name:
                    return False
                if "live" in try_name:
                    return False
                if "webext" in try_name:
                    return False
                if "unity" in try_name:
                    return False
                if "wasm" in try_name:
                    return False
                if "tp6-bench" in try_name:
                    return False
                if "tp6" in try_name:
                    return True
                if "benchmark" in try_name:
                    return True
        elif accept_raptor_android_build(platform):
            if "browsertime" and "geckoview" in try_name:
                return False

        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("weekly_release_perf")
def target_tasks_weekly_release_perf(full_task_graph, parameters, graph_config):
    """
    Select performance tests on the release branch to be run weekly
    """

    def filter(task):
        platform = task.attributes.get("test_platform")
        attributes = task.attributes
        try_name = attributes.get("raptor_try_name") or task.label

        if attributes.get("unittest_suite") not in ("raptor", "awsy"):
            return False
        if not platform:
            return False

        # Select release tasks for awsy
        if "awsy" in try_name:
            if accept_awsy_task(try_name, platform):
                return True
            return False

        # Select browsertime tests
        if accept_raptor_desktop_build(platform):
            if "browsertime" and "firefox" in try_name:
                if "power" in try_name:
                    return False
                if "profiling" in try_name:
                    return False
                if "bytecode" in try_name:
                    return False
                if "live" in try_name:
                    return False
                if "webext" in try_name:
                    return False
                if "tp6-bench" in try_name:
                    return False
                if "tp6" in try_name:
                    return True
                if "benchmark" in try_name:
                    return True
                if "youtube-playback" in try_name:
                    return True
        elif accept_raptor_android_build(platform):
            if "browsertime" and "geckoview" in try_name:
                return False

        return False

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("raptor_tp6m")
def target_tasks_raptor_tp6m(full_task_graph, parameters, graph_config):
    """
    Select tasks required for running raptor cold page-load tests on fenix and refbrow
    """

    def filter(task):
        platform = task.attributes.get("build_platform")
        attributes = task.attributes

        if platform and "android" not in platform:
            return False
        if attributes.get("unittest_suite") != "raptor":
            return False
        try_name = attributes.get("raptor_try_name")
        if "-cold" in try_name and "shippable" in platform:
            # Get browsertime amazon smoke tests
            if (
                "browsertime" in try_name
                and "amazon" in try_name
                and "search" not in try_name
            ):
                return True

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("backfill_all_browsertime")
def target_tasks_backfill_all_browsertime(full_task_graph, parameters, graph_config):
    """
    Search for revisions that contains patches that were reviewed by perftest reviewers
    and landed the day before the cron is running. Trigger backfill-all-browsertime action
    task on each of them.
    """
    from gecko_taskgraph.actions.util import get_decision_task_id, get_pushes

    def date_is_yesterday(date):
        yesterday = datetime.today() - timedelta(days=1)
        date = datetime.fromtimestamp(date)
        return date.date() == yesterday.date()

    def reviewed_by_perftest(push):
        try:
            commit_message = get_hg_commit_message(
                os.path.join(GECKO, graph_config["product-dir"]), rev=push
            )
        except Exception as e:
            print(e)
            return False

        for line in commit_message.split("\n\n"):
            if line.lower().startswith("bug ") and "r=" in line:
                if "perftest-reviewers" in line.split("r=")[-1]:
                    print(line)
                    return True
        return False

    pushes = get_pushes(
        project=parameters["head_repository"],
        end_id=int(parameters["pushlog_id"]),
        depth=200,
        full_response=True,
    )
    for push_id in sorted([int(p) for p in pushes.keys()], reverse=True):
        push_rev = pushes[str(push_id)]["changesets"][-1]
        push_info = find_hg_revision_push_info(
            "https://hg.mozilla.org/integration/" + parameters["project"], push_rev
        )
        pushdate = int(push_info["pushdate"])
        if date_is_yesterday(pushdate) and reviewed_by_perftest(push_rev):
            from gecko_taskgraph.actions.util import trigger_action

            print(
                f"Revision {push_rev} was created yesterday and was reviewed by "
                f"#perftest-reviewers."
            )
            try:
                push_decision_task_id = get_decision_task_id(
                    parameters["project"], push_id
                )
            except Exception:
                print(f"Could not find decision task for push {push_id}")
                continue
            try:
                trigger_action(
                    action_name="backfill-all-browsertime",
                    # This lets the action know on which push we want to add a new task
                    decision_task_id=push_decision_task_id,
                )
            except Exception as e:
                print(f"Failed to trigger action for {push_rev}: {e}")

    return []


@register_target_task("condprof")
def target_tasks_condprof(full_task_graph, parameters, graph_config):
    """
    Select tasks required for building conditioned profiles.
    """
    for name, task in full_task_graph.tasks.items():
        if task.kind == "condprof":
            if "a51" not in name:  # bug 1765348
                yield name


@register_target_task("system_symbols")
def target_tasks_system_symbols(full_task_graph, parameters, graph_config):
    """
    Select tasks for scraping and uploading system symbols.
    """
    for name, task in full_task_graph.tasks.items():
        if task.kind in [
            "system-symbols",
            "system-symbols-upload",
            "system-symbols-reprocess",
        ]:
            yield name


@register_target_task("perftest")
def target_tasks_perftest(full_task_graph, parameters, graph_config):
    """
    Select perftest tasks we want to run daily
    """
    for name, task in full_task_graph.tasks.items():
        if task.kind != "perftest":
            continue
        if task.attributes.get("cron", False):
            yield name


@register_target_task("perftest-on-autoland")
def target_tasks_perftest_autoland(full_task_graph, parameters, graph_config):
    """
    Select perftest tasks we want to run daily
    """
    for name, task in full_task_graph.tasks.items():
        if task.kind != "perftest":
            continue
        if task.attributes.get("cron", False) and any(
            test_name in name for test_name in ["view"]
        ):
            yield name


@register_target_task("eslint-build")
def target_tasks_eslint_build(full_task_graph, parameters, graph_config):
    """Select the task to run additional ESLint rules which require a build."""

    for name, task in full_task_graph.tasks.items():
        if task.kind != "source-test":
            continue
        if "eslint-build" in name:
            yield name


@register_target_task("holly_tasks")
def target_tasks_holly(full_task_graph, parameters, graph_config):
    """Bug 1814661: only run updatebot tasks on holly"""

    def filter(task):
        return task.kind == "updatebot"

    return [l for l, t in full_task_graph.tasks.items() if filter(t)]


@register_target_task("snap_upstream_tasks")
def target_tasks_snap_upstream_tasks(full_task_graph, parameters, graph_config):
    """
    Select tasks for building/testing Snap package built as upstream. Omit -try
    because it does not really make sense on a m-c cron

    Use test tasks for linux64 builds and only builds for arm* until there is
    support for running tests (bug 1855463)
    """
    for name, task in full_task_graph.tasks.items():
        if "snap-upstream" in name and not "-local" in name:
            yield name


@register_target_task("nightly-android")
def target_tasks_nightly_android(full_task_graph, parameters, graph_config):
    def filter(task, parameters):
        # bug 1899553: don't automatically schedule uploads to google play
        if task.kind == "push-bundle":
            return False

        # geckoview
        if task.attributes.get("shipping_product") == "fennec" and task.kind in (
            "beetmover-geckoview",
            "upload-symbols",
        ):
            return True

        # fenix/focus/a-c
        build_type = task.attributes.get("build-type", "")
        return build_type in (
            "nightly",
            "focus-nightly",
            "fenix-nightly",
            "fenix-nightly-firebase",
            "focus-nightly-firebase",
        )

    for platform in ("android", "all"):
        index_path = (
            f"{graph_config['trust-domain']}.v2.{parameters['project']}.revision."
            f"{parameters['head_rev']}.taskgraph.decision-nightly-{platform}"
        )
        if os.environ.get("MOZ_AUTOMATION") and retry(
            index_exists,
            args=(index_path,),
            kwargs={
                "reason": "to avoid triggering multiple nightlies off the same revision",
            },
        ):
            return []

    return [l for l, t in full_task_graph.tasks.items() if filter(t, parameters)]


@register_target_task("android-l10n-import")
def target_tasks_android_l10n_import(full_task_graph, parameters, graph_config):
    return [l for l, t in full_task_graph.tasks.items() if l == "android-l10n-import"]


@register_target_task("android-l10n-sync")
def target_tasks_android_l10n_sync(full_task_graph, parameters, graph_config):
    return [l for l, t in full_task_graph.tasks.items() if l == "android-l10n-sync"]


@register_target_task("os-integration")
def target_tasks_os_integration(full_task_graph, parameters, graph_config):
    candidate_attrs = load_yaml(
        os.path.join(GECKO, "taskcluster", "kinds", "test", "os-integration.yml")
    )

    labels = []
    for label, task in full_task_graph.tasks.items():
        if task.kind not in ("test", "source-test", "perftest"):
            continue

        # Match tasks against attribute sets defined in os-integration.yml.
        if not any(attrmatch(task.attributes, **c) for c in candidate_attrs):
            continue

        if not is_try(parameters):
            # Only run hardware tasks if scheduled from try. We do this because
            # the `cron` task is designed to provide a base for testing worker
            # images, which isn't something that impacts our hardware pools.
            if (
                task.attributes.get("build_platform") == "macosx64"
                or "android-hw" in label
            ):
                continue

            # Perform additional filtering for non-try repos. We don't want to
            # limit what can be scheduled on try as os-integration tests are still
            # useful for manual verification of things.
            if not (
                filter_for_project(task, parameters)
                and filter_for_hg_branch(task, parameters)
                and filter_tests_without_manifests(task, parameters)
            ):
                continue

        labels.append(label)
    return labels
