#!/usr/bin/env python
import os
import subprocess
from pathlib import Path
from unittest import mock

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
)
from mozperftest.tests.support import EXAMPLE_SHELL_TEST, get_running_env


def running_env(**kw):
    return get_running_env(flavor="custom-script", **kw)


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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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

    # Test teardown method.
    profiler.teardown()

    # Verify that profile and binary files were removed.
    cleanup_command = "rm -f /data/local/tmp/perf.data /data/local/tmp/simpleperf"
    assert cleanup_command in profiler.device.commands

    # Make sure $MOZPERFTEST_SIMPLEPERF is undefined.
    assert "MOZPERFTEST_SIMPLEPERF" not in os.environ


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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
    assert profiler.get_arg("path") == str(expected_path)

    # Verify binary was installed.
    mock_exists.assert_called_once_with(
        expected_path / "bin" / "android" / "arm64" / "simpleperf"
    )


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


@mock.patch("mozperftest.system.simpleperf.ADBDevice", new=FakeDevice)
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


if __name__ == "__main__":
    mozunit.main()
