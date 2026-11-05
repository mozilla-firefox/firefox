# Any copyright is dedicated to the public domain.
# http://creativecommons.org/publicdomain/zero/1.0/


import json
import time
from datetime import datetime
from time import mktime

import pytest
from mozunit import main
from taskgraph.optimize.base import registry
from taskgraph.task import Task
from taskgraph.util.copy import deepcopy
from taskgraph.util.python_path import find_object
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph import GECKO
from gecko_taskgraph.optimize import experimental, perf_batching, project, tryselect
from gecko_taskgraph.optimize.backstop import SkipUnlessBackstop, SkipUnlessPushInterval
from gecko_taskgraph.optimize.bugbug import (
    FALLBACK,
    BugBugPushSchedules,
    DisperseGroups,
    SkipUnlessDebug,
    merge_bugbug_replies,
)
from gecko_taskgraph.optimize.docs import SkipUnlessSphinxJs
from gecko_taskgraph.optimize.mozlint import SkipUnlessMozlint
from gecko_taskgraph.optimize.perf_batching import (
    PERF_CADENCE_STRATEGIES,
    SkipUnlessTimeSinceLastBatch,
)
from gecko_taskgraph.optimize.strategies import SkipUnlessMissing, SkipUnlessSchedules
from gecko_taskgraph.util.backstop import BACKSTOP_PUSH_INTERVAL
from gecko_taskgraph.util.bugbug import (
    BUGBUG_BASE_URL,
    CT_LOW,
    CT_MEDIUM,
    BugbugTimeoutException,
    push_schedules,
)


@pytest.fixture(autouse=True)
def clear_push_schedules_memoize():
    push_schedules.cache_clear()


@pytest.fixture
def params():
    return {
        "branch": "autoland",
        "head_repository": "https://hg.mozilla.org/integration/autoland",
        "head_rev": "abcdef",
        "project": "autoland",
        "pushlog_id": 1,
        "pushdate": mktime(datetime.now().timetuple()),
    }


def generate_tasks(*tasks):
    for i, task in enumerate(tasks):
        task.setdefault("label", f"task-{i}-label")
        task.setdefault("kind", "test")
        task.setdefault("task", {})
        task.setdefault("attributes", {})
        task["attributes"].setdefault("e10s", True)

        for attr in (
            "optimization",
            "dependencies",
            "soft_dependencies",
        ):
            task.setdefault(attr, None)

        task["task"].setdefault("label", task["label"])
        yield Task.from_json(task)


# task sets

default_tasks = list(
    generate_tasks(
        {"attributes": {"test_manifests": ["foo/test.ini", "bar/test.ini"]}},
        {"attributes": {"test_manifests": ["bar/test.ini"], "build_type": "debug"}},
        {"attributes": {"build_type": "debug"}},
        {"attributes": {"test_manifests": [], "build_type": "opt"}},
        {"attributes": {"build_type": "opt"}},
    )
)


disperse_tasks = list(
    generate_tasks(
        {
            "attributes": {
                "test_manifests": ["foo/test.ini", "bar/test.ini"],
                "test_platform": "linux/opt",
            }
        },
        {
            "attributes": {
                "test_manifests": ["bar/test.ini"],
                "test_platform": "linux/opt",
            }
        },
        {
            "attributes": {
                "test_manifests": ["bar/test.ini"],
                "test_platform": "windows/debug",
            }
        },
        {
            "attributes": {
                "test_manifests": ["bar/test.ini"],
                "test_platform": "linux/opt",
                "unittest_variant": "no-fission",
            }
        },
        {
            "attributes": {
                "e10s": False,
                "test_manifests": ["bar/test.ini"],
                "test_platform": "linux/opt",
            }
        },
    )
)


def idfn(param):
    if isinstance(param, tuple):
        try:
            return param[0].__name__
        except AttributeError:
            return None
    return None


@pytest.mark.parametrize(
    "opt,tasks,arg,expected",
    [
        # debug
        pytest.param(
            SkipUnlessDebug(),
            default_tasks,
            None,
            ["task-0-label", "task-1-label", "task-2-label"],
        ),
        # disperse with no supplied importance
        pytest.param(
            DisperseGroups(),
            disperse_tasks,
            None,
            [t.label for t in disperse_tasks],
        ),
        # disperse with low importance
        pytest.param(
            DisperseGroups(),
            disperse_tasks,
            {"bar/test.ini": "low"},
            ["task-0-label", "task-2-label"],
        ),
        # disperse with medium importance
        pytest.param(
            DisperseGroups(),
            disperse_tasks,
            {"bar/test.ini": "medium"},
            ["task-0-label", "task-1-label", "task-2-label"],
        ),
        # disperse with high importance
        pytest.param(
            DisperseGroups(),
            disperse_tasks,
            {"bar/test.ini": "high"},
            ["task-0-label", "task-1-label", "task-2-label", "task-3-label"],
        ),
    ],
    ids=idfn,
)
def test_optimization_strategy_remove(params, opt, tasks, arg, expected):
    labels = [t.label for t in tasks if not opt.should_remove_task(t, params, arg)]
    assert sorted(labels) == sorted(expected)


@pytest.mark.parametrize(
    "args,data,expected",
    [
        # empty
        pytest.param(
            (0.1,),
            {},
            [],
        ),
        # only tasks without test manifests selected
        pytest.param(
            (0.1,),
            {"tasks": {"task-1-label": 0.9, "task-2-label": 0.1, "task-3-label": 0.5}},
            ["task-2-label"],
        ),
        # tasks which are unknown to bugbug are selected
        pytest.param(
            (0.1,),
            {
                "tasks": {"task-1-label": 0.9, "task-3-label": 0.5},
                "known_tasks": ["task-1-label", "task-3-label", "task-4-label"],
            },
            ["task-2-label"],
        ),
        # tasks containing groups selected
        pytest.param(
            (0.1,),
            {"groups": {"foo/test.ini": 0.4}},
            ["task-0-label"],
        ),
        # tasks matching "tasks" or "groups" selected
        pytest.param(
            (0.1,),
            {
                "tasks": {"task-2-label": 0.2},
                "groups": {"foo/test.ini": 0.25, "bar/test.ini": 0.75},
            },
            ["task-0-label", "task-1-label", "task-2-label"],
        ),
        # tasks matching "tasks" or "groups" selected, when they exceed the confidence threshold
        pytest.param(
            (0.5,),
            {
                "tasks": {"task-2-label": 0.2, "task-4-label": 0.5},
                "groups": {"foo/test.ini": 0.65, "bar/test.ini": 0.25},
            },
            ["task-0-label", "task-4-label"],
        ),
        # tasks matching "reduced_tasks" are selected, when they exceed the confidence threshold
        pytest.param(
            (0.7, True, True),
            {
                "tasks": {"task-2-label": 0.7, "task-4-label": 0.7},
                "reduced_tasks": {"task-4-label": 0.7},
                "groups": {"foo/test.ini": 0.75, "bar/test.ini": 0.25},
            },
            ["task-4-label"],
        ),
        # tasks matching "groups" selected, only on specific platforms.
        pytest.param(
            (0.1, False, False, None, 1, True),
            {
                "tasks": {"task-2-label": 0.2},
                "groups": {"foo/test.ini": 0.25, "bar/test.ini": 0.75},
                "config_groups": {
                    "foo/test.ini": ["task-1-label", "task-0-label"],
                    "bar/test.ini": ["task-0-label"],
                },
            },
            ["task-0-label", "task-2-label"],
        ),
        pytest.param(
            (0.1, False, False, None, 1, True),
            {
                "tasks": {"task-2-label": 0.2},
                "groups": {"foo/test.ini": 0.25, "bar/test.ini": 0.75},
                "config_groups": {
                    "foo/test.ini": ["task-1-label", "task-0-label"],
                    "bar/test.ini": ["task-1-label"],
                },
            },
            ["task-0-label", "task-1-label", "task-2-label"],
        ),
        pytest.param(
            (0.1, False, False, None, 1, True),
            {
                "tasks": {"task-2-label": 0.2},
                "groups": {"foo/test.ini": 0.25, "bar/test.ini": 0.75},
                "config_groups": {
                    "foo/test.ini": ["task-1-label"],
                    "bar/test.ini": ["task-0-label"],
                },
            },
            ["task-0-label", "task-2-label"],
        ),
        pytest.param(
            (0.1, False, False, None, 1, True),
            {
                "tasks": {"task-2-label": 0.2},
                "groups": {"foo/test.ini": 0.25, "bar/test.ini": 0.75},
                "config_groups": {
                    "foo/test.ini": ["task-1-label"],
                    "bar/test.ini": ["task-3-label"],
                },
            },
            ["task-2-label"],
        ),
        # the groups thresholds returned by bugbug replace the preset ones
        pytest.param(
            (CT_LOW,),
            {
                "groups": {"foo/test.ini": 0.35, "bar/test.ini": 0.25},
                "confidence_thresholds": {
                    "groups": {"low": 0.3, "medium": 0.4, "high": 0.5}
                },
            },
            ["task-0-label"],
        ),
        # the tasks thresholds returned by bugbug replace the preset ones
        pytest.param(
            (CT_MEDIUM, True),
            {
                "tasks": {"task-2-label": 0.35, "task-4-label": 0.25},
                "confidence_thresholds": {"tasks": {"medium": 0.3}},
            },
            ["task-2-label"],
        ),
        # the thresholds returned by bugbug are only used for the preset ones
        pytest.param(
            (0.5,),
            {
                "groups": {"foo/test.ini": 0.35},
                "confidence_thresholds": {"groups": {"low": 0.3}},
            },
            [],
        ),
    ],
    ids=idfn,
)
def test_bugbug_push_schedules(responses, params, args, data, expected):
    query = "/push/{branch}/{head_rev}/schedules".format(**params)
    url = BUGBUG_BASE_URL + query

    responses.add(
        responses.GET,
        url,
        json=data,
        status=200,
    )

    opt = BugBugPushSchedules(*args)
    labels = [
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    ]
    assert sorted(labels) == sorted(expected)


def test_bugbug_multiple_pushes(responses, params):
    pushes = {str(pid): {"changesets": [f"c{pid}"]} for pid in range(8, 10)}

    responses.add(
        responses.GET,
        "https://hg.mozilla.org/integration/autoland/json-pushes/?version=2&startID=8&endID=9",
        json={"pushes": pushes},
        status=200,
    )

    responses.add(
        responses.GET,
        BUGBUG_BASE_URL + "/push/{}/c9/schedules".format(params["branch"]),
        json={
            "tasks": {"task-2-label": 0.2, "task-4-label": 0.5},
            "groups": {"foo/test.ini": 0.2, "bar/test.ini": 0.25},
            "config_groups": {"foo/test.ini": ["linux-*"], "bar/test.ini": ["task-*"]},
            "known_tasks": ["task-4-label"],
        },
        status=200,
    )

    # Tasks with a lower confidence don't override task with a higher one.
    # Tasks with a higher confidence override tasks with a lower one.
    # Known tasks are merged.
    responses.add(
        responses.GET,
        BUGBUG_BASE_URL + "/push/{branch}/{head_rev}/schedules".format(**params),
        json={
            "tasks": {"task-2-label": 0.2, "task-4-label": 0.2},
            "groups": {"foo/test.ini": 0.65, "bar/test.ini": 0.25},
            "config_groups": {
                "foo/test.ini": ["task-*"],
                "bar/test.ini": ["windows-*"],
            },
            "known_tasks": ["task-1-label", "task-3-label"],
        },
        status=200,
    )

    params["pushlog_id"] = 10

    opt = BugBugPushSchedules(0.3, False, False, False, 2)
    labels = [
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    ]
    assert sorted(labels) == sorted(["task-0-label", "task-2-label", "task-4-label"])

    opt = BugBugPushSchedules(0.3, False, False, False, 2, True)
    labels = [
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    ]
    assert sorted(labels) == sorted(["task-0-label", "task-2-label", "task-4-label"])

    opt = BugBugPushSchedules(0.2, False, False, False, 2, True)
    labels = [
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    ]
    assert sorted(labels) == sorted([
        "task-0-label",
        "task-1-label",
        "task-2-label",
        "task-4-label",
    ])


def test_bugbug_group_importance_thresholds(responses, params):
    responses.add(
        responses.GET,
        BUGBUG_BASE_URL + "/push/{branch}/{head_rev}/schedules".format(**params),
        json={
            "groups": {"foo/test.ini": 0.45, "bar/test.ini": 0.35},
            "confidence_thresholds": {
                "groups": {"low": 0.3, "medium": 0.4, "high": 0.5}
            },
        },
        status=200,
    )

    importance = {}
    opt = BugBugPushSchedules(CT_LOW)
    assert not opt.should_remove_task(default_tasks[0], params, importance)
    assert importance == {"foo/test.ini": "medium", "bar/test.ini": "low"}


def test_merge_bugbug_replies_confidence_thresholds():
    data = {}
    merge_bugbug_replies(
        data,
        {"tasks": {"a": 0.5}, "confidence_thresholds": {"tasks": {"low": 0.3}}},
    )
    merge_bugbug_replies(
        data,
        {"tasks": {"a": 0.4}, "confidence_thresholds": {"tasks": {"low": 0.2}}},
    )
    # A reply without thresholds doesn't remove them.
    merge_bugbug_replies(data, {"tasks": {}, "confidence_thresholds": {}})
    assert data["tasks"] == {"a": 0.5}
    assert data["confidence_thresholds"] == {"tasks": {"low": 0.2}}


def test_bugbug_timeout(monkeypatch, responses, params):
    query = "/push/{branch}/{head_rev}/schedules".format(**params)
    url = BUGBUG_BASE_URL + query
    responses.add(
        responses.GET,
        url,
        json={"ready": False},
        status=202,
    )

    # Make sure the test runs fast.
    monkeypatch.setattr(time, "sleep", lambda i: None)

    opt = BugBugPushSchedules(0.5)
    with pytest.raises(BugbugTimeoutException):
        opt.should_remove_task(default_tasks[0], params, None)


def test_bugbug_fallback(monkeypatch, responses, params):
    query = "/push/{branch}/{head_rev}/schedules".format(**params)
    url = BUGBUG_BASE_URL + query
    responses.add(
        responses.GET,
        url,
        json={"ready": False},
        status=202,
    )

    opt = BugBugPushSchedules(0.5, fallback=FALLBACK)

    # Make sure the test runs fast.
    monkeypatch.setattr(time, "sleep", lambda i: None)

    def fake_should_remove_task(task, params, _):
        return task.label == default_tasks[0].label

    monkeypatch.setattr(
        registry[FALLBACK], "should_remove_task", fake_should_remove_task
    )

    assert opt.should_remove_task(default_tasks[0], params, None)

    # Make sure we don't hit bugbug more than once.
    responses.reset()

    assert not opt.should_remove_task(default_tasks[1], params, None)


def test_backstop(params):
    all_labels = {t.label for t in default_tasks}
    opt = SkipUnlessBackstop()

    params["backstop"] = False
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == set()

    params["backstop"] = True
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == all_labels


def test_push_interval(params):
    all_labels = {t.label for t in default_tasks}
    opt = SkipUnlessPushInterval(10)  # every 10th push

    # Only multiples of 10 schedule tasks.
    params["pushlog_id"] = 9
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == set()

    params["pushlog_id"] = 10
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == all_labels


def test_expanded(params):
    all_labels = {t.label for t in default_tasks}
    opt = registry["skip-unless-expanded"]

    params["backstop"] = False
    params["pushlog_id"] = BACKSTOP_PUSH_INTERVAL / 2
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == all_labels

    params["pushlog_id"] += 1
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == set()

    params["backstop"] = True
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, None)
    }
    assert scheduled == all_labels


@pytest.mark.parametrize(
    "strategy",
    [
        "skip-unless-backstop",
        "skip-unless-expanded",
        "perf-cadence-backstop",
        "perf-cadence-expanded",
    ],
)
def test_tryselect_backstop_expanded(responses, params, strategy):
    params.update({"project": "try", "backstop": False, "pushlog_id": 11})
    responses.add(
        responses.GET,
        BUGBUG_BASE_URL + "/push/{project}/{head_rev}/schedules".format(**params),
        json={
            "tasks": {"task-0-label": 0.9, "task-1-label": 0.9},
            "reduced_tasks": {"task-0-label": 0.9},
            "known_tasks": ["task-0-label", "task-1-label", "task-2-label"],
        },
        status=200,
    )

    tasks = list(generate_tasks({}, {}, {}))
    opt = tryselect.bugbug_reduced_manifests_config_selection_medium[strategy]
    scheduled = {t.label for t in tasks if not opt.should_remove_task(t, params, None)}
    assert scheduled == {"task-0-label"}


def test_tryselect_changed(responses, params):
    params.update({
        "project": "try",
        "files_changed": ["js/src/foo.cpp"],
        "base_rev": "123456",
    })
    responses.add(
        responses.GET,
        BUGBUG_BASE_URL + "/push/{project}/{head_rev}/schedules".format(**params),
        json={
            "tasks": {"task-1-label": 0.9},
            "reduced_tasks": {"task-1-label": 0.9},
            "known_tasks": ["task-0-label", "task-1-label", "task-2-label"],
        },
        status=200,
    )

    tasks = list(generate_tasks({}, {}, {}))
    opt = tryselect.bugbug_reduced_manifests_config_selection_medium[
        "skip-unless-changed"
    ]
    patterns = {
        "task-0-label": ["js/src/**"],
        "task-1-label": ["dom/**"],
        "task-2-label": ["dom/**"],
    }
    scheduled = {
        t.label
        for t in tasks
        if not opt.should_remove_task(t, params, patterns[t.label])
    }
    assert scheduled == {"task-0-label", "task-1-label"}


def test_project_autoland_test(monkeypatch, responses, params):
    """Tests the behaviour of the `project.autoland["test"]` strategy on
    various types of pushes.
    """
    # This is meant to test the composition of substrategies, and not the
    # actual optimization implementations. So mock them out for simplicity.
    monkeypatch.setattr(SkipUnlessSchedules, "should_remove_task", lambda *args: False)
    monkeypatch.setattr(DisperseGroups, "should_remove_task", lambda *args: False)

    def fake_bugbug_should_remove_task(self, task, params, importance):
        if self.num_pushes > 1:
            return task.label == "task-4-label"
        return task.label in ("task-2-label", "task-3-label", "task-4-label")

    monkeypatch.setattr(
        BugBugPushSchedules, "should_remove_task", fake_bugbug_should_remove_task
    )

    opt = project.autoland["test"]

    # On backstop pushes, nothing gets optimized.
    params["backstop"] = True
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    }
    assert scheduled == {t.label for t in default_tasks}

    # On expanded pushes, some things are optimized.
    params["backstop"] = False
    params["pushlog_id"] = 10
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    }
    assert scheduled == {"task-0-label", "task-1-label", "task-2-label", "task-3-label"}

    # On regular pushes, more things are optimized.
    params["pushlog_id"] = 11
    scheduled = {
        t.label for t in default_tasks if not opt.should_remove_task(t, params, {})
    }
    assert scheduled == {"task-0-label", "task-1-label"}


perf_batch_tasks = list(
    generate_tasks(
        {"attributes": {"unittest_suite": "raptor"}},
        {"attributes": {"unittest_suite": "talos"}},
        {"attributes": {"unittest_suite": "awsy"}},
        {"attributes": {"perftest_name": "browsertime"}},
        {"attributes": {"unittest_suite": "mochitest-plain"}},
        {"attributes": {"build_type": "opt"}},
    )
)


def test_perf_cadence_aliases(params):
    """The perf-cadence aliases behave identically to the strategies they
    wrap, so assigning them to perf tasks changes nothing until a shadow
    scheduler (or later, production) overrides them."""
    task = perf_batch_tasks[0]
    pairs = [
        ("perf-cadence-android", "skip-unless-android-perftest-backstop"),
        ("perf-cadence-backstop", "skip-unless-backstop"),
        ("perf-cadence-expanded", "skip-unless-expanded"),
    ]
    for backstop in (True, False):
        for pushlog_id in (10, 11, 20):
            params["backstop"] = backstop
            params["android_perftest_backstop"] = backstop
            params["pushlog_id"] = pushlog_id
            for alias, original in pairs:
                assert bool(
                    registry[alias].should_remove_task(task, params, None)
                ) == bool(registry[original].should_remove_task(task, params, None))

    # perf-cadence-default forwards to the default `test` strategy.
    assert registry["perf-cadence-default"].substrategies[0] is registry["test"]


def test_perf_cadence_default_mirrors_test():
    """Wherever `test` is overridden, talos/awsy (judged through
    perf-cadence-default) get the identical override."""
    assert project.autoland["perf-cadence-default"] is project.autoland["test"]
    assert project.beta["perf-cadence-default"] is project.beta["test"]
    assert (
        experimental.bugbug_reduced["perf-cadence-default"]
        is experimental.bugbug_reduced["test"]
    )


def test_perf_batch_overrides():
    overrides = experimental.perf_fixed_batch_10
    assert overrides["build"] is project.autoland["build"]
    assert overrides["test"] is project.autoland["test"]

    batch = overrides["perf-cadence-expanded"]
    assert isinstance(batch, SkipUnlessPushInterval)
    assert batch.push_interval == 10
    for name in PERF_CADENCE_STRATEGIES:
        assert overrides[name] is batch


def test_skip_unless_time_since_last_batch(monkeypatch, params):
    saved = []
    monkeypatch.setattr(perf_batching, "save_state", saved.append)

    def set_state(state):
        monkeypatch.setattr(perf_batching, "load_state", lambda *args: state)

    task = perf_batch_tasks[0]
    params["pushdate"] = 1000000

    # Cold start: kept, the current push becomes the last batch.
    opt = SkipUnlessTimeSinceLastBatch(5.5, "perf_time_batch_5_5h")
    params["pushlog_id"] = 100
    set_state(None)
    assert not opt.should_remove_task(task, params, None)
    assert saved[-1]["batched"] and saved[-1]["cold_start"]
    assert saved[-1]["last_batch"]["pushlog_id"] == 100

    # Not enough time since the last batch: removed, last batch carried over.
    opt = SkipUnlessTimeSinceLastBatch(5.5, "perf_time_batch_5_5h")
    params["pushlog_id"] = 101
    set_state({"last_batch": {"pushlog_id": 90, "pushdate": 1000000 - 19800 + 60}})
    assert opt.should_remove_task(task, params, None)
    assert not saved[-1]["batched"] and not saved[-1]["cold_start"]
    assert saved[-1]["last_batch"]["pushlog_id"] == 90

    # Interval elapsed: kept, the last batch advances to the current push.
    opt = SkipUnlessTimeSinceLastBatch(5.5, "perf_time_batch_5_5h")
    params["pushlog_id"] = 102
    set_state({"last_batch": {"pushlog_id": 90, "pushdate": 1000000 - 19800}})
    assert not opt.should_remove_task(task, params, None)
    assert saved[-1]["batched"]
    assert saved[-1]["last_batch"]["pushlog_id"] == 102

    # The decision and state are computed once per push.
    assert len(saved) == 3
    assert not opt.should_remove_task(task, params, None)
    assert len(saved) == 3


def test_save_state(monkeypatch, tmp_path):
    monkeypatch.delenv(perf_batching.STATE_PATH_ENVVAR, raising=False)
    perf_batching.save_state({"batched": True})

    state_path = tmp_path / "state" / "state.json"
    monkeypatch.setenv(perf_batching.STATE_PATH_ENVVAR, str(state_path))
    perf_batching.save_state({"batched": True})
    assert json.loads(state_path.read_text()) == {"batched": True}


def test_shadow_scheduler_strategies_resolve():
    """Every strategy referenced by a shadow scheduler task resolves to a
    non-empty strategy override dict."""
    tasks = load_yaml(
        GECKO, "taskcluster", "kinds", "source-test", "shadow-scheduler.yml"
    )
    strategies = {
        name: task["worker"]["env"]["TASKGRAPH_OPTIMIZE_STRATEGIES"]
        for name, task in tasks.items()
        if name != "task-defaults"
    }
    assert strategies

    for name, strategy_path in strategies.items():
        overrides = find_object(strategy_path)
        assert isinstance(overrides, dict), name
        assert overrides, name


@pytest.mark.parametrize(
    "pushed_files,to_lint,expected",
    [
        pytest.param(
            ["a/b/c.txt"],
            [],
            True,
        ),
        pytest.param(
            ["python/mozlint/a/support_file.txt", "b/c/d.txt"],
            ["python/mozlint/a/support_file.txt"],
            False,
        ),
    ],
    ids=idfn,
)
def test_mozlint_should_remove_task(
    monkeypatch, params, pushed_files, to_lint, expected
):
    import mozlint.pathutils

    class MockParser:
        def __call__(self, *args, **kwargs):
            return []

    def mock_filterpaths(*args, **kwargs):
        return to_lint, None

    monkeypatch.setattr(mozlint.pathutils, "filterpaths", mock_filterpaths)

    opt = SkipUnlessMozlint("")
    monkeypatch.setattr(opt, "mozlint_parser", MockParser())
    params["files_changed"] = pushed_files

    result = opt.should_remove_task(default_tasks[0], params, "")
    assert result == expected


@pytest.mark.parametrize(
    "pushed_files,linter_config,expected",
    [
        pytest.param(
            ["a/b/c.txt"],
            [{"include": ["b/c"]}],
            True,
        ),
        pytest.param(
            ["a/b/c.txt"],
            [{"include": ["a/b"], "exclude": ["a/b/c.txt"]}],
            True,
        ),
        pytest.param(
            ["python/mozlint/a/support_file.txt", "b/c/d.txt"],
            [{}],
            False,
        ),
    ],
    ids=idfn,
)
def test_mozlint_should_remove_task2(
    monkeypatch, params, pushed_files, linter_config, expected
):
    class MockParser:
        def __call__(self, *args, **kwargs):
            return linter_config

    opt = SkipUnlessMozlint("")
    monkeypatch.setattr(opt, "mozlint_parser", MockParser())
    params["files_changed"] = pushed_files

    result = opt.should_remove_task(default_tasks[0], params, "")
    assert result == expected


def test_skip_unless_missing(monkeypatch, responses, params):
    opt = SkipUnlessMissing()
    task = deepcopy(default_tasks[0])
    task.task["deadline"] = "2024-01-02T00:00:00.000Z"
    index = "foo.bar.baz"
    task_id = "abc"
    root_url = "https://taskcluster.example.com"
    monkeypatch.delenv("TASKCLUSTER_PROXY_URL", raising=False)
    monkeypatch.setenv("TASKCLUSTER_ROOT_URL", root_url)

    # Task is missing, don't optimize
    responses.add(
        responses.GET,
        f"{root_url}/api/index/v1/task/{index}",
        status=404,
    )
    result = opt.should_remove_task(task, params, index)
    assert result is False

    # Task is found but failed, don't optimize
    responses.replace(
        responses.GET,
        f"{root_url}/api/index/v1/task/{index}",
        json={"taskId": task_id},
        status=200,
    )
    responses.add(
        responses.GET,
        f"{root_url}/api/queue/v1/task/{task_id}/status",
        json={"status": {"state": "failed"}},
        status=200,
    )
    result = opt.should_remove_task(task, params, index)
    assert result is False

    # Task is found and passed but expires before deadline, don't optimize
    responses.replace(
        responses.GET,
        f"{root_url}/api/index/v1/task/{index}",
        json={"taskId": task_id},
        status=200,
    )
    responses.replace(
        responses.GET,
        f"{root_url}/api/queue/v1/task/{task_id}/status",
        json={"status": {"state": "completed", "expires": "2024-01-01T00:00:00.000Z"}},
        status=200,
    )
    result = opt.should_remove_task(task, params, index)
    assert result is False

    # Task is found and passed and expires after deadline, optimize
    responses.replace(
        responses.GET,
        f"{root_url}/api/index/v1/task/{index}",
        json={"taskId": task_id},
        status=200,
    )
    responses.replace(
        responses.GET,
        f"{root_url}/api/queue/v1/task/{task_id}/status",
        json={"status": {"state": "completed", "expires": "2024-01-03T00:00:00.000Z"}},
        status=200,
    )
    result = opt.should_remove_task(task, params, index)
    assert result is True

    # Task has parameterized deadline, does not raise
    task.task["deadline"] = {"relative-datestamp": "1 day"}
    responses.replace(
        responses.GET,
        f"{root_url}/api/index/v1/task/{index}",
        json={"taskId": task_id},
        status=200,
    )
    responses.replace(
        responses.GET,
        f"{root_url}/api/queue/v1/task/{task_id}/status",
        json={"status": {"state": "completed", "expires": "2024-01-03T00:00:00.000Z"}},
        status=200,
    )
    opt.should_remove_task(task, params, index)


@pytest.mark.parametrize(
    "files_changed,js_source_paths,expected",
    [
        pytest.param(
            [],
            ["browser/components/urlbar"],
            True,
            id="no_files_changed",
        ),
        pytest.param(
            ["browser/components/urlbar/UrlbarView.sys.mjs"],
            ["browser/components/urlbar"],
            False,
            id="matching_file",
        ),
        pytest.param(
            ["browser/components/urlbar/content/quickactions.js"],
            ["browser/components/urlbar", "browser/components/urlbar/content"],
            False,
            id="matching_nested_file",
        ),
        pytest.param(
            ["browser/components/migration/MigrationUtils.sys.mjs"],
            ["browser/components/urlbar"],
            True,
            id="non_matching_file",
        ),
        pytest.param(
            ["README.md", "browser/components/urlbar/UrlbarInput.sys.mjs"],
            ["browser/components/urlbar"],
            False,
            id="one_matching_one_not",
        ),
        pytest.param(
            ["toolkit/actors/AutoScrollChild.sys.mjs"],
            ["toolkit/actors", "browser/components/urlbar"],
            False,
            id="matches_first_path",
        ),
    ],
)
def test_skip_unless_sphinx_js(
    monkeypatch, params, files_changed, js_source_paths, expected
):
    opt = SkipUnlessSphinxJs()

    def mock_get_js_source_paths():
        return js_source_paths

    monkeypatch.setattr(opt, "_get_js_source_paths", mock_get_js_source_paths)
    params["files_changed"] = files_changed

    result = opt.should_remove_task(default_tasks[0], params, None)
    assert result == expected


if __name__ == "__main__":
    main()
