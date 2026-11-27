# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import types
from unittest import mock

import mozunit
import pytest

sys.modules.setdefault(
    "xdg", types.SimpleNamespace(xdg_config_home=lambda: os.path.expanduser("~"))
)

from talos import cmanager_base  # noqa: E402

sys.modules.setdefault("cmanager_base", cmanager_base)

from talos import ttest  # noqa: E402
from talos.utils import TalosCrash, TalosRegression  # noqa: E402

BROWSER_OUTPUT = [
    "__start_report1|2__end_report",
    "__startTimestamp1__endTimestamp",
    "__startBeforeLaunchTimestamp2__endBeforeLaunchTimestamp",
    "__startAfterTerminationTimestamp3__endAfterTerminationTimestamp",
]


@pytest.fixture
def env(tmp_path):
    profile_dir = tmp_path / "profile"
    profile_dir.mkdir()
    setup = mock.Mock(profile_dir=str(profile_dir), env={}, gecko_profile=None)
    pcontext = mock.Mock(output=list(BROWSER_OUTPUT), pid=99)
    with mock.patch.object(ttest, "LOG", new=mock.MagicMock()), mock.patch.object(
        ttest.time, "sleep"
    ), mock.patch.object(
        ttest, "__file__", str(tmp_path / "ttest.py")
    ), mock.patch.object(
        ttest, "run_browser", return_value=pcontext
    ) as run_browser, mock.patch.object(
        ttest.mozcrash, "check_for_crashes", return_value=False
    ) as check_for_crashes, mock.patch.object(
        ttest.subprocess, "call"
    ) as call, mock.patch.object(
        ttest.talosconfig, "generateTalosConfig"
    ) as generate, mock.patch.object(ttest.platform, "system", return_value="Linux"):
        yield mock.Mock(
            here=os.path.dirname(os.path.realpath(str(tmp_path / "ttest.py"))),
            tmp_path=tmp_path,
            setup=setup,
            pcontext=pcontext,
            run_browser=run_browser,
            check_for_crashes=check_for_crashes,
            call=call,
            generate=generate,
            browser_config={
                "browser_path": os.path.join(str(tmp_path), "firefox"),
                "error_filename": str(tmp_path / "errors.log"),
                "symbols_path": "symbols",
                "extra_args": [],
                "debug": False,
                "debugger": None,
                "debugger_args": None,
                "process": "firefox",
            },
            test_config={
                "name": "ts_x",
                "resolution": 1,
                "mainthread": False,
                "url": "http://localhost/${firefox}",
                "cycles": 2,
                "setup": None,
                "cleanup": None,
                "format_pagename": True,
            },
        )


def run(env):
    return ttest.TTest()._runTest(env.browser_config, env.test_config, env.setup)


def test_run_test_runs_each_cycle_and_returns_results_with_metadata(env):
    env.browser_config.update(
        framework={"name": "talos"}, browser_name="Firefox", browser_version="1.0"
    )
    env.test_config["perfherder_framework"] = "custom"
    result = run(env)

    assert result.name() == "ts_x"
    assert len(result.results) == 2
    assert result.results[0].results[0]["runs"] == [1.0, 2.0]
    assert result.framework == "custom"
    assert (result.browser_name, result.browser_version) == ("Firefox", "1.0")
    assert result.all_counter_results == []
    assert (
        env.test_config["url"]
        == "http://localhost/" + env.browser_config["browser_path"]
    )
    assert env.setup.env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "1"
    assert "MOZ_USE_PAGELOADER" not in env.setup.env
    assert env.run_browser.call_count == 2
    args, kwargs = env.run_browser.call_args
    assert args[0][-1] == env.test_config["url"]
    assert args[1] == os.path.join(env.setup.profile_dir, "minidumps")
    assert kwargs["timeout"] == 600
    assert kwargs["on_started"] is None
    assert env.check_for_crashes.call_count == 2
    env.call.assert_not_called()


def test_pageloader_env_and_test_timeout_are_set_for_mainthread_io(env):
    env.test_config.update(url=None, mainthread=True, timeout=30)
    run(env)
    assert env.setup.env["MOZ_USE_PAGELOADER"] == "1"
    assert env.setup.env["MOZ_MAIN_THREAD_IO_LOG"] == os.path.join(
        env.here, "mainthread_io.log"
    )
    assert env.run_browser.call_args.kwargs["timeout"] == 30


def test_mainthread_io_errors_raise_regression(env):
    env.test_config.update(mainthread=True, cycles=1)
    rawlog = os.path.join(env.here, "mainthread_io.log")
    with open(rawlog, "w") as f:
        f.write("log")
    mtio = mock.Mock()
    mtio.communicate.return_value = (b"bad access\n\n", None)
    with mock.patch.object(ttest.subprocess, "Popen", return_value=mtio) as popen:
        with pytest.raises(TalosRegression):
            run(env)
    command = popen.call_args.args[0]
    assert command[1:] == [
        os.path.join(env.here, "mainthreadio.py"),
        rawlog,
        os.path.join(env.here, "mainthread_io.json"),
        os.path.dirname(env.browser_config["browser_path"]),
    ]
    assert not os.path.exists(rawlog)
    env.check_for_crashes.assert_called_once()


def test_mainthread_io_run_with_clean_output_returns_results(env):
    env.test_config.update(mainthread=True, cycles=1)
    with open(os.path.join(env.here, "mainthread_io.log"), "w") as f:
        f.write("log")
    mtio = mock.Mock()
    mtio.communicate.return_value = (b"\n", None)
    with mock.patch.object(ttest.subprocess, "Popen", return_value=mtio):
        assert len(run(env).results) == 1


def test_error_file_raises_regression(env):
    env.test_config["cycles"] = 1
    env.run_browser.side_effect = lambda *a, **k: (
        open(env.browser_config["error_filename"], "w").close() or env.pcontext
    )
    with pytest.raises(TalosRegression):
        run(env)


def test_setup_cleanup_and_reinstall(env):
    source = env.tmp_path / "source"
    source.mkdir()
    (source / "prefs.js").write_text("pref")
    for name in ("sessionstore.js", ".parentlock"):
        (env.tmp_path / "profile" / name).write_text("x")
    env.test_config.update(
        cycles=1,
        setup="setup.py arg",
        cleanup="cleanup.py",
        reinstall=["prefs.js"],
        profile_path=str(source),
    )
    run(env)
    assert (env.tmp_path / "profile" / "prefs.js").read_text() == "pref"
    assert not (env.tmp_path / "profile" / "sessionstore.js").exists()
    assert not (env.tmp_path / "profile" / ".parentlock").exists()
    assert env.call.call_args_list == [
        mock.call(["python", "setup.py", "arg"]),
        mock.call([sys.executable, "cleanup.py"]),
    ]
    assert env.generate.call_args_list[1].kwargs == {"pid": 99}


def test_gecko_profile_sets_profiling_info_and_symbolicates_each_cycle(env):
    env.test_config["cycles"] = 2
    env.setup.gecko_profile = mock.Mock(profiling_info={"gecko_profile_interval": 1})
    run(env)
    assert env.setup.env["TPPROFILINGINFO"] == json.dumps({"gecko_profile_interval": 1})
    assert env.run_browser.call_args.kwargs["timeout"] == 900
    assert "gecko_profile_interval=1" in env.run_browser.call_args.args[0][-1]
    assert env.setup.gecko_profile.symbolicate.call_args_list == [
        mock.call(0),
        mock.call(1),
    ]


def test_counters_start_with_browser_and_are_stored_in_results(env):
    env.test_config.update(cycles=1, linux_counters=["rss"])
    with mock.patch.object(ttest, "CounterManagement") as cm_cls:
        cm = cm_cls.return_value
        cm.results.return_value = {"rss": [1]}
        result = run(env)
    cm_cls.assert_called_once_with("firefox", ["rss"], 1)
    assert env.run_browser.call_args.kwargs["on_started"] is cm.start
    cm.stop.assert_called_once_with()
    assert result.all_counter_results == [{"rss": [1]}]


def test_xperf_global_counters(env):
    env.browser_config["xperf_path"] = "xperf.exe"
    env.test_config.update(cycles=1, xperf_counters=["main_startup_fileio"])
    with mock.patch.object(ttest.results.BrowserLogResults, "xperf"):
        result = run(env)
    assert result.all_counter_results == [{"main_startup_fileio": []}]


def test_debug_mode_skips_results(env):
    env.browser_config["debug"] = True
    assert run(env).results == []


def test_code_coverage_collects_ccov_after_run(env):
    env.browser_config["code_coverage"] = True
    env.test_config["cycles"] = 1
    run(env)
    env.setup.collect_or_clean_ccov.assert_called_once_with()


def test_browser_failure_checks_crashes_and_stops_counters(env):
    env.test_config["linux_counters"] = ["rss"]
    env.run_browser.side_effect = RuntimeError("boom")
    with mock.patch.object(ttest, "CounterManagement") as cm_cls:
        with pytest.raises(RuntimeError, match="boom"):
            run(env)
    env.check_for_crashes.assert_called_once()
    cm_cls.return_value.stop.assert_called_once_with()


def test_check_for_crashes_removes_minidumps_and_raises_on_crash(env, tmp_path):
    minidumps = tmp_path / "minidumps"
    minidumps.mkdir()
    ttest.TTest().check_for_crashes({"symbols_path": "s"}, str(minidumps), "t")
    env.check_for_crashes.assert_called_once_with(str(minidumps), "s", test_name="t")
    assert not minidumps.exists()

    env.check_for_crashes.return_value = True
    with pytest.raises(TalosCrash):
        ttest.TTest().check_for_crashes({"symbols_path": "s"}, str(minidumps), "t")


@pytest.mark.parametrize(
    "system,prefix",
    [
        ("Linux", "linux"),
        ("Windows", "win"),
        ("Microsoft", "win"),
        ("Darwin", "mac"),
        ("Other", None),
    ],
)
def test_counter_prefix_depends_on_platform(system, prefix):
    with mock.patch.object(ttest.platform, "system", return_value=system):
        assert ttest.TTest._get_counter_prefix() == prefix


def test_run_test_uses_ffsetup(env):
    with mock.patch.object(ttest, "FFSetup") as ffsetup, mock.patch.object(
        ttest.TTest, "_runTest", return_value="result"
    ) as run_test:
        assert (
            ttest.TTest().runTest(env.browser_config, env.test_config, "util")
            == "result"
        )
    ffsetup.assert_called_once_with(env.browser_config, env.test_config)
    setup = ffsetup.return_value.__enter__.return_value
    run_test.assert_called_once_with(
        env.browser_config, env.test_config, setup, utility_path="util"
    )


if __name__ == "__main__":
    mozunit.main()
