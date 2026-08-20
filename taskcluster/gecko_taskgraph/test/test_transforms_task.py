# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from gecko_taskgraph.test.conftest import FakeParameters, FakeTransformConfig
from gecko_taskgraph.transforms import job  # noqa: F401
from gecko_taskgraph.transforms.task import (
    TREEHERDER_ROOT_URL,
    get_treeherder_link,
    get_treeherder_project,
    try_task_config_env,
)


@pytest.mark.parametrize(
    "params,branch_map,expected_project",
    [
        pytest.param(
            {
                "head_ref": "refs/heads/main",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {},
            "test-project",
            id="no_branch_map",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/main",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {"main": "firefox-main"},
            "firefox-main",
            id="branch_map_match",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/develop",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {"main": "firefox-main"},
            "test-project",
            id="branch_map_no_match",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/main",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {
                "by-project": {
                    "test-project": {"main": "firefox-main"},
                    "default": {},
                },
            },
            "firefox-main",
            id="by-project-match",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/develop",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {
                "by-project": {
                    "test-project": {"main": "firefox-main"},
                    "default": {},
                },
            },
            "test-project",
            id="by-project-no-match",
        ),
        pytest.param(
            {
                "head_ref": "",
                "project": "test-project",
                "repository_type": "hg",
                "tasks_for": "hg-push",
            },
            {"main": "firefox-main"},
            "test-project",
            id="empty_head_ref",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/release",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-pull-request",
            },
            {"release": "mozilla-release"},
            "test-project-pr",
            id="pull_request",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/test-branch",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {"test-.*": "foo", "default": "bar"},
            "foo",
            id="regex",
        ),
        pytest.param(
            {
                "head_ref": "refs/heads/release",
                "project": "test-project",
                "repository_type": "git",
                "tasks_for": "github-push",
            },
            {"test-.*": "foo", "default": "bar"},
            "bar",
            id="regex-default",
        ),
    ],
)
def test_get_treeherder_project(params, branch_map, expected_project):
    graph_config = {
        "project-repo-param-prefix": "",
        "treeherder": {"group-names": {}},
    }
    if branch_map:
        graph_config["treeherder"]["branch-map"] = branch_map

    config = FakeTransformConfig(
        params=FakeParameters(params),
        graph_config=graph_config,
    )

    project = get_treeherder_project(config)
    assert project == expected_project


def test_get_treeherder_link():
    branch = "main"
    repo = "firefox-main"
    graph_config = {
        "project-repo-param-prefix": "",
        "treeherder": {"group-names": {}, "branch-map": {branch: repo}},
    }

    params = {
        "head_ref": f"refs/heads/{branch}",
        "head_rev": "def456",
        "project": "test-project",
        "repository_type": "git",
        "tasks_for": "github-push",
    }
    config = FakeTransformConfig(
        params=FakeParameters(params),
        graph_config=graph_config,
    )

    link = get_treeherder_link(config)
    expected_link = f"{TREEHERDER_ROOT_URL}/#/jobs?repo={repo}&revision={params['head_rev']}&selectedTaskRun=<self>"
    assert link == expected_link


@pytest.mark.parametrize(
    "implementation",
    ("docker-worker", "generic-worker"),
)
def test_try_task_config_env_applies(run_transform, implementation):
    """Env vars from try_task_config are applied to workers that have an env field.

    This covers both voluptuous (LegacySchema) and msgspec Struct payload
    builder schemas, since docker-worker and generic-worker were converted to
    msgspec in the upstream taskgraph library.
    """
    task = {
        "worker": {
            "implementation": implementation,
            "env": {"EXISTING": "value"},
        }
    }
    params = FakeParameters({"try_task_config": {"env": {"NEW_VAR": "new_value"}}})
    result = list(run_transform(try_task_config_env, task, params=params))[0]
    assert result["worker"]["env"] == {"EXISTING": "value", "NEW_VAR": "new_value"}


def test_try_task_config_env_skips_no_env_impl(run_transform):
    """Env vars are not applied to implementations without an env field."""
    task = {
        "worker": {
            "implementation": "bouncer-aliases",
        }
    }
    params = FakeParameters({"try_task_config": {"env": {"NEW_VAR": "new_value"}}})
    result = list(run_transform(try_task_config_env, task, params=params))[0]
    assert "env" not in result["worker"]


def test_try_task_config_env_no_env_config(run_transform):
    """Transform is a no-op when try_task_config has no env key."""
    task = {
        "worker": {
            "implementation": "docker-worker",
            "env": {"EXISTING": "value"},
        }
    }
    params = FakeParameters({"try_task_config": {}})
    result = list(run_transform(try_task_config_env, task, params=params))[0]
    assert result["worker"]["env"] == {"EXISTING": "value"}


def _test_paths_task(**attributes):
    return {
        "attributes": attributes,
        "worker": {
            "implementation": "docker-worker",
            "env": {"MOZHARNESS_TEST_PATHS": '{"suite": ["dir/manifest.toml"]}'},
        },
    }


def test_try_task_config_env_keeps_restricted_test_paths(run_transform):
    """A task whose manifests were restricted to what try asked for keeps the
    share of the request that its own chunk runs."""
    task = _test_paths_task(**{"test-manifests-restricted": True})
    params = FakeParameters({
        "try_task_config": {"env": {"MOZHARNESS_TEST_PATHS": '{"suite": ["dir"]}'}}
    })

    result = list(run_transform(try_task_config_env, task, params=params))[0]

    assert result["worker"]["env"]["MOZHARNESS_TEST_PATHS"] == (
        '{"suite": ["dir/manifest.toml"]}'
    )


def test_try_task_config_env_overrides_unrestricted_test_paths(run_transform):
    """A task that kept the whole suite needs what try asked for, since the
    harness is what filters the suite down for it."""
    task = _test_paths_task()
    params = FakeParameters({
        "try_task_config": {"env": {"MOZHARNESS_TEST_PATHS": '{"suite": ["dir"]}'}}
    })

    result = list(run_transform(try_task_config_env, task, params=params))[0]

    assert result["worker"]["env"]["MOZHARNESS_TEST_PATHS"] == '{"suite": ["dir"]}'


if __name__ == "__main__":
    main()
