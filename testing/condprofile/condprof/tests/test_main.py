# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozunit
import pytest

from condprof.tests import fakearsenic  # noqa

from condprof import main, runner  # isort: skip


@pytest.fixture
def calls(monkeypatch):
    calls = []

    def fake_run(*args):
        calls.append(args)

    monkeypatch.setattr(runner, "run", fake_run)
    monkeypatch.setenv("CONDPROF_RUNNER", "0")
    return calls


def test_main_passes_default_options_to_runner(calls, monkeypatch):
    monkeypatch.setattr(main.sys, "platform", "linux")
    main.main(["archive"])
    assert os.environ["CONDPROF_RUNNER"] == "1"
    assert calls == [
        (
            "archive",
            None,
            "all",
            None,
            "default",
            False,
            "/tmp/archives",
            False,
            False,
            "geckodriver",
            None,
        )
    ]


def test_main_passes_cli_options_to_runner(calls):
    main.main([
        "out",
        "--firefox",
        "ff",
        "--scenario",
        "settled",
        "--profile",
        "p",
        "--customization",
        "webext",
        "--visible",
        "--archives-dir",
        "a",
        "--force-new",
        "--strict",
        "--geckodriver",
        "gd",
        "--device-name",
        "p6",
    ])
    assert calls == [
        ("out", "ff", "settled", "p", "webext", True, "a", True, True, "gd", "p6")
    ]


def test_main_failure_exits_with_retry_code(monkeypatch, capsys):
    def fail(*args):
        raise ValueError("boom")

    monkeypatch.setattr(runner, "run", fail)
    monkeypatch.setenv("CONDPROF_RUNNER", "0")
    with pytest.raises(SystemExit) as exc:
        main.main(["archive"])
    assert exc.value.code == 4
    assert "ValueError: boom" in capsys.readouterr().out


if __name__ == "__main__":
    mozunit.main()
