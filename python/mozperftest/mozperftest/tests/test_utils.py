#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import shutil
import sys
import tempfile
from datetime import date, timedelta
from pathlib import Path
from subprocess import CalledProcessError
from unittest import mock

import mozunit
import pytest
from mozdevice import ADBError

from mozperftest.tests.support import EXAMPLE_TESTS_DIR, requests_content, temp_file
from mozperftest.utils import (
    archive_folder,
    build_test_list,
    checkout_python_script,
    convert_day,
    create_path,
    download_file,
    ensure_adb_on_path,
    ensure_android_device,
    ensure_java_on_path,
    ensure_profgen_on_path,
    get_adb_device_or_emu,
    get_adb_path,
    get_multi_tasks_url,
    get_output_dir,
    get_revision_namespace_url,
    host_platform,
    install_package,
    install_requirements_file,
    load_class,
    load_class_from_path,
    silence,
    start_test_emulator,
    temp_dir,
)


def test_get_adb_path_from_sdk_root():
    with temp_dir() as sdk_root:
        platform_tools = Path(sdk_root) / "platform-tools"
        platform_tools.mkdir()
        adb = platform_tools / "adb"
        adb.write_text("")
        with mock.patch.dict(os.environ, {"ANDROID_SDK_ROOT": sdk_root}):
            assert get_adb_path() == str(adb)


def test_get_adb_path_fallback():
    with mock.patch.dict(os.environ, {}, clear=True):
        assert get_adb_path() == "adb"


def test_ensure_adb_on_path_prepends_platform_tools():
    with temp_dir() as sdk_root:
        platform_tools = Path(sdk_root) / "platform-tools"
        platform_tools.mkdir()
        with mock.patch.dict(
            os.environ, {"ANDROID_SDK_ROOT": sdk_root, "PATH": "/usr/bin"}
        ):
            ensure_adb_on_path()
            entries = os.environ["PATH"].split(os.pathsep)
            assert entries[0] == str(platform_tools)
            # idempotent: a second call does not add it again
            ensure_adb_on_path()
            assert os.environ["PATH"].split(os.pathsep).count(str(platform_tools)) == 1


def test_ensure_adb_on_path_no_sdk_root_is_noop():
    with mock.patch.dict(os.environ, {"PATH": "/usr/bin"}, clear=True):
        ensure_adb_on_path()
        assert os.environ["PATH"] == "/usr/bin"


def test_ensure_profgen_on_path_prepends_cmdline_tools_bin():
    with temp_dir() as sdk_root:
        # profgen ships in a versioned cmdline-tools/<version>/bin directory.
        cmdline_bin = Path(sdk_root) / "cmdline-tools" / "20.0" / "bin"
        cmdline_bin.mkdir(parents=True)
        with mock.patch.dict(
            os.environ, {"ANDROID_SDK_ROOT": sdk_root, "PATH": "/usr/bin"}
        ):
            ensure_profgen_on_path()
            entries = os.environ["PATH"].split(os.pathsep)
            assert entries[0] == str(cmdline_bin)
            # idempotent: a second call does not add it again
            ensure_profgen_on_path()
            assert os.environ["PATH"].split(os.pathsep).count(str(cmdline_bin)) == 1


def test_ensure_profgen_on_path_no_sdk_root_is_noop():
    with mock.patch.dict(os.environ, {"PATH": "/usr/bin"}, clear=True):
        ensure_profgen_on_path()
        assert os.environ["PATH"] == "/usr/bin"


def test_ensure_java_on_path_sets_java_home_macos_layout():
    with temp_dir() as fetches_dir:
        # macOS JDK layout: jdk/<version>/Contents/Home/bin/java.
        java_home = Path(fetches_dir) / "jdk" / "jdk-17" / "Contents" / "Home"
        java_bin = java_home / "bin"
        java_bin.mkdir(parents=True)
        (java_bin / "java").write_text("")
        with mock.patch.dict(
            os.environ, {"MOZ_FETCHES_DIR": fetches_dir, "PATH": "/usr/bin"}
        ):
            ensure_java_on_path()
            assert os.environ["JAVA_HOME"] == str(java_home)
            assert os.environ["PATH"].split(os.pathsep)[0] == str(java_bin)


def test_ensure_java_on_path_no_fetches_dir_is_noop():
    with mock.patch.dict(os.environ, {"PATH": "/usr/bin"}, clear=True):
        ensure_java_on_path()
        assert "JAVA_HOME" not in os.environ
        assert os.environ["PATH"] == "/usr/bin"


def _mock_adbhost(devices):
    # ADBHost is imported inside ensure_android_device from mozdevice, so patch
    # it there. The emulator decision keys off adbhost.devices().
    adbhost = mock.MagicMock()
    adbhost.devices.return_value = devices
    return adbhost


def test_ensure_android_device_when_device_connected():
    with mock.patch("mozperftest.utils.ensure_adb_on_path"), mock.patch(
        "mozdevice.ADBHost", return_value=_mock_adbhost([{"state": "device"}])
    ), mock.patch("mozperftest.utils.start_test_emulator") as start_emulator:
        ensure_android_device()
    start_emulator.assert_not_called()


def test_ensure_android_device_starts_emulator_when_no_device():
    with mock.patch("mozperftest.utils.ensure_adb_on_path"), mock.patch(
        "mozdevice.ADBHost", return_value=_mock_adbhost([])
    ), mock.patch(
        "mozperftest.utils.start_test_emulator", return_value=True
    ) as start_emulator:
        ensure_android_device()
    start_emulator.assert_called_once()


def test_ensure_android_device_unauthorized_device_starts_emulator():
    # A device present but not in the "device" state is not ready, so the
    # emulator is started rather than the device being treated as usable.
    with mock.patch("mozperftest.utils.ensure_adb_on_path"), mock.patch(
        "mozdevice.ADBHost", return_value=_mock_adbhost([{"state": "unauthorized"}])
    ), mock.patch(
        "mozperftest.utils.start_test_emulator", return_value=True
    ) as start_emulator:
        ensure_android_device()
    start_emulator.assert_called_once()


def test_ensure_android_device_no_emulator_raises():
    from mozdevice import ADBError

    with mock.patch("mozperftest.utils.ensure_adb_on_path"), mock.patch(
        "mozdevice.ADBHost", return_value=_mock_adbhost([])
    ), mock.patch("mozperftest.utils.start_test_emulator", return_value=False):
        with pytest.raises(ADBError):
            ensure_android_device()


def test_ensure_android_device_other_error_reraised():
    # A genuine adb failure (e.g. a broken adb binary) surfaces from ADBHost and
    # must propagate instead of silently starting an emulator.
    from mozdevice import ADBError

    adbhost = mock.MagicMock()
    adbhost.devices.side_effect = ADBError("adb is not executable.")
    with mock.patch("mozperftest.utils.ensure_adb_on_path"), mock.patch(
        "mozdevice.ADBHost", return_value=adbhost
    ), mock.patch("mozperftest.utils.start_test_emulator") as start_emulator:
        with pytest.raises(ADBError):
            ensure_android_device()
    start_emulator.assert_not_called()


def test_start_test_emulator_selects_fetched_avd():
    # Only the arm64 AVD is present. Selection must not depend on the host
    # architecture (platform.machine() reports x86_64 under Rosetta), so the
    # arm64 emulator must be chosen and started.
    created = {}

    def make(avd_type, verbose=False):
        emu = mock.MagicMock()
        emu.is_available.return_value = True
        emu.check_avd.return_value = avd_type == "arm64"
        emu.wait_for_start.return_value = True
        created[avd_type] = emu
        return emu

    with mock.patch.dict(os.environ, {}), mock.patch(
        "mozperftest.utils.ON_TRY", True
    ), mock.patch("mozrunner.devices.android_device.AndroidEmulator", side_effect=make):
        assert start_test_emulator() is True
        # macOS perf workers must render with the host GPU; swiftshader
        # produces static in the screen recordings.
        assert "-gpu host" in os.environ["MOZ_EMULATOR_COMMAND_ARGS"]
    # arm64 is probed first and found, so later types are never constructed.
    assert list(created) == ["arm64"]
    created["arm64"].start.assert_called_once()


def test_start_test_emulator_no_avd_returns_false():
    def make(avd_type, verbose=False):
        emu = mock.MagicMock()
        emu.is_available.return_value = True
        emu.check_avd.return_value = False
        return emu

    with mock.patch.dict(os.environ, {}), mock.patch(
        "mozperftest.utils.ON_TRY", True
    ), mock.patch("mozrunner.devices.android_device.AndroidEmulator", side_effect=make):
        assert start_test_emulator() is False


def test_silence():
    with silence():
        print("HIDDEN")


def test_host_platform():
    plat = host_platform()

    if sys.platform.startswith("darwin"):
        assert plat == "darwin"
    elif sys.maxsize > 2**32:
        assert "64" in plat
    else:
        assert "64" not in plat


def get_raise(*args, **kw):
    raise Exception()


@mock.patch("mozperftest.utils.requests.get", new=get_raise)
def test_download_file_fails():
    with temp_file() as target, silence(), pytest.raises(Exception):
        download_file("http://I don't exist", Path(target), retry_sleep=0.1)


@mock.patch("mozperftest.utils.requests.get", new=requests_content())
def test_download_file_success():
    with temp_file() as target:
        download_file("http://content", Path(target), retry_sleep=0.1)
        with open(target) as f:
            assert f.read() == "some content"


def _req(package):
    class Req:
        location = "nowhere"

        @property
        def satisfied_by(self):
            return self

        def check_if_exists(self, **kw):
            pass

    return Req()


@mock.patch("pip._internal.req.constructors.install_req_from_line", new=_req)
def test_install_package():
    vem = mock.Mock()
    vem.bin_path = "someplace"
    with mock.patch("subprocess.check_call") as mock_check_call:
        assert install_package(vem, "foo")
        mock_check_call.assert_called_once_with([
            vem.python_path,
            "-m",
            "pip",
            "install",
            "foo",
        ])


def test_install_requirements_file():
    vem = mock.Mock()
    vem.bin_path = "someplace"
    with mock.patch("subprocess.check_call") as mock_check_call, mock.patch(
        "mozperftest.utils.os"
    ):
        assert install_requirements_file(vem, "foo")
        mock_check_call.assert_called_once_with([
            vem.python_path,
            "-m",
            "pip",
            "install",
            "--no-build-isolation",
            "-r",
            "foo",
        ])


@mock.patch("pip._internal.req.constructors.install_req_from_line", new=_req)
def test_install_package_failures():
    vem = mock.Mock()
    vem.bin_path = "someplace"

    def check_call(*args):
        raise CalledProcessError(1, "")

    with pytest.raises(CalledProcessError):
        with mock.patch("subprocess.check_call", new=check_call):
            install_package(vem, "foo")

    # we can also absorb the error, and just return False
    assert not install_package(vem, "foo", ignore_failure=True)


@mock.patch("mozperftest.utils.requests.get", requests_content())
def test_build_test_list():
    http_filename = "perftest_one.js"
    tests = [EXAMPLE_TESTS_DIR, f"https://some/location/{http_filename}"]
    try:
        files, tmp_dir = build_test_list(tests)
        assert any(f.startswith(str(EXAMPLE_TESTS_DIR)) for f in files)
        assert any(http_filename in Path(f).name for f in files)
    finally:
        shutil.rmtree(tmp_dir)


def test_build_test_list_on_try_mapping():
    # A source directory can hold tests of several mochitest flavors, which
    # land in different subdirectories of the test package.
    with tempfile.TemporaryDirectory() as tmp:
        packaged = Path(
            tmp, "mochitest", "browser", "dom", "media", "test", "browser_perf.js"
        )
        packaged.parent.mkdir(parents=True)
        packaged.touch()

        cwd = os.getcwd()
        os.chdir(tmp)
        try:
            with mock.patch("mozperftest.utils.ON_TRY", True):
                files, tmp_dir = build_test_list(["dom/media/test/browser_perf.js"])
        finally:
            os.chdir(cwd)

        assert tmp_dir is None
        assert files == [str(packaged.resolve())]


def test_convert_day():
    day = "2020.06.08"
    assert convert_day(day) == day
    with pytest.raises(ValueError):
        convert_day("2020-06-08")
    today = date.today()
    assert convert_day("today"), today.strftime("%Y.%m.%d")
    yesterday = today - timedelta(days=1)
    assert convert_day("yesterday") == yesterday.strftime("%Y.%m.%d")


def test_revision_namespace_url():
    route = "FakeBuildRoute"
    day = "2020.06.08"
    buildurl = get_revision_namespace_url(route, day=day)
    assert day in buildurl and route in buildurl
    assert buildurl.endswith(".revision")


def test_multibuild_url():
    route = "FakeBuildRoute"
    day = "2020.06.08"
    revision = "deadbeef"
    buildurl = get_multi_tasks_url(route, revision, day=day)
    assert all(item in buildurl for item in (route, day, revision))

    with mock.patch("mozperftest.utils.date") as mockeddate:
        mockeddate.today.return_value = mockeddate
        mockeddate.strftime.return_value = "2020.07.09"
        buildurl = get_multi_tasks_url(route, revision, day="today")
        assert "2020.07.09" in buildurl and route in buildurl

        with mock.patch("mozperftest.utils.timedelta"):
            mockeddate.__sub__.return_value = mockeddate
            mockeddate.strftime.return_value = "2020.08.09"
            buildurl = get_multi_tasks_url(route, revision)
            assert "2020.08.09" in buildurl and route in buildurl


class ImportMe:
    pass


def test_load_class():
    with pytest.raises(ImportError):
        load_class("notimportable")

    with pytest.raises(ImportError):
        load_class("notim:por:table")

    with pytest.raises(ImportError):
        load_class("notim:portable")

    with pytest.raises(ImportError):
        load_class("mozperftest.tests.test_utils:NOEXIST")

    klass = load_class("mozperftest.tests.test_utils:ImportMe")
    assert klass is ImportMe


def test_load_class_from_path():
    with pytest.raises(ImportError) as exc:
        load_class_from_path("nonexistent", __file__)
    assert "found but it was not a valid class" in exc.value.args[0]

    with pytest.raises(ImportError) as exc:
        load_class_from_path("nonexistent", "nonexistent-file")
    assert "does not exist." in exc.value.args[0]

    klass = load_class_from_path("ImportMe", __file__)
    assert klass.__name__ is ImportMe.__name__


class _Venv:
    python_path = sys.executable


def test_checkout_python_script():
    with silence() as captured:
        assert checkout_python_script(_Venv(), "lib2to3", ["--help"])

    stdout, stderr = captured
    stdout.seek(0)
    assert stdout.read() == "=> lib2to3 [OK]\n"


def test_run_python_script_failed():
    with silence() as captured:
        assert not checkout_python_script(_Venv(), "nothing")

    stdout, stderr = captured
    stdout.seek(0)
    assert stdout.read().endswith("[FAILED]\n")


def test_get_output_dir():
    with temp_file() as temp_dir:
        output_dir = get_output_dir(temp_dir)
        assert output_dir.exists()
        assert output_dir.is_dir()

        output_dir = get_output_dir(output=temp_dir, folder="artifacts")
        assert output_dir.exists()
        assert output_dir.is_dir()
        assert "artifacts" == output_dir.parts[-1]


def test_create_path():
    path = Path("path/doesnt/exist").resolve()
    if path.exists():
        shutil.rmtree(path.parent.parent)
    try:
        path = create_path(path)

        assert path.exists()
    finally:
        shutil.rmtree(path.parent.parent)


def test_archive_folder():
    with temp_dir() as input_dir, temp_dir():
        input_dir_path = Path(input_dir)
        output_dir_path = Path(input_dir)

        archive_folder(input_dir_path, output_dir_path)
        assert len(list(output_dir_path.glob(f"{input_dir_path.name}.tgz"))) == 1

        archive_folder(input_dir_path, output_dir_path, archive_name="testing")
        assert len(list(output_dir_path.glob("testing.tgz"))) == 1


class MockADBDeviceFactory:
    def __init__(self, verbose=False):
        self.verbose = verbose


class EmulatorMockAvailable:
    def __init__(self, *args, **kwargs):
        pass

    def is_available(self):
        return True

    def check_avd(self):
        return True

    def get_avd_description(self):
        return "test-avd"

    def start(self):
        pass

    def wait_for_start(self):
        pass


@mock.patch("mozperftest.utils.input", return_value="y")
@mock.patch(
    "mozrunner.devices.android_device.AndroidEmulator", new=EmulatorMockAvailable
)
@mock.patch("mozdevice.ADBDeviceFactory")
def test_launch_emulator_click_yes(mock_factory, mock_input):
    fake_device = MockADBDeviceFactory(verbose=True)
    mock_factory.side_effect = [ADBError("No ready devices found."), fake_device]

    result = get_adb_device_or_emu()

    assert isinstance(result, MockADBDeviceFactory)
    assert mock_factory.call_count == 2


if __name__ == "__main__":
    mozunit.main()
