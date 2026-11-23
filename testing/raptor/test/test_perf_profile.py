# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import itertools
import os
import signal
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

import perf_profile
from perf_profile import (
    PERF_STOP_TIMEOUT,
    PERF_TERM_TIMEOUT,
    SAMPLING_FREQUENCY,
    SAMPLY_TIMEOUT,
    PerfProfile,
)

ENV_KEYS = (
    "MOZBUILD_STATE_PATH",
    "MOZ_FETCHES_DIR",
    "MOZ_DEVELOPER_OBJ_DIR",
    "MOZ_CRASHREPORTER",
    "MOZ_CRASHREPORTER_DISABLE",
    "MOZ_CRASHREPORTER_NO_REPORT",
)


@pytest.fixture
def env(monkeypatch):
    with mock.patch.dict(os.environ):
        for key in ENV_KEYS:
            os.environ.pop(key, None)
        yield os.environ


@pytest.fixture
def temp_dirs(tmp_path, monkeypatch):
    counter = itertools.count()

    def fake_mkdtemp(*args, **kwargs):
        path = tmp_path / f"tmp{next(counter)}"
        path.mkdir()
        return str(path)

    monkeypatch.setattr(perf_profile.tempfile, "mkdtemp", fake_mkdtemp)
    return tmp_path


@pytest.fixture
def log(monkeypatch):
    fake = mock.MagicMock()
    monkeypatch.setattr(perf_profile, "LOG", fake)
    return fake


def make_samply(root):
    samply = Path(root) / "samply" / "samply"
    samply.parent.mkdir(parents=True, exist_ok=True)
    samply.write_bytes(b"")
    return samply


@pytest.fixture
def make_profiler(tmp_path, env, temp_dirs, log):
    def inner(local=True, raptor_config=None, test_config=None):
        upload_dir = tmp_path / "upload"
        upload_dir.mkdir(exist_ok=True)
        toolchain = tmp_path / ("mozbuild" if local else "fetches")
        make_samply(toolchain)
        env["MOZBUILD_STATE_PATH" if local else "MOZ_FETCHES_DIR"] = str(toolchain)
        config = {"app": "firefox", "platform": "linux", "run_local": local}
        config.update(raptor_config or {})
        return PerfProfile(
            str(upload_dir),
            config,
            test_config if test_config is not None else {"name": "speedometer3"},
        )

    return inner


def test_rejects_unsupported_app(tmp_path, env, temp_dirs, log):
    """Check that construction fails for an app outside PERF_PROFILE_APPS."""
    with pytest.raises(RuntimeError, match="only supports"):
        PerfProfile(str(tmp_path), {"app": "chrome", "platform": "linux"}, {})


def test_rejects_non_linux(tmp_path, env, temp_dirs, log):
    """Check that construction fails when the platform is not Linux."""
    with pytest.raises(RuntimeError, match="only supported on Linux"):
        PerfProfile(str(tmp_path), {"app": "firefox", "platform": "mac"}, {})


@pytest.mark.parametrize("local", [True, False])
def test_missing_samply(tmp_path, env, temp_dirs, log, local):
    """Check that a missing samply binary raises with its path and bootstrap hint."""
    env["MOZBUILD_STATE_PATH"] = str(tmp_path / "nothing")
    env["MOZ_FETCHES_DIR"] = str(tmp_path / "nothing")
    config = {"app": "firefox", "platform": "linux", "run_local": local}
    with pytest.raises(FileNotFoundError) as exc:
        PerfProfile(str(tmp_path), config, {})
    assert str(tmp_path / "nothing" / "samply" / "samply") in str(exc.value)
    assert ("mach bootstrap" in str(exc.value)) == local


def test_local_samply_defaults_to_home_mozbuild(tmp_path, env, temp_dirs, log):
    """Check that local runs fall back to ~/.mozbuild for samply."""
    monkey_home = tmp_path / "home"
    make_samply(monkey_home / ".mozbuild")
    with mock.patch.object(Path, "home", return_value=monkey_home):
        profiler = PerfProfile(
            str(tmp_path),
            {"app": "firefox", "platform": "linux", "run_local": True},
            {},
        )
    assert profiler.samply_path == monkey_home / ".mozbuild" / "samply" / "samply"
    assert profiler.test_name == "test"


def test_init_paths_and_firefox_env(make_profiler, env):
    """Check that profile paths are set and Firefox env defaults keep existing values."""
    profiler = make_profiler(raptor_config={"environment": {"IONPERF": "custom"}})
    assert profiler.upload_dir == Path(profiler.upload_dir)
    assert profiler.temp_dir.is_dir()
    assert profiler.profile == (
        profiler.upload_dir / "profile_perf_speedometer3_unprocessed.jslb.gz"
    )
    assert profiler.perf_data_path == profiler.temp_dir / "perf-speedometer3.data"
    assert profiler.perf_process is None
    assert profiler.running is False

    environment = profiler.raptor_config["environment"]
    assert environment["IONPERF"] == "custom"
    assert environment["MOZ_USE_PERFORMANCE_MARKER_FILE"] == "1"
    assert environment["MOZ_DISABLE_CONTENT_SANDBOX"] == "1"
    assert environment["JIT_OPTION_enableStubFramePointers"] == "true"
    assert environment["PERF_SPEW_DIR"] == str(profiler.temp_dir)
    assert environment["MOZ_PERFORMANCE_MARKER_DIR"] == str(profiler.temp_dir)


def test_local_no_symbols(make_profiler, env, log):
    """Check that a local run without any symbols skips symbolication setup."""
    profiler = make_profiler()
    assert profiler.symbol_dir is None
    assert "MOZ_CRASHREPORTER_NO_REPORT" not in env
    assert any(
        "No symbol directory found" in c.args[0] for c in log.warning.call_args_list
    )
    assert any(
        "Skipping profile symbolication" in c.args[0] for c in log.info.call_args_list
    )


def test_local_symbols_path(make_profiler, env, tmp_path):
    """Check that a local symbols_path is used and enables the crash reporter."""
    symbols = tmp_path / "symbols"
    symbols.mkdir()
    profiler = make_profiler(raptor_config={"symbols_path": str(symbols)})
    assert profiler.symbol_dir == symbols
    assert env["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert env["MOZ_CRASHREPORTER"] == "1"
    assert "MOZ_CRASHREPORTER_DISABLE" not in env


def test_local_symbols_from_objdir(make_profiler, env, tmp_path):
    """Check that local runs fall back to the objdir crashreporter-symbols folder."""
    sym_dir = tmp_path / "obj" / "dist" / "crashreporter-symbols"
    sym_dir.mkdir(parents=True)
    env["MOZ_DEVELOPER_OBJ_DIR"] = str(tmp_path / "obj")
    profiler = make_profiler(raptor_config={"symbols_path": str(tmp_path / "nope")})
    assert profiler.symbol_dir == sym_dir
    assert env["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert env["MOZ_CRASHREPORTER"] == "1"


def test_local_objdir_without_symbols(make_profiler, env, tmp_path, log):
    """Check that a missing objdir symbols folder yields no symbol dir."""
    env["MOZ_DEVELOPER_OBJ_DIR"] = str(tmp_path / "obj")
    profiler = make_profiler()
    assert profiler.symbol_dir is None
    assert any("mach buildsymbols" in c.args[0] for c in log.warning.call_args_list)


def test_ci_missing_symbol_zip(make_profiler, env, log):
    """Check that CI runs without the symbols zip yield no symbol dir."""
    profiler = make_profiler(local=False)
    assert profiler.symbol_dir is None
    assert any("Symbol zip not found" in c.args[0] for c in log.warning.call_args_list)
    assert profiler.samply_path == (Path(env["MOZ_FETCHES_DIR"]) / "samply" / "samply")


def test_ci_extracts_symbol_zip(make_profiler, env, tmp_path):
    """Check that CI runs extract the symbols zip and disable the crash reporter."""
    fetches = tmp_path / "fetches"
    fetches.mkdir()
    with zipfile.ZipFile(fetches / "target.crashreporter-symbols.zip", "w") as zf:
        zf.writestr("libxul.so/ABC/libxul.so.sym", "MODULE Linux")
    profiler = make_profiler(local=False)
    assert profiler.symbol_dir == profiler.temp_dir / "symbols"
    assert (profiler.symbol_dir / "libxul.so" / "ABC" / "libxul.so.sym").is_file()
    assert env["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert env["MOZ_CRASHREPORTER_DISABLE"] == "1"
    assert "MOZ_CRASHREPORTER" not in env


def test_ci_corrupt_symbol_zip(make_profiler, tmp_path, log):
    """Check that a corrupt CI symbols zip is logged and yields no symbol dir."""
    fetches = tmp_path / "fetches"
    fetches.mkdir()
    (fetches / "target.crashreporter-symbols.zip").write_bytes(b"not a zip")
    profiler = make_profiler(local=False)
    assert profiler.symbol_dir is None
    assert any("Failed to extract" in c.args[0] for c in log.warning.call_args_list)


@pytest.mark.parametrize(
    "returncode,stdout,stderr,messages",
    [
        (0, b"", b"", []),
        (1, b"", b"", ["No perf processes to kill"]),
        (0, b"out", b"err", ["pkill stdout: b'out'", "pkill stderr: b'err'"]),
    ],
)
def test_pkill_process(make_profiler, log, returncode, stdout, stderr, messages):
    """Check that pkill is invoked and its return code and output are logged."""
    profiler = make_profiler()
    log.reset_mock()
    result = subprocess.CompletedProcess([], returncode, stdout, stderr)
    with mock.patch.object(subprocess, "run", return_value=result) as run:
        profiler._pkill_process("perf")
    run.assert_called_once_with(["pkill", "perf"], capture_output=True, check=False)
    logged = [c.args[0] for c in log.info.call_args_list]
    assert f"pkill return code: {returncode}" in logged
    for message in messages:
        assert message in logged
    if returncode == 0:
        assert "No perf processes to kill" not in logged


def test_log_perf_output_tail_and_close(make_profiler, log):
    """Check that only the last 64 KiB of perf stderr is logged before closing."""
    profiler = make_profiler()
    stderr = io.BytesIO(b"x" * 70000 + b"tail\xff")
    profiler.perf_stderr = stderr
    profiler._log_perf_output()
    message = log.info.call_args.args[0]
    assert message.startswith("Perf output: ")
    output = message[len("Perf output: ") :]
    assert len(output) == 65536
    assert output.endswith("tail�")
    assert stderr.closed
    assert profiler.perf_stderr is None


def test_log_perf_output_empty_or_none(make_profiler, log):
    """Check that absent or empty perf stderr logs nothing."""
    profiler = make_profiler()
    log.reset_mock()
    profiler._log_perf_output()
    profiler.perf_stderr = io.BytesIO()
    profiler._log_perf_output()
    log.info.assert_not_called()
    assert profiler.perf_stderr is None


def make_process(wait_effects=None, returncode=0):
    process = mock.MagicMock()
    process.returncode = returncode
    if wait_effects is not None:
        process.wait.side_effect = wait_effects
    return process


def test_start_local_command(make_profiler):
    """Check that a local start runs perf record with the expected options."""
    profiler = make_profiler()
    process = make_process([subprocess.TimeoutExpired("perf", 1)])
    with mock.patch.object(profiler, "_pkill_process") as pkill, mock.patch.object(
        subprocess, "run"
    ) as run, mock.patch.object(subprocess, "Popen", return_value=process) as popen:
        assert profiler.start() is True
    pkill.assert_called_once_with("perf")
    run.assert_called_once_with(["sudo", "-v"], check=False)
    cmd = popen.call_args.args[0]
    assert cmd[:4] == ["sudo", "-n", "perf", "record"]
    assert "-a" in cmd and "-g" in cmd
    assert cmd[cmd.index("-k") + 1] == "mono"
    assert cmd[cmd.index("-F") + 1] == str(SAMPLING_FREQUENCY)
    assert cmd[cmd.index("-o") + 1] == str(profiler.perf_data_path)
    kwargs = popen.call_args.kwargs
    assert kwargs["stdin"] == subprocess.PIPE
    assert kwargs["stdout"] == subprocess.DEVNULL
    assert kwargs["stderr"] is profiler.perf_stderr
    process.wait.assert_called_once_with(timeout=1)
    assert profiler.running is True
    assert profiler.perf_process is process
    profiler.perf_stderr.close()


def test_start_ci_command_writes_to_data_file(make_profiler):
    """Check that a CI start runs the wrapper with stdout sent to perf.data."""
    profiler = make_profiler(local=False)
    process = make_process([subprocess.TimeoutExpired("perf", 1)])
    with mock.patch.object(profiler, "_pkill_process") as pkill, mock.patch.object(
        subprocess, "Popen", return_value=process
    ) as popen:
        assert profiler.start() is True
    pkill.assert_not_called()
    assert popen.call_args.args[0] == [
        "sudo",
        "-n",
        "/usr/local/bin/record-system-perf",
    ]
    stdout = popen.call_args.kwargs["stdout"]
    assert stdout.name == str(profiler.perf_data_path)
    assert stdout.closed
    assert profiler.perf_data_path.is_file()
    profiler.perf_stderr.close()


def test_start_process_exits_early(make_profiler, log):
    """Check that start returns False when perf exits within a second."""
    profiler = make_profiler(local=False)
    process = make_process(returncode=1)
    with mock.patch.object(subprocess, "Popen", return_value=process):
        assert profiler.start() is False
    process.stdin.close.assert_called_once()
    assert profiler.running is False
    assert profiler.perf_stderr is None
    log.error.assert_called_with("Perf process failed to start properly")


def test_start_popen_raises(make_profiler):
    """Check that a Popen failure is re-raised after cleaning up stderr."""
    profiler = make_profiler(local=False)
    with mock.patch.object(subprocess, "Popen", side_effect=OSError("no sudo")):
        with pytest.raises(OSError, match="no sudo"):
            profiler.start()
    assert profiler.perf_stderr is None
    assert profiler.running is False


def running_profiler(profiler, process, data=b"perfdata"):
    profiler.perf_process = process
    profiler.running = True
    if data is not None:
        profiler.perf_data_path.write_bytes(data)
    return profiler


def test_stop_not_running(make_profiler, log):
    """Check that stop returns False when perf was never started."""
    profiler = make_profiler()
    assert profiler.stop() is False
    profiler.running = True
    assert profiler.stop() is False
    log.warning.assert_called_with("Perf is not running")


@pytest.mark.parametrize("local", [True, False])
def test_stop_success(make_profiler, local):
    """Check that stop finalizes perf and keeps a non-empty recording."""
    profiler = running_profiler(make_profiler(local=local), make_process())
    assert profiler.stop() is True
    process = profiler.perf_process
    if local:
        process.send_signal.assert_called_once_with(signal.SIGINT)
    else:
        process.send_signal.assert_not_called()
    process.stdin.close.assert_called_once()
    process.wait.assert_called_once_with(timeout=PERF_STOP_TIMEOUT)
    assert profiler.running is False
    assert profiler.perf_data_path.is_file()


def test_stop_nonzero_exit_removes_data(make_profiler, log):
    """Check that a non-zero perf exit status deletes the recording."""
    profiler = running_profiler(make_profiler(), make_process(returncode=2))
    assert profiler.stop() is False
    assert not profiler.perf_data_path.exists()
    log.error.assert_called_with("Perf exited with status 2")


@pytest.mark.parametrize("data", [None, b""])
def test_stop_missing_or_empty_data(make_profiler, log, data):
    """Check that a missing or empty recording makes stop fail."""
    profiler = running_profiler(make_profiler(), make_process(), data=data)
    assert profiler.stop() is False
    assert not profiler.perf_data_path.exists()
    assert "missing or empty" in log.error.call_args.args[0]


def test_stop_timeout_terminates(make_profiler):
    """Check that a stop timeout terminates perf and deletes the recording."""
    process = make_process([subprocess.TimeoutExpired("perf", 1), None])
    profiler = running_profiler(make_profiler(), process)
    assert profiler.stop() is False
    process.terminate.assert_called_once()
    process.kill.assert_not_called()
    assert process.wait.call_args_list == [
        mock.call(timeout=PERF_STOP_TIMEOUT),
        mock.call(timeout=PERF_TERM_TIMEOUT),
    ]
    assert not profiler.perf_data_path.exists()
    assert profiler.running is False


def test_terminate_perf_force_kill(make_profiler, log):
    """Check that perf is killed when it ignores SIGTERM."""
    process = make_process([subprocess.TimeoutExpired("perf", 1), None])
    profiler = make_profiler()
    profiler.perf_process = process
    profiler._terminate_perf()
    process.terminate.assert_called_once()
    process.kill.assert_called_once()
    assert process.wait.call_args_list[-1] == mock.call(timeout=10)
    log.error.assert_called_with("Perf ignored SIGTERM. Force killing")


def test_symbolicate_missing_data(make_profiler, log):
    """Check that symbolicate does nothing without a perf.data file."""
    profiler = make_profiler()
    with mock.patch.object(subprocess, "run") as run:
        profiler.symbolicate()
    run.assert_not_called()
    assert "does not exist" in log.error.call_args.args[0]


def fake_samply(returncode=0, write=True, stdout="", stderr=""):
    def run(cmd, **kwargs):
        if write:
            Path(cmd[cmd.index("-o") + 1]).write_bytes(b"profile")
        return subprocess.CompletedProcess(cmd, returncode, stdout, stderr)

    return mock.MagicMock(side_effect=run)


def test_symbolicate_success_with_symbols(make_profiler, tmp_path, log):
    """Check that samply gets the symbol dir and its profile is moved to upload."""
    symbols = tmp_path / "symbols"
    symbols.mkdir()
    profiler = make_profiler(raptor_config={"symbols_path": str(symbols)})
    profiler.perf_data_path.write_bytes(b"perfdata")
    run = fake_samply(stdout="a\nb", stderr="c")
    with mock.patch.object(subprocess, "run", run):
        profiler.symbolicate()
    cmd = run.call_args.args[0]
    temp_profile = profiler.temp_dir / profiler.profile.name
    assert cmd[:3] == [
        str(profiler.samply_path),
        "import",
        str(profiler.perf_data_path),
    ]
    assert cmd[cmd.index("-o") + 1] == str(temp_profile)
    assert cmd[cmd.index("--breakpad-symbol-dir") + 1] == str(symbols)
    assert run.call_args.kwargs["timeout"] == SAMPLY_TIMEOUT
    assert profiler.profile.read_bytes() == b"profile"
    assert not temp_profile.exists()
    logged = [c.args[0] for c in log.info.call_args_list]
    assert "Samply stdout: a" in logged
    assert "Samply stdout: b" in logged
    assert "Samply stderr: c" in logged


def test_symbolicate_without_symbols(make_profiler):
    """Check that samply runs without a symbol dir argument when none exists."""
    profiler = make_profiler()
    profiler.perf_data_path.write_bytes(b"perfdata")
    run = fake_samply()
    with mock.patch.object(subprocess, "run", run):
        profiler.symbolicate()
    assert "--breakpad-symbol-dir" not in run.call_args.args[0]
    assert profiler.profile.is_file()


def test_symbolicate_samply_fails(make_profiler, log):
    """Check that a failing samply leaves no uploaded profile."""
    profiler = make_profiler()
    profiler.perf_data_path.write_bytes(b"perfdata")
    with mock.patch.object(subprocess, "run", fake_samply(returncode=1)):
        profiler.symbolicate()
    assert not profiler.profile.exists()
    log.error.assert_called_with("Samply failed to symbolicate profile")


def test_symbolicate_samply_no_output(make_profiler, log):
    """Check that a samply run with no output file leaves no uploaded profile."""
    profiler = make_profiler()
    profiler.perf_data_path.write_bytes(b"perfdata")
    with mock.patch.object(subprocess, "run", fake_samply(write=False)):
        profiler.symbolicate()
    assert not profiler.profile.exists()
    assert "did not produce" in log.error.call_args.args[0]


def test_clean_removes_temp_dirs(make_profiler):
    """Check that clean removes both temp directories and is idempotent."""
    profiler = make_profiler()
    base_temp = Path(profiler.temp_profile_dir)
    assert base_temp.is_dir()
    profiler.clean()
    assert not profiler.temp_dir.exists()
    assert not base_temp.exists()
    profiler.clean()


if __name__ == "__main__":
    mozunit.main()
