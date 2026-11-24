# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import signal
import sys
import types
from types import SimpleNamespace
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import browsertime.base as bt_base
from browsertime import Browsertime
from perftest import Perftest

TIMEOUT = 1000
CHROME_APPS = ["chrome", "chrome-m", "custom-car", "cstm-car-m"]


class FakeBrowsertime(Browsertime):
    chrome_args = ["--chrome.args=--fake-flag"]

    @property
    def browsertime_args(self):
        return []

    def get_browser_meta(self):
        return None, None

    def setup_chrome_args(self, test):
        return list(self.chrome_args)


class FlagBrowsertime(FakeBrowsertime):
    @property
    def browsertime_args(self):
        return ["--skipHar", "--newflag", "--pageLoadStrategy", "eager", "stray"]


class FakeProfile:
    def __init__(self, path, content=""):
        self.profile = str(path)
        os.makedirs(self.profile, exist_ok=True)
        self.userjs = os.path.join(self.profile, "user.js")
        with open(self.userjs, "w") as fh:
            fh.write(content)
        self.set_preferences = mock.Mock()

    def read(self):
        with open(self.userjs) as fh:
            return fh.read()


@pytest.fixture
def make_bt(browsertime_options, monkeypatch, tmp_path):
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(tmp_path))
    monkeypatch.setenv("MOZ_DEVELOPER_REPO_DIR", str(tmp_path / "repo"))
    for var in (
        "MOZ_LOG",
        "MOZ_FETCHES_DIR",
        "MOZ_AUTOMATION",
        "MOZ_DEVELOPER_OBJ_DIR",
        "RAPTOR_LATEST_PROFILE",
        "DISABLE_PROFILE_LAUNCH",
        "MOZ_DISABLE_NONLOCAL_CONNECTIONS",
        "USB_POWER_METER_SERIAL_NUMBER",
    ):
        monkeypatch.delenv(var, raising=False)
    monkeypatch.setattr(
        bt_base, "Benchmark", mock.Mock(return_value=mock.Mock(host="h", port="1"))
    )
    (tmp_path / "browsertime-results" / "raptor-test").mkdir(parents=True)
    created = []

    def inner(profile=None, cls=FakeBrowsertime, **kwargs):
        opts = dict(browsertime_options)
        opts.update(
            post_startup_delay=1000,
            run_local=True,
            gecko_profile_extra_threads=[],
            browsertime_video=False,
            browsertime_visualmetrics=False,
            browsertime_no_ffwindowrecorder=False,
        )
        opts.update(kwargs)
        with mock.patch.object(
            Perftest,
            "build_browser_profile",
            lambda self: setattr(self, "profile", profile),
        ):
            bt = cls(**opts)
        created.append(bt)
        return bt

    yield inner
    for bt in created:
        for d in bt._dirs_to_remove:
            shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def make_test_dict():
    return {"name": "raptor-test", "test_url": "http://example.com"}


def arg_value(cmd, arg):
    return cmd[cmd.index(arg) + 1]


def arg_values(cmd, arg):
    return [cmd[i + 1] for i, v in enumerate(cmd) if v == arg]


def script_of(bt, cmd):
    return cmd[2 + len(bt.driver_paths)]


def compose(bt, test, **kwargs):
    bt.run_test_setup(test)
    return bt._compose_cmd(test, TIMEOUT, **kwargs)


def test_init_sets_browsertime_attributes(make_bt, tmp_path):
    """Check that init stores browsertime_* options as attributes and flags the config."""
    bt = make_bt(browsertime_visualmetrics=True, browsertime_custom="x")
    assert bt.browsertime is True
    assert bt.config["browsertime"] is True
    assert bt.browsertime_custom == "x"
    assert bt.browsertime_node == "browsertime_node"
    assert bt.browsertime_user_args == []
    assert bt.results_handler.browsertime_visualmetrics is True
    assert (tmp_path / "browsertime-results").is_dir()
    assert bt.profile_class == "firefox"


def test_crash_directory_is_lazy_and_cached(make_bt):
    """Check that crash_directory is created once on first access and scheduled for removal."""
    bt = make_bt()
    assert bt._crash_directory is None
    first = bt.crash_directory
    assert os.path.isdir(first)
    assert bt.crash_directory == first
    assert bt._dirs_to_remove.count(first) == 1


def test_build_browser_profile_removes_delimiters(make_bt, tmp_path):
    """Check that building the profile strips mozprofile delimiters and blank lines."""
    profile = FakeProfile(
        tmp_path / "profile",
        "#MozRunner Prefs Start\n"
        'user_pref("a", 1);\n'
        "\n"
        "#MozRunner Prefs End\n"
        'user_pref("b", true);\n',
    )
    make_bt(profile=profile)
    assert profile.read() == 'user_pref("a", 1);\nuser_pref("b", true);\n'


def test_remove_delimiters_logs_on_missing_userjs(make_bt, tmp_path):
    """Check that a missing user.js is logged as critical instead of raising."""
    bt = make_bt()
    bt.profile = SimpleNamespace(profile=str(tmp_path / "missing"))
    with mock.patch.object(bt_base, "LOG") as log:
        bt.remove_mozprofile_delimiters_from_profile()
    assert "while removing mozprofile delimiters" in log.critical.call_args[0][0]


def test_set_browser_test_prefs(make_bt, tmp_path):
    """Check that test prefs are set on the profile and delimiters are stripped afterwards."""
    profile = FakeProfile(tmp_path / "profile")
    bt = make_bt(profile=profile)
    with open(profile.userjs, "w") as fh:
        fh.write('#MozRunner Prefs Start\nuser_pref("c", 3);\n\n')
    bt.set_browser_test_prefs({"c": 3})
    profile.set_preferences.assert_called_once_with({"c": 3})
    assert profile.read() == 'user_pref("c", 3);\n'


def test_convert_prefs_to_dict(make_bt):
    """Check that raw pref lines are parsed into bools, ints and strings."""
    raw = (
        "a.bool=true\r\n"
        "b.bool=False\n"
        "c.int=42\n"
        "d.str=hello\n"
        "e.rue=construe\n"
        "f.eq=key=value\n"
        "no-equals-sign\n"
    )
    assert make_bt()._convert_prefs_to_dict(raw) == {
        "a.bool": True,
        "b.bool": False,
        "c.int": 42,
        "d.str": "hello",
        "e.rue": "construe",
        "f.eq": "key=value",
    }


def test_run_test_setup_converts_preferences(make_bt, make_test_dict):
    """Check that run_test_setup converts string prefs to a dict and applies them."""
    bt = make_bt()
    bt.set_browser_test_prefs = mock.Mock()
    make_test_dict["preferences"] = "x.y=true\nx.z=5"
    bt.run_test_setup(make_test_dict)
    assert make_test_dict["preferences"] == {"x.y": True, "x.z": 5}
    bt.set_browser_test_prefs.assert_called_once_with({"x.y": True, "x.z": 5})
    assert bt.driver_paths == ["--firefox.geckodriverPath", "browsertime_geckodriver"]


@pytest.mark.parametrize(
    "test_patch",
    [{"type": "benchmark"}, {"type": "pageload", "benchmark_webserver": True}],
)
def test_run_test_setup_starts_benchmark_server(make_bt, make_test_dict, test_patch):
    """Check that benchmark tests start a server and fill in its host and port."""
    bt = make_bt(debug_mode=True)
    make_test_dict.update(test_patch)
    make_test_dict["test_url"] = "http://<host>:<port>/bench.html"
    benchmark = mock.Mock(host="127.0.0.1", port="8123")
    with mock.patch.object(bt_base, "Benchmark", return_value=benchmark) as bench:
        bt.run_test_setup(make_test_dict)
    bench.assert_called_once_with(bt.config, make_test_dict, debug_mode=True)
    assert bt.benchmark is benchmark
    assert make_test_dict["test_url"] == "http://127.0.0.1:8123/bench.html"


def test_run_test_setup_starts_playback_if_still_missing(make_bt, make_test_dict):
    """Check that playback is started again when the base setup left none running."""
    bt = make_bt()
    bt.start_playback = mock.Mock()
    make_test_dict["playback"] = "mitmproxy"
    bt.run_test_setup(make_test_dict)
    assert bt.start_playback.call_count == 2


def test_run_test_setup_no_geckodriver(make_bt, make_test_dict):
    """Check that no driver paths are passed when geckodriver is not configured."""
    bt = make_bt(browsertime_geckodriver=None)
    bt.run_test_setup(make_test_dict)
    assert bt.driver_paths == []


@pytest.mark.parametrize("app", CHROME_APPS)
def test_run_test_setup_local_chromedriver(make_bt, make_test_dict, app):
    """Check that Chromium apps get the local chromedriver path as given."""
    bt = make_bt(app=app)
    bt.run_test_setup(make_test_dict)
    assert bt.driver_paths == [
        "--firefox.geckodriverPath",
        "browsertime_geckodriver",
        "--chrome.chromedriverPath",
        "browsertime_chromedriver",
    ]


def test_run_test_setup_chromedriver_ignored_for_firefox(make_bt, make_test_dict):
    """Check that Firefox runs never get a chromedriver path."""
    bt = make_bt(app="firefox")
    bt.run_test_setup(make_test_dict)
    assert "--chrome.chromedriverPath" not in bt.driver_paths


@pytest.mark.parametrize(
    "browser_version, run_local, expected",
    [
        ("120.0.6099.71", False, "chromedriver_120"),
        (None, False, "chromedriver_77"),
        ("121.1", True, "chromedriver_121"),
    ],
)
def test_run_test_setup_versioned_chromedriver(
    make_bt, make_test_dict, tmp_path, browser_version, run_local, expected
):
    """Check that the chromedriver path is formatted with the browser major version."""
    (tmp_path / expected).touch()
    bt = make_bt(
        app="chrome",
        run_local=run_local,
        browsertime_chromedriver=str(tmp_path / "chromedriver_{}"),
    )
    bt.browser_version = browser_version
    bt.run_test_setup(make_test_dict)
    assert bt.browsertime_chromedriver == str(tmp_path / expected)
    assert bt.driver_paths[-2:] == [
        "--chrome.chromedriverPath",
        str(tmp_path / expected),
    ]


def test_run_test_setup_missing_chromedriver_raises(make_bt, make_test_dict, tmp_path):
    """Check that a missing versioned chromedriver raises a clear error."""
    bt = make_bt(
        app="chrome",
        run_local=False,
        browsertime_chromedriver=str(tmp_path / "chromedriver_{}"),
    )
    with pytest.raises(Exception, match="Cannot find the chromedriver"):
        bt.run_test_setup(make_test_dict)


@pytest.mark.parametrize(
    "name, release, expected",
    [
        ("youtube-playback-h264", True, "0"),
        ("youtube-playback-h264", False, None),
        ("tp6", True, None),
    ],
)
def test_run_test_setup_youtube_release(
    make_bt, make_test_dict, name, release, expected
):
    """Check that YouTube playback on release builds re-enables nonlocal connections."""
    bt = make_bt(is_release_build=release)
    make_test_dict["name"] = name
    bt.run_test_setup(make_test_dict)
    assert os.environ.get("MOZ_DISABLE_NONLOCAL_CONNECTIONS") == expected


def test_run_test_teardown_stops_everything(make_bt, make_test_dict):
    """Check that teardown stops playback, benchmark and support class and counts crashes."""
    bt = make_bt(symbols_path="/symbols")
    playback = bt.playback = mock.Mock()
    benchmark = bt.benchmark = mock.Mock()
    support = mock.Mock()
    make_test_dict["support_class"] = support
    with mock.patch.object(bt_base.mozcrash, "log_crashes", return_value=2) as lc:
        bt.run_test_teardown(make_test_dict)
    playback.stop.assert_called_once_with()
    benchmark.stop_http_server.assert_called_once_with()
    support.clean_up.assert_called_once_with()
    assert bt.playback is None and bt.benchmark is None
    lc.assert_called_once_with(bt_base.LOG, bt.crash_directory, "/symbols")
    assert bt.crashes == 2


def test_run_test_teardown_without_resources(make_bt, make_test_dict):
    """Check that teardown works when no playback, benchmark or support class exist."""
    bt = make_bt()
    with mock.patch.object(bt_base.mozcrash, "log_crashes", return_value=0):
        bt.run_test_teardown(make_test_dict)
    assert bt.crashes == 0


def test_clean_up_removes_crash_directory(make_bt):
    """Check that clean_up removes the crash directory."""
    bt = make_bt()
    crash_dir = bt.crash_directory
    with mock.patch.object(Perftest, "clean_up_mitmproxy"):
        bt.clean_up()
    assert not os.path.exists(crash_dir)
    assert bt._dirs_to_remove == []


@pytest.mark.parametrize(
    "app, gecko_profile, extra_run, expose, expected",
    [
        ("firefox", True, False, True, True),
        ("firefox", False, True, True, True),
        ("chrome", False, True, True, True),
        ("firefox", False, False, True, False),
        ("firefox", True, False, False, False),
        ("safari", True, True, True, False),
    ],
)
def test_expose_browser_profiler(
    make_bt, app, gecko_profile, extra_run, expose, expected
):
    """Check when the browser profiler is exposed to browsertime."""
    bt = make_bt(app=app, gecko_profile=gecko_profile)
    test = {"expose_browser_profiler": expose}
    assert bool(bt._expose_browser_profiler(extra_run, test)) is expected


@pytest.mark.parametrize(
    "test_patch, expected",
    [
        ({"type": "scenario"}, "browsertime_scenario.js"),
        ({"type": "benchmark"}, "browsertime_benchmark.js"),
        ({"type": "benchmark", "test_script": "speedometer3.js"}, "speedometer3.js"),
        ({"interactive": True}, "browsertime_interactive.js"),
        ({"test_script": "custom.js"}, "custom.js"),
        ({}, "browsertime_pageload.js"),
    ],
)
def test_compose_cmd_script_selection(make_bt, make_test_dict, test_patch, expected):
    """Check that the browsertime script is chosen from the test type and fields."""
    bt = make_bt()
    make_test_dict.update(test_patch)
    cmd = compose(bt, make_test_dict)
    script = script_of(bt, cmd)
    assert os.path.basename(script) == expected
    assert os.path.basename(os.path.normpath(os.path.dirname(script))) == "browsertime"
    assert cmd[:2] == ["browsertime_node", "browsertime_browsertimejs"]


@pytest.mark.parametrize(
    "user_args, expected",
    [
        (["test_script=pageload"], "browsertime_pageload.js"),
        (["test_script=interactive"], "browsertime_interactive.js"),
        (["url=https://www.sitespeed.io"], "https://www.sitespeed.io"),
        (["test_script=/abs/my_script.js"], "/abs/my_script.js"),
    ],
)
def test_compose_cmd_custom_browsertime_script(
    make_bt, make_test_dict, user_args, expected
):
    """Check that the custom browsertime test takes its script or URL from user args."""
    bt = make_bt(browsertime_user_args=user_args)
    make_test_dict["name"] = "browsertime"
    script = script_of(bt, compose(bt, make_test_dict))
    if expected.startswith("browsertime_"):
        assert os.path.basename(script) == expected
    else:
        assert script == expected


def test_compose_cmd_custom_script_required(make_bt, make_test_dict):
    """Check that the custom browsertime test fails without a script or URL."""
    bt = make_bt(browsertime_user_args=["iterations=2"])
    make_test_dict["name"] = "browsertime"
    with pytest.raises(Exception, match="You must provide a path to the test script"):
        compose(bt, make_test_dict)


def test_compose_cmd_defaults(make_bt, make_test_dict, tmp_path):
    """Check that a plain test passes its URLs, timeouts and result dir and no extras."""
    bt = make_bt()
    make_test_dict["secondary_url"] = "http://example.com/2"
    cmd = compose(bt, make_test_dict)
    expected = {
        "--browsertime.url": "http://example.com",
        "--browsertime.secondary_url": "http://example.com/2",
        "--timeouts.pageLoad": str(TIMEOUT),
        "--timeouts.script": str(TIMEOUT),
        "--browsertime.post_startup_delay": "1000",
        "--resultDir": str(tmp_path / "browsertime-results" / "raptor-test"),
    }
    for arg, value in expected.items():
        assert arg_value(cmd, arg) == value, arg
    assert arg_values(cmd, "--firefox.env") == ["MOZ_REMOTE_SETTINGS_DEVTOOLS=1"]
    for absent in (
        "--firefox.collectMozLog",
        "--browsertime.support_class",
        "--browsertime.browserRestartTries",
        "--firefox.profileTemplate",
        "--firefox.geckoProfiler",
        "--chrome.trace",
        "--browsertime.testName",
        "-vvv",
    ):
        assert absent not in cmd
    assert not bt.results_handler.perfstats


def test_compose_cmd_removes_previous_result_dir(make_bt, make_test_dict, tmp_path):
    """Check that composing the main run removes stale results for the test."""
    bt = make_bt()
    stale = tmp_path / "browsertime-results" / "raptor-test"
    (stale / "old.json").touch()
    compose(bt, make_test_dict)
    assert not stale.exists()


def test_compose_cmd_config_flags(make_bt, make_test_dict, tmp_path, monkeypatch):
    """Check that config and test settings are reflected in the command."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", "/fetches")
    bt = make_bt(
        live_sites=True,
        chimera=True,
        test_bytecode_cache=True,
        debug_mode=True,
        verbose=True,
        environment={"FOO": "bar", "BAZ": "1"},
        profile=FakeProfile(tmp_path / "profile"),
    )
    make_test_dict.update(
        page_cycles=3,
        browser_cycles=2,
        page_complete_wait_time=750,
        perfstats="true",
        test_cmds="['measure', 'pageload'],\n['wait', 5],\n[]",
    )
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--browsertime.page_cycle_delay") == "5000"
    assert arg_value(cmd, "--browsertime.page_cycles") == "3"
    assert arg_value(cmd, "--timeouts.script") == str(3 * TIMEOUT)
    assert arg_value(cmd, "--iterations") == "2"
    assert arg_value(cmd, "--pageCompleteWaitTime") == "750"
    assert arg_value(cmd, "--browsertime.chimera") == "true"
    assert arg_value(cmd, "--browsertime.test_bytecode_cache") == "true"
    assert arg_value(cmd, "--firefox.perfStats") == "true"
    assert arg_value(cmd, "--browsertime.moz_fetch_dir") == "/fetches"
    assert arg_value(cmd, "--debug") == "true"
    assert "-vv" in cmd and "-vvv" in cmd
    assert arg_value(cmd, "--firefox.profileTemplate") == str(tmp_path / "profile")
    assert arg_values(cmd, "--firefox.env") == [
        "FOO=bar",
        "BAZ=1",
        "MOZ_REMOTE_SETTINGS_DEVTOOLS=1",
    ]
    assert arg_value(cmd, "--browsertime.commands") == "measure:::pageload;;;wait:::5"
    assert bt.results_handler.perfstats is True


def test_compose_cmd_extra_profiler_run(make_bt, make_test_dict, tmp_path):
    """Check that the extra profiler run uses its own cycles, delay cap and result dir."""
    bt = make_bt(post_startup_delay=30000, app="safari")
    make_test_dict.update(
        page_cycles=25,
        browser_cycles=5,
        extra_profiler_run_page_cycles=2,
        extra_profiler_run_browser_cycles=1,
    )
    cmd = compose(bt, make_test_dict, extra_profiler_run=True)
    assert arg_value(cmd, "--browsertime.page_cycles") == "2"
    assert arg_value(cmd, "--iterations") == "1"
    assert arg_value(cmd, "--browsertime.post_startup_delay") == "1000"
    profiling_dir = tmp_path / "browsertime-results" / "raptor-test" / "profiling"
    assert arg_value(cmd, "--resultDir") == str(profiling_dir)
    assert profiling_dir.is_dir()


@pytest.mark.parametrize(
    "env_log, test_patch, expected",
    [
        ("nsHttp:5", {"firefox_moz_log": "cookie:1"}, "nsHttp:5"),
        (None, {"firefox_moz_log": "cookie:1"}, "cookie:1"),
        (
            None,
            {"firefox_log_modules": " Network "},
            "timestamp,append,sync,nsHostResolver:5,nsSocketTransport:4,"
            "nsHttp:3,TRRIDLECONNECTION:5,cookie:3",
        ),
        (None, {"firefox_log_modules": "graphics"}, None),
    ],
)
def test_compose_cmd_moz_log(
    make_bt, make_test_dict, monkeypatch, env_log, test_patch, expected
):
    """Check the MOZ_LOG precedence of env var, test value and network preset."""
    if env_log:
        monkeypatch.setenv("MOZ_LOG", env_log)
    bt = make_bt()
    make_test_dict.update(test_patch)
    cmd = compose(bt, make_test_dict)
    if expected is None:
        assert "--firefox.setMozLog" not in cmd
    else:
        assert arg_value(cmd, "--firefox.collectMozLog") == "true"
        assert arg_value(cmd, "--firefox.setMozLog") == expected


def test_compose_cmd_support_class(make_bt, make_test_dict):
    """Check that a support class is flagged and may modify the command."""
    bt = make_bt()
    support = mock.Mock()
    support.modify_command.side_effect = lambda cmd, test: cmd.append("--modified")
    make_test_dict["support_class"] = support
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--browsertime.support_class") == "true"
    assert cmd[-1] == "--modified"
    support.modify_command.assert_called_once_with(cmd, make_test_dict)


def test_compose_cmd_fenix_startup_options(make_bt, make_test_dict):
    """Check that Fenix gets extra browser restart tries and a longer start timeout."""
    bt = make_bt(app="fenix")
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--browsertime.browserRestartTries") == "10"
    assert arg_value(cmd, "--timeouts.browserStart") == "180000"


def test_compose_cmd_test_browsertime_args(make_bt, make_test_dict):
    """Check that test browsertime_args override, add and repeat options correctly."""
    bt = make_bt()
    make_test_dict["browsertime_args"] = (
        " --pageCompleteCheckStartWait=1000 --skipHar --newFlag --new.option=7"
        " --firefox.preference=a:1 --firefox.preference=b:2 "
    )
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--pageCompleteCheckStartWait") == "1000"
    assert cmd.count("--pageCompleteCheckStartWait") == 1
    assert cmd.count("--skipHar") == 1
    assert "--newFlag" in cmd
    assert arg_value(cmd, "--new.option") == "7"
    assert arg_values(cmd, "--firefox.preference") == ["a:1", "b:2"]


def test_compose_cmd_bad_browsertime_args(make_bt, make_test_dict):
    """Check that a malformed test browsertime_arg raises."""
    bt = make_bt()
    make_test_dict["browsertime_args"] = "--a=b=c"
    with pytest.raises(Exception, match="was not split properly"):
        compose(bt, make_test_dict)


def test_compose_cmd_user_args_override(make_bt, make_test_dict):
    """Check that user browsertime args override defaults and add new options."""
    bt = make_bt(
        browsertime_user_args=[
            "pageCompleteWaitTime=10",
            "iterations=4",
            "chrome.args=--headless",
            "firefox.preference=x:y",
            "brand.new=1",
        ]
    )
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--pageCompleteWaitTime") == "10"
    assert cmd.count("--pageCompleteWaitTime") == 1
    assert arg_value(cmd, "--iterations") == "4"
    assert "--chrome.args=--headless" in cmd
    assert arg_values(cmd, "--firefox.preference") == ["x:y"]
    assert arg_value(cmd, "--brand.new") == "1"


def test_compose_cmd_priority1_conflicts(make_bt, make_test_dict):
    """Check how priority-1 options override, append or skip existing options."""
    bt = make_bt(cls=FlagBrowsertime, browsertime_user_args=["dangling=-x"])
    cmd = compose(bt, make_test_dict)
    assert cmd.count("--skipHar") == 1
    assert cmd[cmd.index("--skipHar") + 1] == "--pageLoadStrategy"
    assert arg_value(cmd, "--pageLoadStrategy") == "eager"
    assert cmd[cmd.index("--newflag") + 1] != "--pageLoadStrategy"
    assert "stray" not in cmd
    assert cmd[-1] == "--dangling=-x"


@pytest.mark.parametrize("app", CHROME_APPS)
def test_compose_cmd_chrome_args(make_bt, make_test_dict, app):
    """Check that Chromium apps get their chrome args and chromedriver path."""
    bt = make_bt(app=app)
    cmd = compose(bt, make_test_dict)
    assert "--chrome.args=--fake-flag" in cmd
    assert "--chrome.chromedriverPath" in cmd


@pytest.mark.parametrize(
    "app, no_ffwindowrecorder, visualmetrics, recorder",
    [
        ("firefox", False, True, "true"),
        ("firefox", True, False, "false"),
        ("chrome", False, False, "false"),
    ],
)
def test_compose_cmd_video(
    make_bt, make_test_dict, app, no_ffwindowrecorder, visualmetrics, recorder
):
    """Check the video, visual metrics and window recorder options."""
    bt = make_bt(
        app=app,
        browsertime_video=True,
        browsertime_visualmetrics=visualmetrics,
        browsertime_no_ffwindowrecorder=no_ffwindowrecorder,
    )
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--video") == "true"
    assert arg_value(cmd, "--visualMetrics") == ("true" if visualmetrics else "false")
    for opt in (
        "--visualMetricsContentful",
        "--visualMetricsPerceptual",
        "--visualMetricsPortable",
        "--videoParams.keepOriginalVideo",
    ):
        assert arg_value(cmd, opt) == "true"
    assert arg_value(cmd, "--firefox.windowRecorder") == recorder
    if recorder == "false":
        assert arg_value(cmd, "--xvfbParams.display") == "0"
    else:
        assert "--xvfbParams.display" not in cmd


def test_compose_cmd_gecko_profiler_defaults(make_bt, make_test_dict, tmp_path):
    """Check the default Gecko profiler options when profiling Firefox."""
    bt = make_bt(gecko_profile=True)
    bt._init_gecko_profiling = mock.Mock()
    cmd = compose(bt, make_test_dict)
    bt._init_gecko_profiling.assert_called_once_with(make_test_dict)
    assert "--firefox.geckoProfiler" in cmd
    assert "--firefox.geckoProfilerRecordingType" not in cmd
    assert (
        arg_value(cmd, "--firefox.geckoProfilerParams.features")
        == "js,stackwalk,screenshots,memory,java"
    )
    assert (
        arg_value(cmd, "--firefox.geckoProfilerParams.threads")
        == "GeckoMain,Compositor,Renderer"
    )
    assert arg_value(cmd, "--firefox.geckoProfilerParams.bufferSize") == str(
        128 * 1024 * 1024
    )
    assert "--firefox.geckoProfilerParams.interval" not in cmd
    assert bt.config["browsertime_result_dir"] == str(
        tmp_path / "browsertime-results" / "raptor-test"
    )


def test_compose_cmd_gecko_profiler_overrides(make_bt, make_test_dict):
    """Check that config and test values override Gecko profiler defaults."""
    bt = make_bt(
        app="geckoview",
        gecko_profile=True,
        gecko_profile_features="js,cpu",
        gecko_profile_interval=0,
        gecko_profile_extra_threads=["DOM Worker", "Renderer2"],
    )
    bt._init_gecko_profiling = mock.Mock()
    make_test_dict.update(
        expose_browser_profiler=True,
        gecko_profile_features="ignored",
        gecko_profile_threads="GeckoMain",
        gecko_profile_entries=1000,
    )
    cmd = compose(bt, make_test_dict)
    assert arg_value(cmd, "--firefox.geckoProfilerRecordingType") == "custom"
    assert arg_value(cmd, "--browsertime.expose_profiler") == "true"
    assert arg_value(cmd, "--firefox.geckoProfilerParams.features") == "js,cpu"
    assert arg_value(cmd, "--firefox.geckoProfilerParams.interval") == "0"
    assert (
        arg_value(cmd, "--firefox.geckoProfilerParams.threads")
        == "GeckoMain,DOM Worker,Renderer2"
    )
    assert arg_value(cmd, "--firefox.geckoProfilerParams.bufferSize") == "1000"


def test_compose_cmd_gecko_profiler_on_extra_run(make_bt, make_test_dict):
    """Check that the extra profiler run enables the Gecko profiler."""
    bt = make_bt()
    bt._init_gecko_profiling = mock.Mock()
    cmd = compose(bt, make_test_dict, extra_profiler_run=True)
    assert "--firefox.geckoProfiler" in cmd


def test_compose_cmd_chrome_trace(make_bt, make_test_dict):
    """Check that the extra profiler run on Chrome enables tracing with categories."""
    bt = make_bt(app="chrome", extra_profiler_run=True)
    bt._init_chrome_trace = mock.Mock()
    make_test_dict["expose_browser_profiler"] = True
    cmd = compose(bt, make_test_dict, extra_profiler_run=True)
    bt._init_chrome_trace.assert_called_once_with(make_test_dict)
    assert "--chrome.trace" in cmd
    assert "--chrome.enableTraceScreenshots" in cmd
    assert arg_value(cmd, "--chrome.timelineRecordingType") == "custom"
    categories = arg_values(cmd, "--chrome.traceCategory")
    assert len(categories) == 7
    assert "disabled-by-default-devtools.timeline" in categories
    assert "--firefox.geckoProfiler" not in cmd
    assert "browsertime_result_dir" in bt.config


def test_compose_cmd_chrome_trace_respects_user_categories(make_bt, make_test_dict):
    """Check that user trace categories replace the default ones."""
    bt = make_bt(app="custom-car")
    bt._init_chrome_trace = mock.Mock()
    make_test_dict["browsertime_args"] = "--chrome.traceCategory=mine"
    cmd = compose(bt, make_test_dict, extra_profiler_run=True)
    assert arg_values(cmd, "--chrome.traceCategory") == ["mine"]
    assert "--chrome.timelineRecordingType" not in cmd
    assert "--chrome.enableTraceScreenshots" in cmd


def test_compose_cmd_chrome_no_trace_without_extra_run(make_bt, make_test_dict):
    """Check that Chrome is not traced outside the extra profiler run."""
    bt = make_bt(app="chrome", gecko_profile=True)
    bt._init_chrome_trace = mock.Mock()
    cmd = compose(bt, make_test_dict)
    bt._init_chrome_trace.assert_not_called()
    assert "--chrome.trace" not in cmd


@pytest.mark.parametrize("live_sites, login", [(False, False), (True, True)])
def test_compose_cmd_pageload_finalize(make_bt, make_test_dict, live_sites, login):
    """Check that pageload tests get their name, live-site and login flags."""
    bt = make_bt(live_sites=live_sites)
    make_test_dict["type"] = "pageload"
    with mock.patch.object(
        Browsertime, "is_live_login_site", return_value=login
    ) as login_check:
        cmd = compose(bt, make_test_dict)
    login_check.assert_called_once_with("raptor-test")
    assert arg_value(cmd, "--browsertime.testName") == "raptor-test"
    assert arg_value(cmd, "--browsertime.liveSite") == str(live_sites)
    assert arg_value(cmd, "--browsertime.loginRequired") == str(login)


@pytest.mark.parametrize(
    "login, verbose, automation, stripped",
    [
        (True, True, "1", True),
        (True, True, None, False),
        (True, False, "1", False),
        (False, True, "1", False),
    ],
)
def test_finalize_pageload_strips_verbose_for_login(
    make_bt, make_test_dict, monkeypatch, login, verbose, automation, stripped
):
    """Check that verbose flags are dropped only for login sites in CI."""
    if automation:
        monkeypatch.setenv("MOZ_AUTOMATION", automation)
    bt = make_bt(verbose=verbose)
    options = ["--a", "-v", "-vvv", "--b"]
    with mock.patch.object(Browsertime, "is_live_login_site", return_value=login):
        bt._finalize_pageload_test_setup(
            browsertime_options=options, test=make_test_dict
        )
    if stripped:
        assert options[:2] == ["--a", "--b"]
        assert bt.config["verbose"] is False
    else:
        assert options[:4] == ["--a", "-v", "-vvv", "--b"]
        assert bt.config["verbose"] is verbose


def write_pageload_sites(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({
            "desktop": [
                {"name": "amazon", "login": False},
                {"name": "gmail", "login": True},
            ]
        })
    )


@pytest.mark.parametrize(
    "name, expected", [("gmail", True), ("amazon", False), ("unknown", False)]
)
def test_is_live_login_site_ci(monkeypatch, tmp_path, name, expected):
    """Check the login lookup in pageload_sites.json on CI."""
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    monkeypatch.chdir(tmp_path)
    write_pageload_sites(tmp_path / "tests/raptor/browsertime/pageload_sites.json")
    assert Browsertime.is_live_login_site(name) is expected


def test_is_live_login_site_local(monkeypatch, tmp_path):
    """Check the login lookup in pageload_sites.json in a local run."""
    monkeypatch.delenv("MOZ_AUTOMATION", raising=False)
    cwd = tmp_path / "obj" / "testing"
    cwd.mkdir(parents=True)
    monkeypatch.chdir(cwd)
    write_pageload_sites(tmp_path / "raptor/browsertime/pageload_sites.json")
    assert Browsertime.is_live_login_site("gmail") is True


def test_compute_process_timeout_debug_mode(make_bt, make_test_dict):
    """Check that debug mode disables the process timeout."""
    bt = make_bt(debug_mode=True)
    assert bt._compute_process_timeout(make_test_dict, TIMEOUT, []) == sys.maxsize


def test_compute_process_timeout_video_and_cycles(make_bt, make_test_dict):
    """Check that video, visual metrics and cycles extend the process timeout."""
    bt = make_bt(
        post_startup_delay=3000,
        browsertime_video=True,
        browsertime_visualmetrics=True,
    )
    make_test_dict.update(page_cycles=2, browser_cycles=3)
    assert (
        bt._compute_process_timeout(make_test_dict, 10000, []) == (20 + 3 + 20 + 60) * 3
    )
    cmd = ["node", "--iterations", "5"]
    assert (
        bt._compute_process_timeout(make_test_dict, 10000, cmd)
        == (20 + 3 + 20 + 60) * 5
    )


def test_compute_process_timeout_bad_iterations(make_bt, make_test_dict):
    """Check that a non-int iterations value raises."""
    bt = make_bt()
    with pytest.raises(Exception, match="non-int value for the iterations: abc"):
        bt._compute_process_timeout(make_test_dict, TIMEOUT, ["--iterations", "abc"])


@pytest.mark.parametrize(
    "config, profiler_attr, expected_extra",
    [
        ({"samply_profile": True}, "samply_profiler", 300),
        ({"samply_profile": True}, None, 0),
        ({"etw_profile": True}, "etw_profiler", 300),
        ({"perf_profile": True}, "perf_profiler", 300),
        ({"simpleperf": True}, None, 300),
    ],
)
def test_compute_process_timeout_profilers(
    make_bt, make_test_dict, config, profiler_attr, expected_extra
):
    """Check that active profilers add five minutes to the process timeout."""
    bt = make_bt()
    bt.config.update(config)
    if profiler_attr:
        setattr(bt, profiler_attr, mock.Mock())
    assert (
        bt._compute_process_timeout(make_test_dict, TIMEOUT, []) == 22 + expected_extra
    )


@pytest.mark.parametrize(
    "msg, expected",
    [
        ("Failed to find xpath and double click it", False),
        ("Failed to find xpath", True),
        ("Something else", True),
    ],
)
def test_kill_browsertime_process(msg, expected):
    """Check that only the 2FA xpath double-click error spares the process."""
    assert Browsertime._kill_browsertime_process(msg) is expected


def test_kill_on_windows(make_bt, monkeypatch):
    """Check that kill sends CTRL_BREAK_EVENT on Windows."""
    bt = make_bt()
    bt.config["platform"] = "win"
    monkeypatch.setattr(signal, "CTRL_BREAK_EVENT", 1, raising=False)
    proc = mock.Mock()
    bt.kill(proc)
    proc.send_signal.assert_called_once_with(1)
    proc.wait.assert_called_once_with()


@pytest.mark.parametrize("platform", ["linux", "mac"])
def test_kill_on_posix(make_bt, monkeypatch, platform):
    """Check that kill sends SIGKILL to the process group on POSIX."""
    bt = make_bt()
    bt.config["platform"] = platform
    killpg = mock.Mock()
    monkeypatch.setattr(bt_base.os, "killpg", killpg, raising=False)
    monkeypatch.setattr(signal, "SIGKILL", 9, raising=False)
    proc = mock.Mock(pid=4242)
    bt.kill(proc)
    killpg.assert_called_once_with(4242, 9)
    proc.wait.assert_called_once_with()


@pytest.fixture
def fake_screenshot_modules(monkeypatch):
    mozscreenshot = types.ModuleType("mozscreenshot")
    mozscreenshot.dump_screen = mock.Mock()
    mozscreenshot.dump_device_screen = mock.Mock()
    mozdevice = types.ModuleType("mozdevice")
    mozdevice.ADBDeviceFactory = mock.Mock(return_value="device")
    monkeypatch.setitem(sys.modules, "mozscreenshot", mozscreenshot)
    monkeypatch.setitem(sys.modules, "mozdevice", mozdevice)
    return SimpleNamespace(mozscreenshot=mozscreenshot, mozdevice=mozdevice)


@pytest.mark.parametrize(
    "app, platform, enabled",
    [("firefox", "linux", False), ("chrome", "win", True), ("unknown", "mac", True)],
)
def test_get_failure_screenshot_skipped(
    make_bt, fake_screenshot_modules, app, platform, enabled
):
    """Check the cases where no failure screenshot is taken."""
    bt = make_bt(screenshot_on_failure=enabled)
    bt.config.update(app=app, platform=platform)
    bt.get_failure_screenshot()
    fake_screenshot_modules.mozscreenshot.dump_screen.assert_not_called()
    fake_screenshot_modules.mozscreenshot.dump_device_screen.assert_not_called()


def test_get_failure_screenshot_desktop_ci(
    make_bt, fake_screenshot_modules, tmp_path, monkeypatch
):
    """Check that CI desktop screenshots use the tests/bin utility path."""
    upload = tmp_path / "build" / "blobber_upload_dir"
    upload.mkdir(parents=True)
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(upload))
    bt = make_bt(screenshot_on_failure=True)
    bt.config["platform"] = "linux"
    bt.get_failure_screenshot()
    utility_path, log = fake_screenshot_modules.mozscreenshot.dump_screen.call_args[0]
    assert str(utility_path) == str(tmp_path / "build" / "tests" / "bin")
    assert log is bt_base.LOG


def test_get_failure_screenshot_desktop_local(
    make_bt, fake_screenshot_modules, monkeypatch
):
    """Check that local desktop screenshots use the objdir utility path."""
    monkeypatch.setenv("MOZ_DEVELOPER_OBJ_DIR", "/obj")
    bt = make_bt(app="chrome", screenshot_on_failure=True)
    bt.config["platform"] = "mac"
    bt.get_failure_screenshot()
    fake_screenshot_modules.mozscreenshot.dump_screen.assert_called_once_with(
        os.path.join("/obj", "dist", "bin"), bt_base.LOG
    )


@pytest.mark.parametrize("app", ["fenix", "chrome-m"])
def test_get_failure_screenshot_android(make_bt, fake_screenshot_modules, app):
    """Check that Android screenshots are taken from the device."""
    bt = make_bt(app=app, screenshot_on_failure=True)
    bt.get_failure_screenshot()
    fake_screenshot_modules.mozdevice.ADBDeviceFactory.assert_called_once_with(
        verbose=True
    )
    fake_screenshot_modules.mozscreenshot.dump_device_screen.assert_called_once_with(
        "device", bt_base.LOG
    )


class FakeRun:
    def __init__(self, lines=(), returncodes=(0,), action=None):
        self.lines = list(lines)
        self.returncodes = list(returncodes)
        self.action = action
        self.calls = []

    def __call__(self, cmd, **kwargs):
        proc = mock.Mock()
        self.calls.append(dict(kwargs, cmd=cmd, proc=proc))
        for line in self.lines:
            kwargs["output_line_handler"](proc, line)
        if self.action == "timeout":
            kwargs["timeout_handler"](proc)
        elif self.action == "output_timeout":
            kwargs["output_timeout_handler"](proc)
        code = (
            self.returncodes.pop(0)
            if len(self.returncodes) > 1
            else self.returncodes[0]
        )
        return SimpleNamespace(returncode=code)


@pytest.fixture
def fake_run(monkeypatch):
    def inner(**kwargs):
        run = FakeRun(**kwargs)
        monkeypatch.setattr(bt_base.mozprocess, "run_and_wait", run)
        return run

    return inner


def test_run_extra_profiler_run(make_bt, make_test_dict, fake_run, tmp_path):
    """Check that the extra profiler run launches browsertime with the given settings."""
    bt = make_bt()
    bt._init_gecko_profiling = mock.Mock()
    bt.driver_paths = []
    run = fake_run()
    handler = mock.Mock()
    bt.run_extra_profiler_run(make_test_dict, TIMEOUT, 50, 60, handler, {"A": "1"})
    call = run.calls[0]
    assert call["output_line_handler"] is handler
    assert call["env"] == {"A": "1"}
    assert call["timeout"] == 50
    assert call["output_timeout"] == 60
    assert call["text"] is False
    assert "--firefox.geckoProfiler" in call["cmd"]
    assert arg_value(call["cmd"], "--resultDir").endswith("profiling")


@pytest.mark.parametrize("action", ["timeout", "output_timeout"])
def test_run_extra_profiler_run_timeouts_do_not_raise(
    make_bt, make_test_dict, fake_run, action
):
    """Check that extra profiler run timeouts and failures are only logged."""
    bt = make_bt()
    bt._init_gecko_profiling = mock.Mock()
    bt.driver_paths = []
    bt.kill = mock.Mock()
    bt.browsertime_failure = "boom"
    run = fake_run(action=action)
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_extra_profiler_run(make_test_dict, TIMEOUT, 50, 60, mock.Mock(), {})
    bt.kill.assert_called_once_with(run.calls[0]["proc"])
    assert bt.timed_out is (action == "timeout")
    assert bt.output_timed_out is (action == "output_timeout")
    messages = " ".join(c[0][0] for c in log.info.call_args_list)
    assert "extra profiler run timed out" in messages
    assert "extra profiler run failure: boom" in messages


def test_run_extra_profiler_run_swallows_exceptions(make_bt, make_test_dict):
    """Check that errors in the extra profiler run are logged, not raised."""
    bt = make_bt()
    bt._compose_cmd = mock.Mock(side_effect=RuntimeError("nope"))
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_extra_profiler_run(make_test_dict, TIMEOUT, 50, 60, mock.Mock(), {})
    log.info.assert_called_with("Failed during the extra profiler run: nope")


def line(level, msg):
    return f"[2026-01-01 10:00:00] {level}: {msg}".encode()


def test_run_test_success(make_bt, make_test_dict, fake_run, tmp_path):
    """Check a successful run's env, line handling and browsertime.log output."""
    bt = make_bt()
    support = mock.Mock()
    make_test_dict["support_class"] = support
    run = fake_run(
        lines=[
            line("INFO", "starting"),
            line("WARNING", "careful"),
            line("METRICS", "pages: a, b,c"),
            "plain output \xcf\x83\n".encode("latin-1"),
        ]
    )
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == 1
    call = run.calls[0]
    assert call["cmd"][:2] == ["browsertime_node", "browsertime_browsertimejs"]
    assert call["timeout"] == 22
    assert call["output_timeout"] == bt_base.BROWSERTIME_PAGELOAD_OUTPUT_TIMEOUT
    env = call["env"]
    assert env["PYTHON"] == sys.executable
    assert env["MINIDUMP_SAVE_PATH"] == bt.crash_directory
    assert env["MOZ_REMOTE_SETTINGS_DEVTOOLS"] == "1"
    assert env["MOZ_REMOTE_SETTINGS_DISABLED"] == "1"
    assert env["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS"] == "1"
    assert env["BROWSERTIME_ROOT"] == str(tmp_path / "repo" / "tools" / "browsertime")
    support.modify_env.assert_called_once_with(env, make_test_dict)
    assert bt.page_count == ["a", " b", "c"]
    assert bt.browsertime_failure == ""
    log.warning.assert_called_once_with("careful")
    log.info.assert_any_call("starting")
    log.info.assert_any_call("plain output \n")
    log.error.assert_not_called()
    call["proc"].kill.assert_not_called()
    content = (tmp_path / "browsertime.log").read_bytes()
    assert b"===== raptor-test | " in content
    assert line("INFO", "starting") + b"\n" in content
    assert "plain output \xcf\x83\n".encode("latin-1") in content


def test_run_test_appends_to_log(make_bt, make_test_dict, fake_run, tmp_path):
    """Check that browsertime.log is appended to rather than overwritten."""
    (tmp_path / "browsertime.log").write_bytes(b"previous\n")
    bt = make_bt()
    fake_run(lines=[line("INFO", "x")])
    bt.run_test(make_test_dict, TIMEOUT)
    assert (tmp_path / "browsertime.log").read_bytes().startswith(b"previous\n")


def test_run_test_log_file_unwritable(make_bt, make_test_dict, fake_run, tmp_path):
    """Check that an unopenable browsertime.log only produces a warning."""
    (tmp_path / "browsertime.log").mkdir()
    bt = make_bt()
    run = fake_run(lines=[line("INFO", "x")])
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == 1
    assert "Could not open browsertime stderr log" in log.warning.call_args[0][0]


def test_run_test_error_line_kills_process(make_bt, make_test_dict, fake_run):
    """Check that a browsertime error line records the failure and kills the process."""
    bt = make_bt()
    bt.get_failure_screenshot = mock.Mock()
    run = fake_run(lines=[line("ERROR", "page failed")])
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    run.calls[0]["proc"].kill.assert_called_once_with()
    assert bt.browsertime_failure == "page failed"
    log.error.assert_called_once_with("Browsertime failed to run")
    bt.get_failure_screenshot.assert_called_once_with()


@pytest.mark.parametrize(
    "debug_mode, msg",
    [(True, "page failed"), (False, "no xpath to double click")],
)
def test_run_test_error_line_ignored(
    make_bt, make_test_dict, fake_run, debug_mode, msg
):
    """Check that error lines are ignored in debug mode and for 2FA errors."""
    bt = make_bt(debug_mode=debug_mode)
    run = fake_run(lines=[line("ERROR", msg)])
    bt.run_test(make_test_dict, TIMEOUT)
    run.calls[0]["proc"].kill.assert_not_called()
    assert bt.browsertime_failure == ""


def test_run_test_retries_then_succeeds(make_bt, make_test_dict, fake_run):
    """Check that a non-zero exit is retried until browsertime succeeds."""
    bt = make_bt()
    bt.get_failure_screenshot = mock.Mock()
    run = fake_run(returncodes=[1, 0])
    bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == 2
    assert bt.browsertime_failure == ""
    bt.get_failure_screenshot.assert_not_called()


def test_run_test_gives_up_after_max_attempts(make_bt, make_test_dict, fake_run):
    """Check that run_test raises the browsertime error after the last attempt."""
    bt = make_bt()
    bt.get_failure_screenshot = mock.Mock()
    run = fake_run(lines=[line("ERROR", "page failed")], returncodes=[1])
    with mock.patch.object(bt_base, "LOG") as log:
        with pytest.raises(Exception, match="page failed"):
            bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == bt_base.BROWSERTIME_MAX_ATTEMPTS
    assert log.warning.call_count == bt_base.BROWSERTIME_MAX_ATTEMPTS - 1
    assert bt.get_failure_screenshot.call_count == bt_base.BROWSERTIME_MAX_ATTEMPTS + 1


@pytest.mark.parametrize(
    "action, match",
    [
        ("timeout", "Browsertime process timed out after 22 seconds"),
        ("output_timeout", "timed out after waiting 120 seconds for output"),
    ],
)
def test_run_test_timeouts_raise(make_bt, make_test_dict, fake_run, action, match):
    """Check that process and output timeouts kill browsertime and raise."""
    bt = make_bt()
    bt.kill = mock.Mock()
    bt.get_failure_screenshot = mock.Mock()
    run = fake_run(action=action)
    with mock.patch.object(bt_base, "LOG") as log:
        with pytest.raises(Exception, match=match):
            bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == 1
    bt.kill.assert_called_once_with(run.calls[0]["proc"])
    bt.get_failure_screenshot.assert_called_once_with()
    assert match in log.critical.call_args[0][0]


def test_run_test_fatal_setup_error(make_bt, make_test_dict, fake_run):
    """Check that the Safari remote automation error fails at once with instructions."""
    bt = make_bt(app="safari")
    bt.get_failure_screenshot = mock.Mock()
    run = fake_run(
        lines=[
            line("INFO", "You must enable 'Allow remote automation'"),
            line("INFO", "Allow remote automation again"),
        ],
        returncodes=[1],
    )
    with pytest.raises(Exception, match="sudo safaridriver --enable"):
        bt.run_test(make_test_dict, TIMEOUT)
    assert len(run.calls) == 1
    run.calls[0]["proc"].kill.assert_called_once_with()


@pytest.mark.parametrize(
    "config, test_patch, expected_timeout, expected_output",
    [
        ({}, {"type": "benchmark"}, 22, None),
        ({}, {"output_timeout": "30"}, 30, 30),
        ({}, {"output_timeout": "600"}, 600, 600),
        ({"live_sites": True}, {}, 44, 240),
        ({"app": "fenix"}, {"type": "benchmark"}, 44, None),
    ],
)
def test_run_test_timeouts(
    make_bt,
    make_test_dict,
    fake_run,
    config,
    test_patch,
    expected_timeout,
    expected_output,
):
    """Check the process and output timeouts passed to browsertime."""
    bt = make_bt(**config)
    make_test_dict.update(test_patch)
    run = fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    assert run.calls[0]["timeout"] == expected_timeout
    assert run.calls[0]["output_timeout"] == expected_output


@pytest.mark.parametrize(
    "test_type, flag",
    [
        ("benchmark", "--browsertime.power_test"),
        ("pageload", "--android.usbPowerTesting"),
    ],
)
def test_run_test_power_test_ci(
    make_bt, make_test_dict, fake_run, monkeypatch, tmp_path, test_type, flag
):
    """Check that power tests on CI pass the meter serial and the power flag."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path / "fetches"))
    monkeypatch.setenv("USB_POWER_METER_SERIAL_NUMBER", "SN123")
    bt = make_bt(power_test=True, run_local=False)
    make_test_dict["type"] = test_type
    run = fake_run()
    with mock.patch.object(Browsertime, "is_live_login_site", return_value=False):
        bt.run_test(make_test_dict, TIMEOUT)
    call = run.calls[0]
    assert call["cmd"][-2:] == [flag, "true"]
    assert call["env"]["USB_POWER_METER_SERIAL_NUMBER"] == "SN123"
    assert call["env"]["BROWSERTIME_ROOT"] == str(tmp_path / "fetches" / "browsertime")


def test_run_test_power_test_local(make_bt, make_test_dict, fake_run):
    """Check that local power tests pass the flag without the meter serial."""
    bt = make_bt(power_test=True)
    run = fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    assert run.calls[0]["cmd"][-2:] == ["--android.usbPowerTesting", "true"]
    assert "USB_POWER_METER_SERIAL_NUMBER" not in run.calls[0]["env"]


def test_run_test_video_adds_ffmpeg_to_path(
    make_bt, make_test_dict, fake_run, tmp_path
):
    """Check that video runs put the ffmpeg directory first on PATH."""
    ffmpeg = tmp_path / "ffmpeg-dir" / "ffmpeg"
    bt = make_bt(browsertime_video=True, browsertime_ffmpeg=str(ffmpeg))
    run = fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    path = run.calls[0]["env"]["PATH"]
    assert path.split(os.pathsep)[0] == str(tmp_path / "ffmpeg-dir")


def test_run_test_extra_profiler_run(make_bt, make_test_dict, fake_run):
    """Check that the extra profiler run follows the main run with a quieter handler."""
    bt = make_bt(extra_profiler_run=True)
    bt.run_extra_profiler_run = mock.Mock()
    run = fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    args = bt.run_extra_profiler_run.call_args[0]
    assert args[:4] == (make_test_dict, TIMEOUT, 22, 120)
    assert args[5] is run.calls[0]["env"]

    handler = args[4]
    proc = mock.Mock()
    with mock.patch.object(bt_base, "LOG") as log:
        handler(proc, line("WARNING", "w"))
        handler(proc, line("ERROR", "e"))
    log.warning.assert_not_called()
    log.error.assert_not_called()
    log.info.assert_any_call("w")
    log.info.assert_any_call("Browsertime failed to run on extra profiler run")
    proc.kill.assert_called_once_with()


def test_run_test_no_extra_run_when_gecko_profiling(make_bt, make_test_dict, fake_run):
    """Check that no extra profiler run happens when already profiling."""
    bt = make_bt(extra_profiler_run=True, gecko_profile=True)
    bt._init_gecko_profiling = mock.Mock()
    bt.run_extra_profiler_run = mock.Mock()
    fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    bt.run_extra_profiler_run.assert_not_called()


def test_run_test_etw_not_supported_locally(make_bt, make_test_dict, fake_run):
    """Check that ETW profiling raises in local runs before launching."""
    bt = make_bt(etw_profile=True)
    run = fake_run()
    with pytest.raises(Exception, match="ETW profiling is not supported in local runs"):
        bt.run_test(make_test_dict, TIMEOUT)
    assert run.calls == []


def install_profiler(bt, init_name, attr, profiler):
    def init(test):
        setattr(bt, attr, profiler)

    setattr(bt, init_name, mock.Mock(side_effect=init))


@pytest.mark.parametrize("fail", [False, True])
def test_run_test_etw_profiler(
    make_bt, make_test_dict, fake_run, monkeypatch, tmp_path, fail
):
    """Check that the ETW profiler is started and finalized around the run."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    bt = make_bt(etw_profile=True, run_local=False)
    bt.get_failure_screenshot = mock.Mock()
    etw = mock.Mock()
    etw.start.return_value = True
    install_profiler(bt, "_init_etw_profiling", "etw_profiler", etw)
    if fail:
        fake_run(lines=[line("ERROR", "page failed")], returncodes=[1])
        with pytest.raises(Exception, match="page failed"):
            bt.run_test(make_test_dict, TIMEOUT)
    else:
        fake_run()
        bt.run_test(make_test_dict, TIMEOUT)
    bt._init_etw_profiling.assert_called_once_with(make_test_dict)
    etw.stop.assert_called_once_with()
    etw.upload_etl.assert_called_once_with(debug=fail)
    etw.symbolicate.assert_called_once_with()
    etw.post_process_profiles.assert_called_once_with()
    etw.clean.assert_called_once_with()


def test_run_test_etw_profiler_failures(
    make_bt, make_test_dict, fake_run, monkeypatch, tmp_path
):
    """Check that an ETW start failure disables the profiler without failing the run."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    bt = make_bt(etw_profile=True, run_local=False)
    etw = mock.Mock()
    etw.start.side_effect = OSError("no xperf")
    install_profiler(bt, "_init_etw_profiling", "etw_profiler", etw)
    fake_run()
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    assert bt.etw_profiler is None
    etw.stop.assert_not_called()
    log.warning.assert_called_with("Failed to start ETW profiling: no xperf")


def test_run_test_etw_finalize_error(
    make_bt, make_test_dict, fake_run, monkeypatch, tmp_path
):
    """Check that an ETW finalize failure is logged without failing the run."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    bt = make_bt(etw_profile=True, run_local=False)
    etw = mock.Mock()
    etw.stop.side_effect = RuntimeError("stuck")
    install_profiler(bt, "_init_etw_profiling", "etw_profiler", etw)
    fake_run()
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    log.error.assert_called_with("Failed to finalize ETW profiling: stuck")


def test_run_test_samply_profiler_local(make_bt, make_test_dict, fake_run, tmp_path):
    """Check that a local samply run archives the profile and exposes its path."""
    archive = tmp_path / "profile.json.gz"
    archive.touch()
    bt = make_bt(samply_profile=True)
    samply = mock.Mock()
    samply.start.return_value = True
    samply.post_process_profiles.return_value = ["p"]
    samply.archive.return_value = archive
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    fake_run()
    with mock.patch.object(bt_base, "view_gecko_profile_from_raptor") as view:
        bt.run_test(make_test_dict, TIMEOUT)
    samply.stop.assert_called_once_with()
    samply.archive.assert_called_once_with(["p"])
    samply.clean.assert_called_once_with()
    assert os.environ["RAPTOR_LATEST_PROFILE"] == str(archive)
    view.assert_not_called()


@pytest.mark.parametrize("disable_launch, expect_view", [(None, True), ("1", False)])
def test_run_test_samply_views_failure_profile(
    make_bt,
    make_test_dict,
    fake_run,
    tmp_path,
    monkeypatch,
    disable_launch,
    expect_view,
):
    """Check that a failed local run opens the samply profile unless disabled."""
    if disable_launch:
        monkeypatch.setenv("DISABLE_PROFILE_LAUNCH", disable_launch)
    archive = tmp_path / "profile.json.gz"
    archive.touch()
    bt = make_bt(samply_profile=True)
    bt.get_failure_screenshot = mock.Mock()
    samply = mock.Mock()
    samply.start.return_value = True
    samply.archive.return_value = archive
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    fake_run(lines=[line("ERROR", "page failed")], returncodes=[1])
    with mock.patch.object(bt_base, "view_gecko_profile_from_raptor") as view:
        with pytest.raises(Exception, match="page failed"):
            bt.run_test(make_test_dict, TIMEOUT)
    assert view.called is expect_view
    assert os.environ["RAPTOR_LATEST_PROFILE"] == str(archive)


def test_run_test_view_failure_profile_error(
    make_bt, make_test_dict, fake_run, tmp_path
):
    """Check that an error opening the failure profile is logged."""
    archive = tmp_path / "profile.json.gz"
    archive.touch()
    bt = make_bt(samply_profile=True)
    bt.get_failure_screenshot = mock.Mock()
    samply = mock.Mock()
    samply.archive.return_value = archive
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    fake_run(lines=[line("ERROR", "page failed")], returncodes=[1])
    with mock.patch.object(
        bt_base, "view_gecko_profile_from_raptor", side_effect=OSError("no ui")
    ):
        with mock.patch.object(bt_base, "LOG") as log:
            with pytest.raises(Exception, match="page failed"):
                bt.run_test(make_test_dict, TIMEOUT)
    log.error.assert_called_with(f"Failed to view {archive}: no ui")


def test_run_test_samply_start_and_stop_errors(make_bt, make_test_dict, fake_run):
    """Check that samply start and stop errors are logged without failing the run."""
    bt = make_bt(samply_profile=True)
    samply = mock.Mock()
    samply.start.side_effect = RuntimeError("denied")
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    fake_run()
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    assert bt.samply_profiler is None
    log.warning.assert_called_with("Failed to start Samply profiling: denied")

    bt = make_bt(samply_profile=True)
    samply = mock.Mock()
    samply.stop.side_effect = RuntimeError("crash")
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    log.error.assert_called_with("Failed to finalize Samply profiling: crash")


def test_run_test_samply_ci_does_not_archive(
    make_bt, make_test_dict, fake_run, monkeypatch, tmp_path
):
    """Check that samply profiles are not archived on CI."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    bt = make_bt(samply_profile=True, run_local=False)
    samply = mock.Mock()
    install_profiler(bt, "_init_samply_profiling", "samply_profiler", samply)
    fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    samply.archive.assert_not_called()
    samply.clean.assert_called_once_with()
    assert "RAPTOR_LATEST_PROFILE" not in os.environ


@pytest.mark.parametrize("stop_ok", [True, False])
def test_run_test_perf_profiler(make_bt, make_test_dict, fake_run, tmp_path, stop_ok):
    """Check that the perf profiler is finalized and always cleaned up."""
    archive = tmp_path / "perf.json.gz"
    archive.touch()
    bt = make_bt(perf_profile=True)
    perf = mock.Mock()
    perf.stop.return_value = stop_ok
    perf.post_process_profiles.return_value = ["p"]
    perf.archive.return_value = archive
    install_profiler(bt, "_init_perf_profiling", "perf_profiler", perf)
    fake_run()
    with mock.patch.object(bt_base, "LOG") as log:
        bt.run_test(make_test_dict, TIMEOUT)
    perf.clean.assert_called_once_with()
    if stop_ok:
        perf.symbolicate.assert_called_once_with()
        perf.archive.assert_called_once_with(["p"])
        assert os.environ["RAPTOR_LATEST_PROFILE"] == str(archive)
    else:
        perf.symbolicate.assert_not_called()
        log.error.assert_called_with(
            "Failed to stop perf profiling: perf recording did not complete"
        )
        assert "RAPTOR_LATEST_PROFILE" not in os.environ


def test_run_test_perf_profiler_start_error(make_bt, make_test_dict, fake_run):
    """Check that a perf start failure disables the profiler."""
    bt = make_bt(perf_profile=True)
    perf = mock.Mock()
    perf.start.side_effect = RuntimeError("no perf")
    install_profiler(bt, "_init_perf_profiling", "perf_profiler", perf)
    fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    assert bt.perf_profiler is None
    perf.clean.assert_not_called()


def test_run_test_simpleperf_init(make_bt, make_test_dict, fake_run):
    """Check that simpleperf is initialized and extends the process timeout."""
    bt = make_bt(simpleperf=True)
    bt._init_simpleperf_profiling = mock.Mock()
    run = fake_run()
    bt.run_test(make_test_dict, TIMEOUT)
    bt._init_simpleperf_profiling.assert_called_once_with(make_test_dict)
    assert run.calls[0]["timeout"] == 22 + 300


if __name__ == "__main__":
    mozunit.main()
