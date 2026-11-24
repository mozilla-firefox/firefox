# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from subprocess import CompletedProcess
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import samply_profile
from samply_profile import SamplyProfile

CRASHREPORTER_VARS = (
    "MOZ_CRASHREPORTER",
    "MOZ_CRASHREPORTER_NO_REPORT",
    "MOZ_CRASHREPORTER_DISABLE",
)


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


@pytest.fixture
def clock(monkeypatch):
    fake = FakeClock()
    monkeypatch.setattr(samply_profile.time, "time", fake.time)
    monkeypatch.setattr(samply_profile.time, "sleep", fake.sleep)
    return fake


@pytest.fixture(autouse=True)
def isolated_env(tmp_path, monkeypatch):
    for var in CRASHREPORTER_VARS + ("MOZ_DEVELOPER_OBJ_DIR",):
        monkeypatch.delenv(var, raising=False)
    counter = iter(range(1000))

    def mkdtemp():
        path = tmp_path / f"tmp{next(counter)}"
        path.mkdir()
        return str(path)

    monkeypatch.setattr(tempfile, "mkdtemp", mkdtemp)


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    log = mock.Mock()
    monkeypatch.setattr(samply_profile, "LOG", log)
    return log


def make_toolchain(root, samply=True, clang=True, sdks=("MacOSX14.0.sdk",)):
    root.mkdir(parents=True, exist_ok=True)
    if samply:
        (root / "samply").mkdir()
        (root / "samply" / "samply").touch()
    if clang:
        (root / "clang" / "bin").mkdir(parents=True)
        (root / "clang" / "bin" / "clang++").touch()
    for sdk in sdks:
        (root / sdk).mkdir()
    return root


@pytest.fixture
def local_toolchain(tmp_path, monkeypatch):
    root = make_toolchain(tmp_path / "mozbuild")
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    return root


@pytest.fixture
def upload_dir(tmp_path):
    path = tmp_path / "upload"
    path.mkdir()
    return path


def make_config(**overrides):
    config = {
        "app": "firefox",
        "platform": "mac",
        "run_local": True,
        "binary": "/Applications/Firefox.app/Contents/MacOS/firefox",
    }
    config.update(overrides)
    return config


@pytest.fixture
def profiler(local_toolchain, upload_dir):
    return SamplyProfile(upload_dir, make_config(), {"name": "speedometer3"})


def test_init_rejects_unsupported_app(local_toolchain, upload_dir):
    """Check that the constructor rejects apps other than Firefox."""
    with pytest.raises(RuntimeError, match="only supports"):
        SamplyProfile(upload_dir, make_config(app="chrome"), {})


def test_init_rejects_non_mac_platform(local_toolchain, upload_dir):
    """Check that the constructor rejects platforms other than macOS."""
    with pytest.raises(RuntimeError, match="only supported on macOS"):
        SamplyProfile(upload_dir, make_config(platform="linux"), {})


def test_init_local_paths_and_firefox_env(profiler, local_toolchain, upload_dir):
    """Check that a local run resolves mozbuild tools and sets Firefox profiling env."""
    assert profiler.test_name == "speedometer3"
    assert profiler.upload_dir == upload_dir
    assert (
        profiler.profile
        == upload_dir / "profile_samply_speedometer3_unprocessed.jslb.gz"
    )
    assert profiler.samply_path == local_toolchain / "samply" / "samply"
    assert profiler.clang_path == local_toolchain / "clang" / "bin" / "clang++"
    assert profiler.macosx_sdk == local_toolchain / "MacOSX14.0.sdk"
    assert profiler.original_binary_path == Path(
        "/Applications/Firefox.app/Contents/MacOS/firefox"
    )
    assert profiler.running is False
    assert profiler.symbol_dir is None
    assert "MOZ_CRASHREPORTER_NO_REPORT" not in os.environ

    env = profiler.raptor_config["environment"]
    assert env["IONPERF"] == "func"
    assert env["MOZ_DISABLE_CONTENT_SANDBOX"] == "1"
    assert env["JIT_OPTION_enableStubFramePointers"] == "true"
    assert env["PERF_SPEW_DIR"] == str(profiler.temp_dir)
    assert env["MOZ_PERFORMANCE_MARKER_DIR"] == str(profiler.temp_dir)


def test_init_keeps_existing_environment(local_toolchain, upload_dir):
    """Check that preset environment values are kept and missing ones are added."""
    config = make_config(environment={"IONPERF": "src"})
    SamplyProfile(upload_dir, config, {})
    assert config["environment"]["IONPERF"] == "src"
    assert config["environment"]["MOZ_USE_PERFORMANCE_MARKER_FILE"] == "1"


def test_init_default_test_name(local_toolchain, upload_dir):
    """Check that the profile name falls back to "test" without a test name."""
    profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.profile.name == "profile_samply_test_unprocessed.jslb.gz"


def test_init_picks_latest_sdk(tmp_path, monkeypatch, upload_dir):
    """Check that the newest MacOSX SDK in the toolchain directory is chosen."""
    root = make_toolchain(
        tmp_path / "mozbuild", sdks=("MacOSX13.3.sdk", "MacOSX15.2.sdk")
    )
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.macosx_sdk == root / "MacOSX15.2.sdk"


def test_init_without_sdk(tmp_path, monkeypatch, upload_dir):
    """Check that no SDK is used when the toolchain directory has none."""
    root = make_toolchain(tmp_path / "mozbuild", sdks=())
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.macosx_sdk is None


def test_init_falls_back_to_system_clang(tmp_path, monkeypatch, upload_dir):
    """Check that the system clang++ is used when mozbuild has none."""
    root = make_toolchain(tmp_path / "mozbuild", clang=False)
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    system_clang = tmp_path / "usr" / "bin" / "clang++"
    system_clang.parent.mkdir(parents=True)
    system_clang.touch()
    monkeypatch.setattr(samply_profile.shutil, "which", lambda name: system_clang)
    profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.clang_path == system_clang
    assert profiler.macosx_sdk is None


def test_init_missing_clang(tmp_path, monkeypatch, upload_dir):
    """Check that a missing clang++ raises with a bootstrap hint on local runs."""
    root = make_toolchain(tmp_path / "mozbuild", clang=False)
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    monkeypatch.setattr(samply_profile.shutil, "which", lambda name: None)
    with pytest.raises(FileNotFoundError, match="clang\\+\\+ not found.*bootstrap"):
        SamplyProfile(upload_dir, make_config(), {})


def test_init_missing_samply(tmp_path, monkeypatch, upload_dir):
    """Check that a missing samply raises with a bootstrap hint on local runs."""
    root = make_toolchain(tmp_path / "mozbuild", samply=False)
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(root))
    with pytest.raises(FileNotFoundError, match="samply not found.*bootstrap"):
        SamplyProfile(upload_dir, make_config(), {})


def test_init_local_symbols_path(local_toolchain, upload_dir, tmp_path):
    """Check that a valid symbols_path is used and enables the crash reporter."""
    symbols = tmp_path / "symbols"
    symbols.mkdir()
    profiler = SamplyProfile(upload_dir, make_config(symbols_path=str(symbols)), {})
    assert profiler.symbol_dir == symbols
    assert os.environ["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert os.environ["MOZ_CRASHREPORTER"] == "1"
    assert "MOZ_CRASHREPORTER_DISABLE" not in os.environ


def test_init_local_objdir_symbols(local_toolchain, upload_dir, tmp_path, monkeypatch):
    """Check that objdir symbols are used and the crash reporter stays disabled."""
    objdir = tmp_path / "objdir"
    sym_dir = objdir / "dist" / "crashreporter-symbols"
    sym_dir.mkdir(parents=True)
    monkeypatch.setenv("MOZ_DEVELOPER_OBJ_DIR", str(objdir))
    profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.symbol_dir == sym_dir
    assert os.environ["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert os.environ["MOZ_CRASHREPORTER_DISABLE"] == "1"
    assert "MOZ_CRASHREPORTER" not in os.environ


def test_init_local_objdir_without_symbols(
    local_toolchain, upload_dir, tmp_path, monkeypatch
):
    """Check that a missing objdir symbol directory warns and gives no symbols."""
    monkeypatch.setenv("MOZ_DEVELOPER_OBJ_DIR", str(tmp_path / "objdir"))
    with mock.patch.object(samply_profile.LOG, "warning") as warning:
        profiler = SamplyProfile(upload_dir, make_config(), {})
    assert profiler.symbol_dir is None
    assert "mach buildsymbols" in warning.call_args[0][0]


@pytest.fixture
def ci_toolchain(tmp_path, monkeypatch):
    root = make_toolchain(tmp_path / "fetches")
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(root))
    return root


def test_init_ci_extracts_symbol_zip(ci_toolchain, upload_dir):
    """Check that CI runs extract the fetched symbol zip into the temp dir."""
    with zipfile.ZipFile(ci_toolchain / "target.crashreporter-symbols.zip", "w") as z:
        z.writestr("libxul.so/ABC/libxul.so.sym", "MODULE")
    profiler = SamplyProfile(upload_dir, make_config(run_local=False), {})
    assert profiler.samply_path == ci_toolchain / "samply" / "samply"
    assert profiler.symbol_dir == profiler.temp_dir / "symbols"
    assert (profiler.symbol_dir / "libxul.so" / "ABC" / "libxul.so.sym").is_file()
    assert os.environ["MOZ_CRASHREPORTER_DISABLE"] == "1"


def test_init_ci_missing_symbol_zip(ci_toolchain, upload_dir):
    """Check that CI runs go without symbols when the symbol zip is absent."""
    profiler = SamplyProfile(upload_dir, make_config(run_local=False), {})
    assert profiler.symbol_dir is None
    assert "MOZ_CRASHREPORTER_NO_REPORT" not in os.environ


def test_init_ci_corrupt_symbol_zip(ci_toolchain, upload_dir):
    """Check that CI runs warn and go without symbols when the zip is corrupt."""
    (ci_toolchain / "target.crashreporter-symbols.zip").write_text("not a zip")
    with mock.patch.object(samply_profile.LOG, "warning") as warning:
        profiler = SamplyProfile(upload_dir, make_config(run_local=False), {})
    assert profiler.symbol_dir is None
    assert "Failed to extract" in warning.call_args[0][0]


def test_init_missing_samply_in_ci_has_no_bootstrap_hint(
    tmp_path, monkeypatch, upload_dir
):
    """Check that a missing samply in CI raises without a bootstrap hint."""
    root = make_toolchain(tmp_path / "fetches", samply=False)
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(root))
    with pytest.raises(FileNotFoundError, match="samply not found") as exc:
        SamplyProfile(upload_dir, make_config(run_local=False), {})
    assert "bootstrap" not in str(exc.value)


@pytest.mark.parametrize(
    "returncode,stdout,stderr",
    [(0, b"", b""), (1, b"", b""), (2, b"out", b"err")],
)
def test_pkill_process(profiler, returncode, stdout, stderr):
    """Check that _pkill_process runs pkill for the name whatever its exit code."""
    with mock.patch.object(
        samply_profile.subprocess,
        "run",
        return_value=CompletedProcess([], returncode, stdout, stderr),
    ) as run:
        profiler._pkill_process("samply")
    run.assert_called_once_with(["pkill", "samply"], capture_output=True, check=False)


def test_generate_helper_code(profiler, tmp_path):
    """Check that the helper source writes the samply env vars to the given file."""
    env_file = tmp_path / "env_vars.txt"
    code = profiler._generate_helper_code(env_file)
    assert f'std::ofstream f("{env_file}");' in code
    assert 'getenv("DYLD_INSERT_LIBRARIES")' in code
    assert "pause();" in code


def test_generate_wrapper_code(profiler):
    """Check that the wrapper source sets the samply env vars and execs Firefox."""
    code = profiler._generate_wrapper_code("/path/lib.dylib", "server-123")
    assert f'firefox_binary = "{profiler.original_binary_path}"' in code
    assert 'dyld = "/path/lib.dylib"' in code
    assert 'bootstrap = "server-123"' in code
    assert "execve(firefox_binary, argv, environ);" in code


def fake_clang(returncode=0, create=True, stdout="", stderr=""):
    calls = []

    def run(cmd, **kwargs):
        calls.append(cmd)
        if create and returncode == 0:
            Path(cmd[cmd.index("-o") + 1]).write_bytes(b"\xcf\xfa")
        return CompletedProcess(cmd, returncode, stdout, stderr)

    return run, calls


def test_compile_binary_requires_code(profiler):
    """Check that _compile_binary returns None without source code."""
    assert profiler._compile_binary(binary_name="x") is None


def test_compile_binary_requires_name(profiler):
    """Check that _compile_binary returns None without a binary name."""
    assert profiler._compile_binary(code="int main() {}") is None


def test_compile_binary_success_with_sdk(profiler):
    """Check that _compile_binary runs clang++ with -isysroot and returns the binary."""
    run, calls = fake_clang(stdout="ok", stderr="warning")
    with mock.patch.object(samply_profile.subprocess, "run", side_effect=run):
        binary = profiler._compile_binary(code="int main() {}", binary_name="helper")
    assert binary == profiler.temp_dir / "helper"
    assert (profiler.temp_dir / "helper.cpp").read_text() == "int main() {}"
    assert calls == [
        [
            str(profiler.clang_path),
            str(profiler.temp_dir / "helper.cpp"),
            "-o",
            str(profiler.temp_dir / "helper"),
            "-isysroot",
            str(profiler.macosx_sdk),
        ]
    ]


def test_compile_binary_without_sdk(profiler):
    """Check that _compile_binary omits -isysroot when there is no SDK."""
    profiler.macosx_sdk = None
    run, calls = fake_clang()
    with mock.patch.object(samply_profile.subprocess, "run", side_effect=run):
        assert profiler._compile_binary(code="x", binary_name="helper")
    assert "-isysroot" not in calls[0]


def test_compile_binary_failure(profiler):
    """Check that _compile_binary returns None when clang++ fails."""
    run, _ = fake_clang(returncode=1, stderr="error: boom")
    with mock.patch.object(samply_profile.subprocess, "run", side_effect=run):
        assert profiler._compile_binary(code="x", binary_name="helper") is None


def test_compile_binary_missing_output(profiler):
    """Check that _compile_binary returns None when clang++ writes no binary."""
    run, _ = fake_clang(create=False)
    with mock.patch.object(samply_profile.subprocess, "run", side_effect=run):
        assert profiler._compile_binary(code="x", binary_name="helper") is None


class FakeSubprocess:
    def __init__(
        self,
        profiler,
        env_lines=(
            "DYLD_INSERT_LIBRARIES=/samply/lib.dylib",
            "SAMPLY_BOOTSTRAP_SERVER_NAME=samply-server",
        ),
        failing_compiles=(),
        samply_exits=False,
        popen_error=None,
    ):
        self.profiler = profiler
        self.env_lines = env_lines
        self.failing_compiles = failing_compiles
        self.samply_exits = samply_exits
        self.popen_error = popen_error
        self.run_calls = []
        self.popen_calls = []

    def run(self, cmd, **kwargs):
        self.run_calls.append(cmd)
        if cmd[0] == "pkill":
            return CompletedProcess(cmd, 0, b"", b"")
        output = Path(cmd[cmd.index("-o") + 1])
        if output.name in self.failing_compiles:
            return CompletedProcess(cmd, 1, "", "error")
        output.write_bytes(b"bin")
        return CompletedProcess(cmd, 0, "", "")

    def popen(self, cmd, **kwargs):
        self.popen_calls.append(cmd)
        if self.popen_error:
            raise self.popen_error
        if self.env_lines is not None:
            (self.profiler.temp_dir / "env_vars.txt").write_text(
                "\n".join(self.env_lines) + "\n"
            )
        proc = mock.Mock()
        if self.samply_exits:
            proc.communicate.return_value = ("samply out", "samply err")
        else:
            proc.wait.side_effect = subprocess.TimeoutExpired(cmd, 1)
        return proc

    def pkill_targets(self):
        return [c[1] for c in self.run_calls if c[0] == "pkill"]

    def __enter__(self):
        self._patches = [
            mock.patch.object(samply_profile.subprocess, "run", side_effect=self.run),
            mock.patch.object(
                samply_profile.subprocess, "Popen", side_effect=self.popen
            ),
        ]
        for p in self._patches:
            p.start()
        return self

    def __exit__(self, *exc):
        for p in self._patches:
            p.stop()


def test_start_success(profiler, clock):
    """Check that start records the helper with samply and swaps in the wrapper."""
    profiler.profile.write_bytes(b"stale")
    with FakeSubprocess(profiler) as fake:
        assert profiler.start() is True

    assert not profiler.profile.exists()
    assert profiler.running is True
    wrapper = profiler.temp_dir / "samply_wrapper"
    assert profiler.wrapper_binary_path == wrapper
    assert profiler.raptor_config["binary"] == str(wrapper)
    assert fake.pkill_targets() == ["samply"]
    assert fake.popen_calls == [
        [
            str(profiler.samply_path),
            "record",
            "--save-only",
            "-o",
            str(profiler.profile),
            str(profiler.temp_dir / "samply_helper"),
        ]
    ]
    wrapper_code = (profiler.temp_dir / "samply_wrapper.cpp").read_text()
    assert 'dyld = "/samply/lib.dylib"' in wrapper_code
    assert 'bootstrap = "samply-server"' in wrapper_code
    helper_code = (profiler.temp_dir / "samply_helper.cpp").read_text()
    assert str(profiler.temp_dir / "env_vars.txt") in helper_code


def test_start_with_symbols(local_toolchain, upload_dir, tmp_path, clock):
    """Check that start passes the breakpad symbol options when symbols exist."""
    symbols = tmp_path / "symbols"
    symbols.mkdir()
    profiler = SamplyProfile(upload_dir, make_config(symbols_path=str(symbols)), {})
    with FakeSubprocess(profiler) as fake:
        assert profiler.start() is True
    cmd = fake.popen_calls[0]
    assert cmd[:3] == [str(profiler.samply_path), "record", "--save-only"]
    assert cmd[3:8] == [
        "--presymbolicate",
        "--breakpad-symbol-dir",
        str(symbols),
        "--breakpad-symbol-server",
        "https://symbols.mozilla.org/",
    ]
    assert cmd[8:] == [
        "-o",
        str(profiler.profile),
        str(profiler.temp_dir / "samply_helper"),
    ]


def test_start_helper_compile_failure(profiler, clock):
    """Check that start fails without launching samply if the helper won't build."""
    with FakeSubprocess(profiler, failing_compiles=("samply_helper",)) as fake:
        assert profiler.start() is False
    assert fake.popen_calls == []
    assert profiler.running is False


def test_start_popen_failure(profiler, clock):
    """Check that start fails when samply cannot be launched."""
    with FakeSubprocess(profiler, popen_error=OSError("no exec")):
        assert profiler.start() is False
    assert profiler.running is False


def test_start_samply_exits_early(profiler, clock):
    """Check that start fails when samply exits right after launch."""
    with FakeSubprocess(profiler, samply_exits=True) as fake:
        assert profiler.start() is False
    assert profiler.running is False
    assert not (profiler.temp_dir / "samply_wrapper").exists()
    assert fake.pkill_targets() == ["samply"]


@pytest.mark.parametrize(
    "env_lines",
    [None, ("DYLD_INSERT_LIBRARIES=/samply/lib.dylib",)],
    ids=["no-file", "missing-bootstrap"],
)
def test_start_env_vars_timeout(profiler, clock, env_lines):
    """Check that start gives up and kills samply when the env vars never appear."""
    start = clock.now
    with FakeSubprocess(profiler, env_lines=env_lines) as fake:
        assert profiler.start() is False
    assert clock.now - start >= 5
    assert fake.pkill_targets() == ["samply", "samply_helper", "samply"]
    assert profiler.running is False
    assert Path(profiler.raptor_config["binary"]) == profiler.original_binary_path


def test_start_wrapper_compile_failure(profiler, clock):
    """Check that start fails when the wrapper binary won't build."""
    with FakeSubprocess(profiler, failing_compiles=("samply_wrapper",)):
        assert profiler.start() is False
    assert profiler.running is False
    assert profiler.wrapper_binary_path is None


def test_stop_when_not_running(profiler):
    """Check that stop does nothing when the profiler is not running."""
    with mock.patch.object(samply_profile.subprocess, "run") as run:
        assert profiler.stop() is False
    run.assert_not_called()


def test_stop_waits_for_stable_profile(profiler, clock, monkeypatch):
    """Check that stop waits until the profile size settles before killing samply."""
    profiler.running = True
    pkills = []
    sizes = iter([b"a", b"ab", b"abc"])

    def run(cmd, **kwargs):
        pkills.append(cmd[1])
        return CompletedProcess(cmd, 0, b"", b"")

    def sleep(seconds):
        clock.now += seconds
        data = next(sizes, None)
        if data is not None:
            profiler.profile.write_bytes(data)

    monkeypatch.setattr(samply_profile.time, "sleep", sleep)
    with mock.patch.object(samply_profile.subprocess, "run", side_effect=run):
        start = clock.now
        assert profiler.stop() is True

    assert pkills == ["samply_helper", "samply"]
    assert profiler.running is False
    assert profiler.profile.read_bytes() == b"abc"
    assert 5 <= clock.now - start < 20


def test_stop_profile_never_appears(profiler, clock):
    """Check that stop fails after the wait when no profile is written."""
    profiler.running = True
    with mock.patch.object(
        samply_profile.subprocess,
        "run",
        return_value=CompletedProcess([], 0, b"", b""),
    ) as run:
        start = clock.now
        assert profiler.stop() is False
    assert clock.now - start >= 120
    assert [c[0][0][1] for c in run.call_args_list] == ["samply_helper", "samply"]
    assert profiler.running is False


def test_clean_removes_temp_dir_and_restores_binary(profiler):
    """Check that clean removes temp dirs and restores the original binary."""
    temp_dir = profiler.temp_dir
    temp_profile_dir = Path(profiler.temp_profile_dir)
    profiler.raptor_config["binary"] = str(temp_dir / "samply_wrapper")
    profiler.clean()
    assert not temp_dir.exists()
    assert not temp_profile_dir.exists()
    assert str(profiler.raptor_config["binary"]) == str(profiler.original_binary_path)


def test_clean_twice_is_harmless(profiler):
    """Check that a second clean leaves the binary setting alone."""
    profiler.clean()
    profiler.raptor_config["binary"] = "other"
    profiler.clean()
    assert profiler.raptor_config["binary"] == "other"


if __name__ == "__main__":
    mozunit.main()
