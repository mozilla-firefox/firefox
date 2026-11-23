# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re
import sys
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from browsertime import Browsertime
from browsertime import android as bt_android
from browsertime.android import BrowsertimeAndroid
from perftest import Perftest

FENIX_PACKAGE = "org.mozilla.fenix"
GLEAN_ACTIVITY = "mozilla.telemetry.glean.debug.GleanDebugActivity"


def make_android(app="fenix", binary=FENIX_PACKAGE, device=None, **config):
    bt = object.__new__(BrowsertimeAndroid)
    bt.config = {
        "app": app,
        "binary": binary,
        "activity": "org.mozilla.fenix.IntentReceiverActivity",
        "intent": None,
    }
    bt.config.update(config)
    bt.device = device
    bt._remote_test_root = None
    bt._test_names = None
    bt.android_storage_mode = None
    bt.debug_mode = False
    return bt


def make_device(rooted=True, debuggable=False, exists=False):
    device = mock.Mock()
    device.is_rooted = rooted
    device.is_package_debuggable.return_value = debuggable
    device.exists.return_value = exists
    device.shell_output.return_value = "/sdcard"
    device.confirm_clear_app_data.return_value = True
    return device


def arg_value(args, name):
    return args[args.index(name) + 1]


def path_parts(path):
    return re.split(r"[\\/]+", path)


def test_init_sets_activity_intent_and_resets_state():
    """Check that the constructor stores activity and intent and resets device state."""

    def fake_init(self, app, binary, **kwargs):
        assert self._test_names is None
        self.config = {"app": app, "binary": binary}

    with mock.patch.object(Browsertime, "__init__", fake_init):
        bt = BrowsertimeAndroid(
            "fenix", FENIX_PACKAGE, activity="act", intent="intent", foo=1
        )

    assert bt.config["activity"] == "act"
    assert bt.config["intent"] == "intent"
    assert bt.remote_profile is None
    assert bt.android_storage_mode is None


def test_initialize_device_tunes_rooted_device():
    """Check that a rooted device is created once and perf-tuned with the test names."""
    device = make_device(rooted=True)
    bt = make_android()
    bt._test_names = ["speedometer3"]

    with mock.patch.object(
        bt_android, "ADBDeviceFactory", return_value=device
    ) as factory, mock.patch.object(bt_android, "tune_performance") as tune:
        bt._initialize_device()
        bt._initialize_device()

    factory.assert_called_once_with(verbose=True)
    assert bt.device is device
    tune.assert_called_once_with(
        device, log=bt_android.LOG, test_names=["speedometer3"]
    )


@pytest.mark.parametrize(
    "rooted, config", [(False, {}), (True, {"disable_perf_tuning": True})]
)
def test_initialize_device_skips_tuning(rooted, config):
    """Check that tuning is skipped on non-rooted devices or when disabled."""
    device = make_device(rooted=rooted)
    bt = make_android(**config)

    with mock.patch.object(
        bt_android, "ADBDeviceFactory", return_value=device
    ), mock.patch.object(bt_android, "tune_performance") as tune:
        bt._initialize_device()

    assert bt.device is device
    tune.assert_not_called()


def test_initialize_device_keeps_existing_device():
    """Check that an existing device is reused instead of creating a new one."""
    device = make_device()
    bt = make_android(device=device)

    with mock.patch.object(bt_android, "ADBDeviceFactory") as factory:
        bt._initialize_device()

    factory.assert_not_called()
    assert bt.device is device


def test_external_storage_rooted_uses_per_app_dir():
    """Check that a rooted device uses and caches the per-app external storage dir."""
    device = make_device(rooted=True)
    bt = make_android(device=device)

    root = bt.android_external_storage

    assert path_parts(root) == [
        "",
        "sdcard",
        "Android",
        "data",
        FENIX_PACKAGE,
        "files",
        "test_root",
    ]
    device.shell_output.assert_called_once_with("echo $EXTERNAL_STORAGE")
    device.mkdir.assert_called_once_with(root, parents=True)

    assert bt.android_external_storage == root
    device.shell_output.assert_called_once()


def test_external_storage_rooted_falls_back_when_mkdir_fails():
    """Check that a rooted device falls back to Download when mkdir fails."""
    device = make_device(rooted=True)
    device.mkdir.side_effect = Exception("permission denied")
    bt = make_android(device=device)

    assert path_parts(bt.android_external_storage) == ["", "sdcard", "Download"]


def test_external_storage_not_rooted_uses_download_dir():
    """Check that a non-rooted device uses the Download dir without mkdir."""
    device = make_device(rooted=False)
    bt = make_android(device=device)

    assert path_parts(bt.android_external_storage) == ["", "sdcard", "Download"]
    device.mkdir.assert_not_called()


def test_external_storage_initializes_device():
    """Check that reading external storage creates the device when missing."""
    device = make_device(rooted=False)
    bt = make_android()

    with mock.patch.object(bt_android, "ADBDeviceFactory", return_value=device):
        root = bt.android_external_storage

    assert bt.device is device
    assert path_parts(root) == ["", "sdcard", "Download"]


def test_external_storage_returns_preset_root():
    """Check that a preset remote test root is returned without querying the device."""
    bt = make_android(device=make_device())
    bt._remote_test_root = "/preset"

    assert bt.android_external_storage == "/preset"
    bt.device.shell_output.assert_not_called()


def test_browsertime_args_common_prefix():
    """Check that Android runs always enable browsertime's Android mode."""
    args = make_android(app="geckoview", binary="org.mozilla.geckoview_example")
    args = args.browsertime_args

    assert arg_value(args, "--android.enabled") == "true"


@pytest.mark.parametrize("app", ["chrome-m", "cstm-car-m"])
def test_browsertime_args_chrome(app):
    """Check that Chrome apps select the chrome browser and CaR-m its package."""
    args = make_android(app=app, binary="com.android.chrome").browsertime_args

    assert arg_value(args, "--browser") == "chrome"
    assert not any(a.startswith("--firefox.") for a in args)
    assert "--ignoreShutdownFailures" not in args
    if app == "cstm-car-m":
        assert arg_value(args, "--chrome.android.package") == "org.chromium.chrome"
    else:
        assert "--chrome.android.package" not in args


@pytest.mark.parametrize(
    "storage_mode, expected", [(None, "app"), ("sdcard", "sdcard")]
)
def test_browsertime_args_geckoview(storage_mode, expected):
    """Check that geckoview gets its package, activity, storage mode and shutdown flag."""
    bt = make_android(
        app="geckoview",
        binary="org.mozilla.geckoview_example",
        activity="org.mozilla.geckoview_example.GeckoViewActivity",
    )
    bt.android_storage_mode = storage_mode
    args = bt.browsertime_args

    assert arg_value(args, "--browser") == "firefox"
    assert (
        arg_value(args, "--firefox.android.package") == "org.mozilla.geckoview_example"
    )
    assert (
        arg_value(args, "--firefox.android.activity")
        == "org.mozilla.geckoview_example.GeckoViewActivity"
    )
    assert (
        arg_value(args, '--firefox.geckodriverArgs="--android-storage"')
        == f'--firefox.geckodriverArgs="{expected}"'
    )
    assert arg_value(args, "--ignoreShutdownFailures") == "true"
    assert not any(a.startswith("--firefox.android.intentArgument") for a in args)


def test_browsertime_args_refbrow_no_shutdown_override():
    """Check that refbrow runs Firefox without ignoring shutdown failures."""
    args = make_android(app="refbrow", binary="org.mozilla.reference.browser")
    args = args.browsertime_args

    assert arg_value(args, "--browser") == "firefox"
    assert "--ignoreShutdownFailures" not in args


def test_browsertime_args_fenix_without_intent():
    """Check that Fenix starts the Glean debug activity and passes no intent args."""
    args = make_android().browsertime_args

    assert arg_value(args, "--firefox.android.activity") == GLEAN_ACTIVITY
    assert arg_value(args, "--firefox.android.package") == FENIX_PACKAGE
    assert arg_value(args, "--ignoreShutdownFailures") == "true"
    assert not any(a.startswith("--firefox.android.intentArgument") for a in args)


def test_browsertime_args_fenix_with_intent():
    """Check that Fenix forwards the intent, real activity and about:blank to the app."""
    activity = "org.mozilla.fenix.IntentReceiverActivity"
    bt = make_android(intent="android.intent.action.VIEW", activity=activity)
    args = bt.browsertime_args

    assert arg_value(args, "--firefox.android.activity") == GLEAN_ACTIVITY
    action = args.index("--firefox.android.intentArgument=-a")
    assert args[action + 2] == "android.intent.action.VIEW"
    start_next = args.index("--firefox.android.intentArgument=startNext")
    assert args[start_next + 1] == "--firefox.android.intentArgument=" + activity
    data = args.index("--firefox.android.intentArgument=-d")
    assert args[data + 2] == "about:blank"


def test_setup_chrome_args_defaults():
    """Check that Chrome args disable experiments without playback args by default."""
    bt = make_android(app="chrome-m")
    bt.playback_chrome_args = mock.Mock()

    args = bt.setup_chrome_args({})

    assert "--chrome.args=--enable-benchmarking" in args
    assert "--chrome.args=--auto-open-devtools-for-tabs" not in args
    assert all(a.startswith("--chrome.args=") for a in args)
    bt.playback_chrome_args.assert_not_called()


def test_setup_chrome_args_playback_and_debug():
    """Check that playback and debug mode add args with single quotes converted."""
    bt = make_android(app="chrome-m")
    bt.debug_mode = True
    bt.playback_chrome_args = mock.Mock(
        return_value=["--proxy-server='127.0.0.1:8080'", "--ignore-certificate-errors"]
    )

    args = bt.setup_chrome_args({"playback": True})

    assert args[-3:] == [
        '--chrome.args=--proxy-server="127.0.0.1:8080"',
        "--chrome.args=--ignore-certificate-errors",
        "--chrome.args=--auto-open-devtools-for-tabs",
    ]


@pytest.mark.parametrize("app, merges", [("fenix", True), ("chrome-m", False)])
def test_build_browser_profile(app, merges):
    """Check that only Firefox Android apps get the raptor-android profile merged."""
    bt = make_android(app=app)
    bt.profile = mock.Mock()
    bt.remove_mozprofile_delimiters_from_profile = mock.Mock()

    with mock.patch.object(Perftest, "build_browser_profile"), mock.patch.object(
        Perftest, "profile_data_dir", new_callable=mock.PropertyMock
    ) as data_dir:
        data_dir.return_value = "profile_data"
        bt.build_browser_profile()

    if merges:
        bt.profile.merge.assert_any_call(os.path.join("profile_data", "raptor-android"))
    else:
        bt.profile.merge.assert_not_called()
    bt.remove_mozprofile_delimiters_from_profile.assert_called()


@pytest.mark.parametrize(
    "app, rooted, debuggable, expected_mode",
    [
        ("fenix", True, False, "app"),
        ("fenix", False, True, "app"),
        ("fenix", False, False, "sdcard"),
        ("chrome-m", False, True, "sdcard"),
        ("chrome-m", True, False, "app"),
    ],
)
def test_setup_adb_device_storage_mode(app, rooted, debuggable, expected_mode):
    """Check that the geckodriver storage mode follows root and debuggable state."""
    device = make_device(rooted=rooted, debuggable=debuggable)
    bt = make_android(app=app, device=device)
    bt._remote_test_root = "/root"

    bt.setup_adb_device()

    assert bt.android_storage_mode == expected_mode
    if expected_mode == "app":
        assert device.run_as_package == FENIX_PACKAGE
    else:
        assert not isinstance(device.run_as_package, str)
    if app == "fenix" and not rooted:
        device.is_package_debuggable.assert_called_once_with(FENIX_PACKAGE)
    else:
        device.is_package_debuggable.assert_not_called()
    device.shell.assert_any_call(f"pm clear {FENIX_PACKAGE}")
    device.shell.assert_any_call(f"am set-debug-app --persistent {FENIX_PACKAGE}")
    assert path_parts(bt.geckodriver_profile) == [
        "",
        "root",
        f"{FENIX_PACKAGE}-geckodriver-profile",
    ]
    device.rm.assert_not_called()


def test_setup_adb_device_removes_stale_profile():
    """Check that a stale geckodriver profile on the device is removed."""
    device = make_device(exists=True)
    bt = make_android(device=device)
    bt._remote_test_root = "/root"

    bt.setup_adb_device()

    device.rm.assert_called_once_with(
        bt.geckodriver_profile, force=True, recursive=True
    )


def test_setup_adb_device_ignores_stale_profile_removal_failure():
    """Check that failing to remove a stale profile does not abort setup."""
    device = make_device(exists=True)
    device.rm.side_effect = Exception("rm failed")
    bt = make_android(device=device)
    bt._remote_test_root = "/root"

    bt.setup_adb_device()

    device.rm.assert_called_once()
    assert bt.android_storage_mode == "app"


def test_setup_adb_device_declined_clear_aborts():
    """Check that declining to clear app data aborts device setup."""
    device = make_device()
    device.confirm_clear_app_data.return_value = False
    bt = make_android(device=device)

    with pytest.raises(Exception, match="Declined to clear app data"):
        bt.setup_adb_device()


@pytest.mark.parametrize("app", ["chrome-m", "geckoview"])
def test_run_test_setup_skips_firefox_steps(app):
    """Check that only Firefox apps get the app proxy and delimiter cleanup."""
    bt = make_android(app=app)
    bt.playback = mock.Mock(playback_mode="proxy")
    bt.set_reverse_ports = mock.Mock()
    bt.turn_on_android_app_proxy = mock.Mock()
    bt.remove_mozprofile_delimiters_from_profile = mock.Mock()
    test = {"name": "pageload"}

    with mock.patch.object(Browsertime, "run_test_setup") as base_setup:
        bt.run_test_setup(test)

    base_setup.assert_called_once_with(test)
    bt.set_reverse_ports.assert_called_once_with(test)
    if app == "chrome-m":
        bt.turn_on_android_app_proxy.assert_not_called()
        bt.remove_mozprofile_delimiters_from_profile.assert_not_called()
    else:
        bt.turn_on_android_app_proxy.assert_called_once()
        bt.remove_mozprofile_delimiters_from_profile.assert_called_once()


def test_run_test_setup_without_playback():
    """Check that the app proxy is not enabled when there is no playback."""
    bt = make_android()
    bt.playback = None
    bt.set_reverse_ports = mock.Mock()
    bt.turn_on_android_app_proxy = mock.Mock()
    bt.remove_mozprofile_delimiters_from_profile = mock.Mock()

    with mock.patch.object(Browsertime, "run_test_setup"):
        bt.run_test_setup({"name": "benchmark"})

    bt.turn_on_android_app_proxy.assert_not_called()
    bt.remove_mozprofile_delimiters_from_profile.assert_called_once()


@pytest.mark.parametrize("app, enables_chrome", [("chrome-m", True), ("fenix", False)])
def test_run_tests(app, enables_chrome):
    """Check that run_tests sets up the device and enables Chrome only for Chrome apps."""
    device = make_device()
    bt = make_android(app=app, device=device)
    bt.setup_adb_device = mock.Mock()
    tests = [{"name": "t"}]

    with mock.patch.object(Perftest, "run_tests", return_value="done") as base_run:
        assert bt.run_tests(tests) == "done"

    bt.setup_adb_device.assert_called_once()
    base_run.assert_called_once_with(tests)
    if enables_chrome:
        device.shell_output.assert_called_once_with("pm enable com.android.chrome")
    else:
        device.shell_output.assert_not_called()


def test_run_test_teardown_removes_reverse_sockets():
    """Check that teardown removes reverse socket connections."""
    device = make_device()
    bt = make_android(device=device)
    test = {"name": "t"}

    with mock.patch.object(Browsertime, "run_test_teardown") as base_teardown:
        bt.run_test_teardown(test)

    device.remove_socket_connections.assert_called_once_with("reverse")
    base_teardown.assert_called_once_with(test)


if __name__ == "__main__":
    mozunit.main()
