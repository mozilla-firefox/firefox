# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import os

import mozunit
import pytest

from condprof.tests import fakearsenic

from condprof import desktop  # isort: skip
from condprof.util import DEFAULT_PREFS, get_current_platform  # isort: skip


@pytest.fixture
def env(tmp_path):
    return desktop.DesktopEnv(
        str(tmp_path / "profile"), None, "geckodriver", str(tmp_path), None
    )


@pytest.fixture(autouse=True)
def service_calls():
    fakearsenic.SERVICE_CALLS.clear()
    return fakearsenic.SERVICE_CALLS


def test_get_device_yields_none(env):
    with env.get_device(2828, verbose=True) as device:
        assert device is None


def test_target_platform_is_current_platform(env):
    assert env.target_platform == get_current_platform()


def test_get_browser_makes_firefox_path_absolute(tmp_path, env):
    firefox = tmp_path / "firefox"
    firefox.write_text("")
    env.firefox = str(firefox)
    with env.get_browser():
        assert env.firefox == os.path.abspath(str(firefox))


def test_get_browser_raises_ioerror_for_missing_binary(tmp_path, env):
    env.firefox = str(tmp_path / "missing")
    with pytest.raises(IOError):
        with env.get_browser():
            pass


@pytest.mark.parametrize("headless", [True, False])
def test_get_browser_args_adds_headless_only_when_requested(env, headless):
    args = env.get_browser_args(headless)["moz:firefoxOptions"]
    expected = ["--allow-downgrade", "-profile", env.profile]
    if headless:
        expected.append("-headless")
    assert args["args"] == expected
    assert "binary" not in args
    assert args["prefs"] == DEFAULT_PREFS
    assert args["log"] == {"level": "trace"}


def test_get_browser_args_with_binary_and_prefs(env):
    env.firefox = "/bin/firefox"
    args = env.get_browser_args(False, prefs={"a.b": 1, "marionette.port": 1})
    options = args["moz:firefoxOptions"]
    assert options["binary"] == "/bin/firefox"
    assert options["prefs"]["a.b"] == 1
    assert options["prefs"]["marionette.port"] == 1
    assert DEFAULT_PREFS["marionette.port"] == 2828


def test_get_browser_version_falls_back_to_unknown_on_error(env, monkeypatch):
    monkeypatch.setattr(desktop, "get_version", lambda firefox: "150.0a1")
    assert env.get_browser_version() == "150.0a1"

    def fail(firefox):
        raise OSError()

    monkeypatch.setattr(desktop, "get_version", fail)
    assert env.get_browser_version() == "unknown"


def test_geckodriver_start_launches_service_on_free_port(env, service_calls):
    driver = env.get_geckodriver(log_file="log")
    assert isinstance(driver, desktop.DesktopGeckodriver)
    url = asyncio.run(driver.start())
    port = fakearsenic.FREE_PORT
    assert url == f"http://localhost:{port}"
    assert driver.version_checked
    assert service_calls == [
        (
            [
                "geckodriver",
                "--log",
                "trace",
                "--port",
                str(port),
                "--marionette-port",
                "2828",
            ],
            url,
            "log",
        )
    ]


def test_geckodriver_start_retries_after_process_lookup_error(
    env, monkeypatch, service_calls
):
    calls = []

    async def service(cmd, url, log_file):
        calls.append((url, log_file))
        if len(calls) == 1:
            raise ProcessLookupError()
        return url

    monkeypatch.setattr(desktop, "subprocess_based_service", service)
    url = asyncio.run(env.get_geckodriver(log_file="log").start())
    assert calls == [(url, "log"), (url, "log")]


if __name__ == "__main__":
    mozunit.main()
