# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


from unittest.mock import MagicMock

import mozunit
import pytest
from tryselect import push
from tryselect.selectors import auto
from tryselect.selectors.auto import AutoParser


def test_strategy_validation():
    parser = AutoParser()
    args = parser.parse_args(["--strategy", "relevant_tests"])
    assert args.strategy == "gecko_taskgraph.optimize:tryselect.relevant_tests"

    args = parser.parse_args([
        "--strategy",
        "gecko_taskgraph.optimize:experimental.relevant_tests",
    ])
    assert args.strategy == "gecko_taskgraph.optimize:experimental.relevant_tests"

    with pytest.raises(SystemExit):
        parser.parse_args(["--strategy", "gecko_taskgraph.optimize:tryselect"])

    with pytest.raises(SystemExit):
        parser.parse_args(["--strategy", "foo"])

    with pytest.raises(SystemExit):
        parser.parse_args(["--strategy", "foo:bar"])


def test_returns_zero_exit_code(run_mach):
    """Test that mach try auto returns exit code 0 when selector returns None."""
    # Run with --no-push which causes push_to_try to return None
    # The exit code should be normalized to 0
    assert run_mach(["try", "auto", "--no-push"]) == 0


def test_returns_zero_with_job_id(run_mach, mock_push_to_lando_try, monkeypatch):
    """Test that mach try auto returns 0 even when push_to_lando_try returns job data."""
    monkeypatch.setattr(auto, "get_try_project", lambda remote: "try")
    with mock_push_to_lando_try:
        assert run_mach(["try", "auto"]) == 0


def test_returns_error_exit_code(run_mach):
    """Test that mach try commands return exit code 1 for runtime validation errors."""
    # Use 'try again' with invalid index to trigger a runtime error (not argparse)
    # This tests that our mach_commands.py run() preserves error code 1
    assert run_mach(["try", "again", "--index", "invalid_value"]) == 1


@pytest.mark.parametrize(
    "remote_url,run_args,supported",
    (
        pytest.param("ssh://hg.mozilla.org/try", {}, True, id="hg try"),
        pytest.param(
            "git@github.com:mozilla-firefox/firefox-dev.git", {}, False, id="github"
        ),
        pytest.param("https://example.com/repo", {}, False, id="unknown"),
        pytest.param(
            "git@github.com:mozilla-firefox/firefox-dev.git",
            {"dry_run": True},
            True,
            id="github dry run",
        ),
    ),
)
def test_unsupported_remote(monkeypatch, capsys, remote_url, run_args, supported):
    monkeypatch.setattr(push, "MACH_TRY_REMOTE", "origin")
    push.vcs.get_remote_url.return_value = remote_url
    push._is_hg_try.cache_clear()
    mock_push_to_try = MagicMock()
    monkeypatch.setattr(auto, "push_to_try", mock_push_to_try)

    if supported:
        auto.run(MagicMock(), **run_args)
        mock_push_to_try.assert_called_once()
    else:
        with pytest.raises(SystemExit):
            auto.run(MagicMock(), **run_args)
        mock_push_to_try.assert_not_called()
        assert "can't push to 'origin'" in capsys.readouterr().out

    push._is_hg_try.cache_clear()


if __name__ == "__main__":
    mozunit.main()
