# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import types
from unittest import mock

import marionette_harness
import mozunit
import pytest
from awsy import ITERATIONS, MAX_TABS, PER_TAB_PAUSE, SETTLE_WAIT_TIME
from awsy import awsy_test_case as atc
from marionette_driver.errors import JavascriptException, ScriptTimeoutException


class FakeMarionette:
    def __init__(self, handles=("tab0",)):
        self.handles = list(handles)
        self.current = None
        self.closed = []
        self.navigated = []
        self.context = "content"
        self.execute_script = mock.Mock(side_effect=self._execute_script)
        self.execute_async_script = mock.Mock(return_value="memory report done!")
        self.actions = mock.MagicMock()
        self.new_handle_on_navigate = None
        self._next_tab = len(self.handles)

    @property
    def window_handles(self):
        return list(self.handles)

    @window_handles.setter
    def window_handles(self, handles):
        self.handles = list(handles)

    def _execute_script(self, script, script_timeout=None):
        if "addTab" in script:
            self.handles.insert(0, f"tab{self._next_tab}")
            self._next_tab += 1

    def set_context(self, context):
        self.context = context

    def switch_to_window(self, handle):
        self.current = handle

    def close(self):
        self.closed.append(self.current)
        self.handles.remove(self.current)

    def using_context(self, context):
        fake = self

        class Ctx:
            def __enter__(self):
                fake.context_stack = context

            def __exit__(self, *args):
                fake.context_stack = None

        return Ctx()

    def navigate(self, url):
        self.navigated.append(url)
        if self.new_handle_on_navigate:
            old, new = self.new_handle_on_navigate
            self.handles[self.handles.index(old)] = new


class FakeWait:
    def __init__(self, marionette):
        self.marionette = marionette

    def until(self, condition, message=""):
        assert condition(self.marionette), message


class Case(atc.AwsyTestCase):
    __test__ = False

    def runTest(self):
        pass

    def urls(self):
        return ["http://a/", "http://b/", "http://c/"]

    def perf_suites(self):
        return [{"name": "Resident Memory", "node": "resident"}]

    def perf_checkpoints(self):
        return [{"name": "Start", "path": "memory-report-Start-0.json.gz"}]


@pytest.fixture
def marionette():
    return FakeMarionette()


@pytest.fixture
def logger():
    return mock.Mock()


@pytest.fixture(autouse=True)
def patches(monkeypatch, logger):
    monkeypatch.setattr(marionette_harness.MarionetteTestCase, "setUp", mock.Mock())
    monkeypatch.setattr(marionette_harness.MarionetteTestCase, "tearDown", mock.Mock())
    monkeypatch.setattr(
        atc.mozlog.structured.structuredlog, "get_default_logger", lambda: logger
    )
    monkeypatch.setattr(atc, "Wait", FakeWait)
    monkeypatch.setattr(atc.time, "sleep", mock.Mock())
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)


@pytest.fixture
def results_dir(tmp_path):
    path = tmp_path / "results"
    path.mkdir()
    return path


@pytest.fixture
def tmp_dir(tmp_path, monkeypatch):
    path = tmp_path / "tmp"
    path.mkdir()
    monkeypatch.setattr(atc.tempfile, "gettempdir", lambda: str(path))
    return path


def make_case(marionette, results_dir, cls=Case, **testvars):
    vars_ = {"resultsDir": str(results_dir), "bin": "/path/firefox"}
    vars_.update(testvars)
    case = cls(lambda: marionette, fixtures=None, testvars=vars_)
    case.marionette = marionette
    return case


@pytest.fixture
def case(marionette, results_dir):
    case = make_case(marionette, results_dir)
    case.setUp()
    return case


def test_unimplemented_subclass_methods_raise_and_extra_opts_is_none(
    marionette, results_dir
):
    class Bare(atc.AwsyTestCase):
        __test__ = False

        def runTest(self):
            pass

    bare = make_case(marionette, results_dir, cls=Bare)
    for method in (bare.urls, bare.perf_suites, bare.perf_checkpoints):
        with pytest.raises(NotImplementedError):
            method()
    assert bare.perf_extra_opts() is None


def test_setup_without_testvars_uses_default_settings_and_chrome_context(
    case, marionette, results_dir, logger
):
    marionette_harness.MarionetteTestCase.setUp.assert_called_once_with(case)
    assert marionette.context == "chrome"
    assert case.logger is logger
    assert case._resultsDir == str(results_dir)
    assert case._binary == "/path/firefox"
    assert case._run_local is False
    assert case.iterations() == ITERATIONS
    assert case._perTabPause == PER_TAB_PAUSE
    assert case._settleWaitTime == SETTLE_WAIT_TIME
    assert case._maxTabs == MAX_TABS
    assert case._dmd is False
    assert case.pages_to_load() == 3
    assert case._pages_loaded == 0
    assert case._tabs == ["tab0"]


def test_setup_applies_testvars_cleans_stale_results_and_closes_tabs(
    marionette, results_dir
):
    stale = [
        "memory-report-Start-0.json.gz",
        "perfherder-data.json",
        "dmd-Start-0-1.json.gz",
    ]
    for name in stale + ["keep.txt"]:
        (results_dir / name).write_text("x")
    marionette.window_handles = ["tab0", "tab1", "tab2"]

    case = make_case(
        marionette,
        results_dir,
        run_local=True,
        entities=7,
        iterations=2,
        perTabPause=3,
        settleWaitTime=4,
        maxTabs=5,
        dmd=True,
    )
    case.setUp()

    assert sorted(os.listdir(results_dir)) == ["keep.txt"]
    assert case._run_local is True
    assert case.pages_to_load() == 7
    assert case.iterations() == 2
    assert (case._perTabPause, case._settleWaitTime, case._maxTabs) == (3, 4, 5)
    assert case._dmd is True
    assert marionette.closed == ["tab1", "tab2"]
    assert case._tabs == ["tab0"]
    assert marionette.current == "tab0"


def test_settle_sleeps_for_settle_wait_time(case):
    case._settleWaitTime = 12
    case.settle()
    atc.time.sleep.assert_called_once_with(12)


def test_teardown_writes_and_logs_perfherder_data(case, results_dir, monkeypatch):
    blob = {"framework": {"name": "awsy"}, "suites": []}
    create = mock.Mock(return_value=blob)
    monkeypatch.setattr(atc.process_perf_data, "create_perf_data", create)
    case.perf_extra_opts = lambda: ["opt"]

    case.tearDown()

    marionette_harness.MarionetteTestCase.tearDown.assert_called_once_with(case)
    create.assert_called_once_with(
        str(results_dir), case.perf_suites(), case.perf_checkpoints(), ["opt"]
    )
    with open(results_dir / "perfherder-data.json") as f:
        assert json.load(f) == blob
    case.logger.info.assert_any_call(f"PERFHERDER_DATA: {json.dumps(blob)}")


def test_teardown_uploads_files_to_upload_dir_and_cleans_dmd(
    case, results_dir, tmp_path, monkeypatch
):
    upload = tmp_path / "upload"
    upload.mkdir()
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(upload))
    (results_dir / "memory-report-Start-0.json.gz").write_text("x")
    (results_dir / "subdir").mkdir()
    monkeypatch.setattr(
        atc.process_perf_data, "create_perf_data", mock.Mock(return_value={})
    )
    case._dmd = True
    case.cleanup_dmd = mock.Mock()

    case.tearDown()

    case.cleanup_dmd.assert_called_once_with()
    assert sorted(os.listdir(upload)) == [
        "memory-report-Start-0.json.gz",
        "perfherder-data.json",
    ]


def test_teardown_uploads_results_even_when_perf_data_creation_fails(
    case, results_dir, tmp_path, monkeypatch
):
    upload = tmp_path / "upload"
    upload.mkdir()
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(upload))
    (results_dir / "memory-report-Start-0.json.gz").write_text("x")
    monkeypatch.setattr(
        atc.process_perf_data,
        "create_perf_data",
        mock.Mock(side_effect=RuntimeError("boom")),
    )

    with pytest.raises(RuntimeError, match="boom"):
        case.tearDown()

    assert os.listdir(upload) == ["memory-report-Start-0.json.gz"]
    assert not (results_dir / "perfherder-data.json").exists()


@pytest.fixture
def fake_dmd(monkeypatch):
    module = types.ModuleType("dmd")
    module.fixStackTraces = mock.Mock()
    monkeypatch.setitem(sys.modules, "dmd", module)
    return module


@pytest.mark.parametrize("platform,fixes", [("linux", True), ("win32", False)])
def test_cleanup_dmd_moves_dmd_files_and_fixes_stacks_except_on_windows(
    case, results_dir, tmp_dir, fake_dmd, monkeypatch, platform, fixes
):
    monkeypatch.setattr(sys, "platform", platform)
    for name in (
        "dmd-Start-0-1.json.gz",
        "dmd-Start-0-2.json.gz",
        "unified-memory-report-Start-0.json.gz",
        "other.txt",
    ):
        (tmp_dir / name).write_text("x")

    case.cleanup_dmd()

    assert sorted(os.listdir(results_dir)) == [
        "dmd-Start-0-1.json.gz",
        "dmd-Start-0-2.json.gz",
    ]
    assert "other.txt" in os.listdir(tmp_dir)
    if fixes:
        assert fake_dmd.fixStackTraces.call_count == 2
        path, zipped, opener = fake_dmd.fixStackTraces.call_args[0]
        assert os.path.dirname(path) == str(tmp_dir)
        assert zipped is True
        assert opener is atc.gzip.open
    else:
        fake_dmd.fixStackTraces.assert_not_called()


def test_do_memory_report_returns_checkpoint_path_and_skips_dmd_by_default(
    case, marionette, results_dir, monkeypatch
):
    monkeypatch.setattr(sys, "platform", "linux")
    case.do_dmd = mock.Mock()

    checkpoint = case.do_memory_report("TabsOpen", 3)

    expected = os.path.join(str(results_dir), "memory-report-TabsOpen-3.json.gz")
    assert checkpoint == expected
    script = marionette.execute_async_script.call_args[0][0]
    assert f'"{expected}"' in script
    assert "/* minimize memory usage */ false" in script
    assert marionette.execute_async_script.call_args[1] == {"script_timeout": 60000}
    case.do_dmd.assert_not_called()


def test_do_memory_report_minimize_flag_and_dmd_enabled_calls_do_dmd(case, marionette):
    case._dmd = True
    case.do_dmd = mock.Mock()

    case.do_memory_report("TabsOpenForceGC", 1, minimize=True)

    script = marionette.execute_async_script.call_args[0][0]
    assert "/* minimize memory usage */ true" in script
    case.do_dmd.assert_called_once_with("TabsOpenForceGC", 1)


def test_do_memory_report_escapes_backslashes_in_windows_path(
    case, marionette, monkeypatch
):
    monkeypatch.setattr(sys, "platform", "win32")
    case._resultsDir = "C:\\results/dir"

    checkpoint = case.do_memory_report("Start", 0)

    assert checkpoint.startswith("C:\\\\results\\\\dir")
    assert "/" not in checkpoint
    assert checkpoint in marionette.execute_async_script.call_args[0][0]


def test_do_memory_report_returns_none_when_report_does_not_finish(
    case, marionette, monkeypatch
):
    monkeypatch.setattr(sys, "platform", "linux")
    marionette.execute_async_script.return_value = None
    assert case.do_memory_report("Start", 0) is None
    case.logger.info.assert_any_call(
        "checkpoint created, stored in "
        + os.path.join(case._resultsDir, "memory-report-Start-0.json.gz")
    )


@pytest.mark.parametrize(
    "error,message",
    [
        (JavascriptException("bad js"), "Checkpoint JavaScript error: bad js"),
        (ScriptTimeoutException("slow"), "Memory report timed out"),
        (ValueError("other"), "Unexpected error: <class 'ValueError'>"),
    ],
)
def test_do_memory_report_logs_error_and_returns_none_on_failure(
    case, marionette, error, message
):
    marionette.execute_async_script.side_effect = error
    assert case.do_memory_report("Start", 0) is None
    case.logger.error.assert_called_once_with(message)


def test_do_dmd_dumps_memory_info_without_sleeping_when_nothing_pending(
    case, marionette, tmp_dir
):
    case.do_dmd("TabsOpen", 2)

    script = marionette.execute_script.call_args[0][0]
    assert '"TabsOpen-2"' in script
    assert "dumpMemoryInfoToTempDir" in script
    atc.time.sleep.assert_not_called()
    case.logger.error.assert_not_called()
    case.logger.info.assert_any_call("DMD started, prefixed with TabsOpen-2")


def test_do_dmd_sleeps_until_incomplete_report_disappears(case, marionette, tmp_dir):
    incomplete = tmp_dir / "incomplete-unified-memory-report-TabsOpen-2-99.json.gz"
    incomplete.write_text("x")
    atc.time.sleep.side_effect = lambda _: incomplete.unlink()

    case.do_dmd("TabsOpen", 2)

    atc.time.sleep.assert_called_once_with(1)
    case.logger.error.assert_not_called()


def test_do_dmd_gives_up_after_240_sleeps_and_removes_incomplete_reports(
    case, marionette, tmp_dir
):
    incomplete = tmp_dir / "incomplete-unified-memory-report-TabsOpen-2-99.json.gz"
    incomplete.write_text("x")
    (tmp_dir / "incomplete-unified-memory-report-Other-2-99.json.gz").write_text("x")

    case.do_dmd("TabsOpen", 2)

    assert atc.time.sleep.call_count == 240
    case.logger.error.assert_called_once_with("Incomplete memory reports leftover.")
    assert os.listdir(tmp_dir) == [
        "incomplete-unified-memory-report-Other-2-99.json.gz"
    ]


@pytest.mark.parametrize(
    "error,message",
    [
        (JavascriptException("bad js"), "DMD JavaScript error: bad js"),
        (ScriptTimeoutException("slow"), "DMD timed out"),
        (ValueError("other"), "Unexpected error: <class 'ValueError'>"),
    ],
)
def test_do_dmd_logs_error_on_failure(case, marionette, tmp_dir, error, message):
    marionette.execute_script.side_effect = error
    case.do_dmd("Start", 0)
    case.logger.error.assert_called_once_with(message)


def test_open_and_focus_reuses_first_tab_then_opens_new_tab_and_pauses(
    case, marionette
):
    case._perTabPause = 2
    marionette.current = None

    case.open_and_focus()
    assert marionette.navigated == ["http://a/"]
    assert case._tabs == ["tab0"]
    assert marionette.current is None

    case.open_and_focus()
    assert marionette.navigated == ["http://a/", "http://b/"]
    assert case._tabs == ["tab0", "tab1"]
    assert marionette.current == "tab1"
    assert case._pages_loaded == 2
    atc.time.sleep.assert_called_with(2)


def test_open_and_focus_cycles_urls_and_reuses_tabs_at_max_tabs(case, marionette):
    case._maxTabs = 2
    for _ in range(5):
        case.open_and_focus()

    assert marionette.navigated == [
        "http://a/",
        "http://b/",
        "http://c/",
        "http://a/",
        "http://b/",
    ]
    assert case._tabs == ["tab0", "tab1"]
    assert len(marionette.window_handles) == 2
    assert marionette.current == "tab0"


def test_open_and_focus_tracks_new_window_handle_after_navigate(case, marionette):
    marionette.new_handle_on_navigate = ("tab0", "tab0-new")
    case.open_and_focus()
    assert case._tabs == ["tab0-new"]


def test_signal_user_active_presses_and_releases_shift_key(case, marionette):
    case.signal_user_active()

    marionette.actions.sequence.assert_called_once_with("key", "keyboard_id")
    action = marionette.actions.sequence.return_value
    action.key_down.assert_called_once_with(atc.Keys.SHIFT)
    action.key_up.assert_called_once_with(atc.Keys.SHIFT)
    action.perform.assert_called_once_with()
    marionette.actions.release.assert_called_once_with()


def test_signal_user_active_releases_actions_when_perform_raises(case, marionette):
    marionette.actions.sequence.return_value.perform.side_effect = RuntimeError()
    with pytest.raises(RuntimeError):
        case.signal_user_active()
    marionette.actions.release.assert_called_once_with()


def test_open_pages_opens_and_signals_activity_once_per_page(case):
    case._pages_to_load = 2
    case.open_and_focus = mock.Mock()
    case.signal_user_active = mock.Mock()

    case.open_pages()

    assert case.open_and_focus.call_count == 2
    assert case.signal_user_active.call_count == 2


if __name__ == "__main__":
    mozunit.main()
