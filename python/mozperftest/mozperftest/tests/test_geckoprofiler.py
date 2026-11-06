#!/usr/bin/env python
import gzip
import os
import tarfile
from functools import partial
from pathlib import Path
from unittest import mock
from unittest.mock import MagicMock

import mozunit
import pytest

from mozperftest.system.geckoprofiler import (
    DEFAULT_GECKOPROFILER_OPTS,
    GeckoProfiler,
    GeckoProfilerAlreadyActiveError,
    GeckoProfilerController,
    GeckoProfilerError,
    GeckoProfilerNotActiveError,
)
from mozperftest.tests.support import EXAMPLE_SHELL_TEST, get_running_env


def running_env(**kw):
    return get_running_env(flavor="custom-script", **kw)


class FakeDevice:
    def __init__(self):
        self.pushed_files = {}
        self.commands = []
        self.pulled_files = {}
        self.files_on_device = set()

    def push(self, source, destination):
        self.pushed_files[destination] = source

    def shell(self, command):
        self.commands.append(command)
        return ""

    def pull(self, source, destination):
        self.pulled_files[destination] = source

    def exists(self, path):
        return path in self.files_on_device


def mock_subprocess_run(data, returncode, cmd, stdout=None, **kwargs):
    """Mock subprocess.run that writes data to a file.

    Args:
        data: bytes to write to the file
        returncode: exit code to return
        cmd: Command being run passed by subprocess.run
        stdout: File handle to write to passed by subprocess.run
        **kwargs: Other subprocess.run arguments

    Returns:
        Mock object with returncode and stderr attributes
    """
    if stdout:
        stdout.write(data)
    result = mock.Mock()
    result.returncode = returncode
    result.stderr = b""
    return result


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_geckoprofiler_setup():
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = GeckoProfiler(env, mach_cmd)

    profiler.setup()

    assert os.environ.get("MOZPERFTEST_GECKOPROFILE") == "1"

    # Mock get_layer to simulate Android layer being present
    profiler.env.get_layer = mock.Mock(return_value=True)
    result = profiler.run(metadata)
    assert result == metadata
    assert metadata.get_extra_options() == ["gecko-profile"]

    # Mock _archive_profiles to avoid file operations
    profiler._archive_profiles = mock.Mock()
    profiler.teardown()

    assert "MOZPERFTEST_GECKOPROFILE" not in os.environ
    cleanup_cmd = "rm -f /data/local/tmp/*-geckoview-config.yaml"
    assert cleanup_cmd in profiler.device.commands
    assert "am clear-debug-app" in profiler.device.commands


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_geckoprofiler_run_requires_android_layer():
    """Test to verify that running without the Android layer will throw an error."""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = GeckoProfiler(env, mach_cmd)

    with pytest.raises(GeckoProfilerError) as excinfo:
        profiler.run(metadata)

    assert "only supported on Android" in str(excinfo.value)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_geckoprofiler_is_enabled():
    """Test that verifies that profiling is enabled if MOZPERFTEST_GECKOPROFILE is set to 1 ."""
    assert not GeckoProfiler.is_enabled()

    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    assert GeckoProfiler.is_enabled()

    os.environ.pop("MOZPERFTEST_GECKOPROFILE")
    assert not GeckoProfiler.is_enabled()


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_geckoprofiler_cleanup_resets_state():
    """Test to make sure cleanup removes the GECKOPROFILE environment variable and resets package ID in the controller"""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=None
    )

    profiler = GeckoProfiler(env, mach_cmd)

    GeckoProfilerController.set_package_id("org.mozilla.firefox")
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"

    profiler._cleanup()

    assert "MOZPERFTEST_GECKOPROFILE" not in os.environ
    assert GeckoProfilerController._package_id is None


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
@mock.patch("tempfile.NamedTemporaryFile")
def test_controller_start_with_default_options(mock_temp, mock_run):
    """Test starting the controller with no options uses the default settings and pushes them as a YAML config file to the device"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.fenix")
    mock_temp.return_value.__enter__.return_value.name = "/tmp/test_config"

    controller = GeckoProfilerController()
    controller.start()

    assert controller.profiling_active
    assert controller.package_id == "org.mozilla.fenix"
    assert controller.config_filename == "org.mozilla.fenix-geckoview-config.yaml"

    push_call = None
    set_debug_call = None
    for call_args in mock_run.call_args_list:
        args = call_args[0][0]
        if args[0] == "adb" and args[1] == "push":
            push_call = args
        elif args[0] == "adb" and "set-debug-app" in args:
            set_debug_call = args

    assert push_call is not None
    assert set_debug_call is not None
    assert "org.mozilla.fenix" in set_debug_call

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
@mock.patch("tempfile.NamedTemporaryFile")
def test_controller_start_with_custom_options(mock_temp, mock_run):
    """Test starting the controller with custom options generates a YAML config file with those values"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    custom_opts = {
        "interval": 10,
        "features": "js,stackwalk",
        "filters": "GeckoMain",
    }

    mock_file = MagicMock()
    mock_file.name = "/tmp/test_config"
    mock_temp.return_value.__enter__.return_value = mock_file

    controller = GeckoProfilerController()
    controller.start(custom_opts)

    written_content = b""
    for call_args in mock_file.write.call_args_list:
        written_content += call_args[0][0]

    config_str = written_content.decode()
    assert "MOZ_PROFILER_STARTUP_INTERVAL: 10" in config_str
    assert "MOZ_PROFILER_STARTUP_FEATURES: js,stackwalk" in config_str
    assert "MOZ_PROFILER_STARTUP_FILTERS: GeckoMain" in config_str
    assert controller.profiling_active

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_controller_start_already_active():
    """Test attempting to start profiling when it's already active raises an error"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    controller = GeckoProfilerController()
    controller.profiling_active = True

    with pytest.raises(GeckoProfilerAlreadyActiveError) as excinfo:
        controller.start()

    assert "already active" in str(excinfo.value)

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_controller_stop_not_active():
    """Test attempting to stop the profiler with no active session raises an error"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"

    controller = GeckoProfilerController()
    controller.profiling_active = False

    output_dir = Path("/tmp/output")

    with pytest.raises(GeckoProfilerNotActiveError) as excinfo:
        controller.stop(str(output_dir), 1)

    assert "No active profiling session" in str(excinfo.value)

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
def test_controller_package_id_not_set():
    """Test that trying to resolve a package ID when it is not set raises an erorr"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController._package_id = None

    controller = GeckoProfilerController()

    with pytest.raises(GeckoProfilerError) as excinfo:
        controller._resolve_package_id()

    assert "Package id not set" in str(excinfo.value)

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.extract_tgz_and_find_files")
def test_geckoprofiler_archive_profiles(mock_extract, tmp_path):
    """Test that archived profiles are extracted from .tgz and packaged into a .zip file."""
    mach_cmd, metadata, env = running_env(
        app="fenix", tests=[str(EXAMPLE_SHELL_TEST)], output=str(tmp_path)
    )

    profiler = GeckoProfiler(env, mach_cmd)
    profiler.output_dir = tmp_path
    profiler.test_name = "test_gecko"

    work_dir = tmp_path / "work"
    work_dir.mkdir()

    # Profiles are streamed as .json directly from device and then stored
    # inside test_name/geckoprofiler/ to match real tgz structure
    profile_dir = work_dir / "test_gecko" / "geckoprofiler"
    profile_dir.mkdir(parents=True)
    profile = profile_dir / "profile-0.json"
    with open(profile, "w") as f:
        f.write('{"meta": {"profile_type": "gecko"}}')

    tgz_file = tmp_path / "test_gecko.tgz"
    with tarfile.open(tgz_file, "w:gz") as tar:
        tar.add(profile, arcname="test_gecko/geckoprofiler/profile-0.json")

    mock_extract.return_value = ([profile], work_dir, work_dir)

    profiler._archive_profiles()

    archive_file = tmp_path / "profile_test_gecko.zip"
    assert archive_file.exists()


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
@mock.patch("tempfile.NamedTemporaryFile")
def test_controller_config_file_naming(mock_temp, mock_run):
    """Test that the YAML filename follows the naming rule "{package_id}-geckoview-config.yaml"."""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("com.example.app")
    mock_temp.return_value.__enter__.return_value.name = "/tmp/config"

    controller = GeckoProfilerController()
    controller.start()

    expected_config_name = "com.example.app-geckoview-config.yaml"
    assert controller.config_filename == expected_config_name

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
def test_controller_stop_success(mock_run, tmp_path):
    """Test that the controller stop successfully stops profiling and saves profile"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    controller = GeckoProfilerController()
    controller.profiling_active = True
    controller.package_id = "org.mozilla.firefox"

    output_dir = tmp_path
    index = 1
    profile_path = output_dir / f"profile-{index}.json"

    # Mock subprocess to write valid profile data
    profile_json_data = b'{"meta": {"version": 1}, "threads": []}'
    mock_run.side_effect = partial(mock_subprocess_run, profile_json_data, 0)

    result = controller.stop(str(output_dir), index)

    assert controller.profiling_active is False
    assert result == profile_path
    assert profile_path.exists()
    assert profile_path.read_bytes() == profile_json_data

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
def test_controller_stop_error(mock_run, tmp_path):
    """Test that GeckoProfilerController.stop() handles errors and resets state"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    controller = GeckoProfilerController()
    controller.profiling_active = True
    controller.package_id = "org.mozilla.firefox"

    output_dir = tmp_path
    index = 1

    # Mock subprocess to return error
    mock_run.side_effect = partial(mock_subprocess_run, b"", 1)

    # Call stop() - should handle error gracefully
    result = controller.stop(str(output_dir), index)

    # Verify profiling_active is reset even on error
    assert controller.profiling_active is False
    # Verify None is returned on error
    assert result is None

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
def test_controller_stop_gzipped_profile(mock_run, tmp_path):
    """Test that GeckoProfilerController.stop() decompresses gzipped profiles"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    controller = GeckoProfilerController()
    controller.profiling_active = True
    controller.package_id = "org.mozilla.firefox"

    output_dir = tmp_path
    index = 1
    profile_path = output_dir / f"profile-{index}.json"

    profile_json_data = b'{"meta": {"version": 1}, "threads": []}'
    compressed_data = gzip.compress(profile_json_data)

    mock_run.side_effect = partial(mock_subprocess_run, compressed_data, 0)
    result = controller.stop(str(output_dir), index)

    assert controller.profiling_active is False
    assert result == profile_path
    # Verify file was successfully decompressed
    assert profile_path.read_bytes() == profile_json_data  # Should be decompressed

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


@mock.patch("mozperftest.system.geckoprofiler.get_adb_device_or_emu", new=FakeDevice)
@mock.patch("mozperftest.system.geckoprofiler.subprocess.run")
@mock.patch("tempfile.NamedTemporaryFile")
def test_controller_merges_default_and_custom_options(mock_temp, mock_run):
    """Test that when partial custom options are given, they are merged with the defaults one"""
    os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"
    GeckoProfilerController.set_package_id("org.mozilla.firefox")

    partial_opts = {"interval": 15}

    mock_file = MagicMock()
    mock_file.name = "/tmp/test_config"
    mock_temp.return_value.__enter__.return_value = mock_file

    controller = GeckoProfilerController()
    controller.start(partial_opts)

    written_content = b""
    for call_args in mock_file.write.call_args_list:
        written_content += call_args[0][0]

    config_str = written_content.decode()
    assert "MOZ_PROFILER_STARTUP_INTERVAL: 15" in config_str
    assert (
        f"MOZ_PROFILER_STARTUP_FEATURES: {DEFAULT_GECKOPROFILER_OPTS['features']}"
        in config_str
    )
    assert (
        f"MOZ_PROFILER_STARTUP_FILTERS: {DEFAULT_GECKOPROFILER_OPTS['filters']}"
        in config_str
    )

    os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)


if __name__ == "__main__":
    mozunit.main()
