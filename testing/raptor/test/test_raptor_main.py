# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import signal
import sys
from argparse import Namespace
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import outputhandler
import signal_handler
from browsertime import android as bt_android
from browsertime import desktop as bt_desktop
from signal_handler import SignalHandler, SignalHandlerException


def load_raptor_entry_module_by_path():
    spec = importlib.util.spec_from_file_location(
        "raptor_main_under_test", os.path.join(raptor_dir, "raptor.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


raptor_main = load_raptor_entry_module_by_path()


TESTS = [{"name": "amazon"}, {"name": "google"}]


RUNNER_ARGS = (
    "run_local no_install installerpath obj_path gecko_profile gecko_profile_interval "
    "gecko_profile_entries gecko_profile_extra_threads gecko_profile_threads "
    "gecko_profile_features extra_profiler_run etw_profile samply_profile perf_profile "
    "symbols_path host live_sites cold is_release_build debug_mode post_startup_delay "
    "activity intent device_name disable_perf_tuning conditioned_profile "
    "install_extensions test_bytecode_cache chimera project verbose fission "
    "extra_summary_methods benchmark_repository benchmark_revision benchmark_branch "
    "page_timeout clean screenshot_on_failure power_test simpleperf perfherder_extra_options"
).split()


def make_args(**kwargs):
    defaults = dict.fromkeys(RUNNER_ARGS)
    defaults.update(
        app="firefox",
        binary="path/to/firefox",
        test="amazon",
        run_local=True,
        browsertime=True,
        extra_prefs=None,
        environment=None,
        enable_marionette_trace=False,
        debug_mode=False,
        gecko_profile=False,
        simpleperf=False,
        samply_profile=False,
        perf_profile=False,
        browsertime_node="node",
        browsertime_geckodriver="geckodriver",
        browsertime_existing_results=None,
    )
    unknown = set(kwargs) - set(defaults)
    assert not unknown, f"unknown args: {unknown}"
    defaults.update(kwargs)
    return Namespace(**defaults)


@pytest.fixture
def run_main(monkeypatch):
    def inner(
        args,
        tests=TESTS,
        success=True,
        timeouts=None,
        klass_side_effect=None,
        result_dir="does-not-exist",
        xtreme_regressions=None,
    ):
        instance = mock.MagicMock()
        instance.results_handler.xtreme_regressions = xtreme_regressions or []
        instance.results_handler.result_dir.return_value = str(result_dir)
        instance.run_tests.return_value = success
        instance.get_page_timeout_list.return_value = timeouts or []
        desktop = mock.MagicMock(return_value=instance, side_effect=klass_side_effect)
        android = mock.MagicMock(return_value=instance, side_effect=klass_side_effect)
        log = mock.MagicMock()
        view = mock.MagicMock()
        get_tests = mock.MagicMock(return_value=tests)
        monkeypatch.setattr(raptor_main, "parse_args", lambda: args)
        monkeypatch.setattr(raptor_main.commandline, "setup_logging", mock.Mock())
        monkeypatch.setattr(raptor_main, "get_raptor_test_list", get_tests)
        monkeypatch.setattr(raptor_main, "SignalHandler", mock.Mock())
        monkeypatch.setattr(raptor_main, "LOG", log)
        monkeypatch.setattr(raptor_main, "view_gecko_profile_from_raptor", view)
        monkeypatch.setattr(raptor_main, "traceback", mock.Mock())
        monkeypatch.setattr(bt_desktop, "BrowsertimeDesktop", desktop)
        monkeypatch.setattr(bt_android, "BrowsertimeAndroid", android)
        result = Namespace(
            instance=instance,
            desktop=desktop,
            android=android,
            log=log,
            view=view,
            get_tests=get_tests,
            exit_code=None,
        )
        try:
            raptor_main.main()
        except SystemExit as e:
            result.exit_code = e.code
        return result

    return inner


def critical_messages(log):
    return [c.args[0] for c in log.critical.call_args_list]


@pytest.mark.parametrize("app", ["firefox", "chrome", "custom-car"])
def test_main_desktop_app_uses_browsertime_desktop(run_main, app):
    """Check that desktop apps get BrowsertimeDesktop with the browsertime args and test names."""
    args = make_args(app=app)
    r = run_main(args)

    assert r.exit_code is None
    r.android.assert_not_called()
    r.desktop.assert_called_once()
    pos, kw = r.desktop.call_args
    assert pos == (app, "path/to/firefox")
    assert kw["test_names"] == ["amazon", "google"]
    assert kw["browsertime_node"] == "node"
    assert kw["browsertime_geckodriver"] == "geckodriver"
    assert kw["interrupt_handler"] is raptor_main.SignalHandler.return_value
    assert kw["extra_prefs"] == {}
    assert kw["environment"] == {}
    r.instance.run_tests.assert_called_once_with(TESTS)
    r.instance.results_handler.use_existing_results.assert_called_once_with(None)
    r.view.assert_not_called()


@pytest.mark.parametrize("app", ["fenix", "geckoview", "chrome-m"])
def test_main_mobile_app_uses_browsertime_android(run_main, app):
    """Check that mobile apps get BrowsertimeAndroid instead of the desktop runner."""
    r = run_main(make_args(app=app))

    assert r.exit_code is None
    r.desktop.assert_not_called()
    r.android.assert_called_once()
    assert r.android.call_args[0][0] == app


def test_main_passes_args_to_test_list(run_main):
    """Check that the test list is built from the parsed args and the current OS."""
    args = make_args()
    r = run_main(args)
    r.get_tests.assert_called_once_with(args, raptor_main.mozinfo.os)


def test_main_parses_prefs_and_environment(run_main):
    """Check that prefs, the marionette trace pref and env vars are parsed for the runner."""
    args = make_args(
        extra_prefs=["a.b=1", "c.d=true"],
        environment=["FOO=bar", "X=1=2"],
        enable_marionette_trace=True,
    )
    r = run_main(args)

    kw = r.desktop.call_args[1]
    assert kw["extra_prefs"] == {"a.b": 1, "c.d": True, "remote.log.level": "Trace"}
    assert kw["environment"] == {"FOO": "bar", "X": "1=2"}


def test_main_debug_mode_logged(run_main):
    """Check that debug mode is logged and passed on to the runner."""
    r = run_main(make_args(debug_mode=True))
    r.log.info.assert_any_call("debug-mode enabled")
    assert r.desktop.call_args[1]["debug_mode"] is True


def test_main_exits_when_no_tests(run_main):
    """Check that main exits with code 1 when the manifest has no matching test."""
    r = run_main(make_args(test="nope"), tests=[])

    assert r.exit_code == 1
    r.desktop.assert_not_called()
    assert "test 'nope' could not be found for firefox" in critical_messages(r.log)[0]


def test_main_exits_when_runner_init_fails(run_main):
    """Check that main exits with code 1 when the runner cannot be created."""
    r = run_main(make_args(), klass_side_effect=RuntimeError("boom"))

    assert r.exit_code == 1
    raptor_main.traceback.print_exc.assert_called_once()
    assert critical_messages(r.log) == [
        "TEST-UNEXPECTED-FAIL: could not initialize the raptor test runner"
    ]


def test_main_exits_when_no_results(run_main):
    """Check that main exits with code 1 and names the tests when no results came back."""
    r = run_main(make_args(), success=False)

    assert r.exit_code == 1
    assert critical_messages(r.log) == [
        "TEST-UNEXPECTED-FAIL: no raptor test results were found for amazon, google"
    ]


def test_main_exits_on_page_timeouts(run_main):
    """Check that main exits with code 1 and reports each timed-out page."""
    timeouts = [
        {"test_name": "amazon", "page_cycle": 2, "pending_metrics": ["fcp"]},
        {"test_name": "google", "page_cycle": 1},
    ]
    r = run_main(make_args(), success=False, timeouts=timeouts)

    assert r.exit_code == 1
    msgs = critical_messages(r.log)
    assert msgs == [
        "TEST-UNEXPECTED-FAIL: test 'amazon' timed out loading test page: "
        "waiting for pending metrics",
        "TEST-UNEXPECTED-FAIL: test 'google' timed out loading test page: "
        "waiting for pending metrics",
    ]
    r.log.warning.assert_called_once_with("page cycle 2 has pending metrics: ['fcp']")


def test_main_exits_on_xtreme_regressions(run_main):
    """Check that main exits with code 1 when extreme regressions were detected."""
    regressions = [{"suite": "speedometer3", "test": None, "exceeds_variance": True}]
    r = run_main(make_args(), xtreme_regressions=regressions)

    assert r.exit_code == 1
    (error,) = r.log.critical.call_args.args
    assert isinstance(error, raptor_main.XtremeRegressionError)
    r.view.assert_not_called()


@pytest.mark.parametrize("gecko_profile", [False, True])
def test_main_archives_results_in_ci(run_main, tmp_path, gecko_profile):
    """Check that CI runs archive an existing result dir with the profiling flag."""
    r = run_main(
        make_args(run_local=False, gecko_profile=gecko_profile), result_dir=tmp_path
    )

    assert r.exit_code is None
    archive = r.instance.results_handler.archive_raptor_artifacts
    archive.assert_called_once_with(gecko_profile)


def test_main_skips_archive_when_result_dir_missing(run_main, tmp_path):
    """Check that nothing is archived in CI when the result dir is missing."""
    r = run_main(make_args(run_local=False), result_dir=tmp_path / "missing")
    r.instance.results_handler.archive_raptor_artifacts.assert_not_called()


@pytest.mark.parametrize(
    "run_local,browsertime", [(True, True), (False, False), (True, False)]
)
def test_main_skips_archive_locally_or_without_browsertime(
    run_main, tmp_path, run_local, browsertime
):
    """Check that results are not archived on local runs or without browsertime."""
    r = run_main(
        make_args(run_local=run_local, browsertime=browsertime), result_dir=tmp_path
    )
    r.instance.results_handler.result_dir.assert_not_called()
    r.instance.results_handler.archive_raptor_artifacts.assert_not_called()


def test_main_passes_existing_results(run_main):
    """Check that existing browsertime results are handed to the results handler."""
    r = run_main(make_args(browsertime_existing_results="/some/results"))
    r.instance.results_handler.use_existing_results.assert_called_once_with(
        "/some/results"
    )


@pytest.mark.parametrize(
    "flag", ["gecko_profile", "simpleperf", "samply_profile", "perf_profile"]
)
def test_main_launches_profile_viewer_locally(run_main, monkeypatch, flag):
    """Check that any profiler flag opens the profile viewer on local runs."""
    monkeypatch.delenv("DISABLE_PROFILE_LAUNCH", raising=False)
    r = run_main(make_args(**{flag: True}))
    r.view.assert_called_once_with()


def test_main_profile_viewer_disabled_by_env(run_main, monkeypatch):
    """Check that DISABLE_PROFILE_LAUNCH=1 stops the profile viewer from opening."""
    monkeypatch.setenv("DISABLE_PROFILE_LAUNCH", "1")
    r = run_main(make_args(gecko_profile=True))
    r.view.assert_not_called()
    r.log.info.assert_any_call(
        "Not launching profiler.firefox.com because DISABLE_PROFILE_LAUNCH=1"
    )


def test_main_profile_viewer_not_launched_in_ci(run_main, monkeypatch):
    """Check that the profile viewer is not opened on CI runs."""
    monkeypatch.delenv("DISABLE_PROFILE_LAUNCH", raising=False)
    r = run_main(make_args(gecko_profile=True, run_local=False))
    r.view.assert_not_called()


@pytest.fixture
def handler(monkeypatch):
    log = mock.MagicMock()
    monkeypatch.setattr(outputhandler, "LOG", log)
    h = outputhandler.OutputHandler()
    h.proc = mock.Mock(pid=1234)
    return h, log


@pytest.mark.parametrize("line", [b"", b"   ", b"\n"])
def test_output_handler_ignores_blank_lines(handler, line):
    """Check that blank lines produce no log output."""
    h, log = handler
    h(line)
    assert not log.mock_calls


def test_output_handler_raw_log_for_structured_json(handler):
    """Check that JSON with an action key is passed to log_raw."""
    h, log = handler
    h(b'{"action": "log", "message": "hi"}')
    log.log_raw.assert_called_once_with({"action": "log", "message": "hi"})
    log.process_output.assert_not_called()


def test_output_handler_plain_text_with_raptor(handler):
    """Check that plain lines mentioning raptor are logged with the process pid."""
    h, log = handler
    h(b"raptor: started")
    log.process_output.assert_called_once_with(1234, "raptor: started")


def test_output_handler_plain_text_filtered_when_not_verbose(handler):
    """Check that unrelated plain lines are dropped when not verbose."""
    h, log = handler
    h(b"some browser noise")
    log.process_output.assert_not_called()


def test_output_handler_verbose_logs_everything(handler):
    """Check that verbose mode logs unrelated plain lines too."""
    h, log = handler
    h.verbose = True
    h(b"some browser noise")
    log.process_output.assert_called_once_with(1234, "some browser noise")


def test_output_handler_invalid_utf8_replaced(handler):
    """Check that invalid UTF-8 bytes are replaced rather than raising."""
    h, log = handler
    h(b"raptor \xff")
    log.process_output.assert_called_once_with(1234, "raptor \ufffd")


@pytest.mark.parametrize(
    "line,expected",
    [
        (b'{"raptor": 1}', '{"raptor": 1}'),
        (b'["raptor", 2]', '["raptor", 2]'),
        (b'"raptor"', '"raptor"'),
    ],
)
def test_output_handler_non_structured_json_reserialized(handler, line, expected):
    """Check that JSON without an action key is re-serialised and logged as text."""
    h, log = handler
    h(line)
    log.log_raw.assert_not_called()
    log.process_output.assert_called_once_with(1234, expected)


def test_output_handler_defaults():
    """Check that a new handler has no process and is not verbose."""
    h = outputhandler.OutputHandler()
    assert h.proc is None
    assert h.verbose is False


def test_signal_handler_installs_for_sigint_and_sigterm(monkeypatch):
    """Check that the handler is installed for SIGINT and SIGTERM."""
    fake_signal = mock.Mock()
    monkeypatch.setattr(signal_handler.signal, "signal", fake_signal)
    h = SignalHandler()
    assert fake_signal.call_args_list == [
        mock.call(signal.SIGINT, h.handle_signal),
        mock.call(signal.SIGTERM, h.handle_signal),
    ]


def test_signal_handler_raises(monkeypatch):
    """Check that a received signal raises SignalHandlerException naming the signal."""
    monkeypatch.setattr(signal_handler.signal, "signal", mock.Mock())
    h = SignalHandler()
    with pytest.raises(SignalHandlerException, match="aborted due to signal 15"):
        h.handle_signal(15, None)
    assert issubclass(SignalHandlerException, Exception)


if __name__ == "__main__":
    mozunit.main()
