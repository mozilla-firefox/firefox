#!/usr/bin/env python
import os
import subprocess
import tarfile
import zipfile
from pathlib import Path
from unittest import mock
from unittest.mock import call

import mozunit
import pytest

from mozperftest.system.simpleperf import (
    DEFAULT_SIMPLEPERF_OPTS,
    SimpleperfAlreadyRunningError,
    SimpleperfBinaryNotFoundError,
    SimpleperfController,
    SimpleperfExecutionError,
    SimpleperfNotRunningError,
    SimpleperfProfiler,
    SimpleperfSymbolicationError,
)
from mozperftest.tests.support import EXAMPLE_SHELL_TEST, get_running_env


def running_env(**kw):
    return get_running_env(flavor="custom-script", **kw)


def make_mock_process(return_code=0, context=True):

    process = mock.MagicMock()
    process.returncode = return_code
    process.stdout = mock.MagicMock()

    if context:
        process.__enter__.return_value = process
        process.__exit__.return_value = None

    return process


def create_mock_symbolication_directories(base, CI=True):

    (mock_work_dir_path := base / "mock_work_dir").mkdir(parents=True, exist_ok=True)

    if CI:
        (mock_fetch_path := base / "fetch").mkdir(parents=True, exist_ok=True)
        (symbol_dir := mock_fetch_path / "target.crashreporter-symbols").mkdir(
            parents=True, exist_ok=True
        )
        (output_dir := base / "output").mkdir(parents=True, exist_ok=True)

        return mock_work_dir_path, mock_fetch_path, symbol_dir, output_dir
    else:
        (symbolicator_dir := base / "symbolicator-cli").mkdir(
            parents=True, exist_ok=True
        )
        (symbol_dir := base / "target.crashreporter-symbols").mkdir(
            parents=True, exist_ok=True
        )
        (output_dir := base / "unit_test").mkdir(parents=True, exist_ok=True)

        return mock_work_dir_path, symbolicator_dir, symbol_dir, output_dir


class FakeDevice:
    def __init__(self):
        self.pushed_files = {}
        self.commands = []
        self.pulled_files = {}

    def push(self, source, destination):
        self.pushed_files[destination] = source

    def shell(self, command):
        self.commands.append(command)
        return ""

    def pull(self, source, destination):
        self.pulled_files[destination] = source


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
def test_simpleperf_setup():
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = SimpleperfProfiler(env, mach_cmd)

    # Pass a mock path to the simpleperf NDK.
    mock_path = Path("mock") / "simpleperf" / "path"
    profiler.set_arg("path", str(mock_path))

    # Make sure binary exists
    with mock.patch("os.path.exists", return_value=True):
        # Test setup method.
        profiler.setup()

    # Verify binary was pushed to device properly.
    expected_source = mock_path / "bin" / "android" / "arm64" / "simpleperf"
    assert profiler.device.pushed_files["/data/local/tmp"] == expected_source
    assert "chmod a+x /data/local/tmp/simpleperf" in profiler.device.commands

    # Verify environment variable was set to activate layer.
    assert os.environ.get("MOZPERFTEST_SIMPLEPERF") == "1"

    # Test run step which should be a no-op.
    result = profiler.run(metadata)
    assert result == metadata

    assert metadata.get_extra_options() == ["simpleperf"]

    # Test teardown method.
    with mock.patch.object(SimpleperfProfiler, "_symbolicate", return_value=None):
        profiler.teardown()

    # Verify that profile and binary files were removed.
    cleanup_command = "rm -f /data/local/tmp/perf.data /data/local/tmp/simpleperf"
    assert cleanup_command in profiler.device.commands

    # Make sure $MOZPERFTEST_SIMPLEPERF is undefined.
    assert "MOZPERFTEST_SIMPLEPERF" not in os.environ


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("os.path.exists", return_value=True)
def test_simpleperf_setup_with_path(mock_exists):
    """Test setup_simpleperf_path when path is provided."""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = SimpleperfProfiler(env, mach_cmd)
    custom_path = Path("custom") / "simpleperf" / "path"
    profiler.set_arg("path", str(custom_path))

    profiler.setup_simpleperf_path()

    # Verify binary was pushed to device properly.
    mock_exists.assert_called_once_with(
        custom_path / "bin" / "android" / "arm64" / "simpleperf"
    )


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("os.path.exists", return_value=True)
def test_simpleperf_setup_without_path(mock_exists):
    """Test setup_simpleperf_path when no path is provided and NDK needs to be installed."""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = SimpleperfProfiler(env, mach_cmd)

    # Setup mocks for the imports inside the method
    mock_platform = mock.MagicMock()
    mock_platform.system.return_value = "Linux"
    mock_platform.machine.return_value = "x86_64"

    # Create platform-agnostic paths
    mock_ndk = Path("mock") / "ndk"

    mock_android = mock.MagicMock()
    mock_android.NDK_PATH = mock_ndk

    # Mock the imports that happen
    with mock.patch.dict(
        "sys.modules", {"platform": mock_platform, "mozboot.android": mock_android}
    ):
        # Call the method directly
        profiler.setup_simpleperf_path()

    # Verify Android NDK was installed
    mock_android.ensure_android_ndk.assert_called_once_with("linux")

    # Verify simpleperf path was set correctly.
    expected_path = mock_ndk / "simpleperf"
    assert profiler.get_arg("path") == expected_path

    # Verify binary was installed.
    mock_exists.assert_called_once_with(
        expected_path / "bin" / "android" / "arm64" / "simpleperf"
    )


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("os.path.exists", return_value=False)
def test_simpleperf_setup_missing_binary(mock_exists):
    """Test setup_simpleperf_path when the binary doesn't exist."""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = SimpleperfProfiler(env, mach_cmd)
    missing_path = Path("missing") / "binary" / "path"
    profiler.set_arg("path", str(missing_path))

    # This should raise an exception
    with pytest.raises(SimpleperfBinaryNotFoundError) as excinfo:
        profiler.setup_simpleperf_path()

    # Verify the error message contains the path
    assert "Cannot find simpleperf binary" in str(excinfo.value)
    assert str(missing_path) in str(excinfo.value)


# Tests for SimpleperfController


class MockProcess:
    def __init__(self, returncode=0):
        self.returncode = returncode
        self.stdout = None
        self.stderr = None

    def communicate(self):
        return b"stdout data", b"stderr data"


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.simpleperf.subprocess.Popen")
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_start_default_options(mock_is_enabled, mock_popen):
    """Test for SimpleperfController.start()."""
    mock_process = MockProcess()
    mock_popen.return_value = mock_process

    # Create controller
    controller = SimpleperfController()

    # Test start with default options
    controller.start(None)

    # Verify subprocess.Popen was called with su, proper paths and the default args.
    mock_popen.assert_called_once_with(
        [
            "adb",
            "shell",
            "su",
            "-c",
            f"/data/local/tmp/simpleperf record {DEFAULT_SIMPLEPERF_OPTS} -o /data/local/tmp/perf.data",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    # Verify profiler_process was set
    assert controller.profiler_process == mock_process


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.simpleperf.subprocess.Popen")
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_start_custom_options(mock_is_enabled, mock_popen):
    """Test that SimpleperfController.start() works with custom options."""
    mock_process = MockProcess()
    mock_popen.return_value = mock_process

    controller = SimpleperfController()
    custom_opts = "some random options here."

    # Test start()
    controller.start(custom_opts)

    # Verify the correct arguments are used
    mock_popen.assert_called_once_with(
        [
            "adb",
            "shell",
            "su",
            "-c",
            f"/data/local/tmp/simpleperf record {custom_opts} -o /data/local/tmp/perf.data",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert controller.profiler_process == mock_process


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.simpleperf.Path")
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_stop(mock_is_enabled, mock_path):
    """Test that the SimpleperfController.stop() method works correctly."""
    mock_process = MockProcess()

    output_dir = Path("mock") / "output"
    index = 5
    expected_output = output_dir / f"perf-{index}.data"
    mock_path.return_value = expected_output

    controller = SimpleperfController()
    controller.profiler_process = mock_process

    # Test Stop()
    with mock.patch.object(
        mock_process, "communicate", return_value=(b"stdout data", b"stderr data")
    ):
        controller.stop(str(output_dir), index)

    assert "kill $(pgrep simpleperf)" in controller.device.commands
    assert (
        controller.device.pulled_files[str(expected_output)]
        == "/data/local/tmp/perf.data"
    )
    assert "rm -f /data/local/tmp/perf.data" in controller.device.commands
    assert controller.profiler_process is None


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_start_already_running(mock_is_enabled):
    """Test that SimpleperfController.start() raises an exception if already running."""
    controller = SimpleperfController()
    controller.profiler_process = MockProcess()

    with pytest.raises(SimpleperfAlreadyRunningError) as excinfo:
        controller.start(None)

    assert "simpleperf already running" in str(excinfo.value)


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_stop_not_running(mock_is_enabled):
    """Test that SimpleperfController.stop() raises an exception if not running."""
    controller = SimpleperfController()
    controller.profiler_process = None

    output_dir = Path("mock") / "output"

    with pytest.raises(SimpleperfNotRunningError) as excinfo:
        controller.stop(str(output_dir), 1)

    assert "no profiler process found" in str(excinfo.value)


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.simpleperf.subprocess.Popen")
@mock.patch(
    "mozperftest.system.simpleperf.SimpleperfProfiler.is_enabled", return_value=True
)
def test_simpleperf_controller_stop_error(mock_is_enabled, mock_popen):
    """Test that SimpleperfController.stop() handles process errors."""

    mock_process = MockProcess(returncode=1)
    mock_popen.return_value = mock_process

    controller = SimpleperfController()
    controller.start(None)

    output_dir = Path("mock") / "output"

    with pytest.raises(SimpleperfExecutionError) as excinfo:
        controller.stop(str(output_dir), 1)

    assert "failed to run simpleperf" in str(excinfo.value)


# Tests for Simpleperf Symbolication


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
def test_simpleperf_invalid_symbolicate_arguments():
    """Test simpleperf symbolication when empty or invalid arguments are passed"""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = SimpleperfProfiler(env, mach_cmd)
    profiler.metadata = mock.Mock()
    profiler.metadata.script = {"name": "unit_test"}

    # Invalid inputs should throw SimpleperfSymbolicationError exception
    with pytest.raises(SimpleperfSymbolicationError):
        profiler._validate_symbolication_paths(
            "/fake/symbol/path", "/fake/symbolicator/path"
        )

    with pytest.raises(SimpleperfSymbolicationError):
        profiler._validate_symbolication_paths("", "")

    with pytest.raises(SimpleperfSymbolicationError):
        profiler._validate_symbolication_paths(None, None)

    # Verify local symbolication is skipped if args not provided
    with mock.patch.object(profiler, "_cleanup") as mock_cleanup, mock.patch(
        "mozperftest.system.simpleperf.ON_TRY", False
    ):
        profiler.teardown()
        mock_cleanup.assert_called_once()

    # Check if exception was thrown and handled by verifying that
    # breakpad_symbol_dir and profiler_edit_dir do not exist
    assert not hasattr(profiler, "breakpad_symbol_dir")
    assert not hasattr(profiler, "profiler_node_tools_dir")

    profiler.set_arg("symbol-path", "/fake/symbol/path")
    profiler.set_arg("profiler-node-tools-path", "/fake/profiler-edit/path")

    # Verify local symbolication is skipped if args are invalid
    with mock.patch.object(profiler, "_cleanup") as mock_cleanup, mock.patch(
        "mozperftest.system.simpleperf.ON_TRY", False
    ):
        profiler.teardown()
        mock_cleanup.assert_called_once()

    assert not hasattr(profiler, "breakpad_symbol_dir")
    assert not hasattr(profiler, "profiler_node_tools_dir")


def expected_samply_import(samply_path, perf_data_path, symbol_dir, output_path):
    return call(
        [
            str(samply_path),
            "import",
            str(perf_data_path),
            "--presymbolicate",
            "--breakpad-symbol-dir",
            str(symbol_dir),
            "--breakpad-symbol-server",
            "https://symbols.mozilla.org/",
            "--save-only",
            "-o",
            str(output_path),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
def test_local_simpleperf_symbolicate(tmp_path):

    # Mock profiler
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )
    profiler = SimpleperfProfiler(env, mach_cmd)

    # Mock directories
    mock_work_dir_path, _, symbol_dir, output_dir = (
        create_mock_symbolication_directories(tmp_path, CI=False)
    )

    # Mock files
    simpleperf_dir = output_dir / "simpleperf"
    simpleperf_dir.mkdir(parents=True, exist_ok=True)
    (mock_perf_data_path := simpleperf_dir / "mock_perf-0.data").write_text("mock-data")
    (simpleperf_dir / "profile-0.json").write_text("mock-symbolicated-profile")

    # Mock args. The profiler-node-tools path is only needed for merging.
    profiler.set_arg("symbol-path", symbol_dir)
    profiler.env.set_arg("output", output_dir)
    profiler.test_name = "unit_test"

    # Test local symbolication
    with mock.patch("mozperftest.system.simpleperf.ON_TRY", False), mock.patch(
        "tempfile.mkdtemp", return_value=str(mock_work_dir_path)
    ), mock.patch("shutil.rmtree") as mock_rmtree, mock.patch(
        "subprocess.Popen"
    ) as mock_popen, mock.patch(
        "mozperftest.system.simpleperf.find_node_executable",
        return_value=[str(tmp_path / "node")],
    ):
        mock_popen.side_effect = [make_mock_process(context=True)]

        # Test _symbolicate() via teardown()
        profiler.teardown()

        # Verify the temporary work directory is deleted
        mock_rmtree.assert_not_called()

        assert mock_popen.call_args_list == [
            expected_samply_import(
                "samply",
                mock_perf_data_path,
                symbol_dir,
                simpleperf_dir / "profile-0.json",
            )
        ]

        # Verify exported symbolicated profiles
        output_zip = output_dir / "profile_unit_test.zip"
        assert output_zip.exists()


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
def test_local_simpleperf_symbolicate_failure(tmp_path):

    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )
    profiler = SimpleperfProfiler(env, mach_cmd)

    mock_work_dir_path, _, symbol_dir, output_dir = (
        create_mock_symbolication_directories(tmp_path, CI=False)
    )

    simpleperf_dir = output_dir / "simpleperf"
    simpleperf_dir.mkdir(parents=True, exist_ok=True)
    (simpleperf_dir / "mock_perf-0.data").write_text("mock-data")

    profiler.set_arg("symbol-path", symbol_dir)
    profiler.env.set_arg("output", output_dir)
    profiler.test_name = "unit_test"

    with mock.patch("mozperftest.system.simpleperf.ON_TRY", False), mock.patch(
        "subprocess.Popen"
    ) as mock_popen, mock.patch(
        "mozperftest.system.simpleperf.find_node_executable",
        return_value=[str(tmp_path / "node")],
    ), mock.patch.object(profiler, "_cleanup") as mock_cleanup:
        mock_popen.side_effect = [make_mock_process(return_code=1, context=True)]

        profiler.teardown()

        # A failed import leaves nothing to archive.
        assert not (output_dir / "profile_unit_test.zip").exists()
        mock_cleanup.assert_called_once()


@mock.patch("mozperftest.system.simpleperf.get_adb_device_or_emu", new=FakeDevice)
def test_ci_simpleperf_symbolicate(tmp_path):

    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )
    profiler = SimpleperfProfiler(env, mach_cmd)

    # Mock directories
    mock_work_dir_path, mock_fetch_path, symbol_dir, output_dir = (
        create_mock_symbolication_directories(tmp_path)
    )

    # The tgz will extract to work_dir/unit_test/simpleperf/mock_perf-0.data
    perf_dir = mock_work_dir_path / "unit_test" / "simpleperf"

    # Mock profile files at the path where _import_perf_data will output them
    # (work_dir / parent_folder, where parent_folder = "simpleperf")
    profile_dir = mock_work_dir_path / "simpleperf"
    profile_dir.mkdir(parents=True, exist_ok=True)
    (profile_dir / "profile-0.json").write_text("mock-symbolicated-profile")

    # Mock executables
    samply_path = mock_fetch_path / "samply" / "samply"

    # Mock .zip file with a symbol file
    mock_sym_zip_file = mock_fetch_path / "target.crashreporter-symbols.zip"
    with zipfile.ZipFile(mock_sym_zip_file, "w") as mock_zip:
        mock_zip.writestr("libxul.so/ABCD1234/libxul.so.sym", "some_data")

    # Mock tar file with a perf data file
    mock_perf_data = output_dir / "unit_test.tgz"
    with tarfile.open(mock_perf_data, "w:gz") as tar:
        perf_path = tmp_path / "mock_perf-0.data"
        perf_path.write_text("mock-data")
        tar.add(perf_path, arcname="unit_test/simpleperf/mock_perf-0.data")

    # Set env and metadata
    profiler.env.set_arg("output", output_dir)
    profiler.test_name = "unit_test"

    # Test symbolication in CI
    with mock.patch.dict(
        os.environ,
        {
            "MOZ_FETCHES_DIR": str(mock_fetch_path),
        },
        clear=False,
    ), mock.patch("mozperftest.system.simpleperf.ON_TRY", True), mock.patch(
        "mozperftest.utils.ON_TRY", True
    ), mock.patch("tempfile.mkdtemp", return_value=str(mock_work_dir_path)), mock.patch(
        "shutil.rmtree"
    ) as mock_rmtree, mock.patch("subprocess.Popen") as mock_popen:
        mock_popen.side_effect = [make_mock_process(context=True)]

        # Test _symbolicate() via teardown()
        profiler.teardown()

        # Verify the temporary work directory is deleted
        mock_rmtree.assert_called_once()

        # Verify proper .zip extraction
        mock_symbol_path = (
            mock_fetch_path
            / "target.crashreporter-symbols/libxul.so/ABCD1234/libxul.so.sym"
        )
        assert mock_symbol_path.exists()

        # Verify proper .tgz extraction
        assert (perf_dir / "mock_perf-0.data").exists()

        assert mock_popen.call_args_list == [
            expected_samply_import(
                samply_path,
                perf_dir / "mock_perf-0.data",
                symbol_dir,
                mock_work_dir_path / "simpleperf" / "profile-0.json",
            )
        ]

        # Verify exported symbolicated profiles
        output_zip = output_dir / "profile_unit_test.zip"
        with zipfile.ZipFile(output_zip) as zf:
            assert zf.namelist() == ["simpleperf/profile-0.json"]


if __name__ == "__main__":
    mozunit.main()
