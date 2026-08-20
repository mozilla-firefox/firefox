# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import re
import unittest

import pytest
from mozunit import main
from taskgraph.graph import Graph
from taskgraph.target_tasks import get_method
from taskgraph.task import Task
from taskgraph.taskgraph import TaskGraph

from gecko_taskgraph import target_tasks


class TestTargetTasks(unittest.TestCase):
    def default_matches_project(self, run_on_projects, project):
        return self.default_matches(
            attributes={
                "run_on_projects": run_on_projects,
            },
            parameters={
                "project": project,
                "repository_type": "hg",
                "hg_branch": "default",
                "level": "3",
            },
        )

    def default_matches_hg_branch(self, run_on_hg_branches, hg_branch):
        attributes = {"run_on_projects": ["all"]}
        if run_on_hg_branches is not None:
            attributes["run_on_hg_branches"] = run_on_hg_branches

        return self.default_matches(
            attributes=attributes,
            parameters={
                "project": "mozilla-central",
                "repository_type": "hg",
                "hg_branch": hg_branch,
            },
        )

    def default_matches(self, attributes, parameters):
        method = get_method("default")
        graph = TaskGraph(
            tasks={
                "a": Task(kind="build", label="a", attributes=attributes, task={}),
            },
            graph=Graph(nodes={"a"}, edges=set()),
        )
        return "a" in method(graph, parameters, {})

    def test_default_all(self):
        """run_on_projects=[all] includes release, integration, and other projects"""
        self.assertTrue(self.default_matches_project(["all"], "mozilla-central"))
        self.assertTrue(self.default_matches_project(["all"], "baobab"))

    def test_default_integration(self):
        """run_on_projects=[integration] includes integration projects"""
        self.assertFalse(
            self.default_matches_project(["integration"], "mozilla-central")
        )
        self.assertFalse(self.default_matches_project(["integration"], "baobab"))

    def test_default_release(self):
        """run_on_projects=[release] includes release projects"""
        self.assertTrue(self.default_matches_project(["release"], "mozilla-central"))
        self.assertFalse(self.default_matches_project(["release"], "baobab"))

    def test_default_nothing(self):
        """run_on_projects=[] includes nothing"""
        self.assertFalse(self.default_matches_project([], "mozilla-central"))
        self.assertFalse(self.default_matches_project([], "baobab"))

    def test_default_hg_branch(self):
        self.assertTrue(self.default_matches_hg_branch(None, "default"))
        self.assertTrue(self.default_matches_hg_branch(None, "GECKOVIEW_62_RELBRANCH"))

        self.assertFalse(self.default_matches_hg_branch([], "default"))
        self.assertFalse(self.default_matches_hg_branch([], "GECKOVIEW_62_RELBRANCH"))

        self.assertTrue(self.default_matches_hg_branch(["all"], "default"))
        self.assertTrue(
            self.default_matches_hg_branch(["all"], "GECKOVIEW_62_RELBRANCH")
        )

        self.assertTrue(self.default_matches_hg_branch(["default"], "default"))
        self.assertTrue(self.default_matches_hg_branch([r"default"], "default"))
        self.assertFalse(
            self.default_matches_hg_branch([r"default"], "GECKOVIEW_62_RELBRANCH")
        )

        self.assertTrue(
            self.default_matches_hg_branch(
                ["GECKOVIEW_62_RELBRANCH"], "GECKOVIEW_62_RELBRANCH"
            )
        )
        self.assertTrue(
            self.default_matches_hg_branch(
                [r"GECKOVIEW_\d+_RELBRANCH"], "GECKOVIEW_62_RELBRANCH"
            )
        )
        self.assertTrue(
            self.default_matches_hg_branch(
                [r"GECKOVIEW_\d+_RELBRANCH"], "GECKOVIEW_62_RELBRANCH"
            )
        )
        self.assertFalse(
            self.default_matches_hg_branch([r"GECKOVIEW_\d+_RELBRANCH"], "default")
        )

    def make_task_graph(self):
        tasks = {
            "a": Task(kind=None, label="a", attributes={}, task={}),
            "b": Task(kind=None, label="b", attributes={"at-at": "yep"}, task={}),
            "c": Task(
                kind=None, label="c", attributes={"run_on_projects": ["try"]}, task={}
            ),
            "ddd-1": Task(kind="test", label="ddd-1", attributes={}, task={}),
            "ddd-2": Task(kind="test", label="ddd-2", attributes={}, task={}),
            "ddd-1-cf": Task(kind="test", label="ddd-1-cf", attributes={}, task={}),
            "ddd-2-cf": Task(kind="test", label="ddd-2-cf", attributes={}, task={}),
            "ddd-var-1": Task(kind="test", label="ddd-var-1", attributes={}, task={}),
            "ddd-var-2": Task(kind="test", label="ddd-var-2", attributes={}, task={}),
            # Unlike ddd-*, eee-* had its manifests restricted to what try asked
            # for, so each of its chunks runs its own share of the request.
            "eee-1": Task(
                kind="test",
                label="eee-1",
                attributes={"test-manifests-restricted": True},
                task={},
            ),
            "eee-2": Task(
                kind="test",
                label="eee-2",
                attributes={"test-manifests-restricted": True},
                task={},
            ),
            # Not a test task, but its label ends with a number.
            "fetch-clang-14": Task(
                kind="fetch", label="fetch-clang-14", attributes={}, task={}
            ),
        }
        graph = Graph(
            nodes=set([
                "a",
                "b",
                "c",
                "ddd-1",
                "ddd-2",
                "ddd-1-cf",
                "ddd-2-cf",
                "ddd-var-1",
                "ddd-var-2",
                "eee-1",
                "eee-2",
                "fetch-clang-14",
            ]),
            edges=set(),
        )
        return TaskGraph(tasks, graph)

    def test_empty_try(self):
        "try_mode = None runs nothing"
        tg = self.make_task_graph()
        method = get_method("try_tasks")
        params = {
            "try_mode": None,
            "project": "try",
            "message": "",
        }
        # only runs the task with run_on_projects: try
        self.assertEqual(method(tg, params, {}), [])

    def test_try_task_config(self):
        "try_mode = try_task_config uses the try config"
        tg = self.make_task_graph()
        method = get_method("try_tasks")
        params = {
            "try_mode": "try_task_config",
            "try_task_config": {"tasks": ["a"]},
        }
        self.assertEqual(method(tg, params, {}), ["a"])

    def test_try_task_config_regex(self):
        "try_mode = try_task_config uses the try config with regex instead of chunk numbers"
        tg = self.make_task_graph()
        method = get_method("try_tasks")
        params = {
            "try_mode": "try_task_config",
            "try_task_config": {"new-test-config": True, "tasks": ["ddd-*"]},
            "project": "try",
        }
        self.assertEqual(sorted(method(tg, params, {})), ["ddd-1", "ddd-2"])

    def _try_task_config_params(self, tasks, **env):
        return {
            "try_mode": "try_task_config",
            "try_task_config": {
                "new-test-config": True,
                "tasks": tasks,
                "env": env,
            },
            "project": "try",
        }

    def test_try_task_config_regex_with_paths(self):
        """Only the first chunk of a task the taskgraph couldn't restrict to the
        requested paths is selected, as all of its chunks run the same tests."""
        method = get_method("try_tasks")
        params = self._try_task_config_params(
            ["ddd-*"], MOZHARNESS_TEST_PATHS='{"suite": ["foo/bar"]}'
        )
        self.assertEqual(sorted(method(self.make_task_graph(), params, {})), ["ddd-1"])

    def test_try_task_config_regex_with_paths_restricted(self):
        """Every chunk of a task whose manifests were restricted to the requested
        paths is selected, as each runs its own share of them."""
        method = get_method("try_tasks")
        params = self._try_task_config_params(
            ["eee-*"], MOZHARNESS_TEST_PATHS='{"suite": ["foo/bar"]}'
        )
        self.assertEqual(
            sorted(method(self.make_task_graph(), params, {})), ["eee-1", "eee-2"]
        )

    def test_try_task_config_regex_with_tag(self):
        """A test tag restricts the selection the same way test paths do."""
        method = get_method("try_tasks")
        tag = '["foo"]'
        self.assertEqual(
            sorted(
                method(
                    self.make_task_graph(),
                    self._try_task_config_params(["ddd-*"], MOZHARNESS_TEST_TAG=tag),
                    {},
                )
            ),
            ["ddd-1"],
        )
        self.assertEqual(
            sorted(
                method(
                    self.make_task_graph(),
                    self._try_task_config_params(["eee-*"], MOZHARNESS_TEST_TAG=tag),
                    {},
                )
            ),
            ["eee-1", "eee-2"],
        )

    def test_try_task_config_renumbered_chunk(self):
        """An explicitly requested chunk that no longer exists, because chunk
        counts were computed from the requested paths, is replaced by the chunks
        the task ended up with rather than dropped."""
        method = get_method("try_tasks")
        params = self._try_task_config_params(
            ["eee-7"], MOZHARNESS_TEST_PATHS='{"suite": ["foo/bar"]}'
        )
        self.assertEqual(
            sorted(method(self.make_task_graph(), params, {})), ["eee-1", "eee-2"]
        )

    def test_try_task_config_renumbered_chunk_of_unrestricted_task(self):
        """Substituting the chunks of a task that wasn't restricted to the
        request must not schedule the whole set of them: they all run the same
        tests, so only the first is kept."""
        method = get_method("try_tasks")
        params = self._try_task_config_params(
            ["ddd-7"], MOZHARNESS_TEST_PATHS='{"suite": ["foo/bar"]}'
        )
        self.assertEqual(sorted(method(self.make_task_graph(), params, {})), ["ddd-1"])

    def test_try_task_config_renumbered_chunk_only_for_tests(self):
        """A missing label that isn't a test task keeps being reported as
        missing, rather than pulling in whatever shares its prefix."""
        method = get_method("try_tasks")
        params = self._try_task_config_params(
            ["fetch-clang-20"], MOZHARNESS_TEST_PATHS='{"suite": ["foo/bar"]}'
        )
        self.assertEqual(sorted(method(self.make_task_graph(), params, {})), [])

    def test_try_task_config_absolute(self):
        "try_mode = try_task_config uses the try config with full task labels"
        tg = self.make_task_graph()
        method = get_method("try_tasks")
        params = {
            "try_mode": "try_task_config",
            "try_task_config": {
                "new-test-config": True,
                "tasks": ["ddd-var-2", "ddd-1"],
            },
            "project": "try",
        }
        self.assertEqual(sorted(method(tg, params, {})), ["ddd-1", "ddd-var-2"])

    def test_try_task_config_regex_var(self):
        "try_mode = try_task_config uses the try config with regex instead of chunk numbers and a test variant"
        tg = self.make_task_graph()
        method = get_method("try_tasks")
        params = {
            "try_mode": "try_task_config",
            "try_task_config": {"new-test-config": True, "tasks": ["ddd-var-*"]},
            "project": "try",
        }
        self.assertEqual(sorted(method(tg, params, {})), ["ddd-var-1", "ddd-var-2"])


# tests for specific filters


@pytest.mark.parametrize(
    "name,params,expected",
    (
        pytest.param(
            "filter_tests_without_manifests",
            {
                "task": Task(kind="test", label="a", attributes={}, task={}),
                "parameters": None,
            },
            True,
            id="filter_tests_without_manifests_not_in_attributes",
        ),
        pytest.param(
            "filter_tests_without_manifests",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"test_manifests": ["foo"]},
                    task={},
                ),
                "parameters": None,
            },
            True,
            id="filter_tests_without_manifests_has_test_manifests",
        ),
        pytest.param(
            "filter_tests_without_manifests",
            {
                "task": Task(
                    kind="build",
                    label="a",
                    attributes={"test_manifests": None},
                    task={},
                ),
                "parameters": None,
            },
            True,
            id="filter_tests_without_manifests_not_a_test",
        ),
        pytest.param(
            "filter_tests_without_manifests",
            {
                "task": Task(
                    kind="test", label="a", attributes={"test_manifests": None}, task={}
                ),
                "parameters": None,
            },
            False,
            id="filter_tests_without_manifests_has_no_test_manifests",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("build")],
                "mode": "include",
            },
            True,
            id="filter_regex_simple_include",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("linux(.+)debug")],
                "mode": "include",
            },
            True,
            id="filter_regex_re_include",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("nothing"), re.compile("linux(.+)debug")],
                "mode": "include",
            },
            True,
            id="filter_regex_re_include_multiple",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("build")],
                "mode": "exclude",
            },
            False,
            id="filter_regex_simple_exclude",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("linux(.+)debug")],
                "mode": "exclude",
            },
            False,
            id="filter_regex_re_exclude",
        ),
        pytest.param(
            "filter_by_regex",
            {
                "task_label": "build-linux64-debug",
                "regexes": [re.compile("linux(.+)debug"), re.compile("nothing")],
                "mode": "exclude",
            },
            False,
            id="filter_regex_re_exclude_multiple",
        ),
        pytest.param(
            "filter_unsupported_artifact_builds",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"supports-artifact-builds": False},
                    task={},
                ),
                "parameters": {
                    "try_task_config": {
                        "use-artifact-builds": False,
                    },
                },
            },
            True,
            id="filter_unsupported_artifact_builds_no_artifact_builds",
        ),
        pytest.param(
            "filter_unsupported_artifact_builds",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"supports-artifact-builds": False},
                    task={},
                ),
                "parameters": {
                    "try_task_config": {
                        "use-artifact-builds": True,
                    },
                },
            },
            False,
            id="filter_unsupported_artifact_builds_removed",
        ),
        pytest.param(
            "filter_unsupported_artifact_builds",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"supports-artifact-builds": True},
                    task={},
                ),
                "parameters": {
                    "try_task_config": {
                        "use-artifact-builds": True,
                    },
                },
            },
            True,
            id="filter_unsupported_artifact_builds_not_removed",
        ),
        pytest.param(
            "filter_unsupported_artifact_builds",
            {
                "task": Task(kind="test", label="a", attributes={}, task={}),
                "parameters": {
                    "try_task_config": {
                        "use-artifact-builds": True,
                    },
                },
            },
            True,
            id="filter_unsupported_artifact_builds_not_removed",
        ),
        pytest.param(
            "filter_for_repo_type",
            {
                "task": Task(kind="test", label="a", attributes={}, task={}),
                "parameters": {
                    "repository_type": "hg",
                },
            },
            True,
            id="filter_for_repo_type_default_hg_not_removed",
        ),
        pytest.param(
            "filter_for_repo_type",
            {
                "task": Task(kind="test", label="a", attributes={}, task={}),
                "parameters": {
                    "repository_type": "git",
                },
            },
            True,
            id="filter_for_repo_type_default_git_not_removed",
        ),
        pytest.param(
            "filter_for_repo_type",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"run_on_repo_type": ["hg"]},
                    task={},
                ),
                "parameters": {
                    "repository_type": "git",
                },
            },
            False,
            id="filter_for_repo_type_no_match_removed",
        ),
        pytest.param(
            "filter_for_repo_type",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"run_on_repo_type": ["git"]},
                    task={},
                ),
                "parameters": {
                    "repository_type": "git",
                },
            },
            True,
            id="filter_for_repo_type_match_not_removed",
        ),
        pytest.param(
            "filter_for_repo_type",
            {
                "task": Task(
                    kind="test",
                    label="a",
                    attributes={"run_on_repo_type": ["all"]},
                    task={},
                ),
                "parameters": {
                    "repository_type": "git",
                },
            },
            True,
            id="filter_for_repo_type_all_not_removed",
        ),
    ),
)
def test_filters(name, params, expected):
    func = getattr(target_tasks, name)
    assert func(**params) is expected


def _os_integration_params(**overrides):
    params = {
        "project": "mozilla-central",
        "tasks_for": "cron",
        "target_tasks_method": "os-integration",
        "try_mode": None,
        "repository_type": "hg",
        "hg_branch": "default",
        "level": "3",
    }
    params.update(overrides)
    return params


def _snap_test_task(
    label,
    *,
    snap_test_type="basic",
    snap_test_release="2404",
    primary_dependency_label=None,
    run_on_projects=None,
):
    if primary_dependency_label is None:
        primary_dependency_label = f"snap-upstream-build-{label.split('-', 5)[5]}"

    if run_on_projects is None:
        run_on_projects = ["all"]

    return Task(
        kind="snap-upstream-test",
        label=label,
        attributes={
            "kind": "snap-upstream-test",
            "snap_test_type": snap_test_type,
            "snap_test_release": snap_test_release,
            "primary-dependency-label": primary_dependency_label,
            "cron": True,
            "run_on_projects": run_on_projects,
        },
        task={},
    )


def test_os_integration_includes_snap_basic_2404():
    """target_tasks_os_integration must surface snap-upstream-test basic-2404 on m-c cron.

    Guards against regressions in either the kind allow-list in target_tasks.py
    or the attrmatch entry in os-integration.yml; see bug 1941642.
    """
    tasks = {
        "snap-upstream-test-basic-2404-amd64-nightly/opt": _snap_test_task(
            "snap-upstream-test-basic-2404-amd64-nightly/opt"
        ),
        "snap-upstream-test-basic-2404-amd64-local/opt": _snap_test_task(
            "snap-upstream-test-basic-2404-amd64-local/opt"
        ),
        "snap-upstream-test-basic-2404-amd64-nightly/debug": _snap_test_task(
            "snap-upstream-test-basic-2404-amd64-nightly/debug"
        ),
        "snap-upstream-test-basic-2404-amd64-beta/opt": _snap_test_task(
            "snap-upstream-test-basic-2404-amd64-beta/opt"
        ),
        "snap-upstream-test-qa-2404-amd64-nightly/opt": _snap_test_task(
            "snap-upstream-test-qa-2404-amd64-nightly/opt", snap_test_type="qa"
        ),
        "snap-upstream-test-basic-2204-amd64-nightly/opt": _snap_test_task(
            "snap-upstream-test-basic-2204-amd64-nightly/opt", snap_test_release="2204"
        ),
        "snap-upstream-test-basic-2404-amd64-try-only/opt": _snap_test_task(
            "snap-upstream-test-basic-2404-amd64-try-only/opt",
            run_on_projects=["try"],
        ),
    }
    graph = TaskGraph(tasks, Graph(nodes=set(tasks), edges=set()))

    method = get_method("os-integration")
    selected = set(method(graph, _os_integration_params(), {}))

    assert "snap-upstream-test-basic-2404-amd64-nightly/opt" in selected
    assert "snap-upstream-test-basic-2404-amd64-local/opt" not in selected
    assert "snap-upstream-test-basic-2404-amd64-nightly/debug" not in selected
    assert "snap-upstream-test-basic-2404-amd64-beta/opt" not in selected
    assert "snap-upstream-test-qa-2404-amd64-nightly/opt" not in selected
    assert "snap-upstream-test-basic-2204-amd64-nightly/opt" not in selected
    assert "snap-upstream-test-basic-2404-amd64-try-only/opt" not in selected


_MACOS = "macosx1470-64-shippable/opt"


def _raptor_label(try_name, platform):
    return f"test-{platform}-{try_name}"


def _raptor_task(try_name, platform):
    return Task(
        kind="test",
        label=_raptor_label(try_name, platform),
        attributes={
            "unittest_suite": "raptor",
            "raptor_try_name": try_name,
            "test_platform": platform,
        },
        task={},
    )


def _general_perf_selection(*tasks):
    graph = TaskGraph(
        {t.label: t for t in tasks}, Graph(nodes={t.label for t in tasks}, edges=set())
    )
    return set(get_method("general_perf_testing")(graph, {}, {}))


def test_general_perf_testing_selects_safari_video_playback_latency():
    """The perf cron must select Safari for the video playback latency suite."""
    vpl_safari = "browsertime-video-playback-latency-safari-vpl-h264"
    selected = _general_perf_selection(_raptor_task(vpl_safari, _MACOS))

    assert _raptor_label(vpl_safari, _MACOS) in selected


if __name__ == "__main__":
    main()
