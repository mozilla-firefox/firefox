# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import logging

import mozunit
import pytest
import yaml
from mozdevice import ADBError

from condprof.tests import fakearsenic

from condprof import android  # isort: skip
from condprof.util import DEFAULT_PREFS  # isort: skip

APP = "org.mozilla.geckoview_example"


class FakeDevice:
    def __init__(self, fail=(), installed=True, confirm=True, rooted=False):
        self.fail = set(fail)
        self.installed = installed
        self.confirm = confirm
        self.is_rooted = rooted
        self.running = [True]
        self.calls = []
        self.pushed = {}
        self.logcat = ["line one", "line two"]
        self._verbose = True

    def _record(self, name, *args, **kw):
        self.calls.append((name,) + args)
        for key in self.fail:
            if key == name or (
                args and isinstance(args[0], str) and args[0].startswith(key)
            ):
                raise ADBError(key)

    def is_app_installed(self, app):
        return self.installed

    def confirm_clear_app_data(self, app):
        return self.confirm

    def shell_output(self, cmd):
        self._record("shell_output", cmd)

    def shell(self, cmd, stdout_callback=None):
        self._record("shell", cmd)

    def rm(self, path, force=False, recursive=False):
        self._record("rm", path)

    def push(self, local, remote):
        self._record("push", local, remote)
        self.pushed[remote] = local

    def chmod(self, path, recursive=False):
        self._record("chmod", path)

    def exists(self, path):
        return "exists" not in self.fail

    def stop_application(self, app):
        self._record("stop_application", app)

    def launch_application(self, app, activity, intent, extras=None, url=None):
        self._record("launch_application", app, activity, intent, url)

    def process_exist(self, app):
        if len(self.running) > 1:
            return self.running.pop(0)
        return self.running[0]

    def forward(self, local, remote):
        self._record("forward", local, remote)

    def pkill(self, app):
        self._record("pkill", app)

    def pull(self, remote, local):
        self._record("pull", remote, local)

    def remove_forwards(self, local):
        self._record("remove_forwards", local)

    def clear_logcat(self, timeout, buffers):
        self._record("clear_logcat", timeout, buffers)

    def get_logcat(self):
        assert self._verbose is False
        return self.logcat


@pytest.fixture(autouse=True)
def tmp_dirs(tmp_path, monkeypatch):
    created = []

    def mkdtemp():
        path = tmp_path / f"tmp{len(created)}"
        path.mkdir()
        created.append(path)
        return str(path)

    monkeypatch.setattr(android.tempfile, "mkdtemp", mkdtemp)
    return created


@pytest.fixture(autouse=True)
def no_sleep(monkeypatch):
    sleeps = []
    monkeypatch.setattr(android.time, "sleep", sleeps.append)
    return sleeps


@pytest.fixture
def make_device(monkeypatch):
    created = []

    def make(**kw):
        fake = FakeDevice(**kw)

        def factory(**factory_kw):
            created.append(factory_kw)
            return fake

        monkeypatch.setattr(android, "ADBDeviceFactory", factory)
        return fake

    make.created = created
    return make


def _names(fake):
    return [call[0] + " " + str(call[1]) for call in fake.calls]


@pytest.mark.parametrize(
    "app,activity",
    [
        ("org.mozilla.fenix", "org.mozilla.fenix.IntentReceiverActivity"),
        (APP, "org.mozilla.geckoview_example.GeckoViewActivity"),
    ],
)
def test_device_activity_matches_app_package(app, activity):
    assert android.AndroidDevice(app).activity == activity


def test_prepare_pushes_profile_and_config_then_launches_app(
    tmp_path, make_device, no_sleep
):
    fake = make_device()
    device = android.AndroidDevice(APP, verbose=True, remote_test_root="/sdcard/t/")
    device.prepare(str(tmp_path), None)

    assert make_device.created == [
        {"verbose": True, "logger_name": "adb", "test_root": "/sdcard/t/"}
    ]
    assert device.remote_profile == "/sdcard/t/profile"
    assert fake.pushed["/sdcard/t/profile"] == str(tmp_path)
    yml_on_device = f"/data/local/tmp/{APP}-geckoview-config.yaml"
    with open(fake.pushed[yml_on_device]) as f:
        config = yaml.safe_load(f)
    assert config["args"] == ["-marionette", "-profile", "/sdcard/t/profile"]
    assert config["prefs"] == DEFAULT_PREFS
    assert _names(fake) == [
        "shell_output pm clear " + APP,
        "shell am set-debug-app --persistent " + APP,
        f"shell pm grant {APP} android.permission.READ_EXTERNAL_STORAGE",
        f"shell pm grant {APP} android.permission.WRITE_EXTERNAL_STORAGE",
        f"shell_output appops set {APP} MANAGE_EXTERNAL_STORAGE allow",
        "shell_output appops set --uid 2000 MANAGE_EXTERNAL_STORAGE allow",
        "rm /sdcard/t/profile",
        "push " + str(tmp_path),
        "chmod /sdcard/t/profile",
        "rm " + yml_on_device,
        "push " + fake.pushed[yml_on_device],
        "chmod " + yml_on_device,
        "stop_application " + APP,
        "launch_application " + APP,
        "forward tcp:2828",
    ]
    assert fake.calls[-2][2:] == (
        device.activity,
        "android.intent.action.VIEW",
        "about:blank",
    )
    assert no_sleep == [30]


def test_prepare_raises_adberror_after_best_effort_steps_fail(tmp_path, make_device):
    fake = make_device(
        fail=(
            "pm clear",
            "pm grant",
            "appops set " + APP,
            "appops set --uid 2000",
            "rm",
            "chmod",
        ),
    )
    with pytest.raises(ADBError):
        android.AndroidDevice(APP).prepare(str(tmp_path), None)
    names = _names(fake)
    assert "shell_output appops set --uid shell MANAGE_EXTERNAL_STORAGE allow" in names
    assert "push " + str(tmp_path) in names
    assert names[-1].startswith("rm /data/local/tmp/")


def test_prepare_continues_when_shell_uid_grant_fails(tmp_path, make_device):
    fake = make_device(fail=("appops set --uid",))
    android.AndroidDevice(APP).prepare(str(tmp_path), None)
    names = _names(fake)
    assert "shell_output appops set --uid shell MANAGE_EXTERNAL_STORAGE allow" in names
    assert names[-1] == "forward tcp:2828"


def test_prepare_rooted_skips_shell_grant(tmp_path, make_device):
    fake = make_device(rooted=True)
    android.AndroidDevice(APP).prepare(str(tmp_path), None)
    assert not any("--uid" in name for name in _names(fake))


def test_prepare_device_init_fails(tmp_path, monkeypatch):
    def factory(**kw):
        raise ADBError("no device")

    monkeypatch.setattr(android, "ADBDeviceFactory", factory)
    with pytest.raises(ADBError):
        android.AndroidDevice(APP).prepare(str(tmp_path), None)


@pytest.mark.parametrize(
    "kw,message",
    [
        ({"installed": False}, "is not installed"),
        ({"confirm": False}, "Declined to clear app data"),
        ({"fail": ("exists",)}, "does not exists on the device"),
    ],
)
def test_prepare_aborts_with_message_for_each_bad_state(
    tmp_path, make_device, kw, message
):
    make_device(**kw)
    with pytest.raises(Exception, match=message):
        android.AndroidDevice(APP).prepare(str(tmp_path), None)


def test_prepare_app_does_not_start(tmp_path, make_device):
    fake = make_device()
    fake.running = [False]
    with pytest.raises(Exception, match="Could not start"):
        android.AndroidDevice(APP).prepare(str(tmp_path), None)


def test_adb_log_handler_attached_on_prepare_and_removed_on_close(
    tmp_path, make_device
):
    make_device()
    log_file = tmp_path / "adb.log"
    device = android.AndroidDevice(APP, marionette_port=2829)
    device.prepare(str(tmp_path / "profile"), str(log_file))
    handler = device._adb_fh
    try:
        assert handler in logging.getLogger("adb").handlers
        logging.getLogger("adb").debug("hello adb")
        device.close()
        assert handler not in logging.getLogger("adb").handlers
        assert device._adb_fh is None
        assert device.device.calls[-1] == ("remove_forwards", "tcp:2829")
    finally:
        handler.close()
    assert "hello adb" in log_file.read_text()


def test_close_without_device():
    device = android.AndroidDevice(APP)
    device.close()
    assert device.device is None


def test_close_forward_error():
    device = android.AndroidDevice(APP)
    device.device = FakeDevice(fail=("remove_forwards",))
    device.close()
    assert device.device.calls == [("remove_forwards", "tcp:2828")]


def test_logcat_is_none_without_device_and_proxied_with_device():
    device = android.AndroidDevice(APP)
    assert device.get_logcat() is None
    assert device.clear_logcat() is None
    device.device = FakeDevice()
    assert device.get_logcat() == ["line one", "line two"]
    assert device.device._verbose is True
    device.clear_logcat(timeout=5, buffers=["main"])
    assert device.device.calls == [("clear_logcat", 5, ["main"])]


def test_stop_browser(no_sleep):
    device = android.AndroidDevice(APP)
    device.device = FakeDevice()
    device.device.running = [False]
    device.stop_browser()
    assert device.device.calls == [("stop_application", APP)]
    assert no_sleep == [5]


def test_stop_browser_needs_kill(no_sleep):
    device = android.AndroidDevice(APP)
    device.device = FakeDevice(fail=("stop_application", "pkill"))
    device.device.running = [True, True, True, False]
    device.stop_browser()
    assert [call[0] for call in device.device.calls] == [
        "stop_application",
        "pkill",
        "pkill",
    ]
    assert no_sleep == [5, 1, 1]


def test_stop_browser_gives_up(no_sleep):
    device = android.AndroidDevice(APP)
    device.device = FakeDevice()
    device.stop_browser()
    assert [call[0] for call in device.device.calls].count("pkill") == 5


def test_collect_profile_pulls_remote_profile_to_local():
    device = android.AndroidDevice(APP)
    device.device = FakeDevice()
    device.remote_profile, device.profile = "/sdcard/profile", "/local/profile"
    device.collect_profile()
    assert device.device.calls == [("pull", "/sdcard/profile", "/local/profile")]


def test_device_context_manager_closes_device_on_exit():
    with android.device(APP, 2830, False, remote_test_root="/r/") as device:
        assert device.marionette_port == 2830
        assert device.verbose is False
        assert device.remote_test_root == "/r/"
        device.device = FakeDevice()
    assert device.device.calls == [("remove_forwards", "tcp:2830")]


def test_android_geckodriver_start_connects_to_existing_marionette():
    fakearsenic.SERVICE_CALLS.clear()
    driver = android.AndroidGeckodriver(binary="gd", log_file="log")
    url = asyncio.run(driver.start())
    port = str(fakearsenic.FREE_PORT)
    assert driver.version_checked
    assert fakearsenic.SERVICE_CALLS == [
        (
            [
                "gd",
                "--log",
                "trace",
                "--port",
                port,
                "--marionette-port",
                "2828",
                "--connect-existing",
            ],
            "http://localhost:" + port,
            "log",
        )
    ]
    assert url == "http://localhost:" + port


@pytest.fixture
def env(tmp_path):
    return android.AndroidEnv(
        str(tmp_path / "profile"), APP, "geckodriver", str(tmp_path), None
    )


@pytest.mark.parametrize(
    "device_name,expected",
    [(None, "geckoview_example"), ("p6", "p6-geckoview_example")],
)
def test_env_target_platform_includes_device_name(env, device_name, expected):
    env.device_name = device_name
    assert env.target_platform == expected
    assert env.get_browser_version() == expected + "-XXXneedtograbversion"


def test_env_runs_full_device_lifecycle_and_pulls_profile(
    env, make_device, tmp_path, no_sleep
):
    fake = make_device()
    with env.get_device(2828, verbose=False, remote_test_root="/r/") as device:
        assert env.device is device
        env.prepare(None)
        assert device.profile == env.profile
        with env.get_browser():
            pass
        env.collect_profile()
        fake.running = [False]
        env.stop_browser()
    names = [call[0] for call in fake.calls]
    assert ("pull", "/r/profile", env.profile) in fake.calls
    assert names[-2:] == ["stop_application", "remove_forwards"]


def test_env_dump_logs_writes_logcat_file(env, tmp_path):
    env.device = android.AndroidDevice(APP)
    env.device.device = FakeDevice()
    env.device.device.logcat = ["one", "café"]
    env.dump_logs()
    assert (tmp_path / "logcat.log").read_bytes() == "one\ncafé\n".encode()


def test_env_dump_logs_without_logcat_writes_no_file(env, tmp_path):
    env.device = android.AndroidDevice(APP)
    env.dump_logs()
    assert not (tmp_path / "logcat.log").exists()


def test_env_dump_logs_ignores_write_errors(env, tmp_path):
    env.device = android.AndroidDevice(APP)
    env.device.device = FakeDevice()
    env.archive = str(tmp_path / "missing")
    env.dump_logs()
    assert not (tmp_path / "missing").exists()


@pytest.mark.parametrize("headless", [True, False])
def test_env_browser_args_add_headless_only_when_requested(env, headless):
    expected = ["--allow-downgrade"] + (["-headless"] if headless else [])
    assert env.get_browser_args(headless) == {
        "moz:firefoxOptions": {"args": expected, "prefs": {}}
    }
    args = env.get_browser_args(headless, prefs={"a": 1})
    assert args["moz:firefoxOptions"]["prefs"] == {"a": 1}


def test_env_geckodriver_is_android_geckodriver(env):
    driver = env.get_geckodriver(log_file="log")
    assert isinstance(driver, android.AndroidGeckodriver)
    assert (driver.binary, driver.log_file) == ("geckodriver", "log")


def test_env_check_session_leaves_session_open(env):
    class Session:
        async def close(self):
            raise AssertionError("the session must stay open")

    session = Session()
    env.check_session(session)
    assert asyncio.run(session.close()) is None


if __name__ == "__main__":
    mozunit.main()
