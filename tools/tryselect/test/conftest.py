# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from unittest.mock import MagicMock, patch

import mach_initialize
import pytest
import yaml
from moztest.resolve import TestResolver
from responses import RequestsMock
from taskgraph.graph import Graph
from taskgraph.task import Task
from taskgraph.taskgraph import TaskGraph
from tryselect import push, tasks


@pytest.fixture
def responses():
    with RequestsMock() as rsps:
        yield rsps


@pytest.fixture
def tg(request):
    if not hasattr(request.module, "TASKS"):
        pytest.fail(
            "'tg' fixture used from a module that didn't define the TASKS variable"
        )

    tasks = request.module.TASKS
    for task in tasks:
        task.setdefault("task", {})
        task["task"].setdefault("tags", {})

    tasks = {t["label"]: Task(**t) for t in tasks}
    return TaskGraph(tasks, Graph(tasks.keys(), set()))


@pytest.fixture
def patch_resolver(monkeypatch):
    def inner(suites, tests):
        def fake_test_metadata(*args, **kwargs):
            return suites, tests

        monkeypatch.setattr(TestResolver, "resolve_metadata", fake_test_metadata)

    return inner


@pytest.fixture()
def mock_push_to_lando_try():
    return patch(
        "tryselect.push.push_to_lando_try",
        return_value={
            "lando_instance": "lando-test",
            "lando_job_id": 42,
            "duration": 1.5,
        },
    )


@pytest.fixture(autouse=True)
def patch_vcs(monkeypatch):
    attrs = {
        "name": "git",
        "path": push.vcs.path,
        "push.return_value": "abc123fakegitsha",
        "push_to_try.return_value": None,
        "base_ref_as_commit.return_value": "def456fakebaserev",
    }
    mock = MagicMock()
    mock.configure_mock(**attrs)
    monkeypatch.setattr(push, "vcs", mock)


@pytest.fixture(autouse=True)
def mock_taskcluster_secret(monkeypatch):
    mock_client = MagicMock()
    mock_client.get.return_value = {"secret": {"ssh_privkey": "fake_ssh_key"}}
    monkeypatch.setattr(push, "get_client", MagicMock(return_value=mock_client))


@pytest.fixture(autouse=True)
def mock_tree_status(monkeypatch):
    monkeypatch.setattr(push, "check_tree_status", MagicMock())


@pytest.fixture(scope="session")
def run_mach():
    mach = mach_initialize.initialize(tasks.build.topsrcdir, ["try"])

    def inner(args):
        return mach.run(args)

    return inner


def pytest_generate_tests(metafunc):
    if all(
        fixture in metafunc.fixturenames
        for fixture in ("task_config", "args", "expected")
    ):

        def load_tests():
            for task_config, tests in metafunc.module.TASK_CONFIG_TESTS.items():
                for args, expected in tests:
                    yield (task_config, args, expected)

        tests = list(load_tests())
        ids = ["{} {}".format(t[0], " ".join(t[1])).strip() for t in tests]
        metafunc.parametrize("task_config,args,expected", tests, ids=ids)

    elif all(
        fixture in metafunc.fixturenames for fixture in ("shared_name", "shared_preset")
    ):
        preset_path = os.path.join(
            push.build.topsrcdir, "tools", "tryselect", "try_presets.yml"
        )
        with open(preset_path) as fh:
            presets = list(yaml.safe_load(fh).items())

        ids = [p[0] for p in presets]

        # Mark fuzzy presets on Windows xfail due to fzf not being installed.
        if os.name == "nt":
            for i, preset in enumerate(presets):
                if preset[1]["selector"] == "fuzzy":
                    presets[i] = pytest.param(*preset, marks=pytest.mark.xfail)

        metafunc.parametrize("shared_name,shared_preset", presets, ids=ids)
