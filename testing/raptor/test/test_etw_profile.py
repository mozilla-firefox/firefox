# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
import zipfile
from pathlib import Path
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import etw_profile
from etw_profile import (
    CHROME_SYMBOL_SERVER,
    MICROSOFT_SYMBOL_SERVER,
    SAMPLY_TIMEOUT,
    SCHTASKS_RUN_TIMEOUT,
    XPERF_START_TASK,
    XPERF_STOP_TASK,
    ETWProfile,
)


class FakeClock:
    def __init__(self):
        self.now = 1000.0
        self.sleeps = []

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    fake = mock.MagicMock()
    monkeypatch.setattr(etw_profile, "LOG", fake)
    return fake


@pytest.fixture
def clock(monkeypatch):
    fake = FakeClock()
    monkeypatch.setattr(etw_profile.time, "time", fake.time)
    monkeypatch.setattr(etw_profile.time, "sleep", fake.sleep)
    return fake


@pytest.fixture
def env(tmp_path, monkeypatch):
    userprofile = tmp_path / "home"
    (userprofile / "xperf").mkdir(parents=True)
    fetches = tmp_path / "fetches"
    fetches.mkdir()
    upload = tmp_path / "upload"
    upload.mkdir()
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    counter = iter(range(1000))

    def fake_mkdtemp(*args, **kwargs):
        path = scratch / f"tmp{next(counter)}"
        path.mkdir()
        return str(path)

    monkeypatch.setenv("USERPROFILE", str(userprofile))
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(fetches))
    monkeypatch.setattr(etw_profile.tempfile, "mkdtemp", fake_mkdtemp)
    return {"home": userprofile, "fetches": fetches, "upload": upload}


@pytest.fixture
def mock_run():
    with mock.patch.object(etw_profile.subprocess, "run") as run:
        run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="SUCCESS\n", stderr=""
        )
        yield run


@pytest.fixture
def make_profile(env):
    def inner(raptor_config=None, test_config=None):
        if raptor_config is None:
            raptor_config = {}
        if test_config is None:
            test_config = {"name": "tp6"}
        return ETWProfile(str(env["upload"]), raptor_config, test_config)

    return inner


def _write(path, data=b"etl"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def test_init_paths_from_environment(env, make_profile):
    """Check that ETL, profile and samply paths derive from USERPROFILE and MOZ_FETCHES_DIR."""
    profile = make_profile()
    home = env["home"]
    assert profile.test_name == "tp6"
    assert profile.etl_source == home / "xperf" / "combined.etl"
    assert profile.etl_kernel_session_path == home / "xperf" / "kernel_session.etl"
    assert profile.etl_user_session_path == home / "xperf" / "user_session.etl"
    assert profile.upload_dir == env["upload"]
    assert profile.profile == env["upload"] / "profile_etw_tp6_unprocessed.jslb.gz"
    assert profile.samply_path == env["fetches"] / "samply" / "samply.exe"
    assert profile.temp_dir.is_dir()
    assert profile.running is False


def test_init_run_local_skips_samply_and_defaults_name(env, make_profile):
    """Check that local runs skip the samply path and the test name defaults to test."""
    profile = make_profile({"run_local": True}, {})
    assert profile.test_name == "test"
    assert not hasattr(profile, "samply_path")
    assert profile.profile.name == "profile_etw_test_unprocessed.jslb.gz"


def test_init_requires_moz_fetches_dir_in_ci(env, make_profile, monkeypatch):
    """Check that a CI run without MOZ_FETCHES_DIR fails with a KeyError."""
    monkeypatch.delenv("MOZ_FETCHES_DIR")
    with pytest.raises(KeyError, match="MOZ_FETCHES_DIR"):
        make_profile()


def test_run_schtask_success(make_profile, mock_run):
    """Check that _run_schtask runs schtasks /run /tn with the task name and timeout."""
    make_profile()._run_schtask("my_task")
    mock_run.assert_called_once_with(
        ["schtasks", "/run", "/tn", "my_task"],
        capture_output=True,
        text=True,
        timeout=SCHTASKS_RUN_TIMEOUT,
        check=False,
    )


def test_run_schtask_failure_raises(make_profile, mock_run):
    """Check that a failing schtasks call raises RuntimeError and logs stderr."""
    mock_run.return_value = subprocess.CompletedProcess(
        args=[], returncode=1, stdout="", stderr="access denied"
    )
    with mock.patch.object(etw_profile.LOG, "error") as log_error:
        with pytest.raises(RuntimeError, match="schtasks /run /tn my_task failed"):
            make_profile()._run_schtask("my_task")
    assert "access denied" in log_error.call_args[0][0]


def test_start_removes_stale_files_and_runs_start_task(make_profile, mock_run, clock):
    """Check that start deletes stale ETL files, runs the start task and waits for xperf."""
    profile = make_profile()
    stale = [
        _write(profile.etl_source),
        _write(profile.etl_kernel_session_path),
        _write(profile.etl_user_session_path),
    ]
    assert profile.start() is True
    assert profile.running is True
    for path in stale:
        assert not path.exists()
    assert mock_run.call_args[0][0] == ["schtasks", "/run", "/tn", XPERF_START_TASK]
    assert clock.sleeps == [etw_profile.XPERF_STARTUP_TIME]


def test_start_without_stale_files(make_profile, mock_run, clock):
    """Check that start succeeds when no stale ETL files exist."""
    profile = make_profile()
    assert profile.start() is True
    assert mock_run.call_count == 1


def test_start_failure_leaves_not_running(make_profile, mock_run, clock):
    """Check that a failed start task leaves the profiler marked as not running."""
    mock_run.return_value = subprocess.CompletedProcess(
        args=[], returncode=5, stdout="", stderr="boom"
    )
    profile = make_profile()
    with pytest.raises(RuntimeError, match="xperf_kernel_trace_start failed"):
        profile.start()
    assert profile.running is False
    assert clock.sleeps == []


def test_stop_when_not_running(make_profile, mock_run):
    """Check that stop returns False without running a task when nothing is running."""
    profile = make_profile()
    assert profile.stop() is False
    mock_run.assert_not_called()


def test_stop_runs_stop_task(make_profile, mock_run, clock):
    """Check that stop runs the stop task and clears the running flag."""
    profile = make_profile()
    profile.start()
    assert profile.stop() is True
    assert profile.running is False
    assert mock_run.call_args[0][0] == ["schtasks", "/run", "/tn", XPERF_STOP_TASK]


def test_wait_for_combined_etl_ready(make_profile, clock):
    """Check that waiting returns True at once when a non-empty combined ETL exists."""
    profile = make_profile()
    _write(profile.etl_source, b"x" * 10)
    assert profile._wait_for_combined_etl() is True
    assert clock.sleeps == [1]


def test_wait_for_combined_etl_appears_later(make_profile, clock, monkeypatch):
    """Check that waiting keeps polling until the combined ETL appears and is non-empty."""
    profile = make_profile()
    real_sleep = clock.sleep

    def sleep(seconds):
        real_sleep(seconds)
        if len(clock.sleeps) == 2:
            _write(profile.etl_source, b"")
        elif len(clock.sleeps) == 4:
            _write(profile.etl_source, b"data")

    monkeypatch.setattr(etw_profile.time, "sleep", sleep)
    assert profile._wait_for_combined_etl(timeout=100) is True
    assert len(clock.sleeps) == 5


def test_wait_for_combined_etl_timeout(make_profile, clock):
    """Check that waiting returns False after the timeout when no combined ETL appears."""
    profile = make_profile()
    assert profile._wait_for_combined_etl(timeout=3) is False
    assert clock.sleeps == [etw_profile.SCHTASKS_POLL_INTERVAL] * 3


def test_wait_for_combined_etl_empty_file_times_out(make_profile, clock):
    """Check that an ETL file that stays empty makes the wait time out."""
    profile = make_profile()
    _write(profile.etl_source, b"")
    assert profile._wait_for_combined_etl(timeout=4) is False


def test_upload_etl_moves_combined(make_profile, env):
    """Check that upload_etl moves only the combined ETL into the upload directory."""
    profile = make_profile()
    _write(profile.etl_source, b"combined")
    kernel = _write(profile.etl_kernel_session_path)
    with mock.patch.object(profile, "_wait_for_combined_etl") as wait:
        profile.upload_etl()
    wait.assert_called_once_with()
    dest = env["upload"] / "xperf-combined-tp6.etl"
    assert dest.read_bytes() == b"combined"
    assert not profile.etl_source.exists()
    assert kernel.exists()
    assert sorted(p.name for p in env["upload"].iterdir()) == [dest.name]


def test_upload_etl_debug_copies_sessions(make_profile, env):
    """Check that debug uploads also copy the kernel and user session ETL files."""
    profile = make_profile()
    _write(profile.etl_source, b"combined")
    _write(profile.etl_kernel_session_path, b"kernel")
    _write(profile.etl_user_session_path, b"user")
    with mock.patch.object(profile, "_wait_for_combined_etl"):
        profile.upload_etl(debug=True)
    upload = env["upload"]
    assert (upload / "xperf-kernel-tp6.etl").read_bytes() == b"kernel"
    assert (upload / "xperf-user-tp6.etl").read_bytes() == b"user"
    assert (upload / "xperf-combined-tp6.etl").read_bytes() == b"combined"
    assert profile.etl_kernel_session_path.exists()
    assert profile.etl_user_session_path.exists()


def _symbol_args(cmd):
    return cmd[cmd.index(MICROSOFT_SYMBOL_SERVER) + 1 :]


@pytest.fixture
def symbolicate_profile(make_profile, mock_run):
    def inner(raptor_config=None, produce=True):
        profile = make_profile(raptor_config)
        _write(profile.etl_kernel_session_path)
        _write(profile.etl_user_session_path)

        def run(cmd, **kwargs):
            if produce:
                _write(profile.profile, b"profile-data")
            return mock_run.return_value

        mock_run.side_effect = run
        return profile

    return inner


def test_symbolicate_missing_kernel_etl(make_profile, mock_run):
    """Check that symbolicate fails without running samply when the kernel ETL is missing."""
    profile = make_profile()
    _write(profile.etl_user_session_path)
    assert profile.symbolicate() is False
    mock_run.assert_not_called()


def test_symbolicate_missing_user_etl(make_profile, mock_run):
    """Check that symbolicate fails without running samply when the user ETL is missing."""
    profile = make_profile()
    _write(profile.etl_kernel_session_path)
    assert profile.symbolicate() is False
    mock_run.assert_not_called()


def test_symbolicate_without_symbols(symbolicate_profile, mock_run):
    """Check the samply import command and the error logged when no symbols are available."""
    profile = symbolicate_profile()
    with mock.patch.object(etw_profile.LOG, "error") as log_error:
        assert profile.symbolicate() is True
    log_error.assert_called_once_with("Symbols artifact not found.")
    mock_run.assert_called_once()
    args, kwargs = mock_run.call_args
    cmd = args[0]
    assert cmd[:4] == [
        str(profile.samply_path),
        "import",
        str(profile.etl_kernel_session_path),
        str(profile.etl_user_session_path),
    ]
    assert cmd[cmd.index("-o") + 1] == str(profile.profile)
    assert "--presymbolicate" in cmd
    assert cmd[cmd.index(MICROSOFT_SYMBOL_SERVER) - 1] == "--windows-symbol-server"
    assert _symbol_args(cmd) == []
    assert kwargs == {
        "capture_output": True,
        "text": True,
        "timeout": SAMPLY_TIMEOUT,
        "check": False,
    }


def test_symbolicate_chrome_uses_chrome_symbol_server(symbolicate_profile, mock_run):
    """Check that Chrome runs add the Chromium symbol server to the samply command."""
    profile = symbolicate_profile({"app": "chrome"})
    assert profile.symbolicate() is True
    assert _symbol_args(mock_run.call_args[0][0]) == [
        "--windows-symbol-server",
        CHROME_SYMBOL_SERVER,
    ]


def test_symbolicate_custom_car_with_local_symbols(symbolicate_profile, mock_run, env):
    """Check that custom-car runs pass the local Chromium symbol directory to samply."""
    symbol_dir = env["fetches"] / "chromium" / "Default"
    symbol_dir.mkdir(parents=True)
    profile = symbolicate_profile({"app": "custom-car"})
    assert profile.symbolicate() is True
    assert _symbol_args(mock_run.call_args[0][0]) == [
        "--symbol-dir",
        str(symbol_dir),
    ]


def test_symbolicate_custom_car_falls_back_to_breakpad_zip(
    symbolicate_profile, mock_run, env
):
    """Check that the breakpad symbol zip is extracted and passed to samply as a fallback."""
    with zipfile.ZipFile(env["fetches"] / "target.crashreporter-symbols.zip", "w") as z:
        z.writestr("xul.pdb/ABC/xul.sym", "MODULE windows x86_64 ABC xul.pdb")
    profile = symbolicate_profile({"app": "custom-car"})
    assert profile.symbolicate() is True
    symbol_dir = profile.temp_dir / "target.crashreporter-symbols"
    assert _symbol_args(mock_run.call_args[0][0]) == [
        "--breakpad-symbol-dir",
        str(symbol_dir),
    ]
    assert (symbol_dir / "xul.pdb" / "ABC" / "xul.sym").read_text().startswith("MODULE")


def test_symbolicate_chrome_ignores_breakpad_zip(symbolicate_profile, mock_run, env):
    """Check that Chrome runs do not extract or use the breakpad symbol zip."""
    with zipfile.ZipFile(env["fetches"] / "target.crashreporter-symbols.zip", "w") as z:
        z.writestr("a.sym", "x")
    profile = symbolicate_profile({"app": "chrome"})
    assert profile.symbolicate() is True
    assert "--breakpad-symbol-dir" not in mock_run.call_args[0][0]
    assert not (profile.temp_dir / "target.crashreporter-symbols").exists()


def test_symbolicate_logs_samply_output(symbolicate_profile, mock_run):
    """Check that every line of samply stdout and stderr is logged."""
    mock_run.return_value = subprocess.CompletedProcess(
        args=[], returncode=0, stdout="line1\nline2\n", stderr="warn1\n"
    )
    profile = symbolicate_profile()
    with mock.patch.object(etw_profile.LOG, "info") as log_info:
        assert profile.symbolicate() is True
    messages = [c[0][0] for c in log_info.call_args_list]
    assert "samply stdout: line1" in messages
    assert "samply stdout: line2" in messages
    assert "samply stderr: warn1" in messages
    assert any(m.startswith("Profile symbolicated:") for m in messages)


def test_symbolicate_samply_failure(symbolicate_profile, mock_run):
    """Check that a non-zero samply exit code makes symbolicate return False."""
    mock_run.return_value = subprocess.CompletedProcess(
        args=[], returncode=3, stdout="", stderr="error\n"
    )
    profile = symbolicate_profile()
    with mock.patch.object(etw_profile.LOG, "error") as log_error:
        assert profile.symbolicate() is False
    assert "samply exited with code 3" in [c[0][0] for c in log_error.call_args_list]


def test_symbolicate_no_profile_produced(symbolicate_profile, mock_run):
    """Check that symbolicate returns False when samply writes no profile."""
    profile = symbolicate_profile(produce=False)
    with mock.patch.object(etw_profile.LOG, "error") as log_error:
        assert profile.symbolicate() is False
    assert log_error.call_args[0][0].startswith("samply did not produce a profile")


def test_clean_removes_sessions_and_temp_dirs(make_profile):
    """Check that clean removes session ETLs and temp dirs but keeps the combined ETL."""
    profile = make_profile()
    kernel = _write(profile.etl_kernel_session_path)
    user = _write(profile.etl_user_session_path)
    combined = _write(profile.etl_source)
    _write(profile.temp_dir / "nested" / "file")
    temp_profile_dir = Path(profile.temp_profile_dir)
    assert temp_profile_dir.is_dir()
    profile.clean()
    assert not kernel.exists()
    assert not user.exists()
    assert combined.exists()
    assert not profile.temp_dir.exists()
    assert not temp_profile_dir.exists()


def test_clean_is_idempotent(make_profile):
    """Check that calling clean twice does not fail."""
    profile = make_profile()
    profile.clean()
    profile.clean()
    assert not profile.temp_dir.exists()


if __name__ == "__main__":
    mozunit.main()
