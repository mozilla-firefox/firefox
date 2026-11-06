# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import os
from types import SimpleNamespace

import mozpack.path as mozpath
import mozunit
import pytest
from tryselect import tasks as tasks_mod
from tryselect.tasks import (
    WATCHMAN_TRIGGER_NAME,
    cache_key,
    filter_tasks_by_paths,
    filter_tasks_by_worker_type,
    resolve_tests_by_suite,
    suggest_watchman_setup,
)


class task:
    def __init__(self, workerType):
        self.workerType = workerType

    @property
    def task(self):
        return {"workerType": self.workerType}


@pytest.mark.parametrize(
    "tasks, params, expected",
    (
        pytest.param(
            {
                "foobar/xpcshell-1": task("t-unittest-314"),
                "foobar/mochitest": task("t-unittest-157"),
                "foobar/xpcshell-gpu": task("t-unittest-314-gpu"),
                "foobar/xpcshell": task("t-unittest-314"),
            },
            {"try_task_config": {"worker-types": ["t-unittest-314"]}},
            [
                "foobar/xpcshell-1",
                "foobar/xpcshell",
            ],
            id="single worker",
        ),
        pytest.param(
            {
                "foobar/xpcshell-1": task("t-unittest-314"),
                "foobar/mochitest": task("t-unittest-157"),
                "foobar/xpcshell-gpu": task("t-unittest-314-gpu"),
                "foobar/xpcshell": task("t-unittest-314"),
            },
            {
                "try_task_config": {
                    "worker-types": ["t-unittest-314", "t-unittest-314-gpu"]
                }
            },
            [
                "foobar/xpcshell-1",
                "foobar/xpcshell-gpu",
                "foobar/xpcshell",
            ],
            id="multiple workers worker",
        ),
        pytest.param(
            {
                "foobar/xpcshell-1": task("t-unittest-314"),
                "foobar/mochitest": task("t-unittest-157"),
                "foobar/xpcshell-gpu": task("t-unittest-314-gpu"),
                "foobar/xpcshell": task("t-unittest-314"),
            },
            {"try_task_config": {"worker-types": ["t-unittest-157"]}},
            [
                "foobar/mochitest",
            ],
            id="single task",
        ),
        pytest.param(
            {
                "foobar/xpcshell-1": task("t-unittest-314"),
                "foobar/mochitest": task("t-unittest-157"),
                "foobar/xpcshell-gpu": task("t-unittest-314-gpu"),
                "foobar/xpcshell": task("t-unittest-314"),
            },
            {"try_task_config": {"worker-types": []}},
            [
                "foobar/xpcshell-1",
                "foobar/mochitest",
                "foobar/xpcshell-gpu",
                "foobar/xpcshell",
            ],
            id="no worker",
        ),
        pytest.param(
            {
                "foobar/xpcshell-1": task("t-unittest-314"),
                "foobar/mochitest": task("t-unittest-157"),
                "foobar/xpcshell-gpu": task("t-unittest-314-gpu"),
                "foobar/xpcshell": task("t-unittest-314"),
            },
            {"try_task_config": {"worker-types": ["fake-worker"]}},
            [],
            id="invalid worker",
        ),
    ),
)
def test_filter_tasks_by_worker_type(patch_resolver, tasks, params, expected):
    assert list(filter_tasks_by_worker_type(tasks, params)) == expected


def test_filter_tasks_by_paths(patch_resolver):
    tasks = {"foobar/xpcshell-1": {}, "foobar/mochitest": {}, "foobar/xpcshell": {}}

    patch_resolver(["xpcshell"], {})
    assert list(filter_tasks_by_paths(tasks, "dummy")) == []

    patch_resolver([], [{"flavor": "xpcshell"}])
    assert list(filter_tasks_by_paths(tasks, "dummy")) == [
        "foobar/xpcshell-1",
        "foobar/xpcshell",
    ]


@pytest.mark.parametrize(
    "input, tests, expected",
    (
        pytest.param(
            ["xpcshell.js"],
            [{"flavor": "xpcshell", "srcdir_relpath": "xpcshell.js"}],
            {"xpcshell": ["xpcshell.js"]},
            id="single test",
        ),
        pytest.param(
            ["xpcshell.ini"],
            [
                {
                    "flavor": "xpcshell",
                    "srcdir_relpath": "xpcshell.js",
                    "manifest_relpath": "xpcshell.ini",
                },
            ],
            {"xpcshell": ["xpcshell.ini"]},
            id="single manifest",
        ),
        pytest.param(
            ["xpcshell.js", "mochitest.js"],
            [
                {"flavor": "xpcshell", "srcdir_relpath": "xpcshell.js"},
                {"flavor": "mochitest", "srcdir_relpath": "mochitest.js"},
            ],
            {
                "xpcshell": ["xpcshell.js"],
                "mochitest-plain": ["mochitest.js"],
            },
            id="two tests",
        ),
        pytest.param(
            ["test/xpcshell.ini"],
            [
                {
                    "flavor": "xpcshell",
                    "srcdir_relpath": "test/xpcshell.js",
                    "manifest_relpath": os.path.join("test", "xpcshell.ini"),
                },
            ],
            {"xpcshell": ["test/xpcshell.ini"]},
            id="mismatched path separators",
        ),
    ),
)
def test_resolve_tests_by_suite(patch_resolver, input, tests, expected):
    patch_resolver([], tests)
    assert resolve_tests_by_suite(input) == expected


@pytest.mark.parametrize(
    "attr,params,disable_target_task_filter,target_tasks_method,expected",
    (
        ("target_task_set", None, False, None, "target_task_set"),
        ("target_task_set", {"project": "autoland"}, False, None, "target_task_set"),
        (
            "target_task_set",
            {"project": "mozilla-central"},
            False,
            None,
            "target_task_set",
        ),
        ("target_task_set", None, True, None, "target_task_set-uncommon"),
        ("target_task_set", None, False, "foo", "target_task_set-target_foo"),
        ("full_task_set", {"project": "pine"}, False, None, "full_task_set-pine"),
        ("full_task_set", None, True, None, "full_task_set"),
        ("full_task_set", None, True, "foo", "full_task_set-target_foo"),
    ),
)
def test_cache_key(
    attr, params, disable_target_task_filter, target_tasks_method, expected
):
    assert (
        cache_key(attr, params, disable_target_task_filter, target_tasks_method)
        == expected
    )


class _FakeStdout(io.StringIO):
    def __init__(self, tty):
        super().__init__()
        self._tty = tty

    def isatty(self):
        return self._tty


def _run_suggest(
    monkeypatch,
    *,
    tty=True,
    watchman="/usr/bin/watchman",
    run_result=None,
    run_exc=None,
    platform="linux",
    shell="/bin/bash",
):
    """Drive suggest_watchman_setup() with the environment stubbed out and
    return whatever it printed."""
    stdout = _FakeStdout(tty)
    monkeypatch.setattr(tasks_mod.sys, "stdout", stdout)
    monkeypatch.setattr(tasks_mod.sys, "platform", platform)
    if shell is None:
        monkeypatch.delenv("SHELL", raising=False)
    else:
        monkeypatch.setenv("SHELL", shell)
    monkeypatch.setattr(tasks_mod.shutil, "which", lambda _: watchman)

    def fake_run(*args, **kwargs):
        if run_exc is not None:
            raise run_exc
        return run_result

    monkeypatch.setattr(tasks_mod.subprocess, "run", fake_run)
    suggest_watchman_setup()
    return stdout.getvalue()


def test_suggest_watchman_setup_shows_hint(monkeypatch):
    # watchman is watching the checkout and the trigger is not registered yet.
    out = _run_suggest(
        monkeypatch,
        run_result=SimpleNamespace(returncode=0, stdout="some-other-trigger\n"),
    )
    watchman_json = mozpath.normsep(os.path.join(tasks_mod.here, "watchman.json"))
    assert f'\n    watchman -j < "{watchman_json}"\n' in out


def test_suggest_watchman_setup_hint_windows_shell(monkeypatch):
    out = _run_suggest(
        monkeypatch,
        platform="win32",
        run_result=SimpleNamespace(returncode=0, stdout="some-other-trigger\n"),
    )
    watchman_json = mozpath.normsep(os.path.join(tasks_mod.here, "watchman.json"))
    assert f'\n    watchman -j < "{watchman_json}"\n' in out


def test_suggest_watchman_setup_hint_windows_cmd(monkeypatch):
    out = _run_suggest(
        monkeypatch,
        platform="win32",
        shell=None,
        run_result=SimpleNamespace(returncode=0, stdout="some-other-trigger\n"),
    )
    watchman_json = os.path.join(tasks_mod.here, "watchman.json")
    assert f'\n    more "{watchman_json}" | watchman -j\n' in out


def test_suggest_watchman_setup_silent_when_not_tty(monkeypatch):
    out = _run_suggest(
        monkeypatch,
        tty=False,
        run_result=SimpleNamespace(returncode=0, stdout=""),
    )
    assert out == ""


def test_suggest_watchman_setup_silent_when_watchman_missing(monkeypatch):
    out = _run_suggest(monkeypatch, watchman=None)
    assert out == ""


def test_suggest_watchman_setup_silent_when_not_watching(monkeypatch):
    # A non-zero `trigger-list` means watchman is not watching this checkout.
    out = _run_suggest(
        monkeypatch,
        run_result=SimpleNamespace(returncode=1, stdout=""),
    )
    assert out == ""


def test_suggest_watchman_setup_silent_when_already_registered(monkeypatch):
    out = _run_suggest(
        monkeypatch,
        run_result=SimpleNamespace(returncode=0, stdout=f"{WATCHMAN_TRIGGER_NAME}\n"),
    )
    assert out == ""


def test_suggest_watchman_setup_silent_on_subprocess_error(monkeypatch):
    out = _run_suggest(monkeypatch, run_exc=OSError("boom"))
    assert out == ""


if __name__ == "__main__":
    mozunit.main()
