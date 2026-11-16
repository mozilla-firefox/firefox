# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import inspect
import os
import sys
from argparse import ArgumentParser
from textwrap import dedent

import mozunit
import msgspec
import pytest
from gecko_taskgraph.parameters import TryTaskConfig
from tryselect import push, task_config
from tryselect.task_config import (
    NON_COMPILED_SUFFIXES,
    Artifact,
    Pernosco,
    PushDate,
    all_task_configs,
)

TC_URL = "https://taskcluster.example.com"
TH_URL = "https://treeherder.mozilla.org"

# task configs have a list of tests of the form (input, expected)
TASK_CONFIG_TESTS = {
    "artifact": [
        (["--no-artifact"], None),
        (
            ["--artifact"],
            {"try_task_config": {"use-artifact-builds": True, "disable-pgo": True}},
        ),
    ],
    "chemspill-prio": [
        ([], None),
        (["--chemspill"], {"try_task_config": {"priority": "low"}}),
        (["--chemspill-priority"], {"try_task_config": {"priority": "low"}}),
    ],
    "env": [
        ([], None),
        (
            ["--env", "foo=bar", "--env", "num=10"],
            {"try_task_config": {"env": {"foo": "bar", "num": "10"}}},
        ),
        (
            ["--profiler"],
            {
                "try_task_config": {
                    "env": {
                        "MOZ_PROFILER_STARTUP": "1",
                        "MOZ_PROFILER_STARTUP_FEATURES": "default",
                        "MOZ_PROFILER_STARTUP_INTERVAL": "1",
                    }
                }
            },
        ),
        (
            ["--record"],
            {"try_task_config": {"env": {"MOZ_RECORD_TEST": "1"}}},
        ),
        (
            ["--profiler", "--record"],
            {
                "try_task_config": {
                    "env": {
                        "MOZ_PROFILER_STARTUP": "1",
                        "MOZ_PROFILER_STARTUP_FEATURES": "default",
                        "MOZ_PROFILER_STARTUP_INTERVAL": "1",
                        "MOZ_RECORD_TEST": "1",
                    }
                }
            },
        ),
    ],
    "path": [
        ([], None),
        (
            ["dom/indexedDB"],
            {
                "try_task_config": {
                    "env": {"MOZHARNESS_TEST_PATHS": '{"xpcshell": ["dom/indexedDB"]}'}
                }
            },
        ),
        (
            ["dom/indexedDB/test/head.js"],
            {
                "try_task_config": {
                    "env": {
                        "MOZHARNESS_TEST_PATHS": '{"xpcshell": ["dom/indexedDB/test"]}'
                    }
                }
            },
        ),
        (
            ["dom/indexedDB/test/test_add_put.html", "--allow-testfile-path"],
            {
                "try_task_config": {
                    "env": {
                        "MOZHARNESS_TEST_PATHS": '{"xpcshell": ["dom/indexedDB/test/test_add_put.html"]}'
                    }
                }
            },
        ),
        (
            ["dom/indexedDB", "testing"],
            {
                "try_task_config": {
                    "env": {
                        "MOZHARNESS_TEST_PATHS": '{"xpcshell": ["dom/indexedDB", "testing"]}'
                    }
                }
            },
        ),
        (["invalid/path"], SystemExit),
    ],
    "pernosco": [
        ([], None),
    ],
    "pushdate": [
        ([], None),
        (
            ["--pushdate", "20260424043035"],
            {
                "build_date": 1777005035,
                "moz_build_date": "20260424043035",
                "pushdate": 1777005035,
            },
        ),
        (["--pushdate", "notadate"], SystemExit),
    ],
    "rebuild": [
        ([], None),
        (["--rebuild", "10"], {"try_task_config": {"rebuild": 10}}),
        (["--rebuild", "1"], {"try_task_config": {"rebuild": 1}}),
        (["--rebuild", "0"], SystemExit),
        (["--rebuild", "21"], SystemExit),
    ],
    "repeat-tests": [
        ([], None),
        (["--repeat-tests", "3"], {"try_task_config": {"repeat-tests": 3}}),
        (["--repeat-tests", "21"], SystemExit),
    ],
    "worker-overrides": [
        ([], None),
        (
            ["--worker-override", "b-linux=worker/pool"],
            {"try_task_config": {"worker-overrides": {"b-linux": "worker/pool"}}},
        ),
        (
            [
                "--worker-override",
                "b-linux=worker/pool",
                "--worker-override",
                "b-linux=other/pool",
            ],
            SystemExit,
        ),
        (
            ["--worker-suffix", "b-linux=-dev"],
            {
                "try_task_config": {
                    "worker-overrides": {"b-linux": "gecko-1/b-linux-dev"}
                }
            },
        ),
        (
            [
                "--worker-override",
                "b-linux=worker/pool--worker-suffix",
                "b-linux=-dev",
            ],
            SystemExit,
        ),
    ],
    "new-test-config": [
        ([], None),
        (["--new-test-config"], {"try_task_config": {"new-test-config": True}}),
    ],
}


@pytest.fixture
def config_patch_resolver(patch_resolver):
    def inner(paths, allow_testfile_path):
        patch_resolver(
            [], [{"flavor": "xpcshell", "srcdir_relpath": path} for path in paths]
        )

    return inner


@pytest.fixture(autouse=True)
def mock_root_url(monkeypatch):
    monkeypatch.delenv("TASKCLUSTER_PROXY_URL", raising=False)
    monkeypatch.setenv("TASKCLUSTER_ROOT_URL", TC_URL)


@pytest.fixture(autouse=True)
def mock_get_worker_type(mocker):
    mocker.patch(
        "gecko_taskgraph.util.workertypes.get_worker_type",
        side_effect=lambda gc, worker_type, parameters: ("gecko-1", worker_type),
    )


def test_task_configs(config_patch_resolver, task_config, args, expected):
    parser = ArgumentParser()

    cfg = all_task_configs[task_config]()
    cfg.add_arguments(parser)

    if inspect.isclass(expected) and issubclass(expected, BaseException):
        with pytest.raises(expected):
            args = parser.parse_args(args)
            if task_config == "path":
                config_patch_resolver(**vars(args))

            cfg.get_parameters(**vars(args))
    else:
        args = parser.parse_args(args)
        if task_config == "path":
            config_patch_resolver(**vars(args))

        params = cfg.get_parameters(**vars(args))
        assert params == expected
        if params and "try_task_config" in params:
            msgspec.convert(params["try_task_config"], TryTaskConfig)


@pytest.fixture
def patch_ssh_user(mocker):
    def inner(user):
        mock_stdout = mocker.Mock()
        mock_stdout.stdout = dedent(
            f"""
            key1 foo
            user {user}
            key2 bar
            """
        )
        return mocker.patch(
            "tryselect.util.ssh.subprocess.run", return_value=mock_stdout
        )

    return inner


def test_pernosco(patch_ssh_user):
    patch_ssh_user("user@mozilla.com")
    parser = ArgumentParser()

    cfg = Pernosco()
    cfg.add_arguments(parser)
    args = parser.parse_args(["--pernosco"])
    params = cfg.get_parameters(**vars(args))
    assert params == {"try_task_config": {"env": {"PERNOSCO": "1"}, "pernosco": True}}


def test_pushdate():
    parser = ArgumentParser()

    cfg = PushDate()
    cfg.add_arguments(parser)
    args = parser.parse_args(["--pushdate", "20260424043035"])
    params = cfg.get_parameters(**vars(args))
    assert params == {
        "build_date": 1777005035,
        "moz_build_date": "20260424043035",
        "pushdate": 1777005035,
    }


@pytest.mark.parametrize(
    "changed_files,answer,expected",
    [
        (
            ["foo.js", "bar/baz.ts", "qux.toml"],
            "y",
            {"try_task_config": {"use-artifact-builds": True, "disable-pgo": True}},
        ),
        (
            ["foo.jsx", "bar.mjs", "baz.cjs", "qux.tsx", "quux.json"],
            "",
            {"try_task_config": {"use-artifact-builds": True, "disable-pgo": True}},
        ),
        (
            [
                "browser/foo.css",
                "browser/foo.scss",
                "browser/icon.ico",
                "browser/icon.png",
                "browser/icon.svg",
                "browser/jar.mn",
                "browser/moz.build",
                "browser/test.html.headers",
                "browser/test.png^headers^",
            ],
            "y",
            {"try_task_config": {"use-artifact-builds": True, "disable-pgo": True}},
        ),
        (["foo.js", "bar/baz.ts", "qux.toml"], "n", None),
        ([], "y", None),
        (["foo.js", "bar.cpp"], "y", None),
    ],
)
def test_artifact_non_compiled_patch_prompt(mocker, changed_files, answer, expected):
    push.vcs.get_outgoing_files.return_value = changed_files
    mocker.patch.object(Artifact, "is_artifact_build", return_value=False)
    mock_prompt = mocker.patch.object(
        task_config, "prompt_with_timeout", return_value=answer
    )

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args([])

    assert cfg.get_parameters(**vars(args)) == expected

    is_eligible = bool(changed_files) and all(
        f.endswith(NON_COMPILED_SUFFIXES) for f in changed_files
    )
    assert mock_prompt.called is is_eligible


def test_artifact_non_compiled_patch_prompt_reprompts_on_invalid_answer(mocker):
    push.vcs.get_outgoing_files.return_value = ["foo.js"]
    mocker.patch.object(Artifact, "is_artifact_build", return_value=False)
    mock_prompt = mocker.patch.object(
        task_config, "prompt_with_timeout", side_effect=["maybe", "y"]
    )

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args([])

    params = cfg.get_parameters(**vars(args))
    assert params == {
        "try_task_config": {"use-artifact-builds": True, "disable-pgo": True}
    }
    assert mock_prompt.call_count == 2


def test_artifact_non_compiled_patch_prompt_unanswered(mocker):
    # An unanswered prompt must not block the push, and must not silently turn
    # on artifact builds either.
    push.vcs.get_outgoing_files.return_value = ["foo.js"]
    mocker.patch.object(Artifact, "is_artifact_build", return_value=False)
    mock_prompt = mocker.patch.object(
        task_config, "prompt_with_timeout", return_value=None
    )

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args([])

    assert cfg.get_parameters(**vars(args)) is None
    assert mock_prompt.call_count == 1


def test_artifact_non_compiled_patch_vcs_failure(mocker):
    # A VCS failure shouldn't block the push, we just don't prompt.
    push.vcs.get_outgoing_files.side_effect = Exception("hg failed")
    mocker.patch.object(Artifact, "is_artifact_build", return_value=False)
    mock_prompt = mocker.patch.object(task_config, "prompt_with_timeout")

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args([])

    assert cfg.get_parameters(**vars(args)) is None
    mock_prompt.assert_not_called()


def test_artifact_configured_build_skips_prompt(mocker):
    # A build already configured with artifact builds is handled by the
    # pre-existing silent check, so it shouldn't also trigger the prompt.
    push.vcs.get_outgoing_files.return_value = ["foo.cpp"]
    mocker.patch.object(Artifact, "is_artifact_build", return_value=True)
    mock_prompt = mocker.patch.object(task_config, "prompt_with_timeout")

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args([])

    params = cfg.get_parameters(**vars(args))
    assert params == {
        "try_task_config": {"use-artifact-builds": True, "disable-pgo": True}
    }
    mock_prompt.assert_not_called()


def test_artifact_explicit_flag_skips_prompt(mocker):
    push.vcs.get_outgoing_files.return_value = ["foo.js"]
    mock_prompt = mocker.patch.object(task_config, "prompt_with_timeout")

    parser = ArgumentParser()
    cfg = Artifact()
    cfg.add_arguments(parser)
    args = parser.parse_args(["--artifact"])

    params = cfg.get_parameters(**vars(args))
    assert params == {
        "try_task_config": {"use-artifact-builds": True, "disable-pgo": True}
    }
    mock_prompt.assert_not_called()


class FakeStdin:
    """A stdin replacement backed by a pipe, so prompts can be answered."""

    def __init__(self, answer=None, isatty=True):
        self._read_fd, self._write_fd = os.pipe()
        if answer is not None:
            os.write(self._write_fd, answer.encode("utf-8"))
        self._file = os.fdopen(self._read_fd)
        self._isatty = isatty

    def isatty(self):
        return self._isatty

    def fileno(self):
        return self._file.fileno()

    def readline(self):
        return self._file.readline()

    def close(self):
        self._file.close()
        os.close(self._write_fd)


@pytest.mark.skipif(
    sys.platform == "win32", reason="exercises the select() based code path"
)
@pytest.mark.parametrize(
    "answer,expected",
    [("y\n", "y"), ("  N \n", "N"), ("\n", "")],
)
def test_prompt_with_timeout_answered(mocker, answer, expected):
    with contextlib.closing(FakeStdin(answer)) as stdin:
        mocker.patch.object(sys, "stdin", stdin)
        assert task_config.prompt_with_timeout("Answer? ", 30) == expected


@pytest.mark.skipif(
    sys.platform == "win32", reason="exercises the select() based code path"
)
def test_prompt_with_timeout_unanswered(mocker):
    with contextlib.closing(FakeStdin()) as stdin:
        mocker.patch.object(sys, "stdin", stdin)
        assert task_config.prompt_with_timeout("Answer? ", 0.01) is None


def test_prompt_with_timeout_not_interactive(mocker):
    with contextlib.closing(FakeStdin("y\n", isatty=False)) as stdin:
        mocker.patch.object(sys, "stdin", stdin)
        assert task_config.prompt_with_timeout("Answer? ", 30) is None


def test_extensions(mocker):
    parser = ArgumentParser()
    cfg = all_task_configs["extensions"]()
    cfg.add_arguments(parser)

    def fake_get(url, **kwargs):
        addon_id = url.rstrip("/").split("/")[-1]
        resp = mocker.Mock()
        resp.json.return_value = {
            "current_version": {"file": {"url": f"https://amo/{addon_id}.xpi"}}
        }
        return resp

    mocker.patch("tryselect.task_config.requests.get", side_effect=fake_get)
    mocker.patch("tryselect.task_config.requests.head", return_value=mocker.Mock())

    # No extensions requested -> no parameters.
    args = parser.parse_args([])
    assert cfg.get_parameters(**vars(args)) is None

    # GUIDs/slugs are resolved; a .xpi URL is passed through unchanged.
    args = parser.parse_args([
        "--extension",
        "a@b",
        "--extension",
        "https://example.com/x.xpi",
    ])
    params = cfg.get_parameters(**vars(args))
    assert params == {
        "try_task_config": {
            "env": {
                "PERF_FLAGS": (
                    "install-extension=https://amo/a@b.xpi,https://example.com/x.xpi"
                )
            }
        }
    }

    # Local paths are rejected (CI can't reach them).
    for bad in ("/tmp/local.xpi", "ext.xpi"):
        args = parser.parse_args(["--extension", bad])
        with pytest.raises(Exception):
            cfg.get_parameters(**vars(args))


def test_exisiting_tasks(mocker, responses, patch_ssh_user):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    user = "user@example.com"
    rev = "a" * 40
    task_id = "abc"
    label_to_taskid = {"task-foo": "123", "task-bar": "456"}

    args = ["--use-existing-tasks"]
    args = parser.parse_args(args)

    responses.add(
        responses.GET,
        f"{TH_URL}/api/project/try/push/?count=1&author={user}",
        json={"meta": {"count": 1}, "results": [{"revision": rev}]},
    )

    responses.add(
        responses.GET,
        f"{TC_URL}/api/index/v1/task/gecko.v2.try.revision.{rev}.taskgraph.decision",
        json={"taskId": task_id},
    )

    responses.add(
        responses.GET,
        f"{TC_URL}/api/queue/v1/task/{task_id}/artifacts/public%2flabel-to-taskid.json",
        status=303,
        json={"url": f"{TC_URL}/artifacts/label-to-taskid.json"},
    )
    responses.add(
        responses.GET,
        f"{TC_URL}/artifacts/label-to-taskid.json",
        json=label_to_taskid,
    )

    m = patch_ssh_user(user)
    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": label_to_taskid}

    m.assert_called_once_with(
        ["ssh", "-G", "hg.mozilla.org"], text=True, check=True, capture_output=True
    )


def test_exisiting_tasks_task_id(responses):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    task_id = "abc"
    label_to_taskid = {"task-foo": "123", "task-bar": "456"}

    args = ["--use-existing-tasks", f"task-id={task_id}"]
    args = parser.parse_args(args)

    responses.add(
        responses.GET,
        f"{TC_URL}/api/queue/v1/task/{task_id}/artifacts/public%2flabel-to-taskid.json",
        status=303,
        json={"url": f"{TC_URL}/artifacts/label-to-taskid.json"},
    )
    responses.add(
        responses.GET,
        f"{TC_URL}/artifacts/label-to-taskid.json",
        json=label_to_taskid,
    )

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": label_to_taskid}


def test_exisiting_tasks_rev(responses):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    rev = "aaaaaa"
    task_id = "abc"
    label_to_taskid = {"task-foo": "123", "task-bar": "456"}

    args = ["--use-existing-tasks", f"rev={rev}"]
    args = parser.parse_args(args)

    responses.add(
        responses.GET,
        f"{TC_URL}/api/index/v1/task/gecko.v2.try.revision.{rev}.taskgraph.decision",
        json={"taskId": task_id},
    )

    responses.add(
        responses.GET,
        f"{TC_URL}/api/queue/v1/task/{task_id}/artifacts/public%2flabel-to-taskid.json",
        status=303,
        json={"url": f"{TC_URL}/artifacts/label-to-taskid.json"},
    )
    responses.add(
        responses.GET,
        f"{TC_URL}/artifacts/label-to-taskid.json",
        json=label_to_taskid,
    )

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": label_to_taskid}


def test_exisiting_tasks_project(responses):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    task_id = "abc"
    label_to_taskid = {"task-foo": "123", "task-bar": "456"}

    args = ["--use-existing-tasks", "project=mozilla-central"]
    args = parser.parse_args(args)

    responses.add(
        responses.GET,
        f"{TC_URL}/api/index/v1/task/gecko.v2.mozilla-central.latest.taskgraph.decision",
        json={"taskId": task_id},
    )

    responses.add(
        responses.GET,
        f"{TC_URL}/api/queue/v1/task/{task_id}/artifacts/public%2flabel-to-taskid.json",
        status=303,
        json={"url": f"{TC_URL}/artifacts/label-to-taskid.json"},
    )
    responses.add(
        responses.GET,
        f"{TC_URL}/artifacts/label-to-taskid.json",
        json=label_to_taskid,
    )

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": label_to_taskid}


def test_use_indexed_tasks(responses):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args([
        "--use-indexed-tasks",
        "task-foo=gecko.v2.foo.latest",
        "--use-indexed-tasks",
        "task-bar=gecko.v2.bar.latest",
    ])

    for index_path, task_id in (
        ("gecko.v2.foo.latest", "123"),
        ("gecko.v2.bar.latest", "456"),
    ):
        responses.add(
            responses.GET,
            f"{TC_URL}/api/index/v1/task/{index_path}",
            json={"taskId": task_id},
        )

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": {"task-foo": "123", "task-bar": "456"}}


def test_use_indexed_tasks_overrides_use_existing_tasks(mocker):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args([
        "--use-existing-tasks",
        "task-id=abc",
        "--use-indexed-tasks",
        "task-foo=gecko.v2.foo.latest",
    ])

    mocker.patch(
        "taskgraph.util.taskcluster.get_artifact",
        return_value={"task-foo": "123", "task-bar": "456"},
    )
    mocker.patch("taskgraph.util.taskcluster.find_task_id", return_value="789")

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": {"task-foo": "789", "task-bar": "456"}}


@pytest.mark.parametrize(
    "filters,expected",
    [
        (
            ["--existing-tasks-include", "^(docker-image|toolchain)-"],
            {
                "docker-image-ubuntu2404-test": "1",
                "docker-image-debian12-base": "2",
                "toolchain-linux64-clang": "3",
            },
        ),
        (
            ["--existing-tasks-exclude", "^build-"],
            {
                "docker-image-ubuntu2404-test": "1",
                "docker-image-debian12-base": "2",
                "toolchain-linux64-clang": "3",
                "fetch-psutil": "4",
            },
        ),
        (
            [
                "--existing-tasks-include",
                "^docker-image-",
                "--existing-tasks-include",
                "^fetch-",
                "--existing-tasks-exclude",
                "^docker-image-ubuntu2404-test$",
            ],
            {"docker-image-debian12-base": "2", "fetch-psutil": "4"},
        ),
    ],
)
def test_existing_tasks_filters(mocker, filters, expected):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args(["--use-existing-tasks", "task-id=abc"] + filters)

    mocker.patch(
        "taskgraph.util.taskcluster.get_artifact",
        return_value={
            "docker-image-ubuntu2404-test": "1",
            "docker-image-debian12-base": "2",
            "toolchain-linux64-clang": "3",
            "fetch-psutil": "4",
            "build-linux64/opt": "5",
        },
    )

    params = cfg.get_parameters(**vars(args))
    assert params == {"existing_tasks": expected}


def test_existing_tasks_filters_dont_apply_to_indexed_tasks(mocker):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args([
        "--use-existing-tasks",
        "task-id=abc",
        "--existing-tasks-include",
        "^toolchain-",
        "--use-indexed-tasks",
        "build-linux64/opt=gecko.v2.foo.latest",
    ])

    mocker.patch(
        "taskgraph.util.taskcluster.get_artifact",
        return_value={"toolchain-linux64-clang": "3", "build-linux64/opt": "5"},
    )
    mocker.patch("taskgraph.util.taskcluster.find_task_id", return_value="789")

    params = cfg.get_parameters(**vars(args))
    assert params == {
        "existing_tasks": {"toolchain-linux64-clang": "3", "build-linux64/opt": "789"}
    }


@pytest.mark.parametrize(
    "filters",
    [["--existing-tasks-include", "^toolchain-"], ["--existing-tasks-exclude", "x"]],
)
def test_existing_tasks_filters_require_use_existing_tasks(filters):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args(filters)
    with pytest.raises(SystemExit):
        cfg.get_parameters(**vars(args))


@pytest.mark.parametrize("label_and_index", ["task-foo", "=gecko.v2.foo", "task-foo="])
def test_use_indexed_tasks_invalid(label_and_index):
    parser = ArgumentParser()
    cfg = all_task_configs["existing-tasks"]()
    cfg.add_arguments(parser)

    args = parser.parse_args(["--use-indexed-tasks", label_and_index])
    with pytest.raises(SystemExit):
        cfg.get_parameters(**vars(args))


if __name__ == "__main__":
    mozunit.main()
