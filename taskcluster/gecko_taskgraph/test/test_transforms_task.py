# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main
from taskgraph.config import GraphConfig

from gecko_taskgraph.test.conftest import FakeParameters, FakeTransformConfig
from gecko_taskgraph.transforms import job  # noqa: F401
from gecko_taskgraph.transforms.task import (
    TREEHERDER_ROOT_URL,
    _run_task_suffix,
    check_run_task_caches,
    get_default_priority,
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


TASK_PRIORITY_CONFIG = {
    "by-project": {
        "mozilla-release": "highest",
        "mozilla-esr.*": {
            "by-shipping": {"true": "very-high", "default": "low"},
        },
        "mozilla-beta": {
            "by-shipping": {"true": "very-high", "default": "low"},
        },
        "mozilla-central": "medium",
        "autoland": "low",
        "default": "very-low",
    }
}


@pytest.mark.parametrize(
    "project,shipping,expected",
    [
        # Beta and ESR are integration branches by default, and are only
        # raised when the push is flagged as shipping.
        pytest.param("mozilla-beta", False, "low", id="beta"),
        pytest.param("mozilla-beta", True, "very-high", id="beta-shipping"),
        pytest.param("mozilla-esr140", False, "low", id="esr"),
        pytest.param("mozilla-esr140", True, "very-high", id="esr-shipping"),
        # Every other project ignores the shipping flag.
        pytest.param("mozilla-release", True, "highest", id="release-shipping"),
        pytest.param("mozilla-central", True, "medium", id="central-shipping"),
        pytest.param("autoland", True, "low", id="autoland-shipping"),
        pytest.param("try", True, "very-low", id="try-shipping"),
        pytest.param("mozilla-central", False, "medium", id="central"),
        pytest.param("autoland", False, "low", id="autoland"),
        pytest.param("try", False, "very-low", id="try"),
    ],
)
def test_get_default_priority(project, shipping, expected):
    # GraphConfig is hashable, which get_default_priority requires as it is cached.
    graph_config = GraphConfig({"task-priority": TASK_PRIORITY_CONFIG}, root_dir=".")
    assert get_default_priority(graph_config, project, shipping) == expected


def _checkout_task(clone_with, cache, *run_task_args):
    return {
        "label": "fake-task-label",
        "attributes": {"clone_with": clone_with},
        "task": {
            "payload": {
                "command": [
                    f"/builds/worker/bin/run-task-{clone_with}",
                    "--gecko-checkout=/builds/worker/checkouts/gecko",
                    *run_task_args,
                    "--",
                    "true",
                ],
                "cache": {
                    f"test-domain-level-1-{cache}-v3-{_run_task_suffix(clone_with)}": (
                        "/builds/worker/checkouts"
                    ),
                },
            },
        },
    }


def _check_run_task_caches(run_transform, task):
    params = FakeParameters({"level": "1", "repository_type": "hg"})
    return list(run_transform(check_run_task_caches, task, params=params))


def test_check_run_task_caches_git_sparse_shallow(run_transform):
    task = _checkout_task(
        "git",
        "checkouts-git-shallow",
        "--gecko-shallow-clone",
        "--gecko-sparse-profile=build/sparse-profiles/mach",
    )
    assert _check_run_task_caches(run_transform, task) == [task]


def test_check_run_task_caches_git_missing_sparse_profile(run_transform):
    task = _checkout_task(
        "git",
        "checkouts-git-shallow",
        "--gecko-shallow-clone",
        "--gecko-sparse-profile=build/sparse-profiles/does-not-exist",
    )
    with pytest.raises(Exception, match="is using non-existant sparse profile"):
        _check_run_task_caches(run_transform, task)


def test_check_run_task_caches_git_sparse_then_shallow(run_transform):
    task = _checkout_task(
        "git",
        "checkouts-git",
        "--gecko-sparse-profile=build/sparse-profiles/mach",
        "--gecko-shallow-clone",
    )
    with pytest.raises(Exception, match="not using a shallow checkout cache"):
        _check_run_task_caches(run_transform, task)


def test_check_run_task_caches_hg_sparse_git_cache(run_transform):
    task = _checkout_task(
        "hg",
        "checkouts-git",
        "--gecko-sparse-profile=build/sparse-profiles/mach",
    )
    with pytest.raises(Exception, match="not using a sparse checkout cache"):
        _check_run_task_caches(run_transform, task)


if __name__ == "__main__":
    main()
