# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import mozunit
import psutil
import pytest

from talos import talos_process
from talos.utils import TalosError


@pytest.fixture(autouse=True)
def log():
    with mock.patch.object(talos_process, "LOG", new=mock.MagicMock()) as log:
        yield log


def make_proc(running=True, children=()):
    proc = mock.MagicMock()
    proc.is_running.return_value = running
    proc.children.return_value = list(children)
    return proc


def test_context_pid_is_none_until_process_is_set():
    context = talos_process.ProcessContext()
    assert context.pid is None
    context.process = mock.Mock(pid=42)
    assert context.pid == 42


def test_kill_process_without_process():
    assert talos_process.ProcessContext().kill_process() is None


def test_kill_process_not_running():
    context = talos_process.ProcessContext()
    context.process = make_proc(running=False)
    assert context.kill_process() is None
    context.process.terminate.assert_not_called()


def test_kill_process_terminates_and_waits():
    context = talos_process.ProcessContext()
    context.process = make_proc()
    context.process.wait.return_value = 0
    assert context.kill_process() == 0
    context.process.terminate.assert_called_once_with()
    context.process.wait.assert_called_once_with(3)


def test_kill_process_kills_after_timeout():
    context = talos_process.ProcessContext()
    context.process = make_proc()
    context.process.wait.side_effect = [psutil.TimeoutExpired(3), -9]
    assert context.kill_process() == -9
    context.process.kill.assert_called_once_with()


def test_kill_process_no_such_process_kills_children():
    child = make_proc()
    context = talos_process.ProcessContext()
    context.process = make_proc(children=[child])
    context.process.terminate.side_effect = psutil.NoSuchProcess(1)
    context.kill_process()
    child.terminate.assert_called_once_with()


def test_kill_process_launcher_terminates_child():
    child = make_proc()
    child.wait.return_value = 0
    context = talos_process.ProcessContext(is_launcher=True)
    context.process = make_proc(children=[child])
    assert context.kill_process() == 0
    child.terminate.assert_called_once_with()
    context.process.terminate.assert_not_called()


def test_kill_process_launcher_with_several_children_terminates_parent():
    context = talos_process.ProcessContext(is_launcher=True)
    context.process = make_proc(children=[make_proc(), make_proc()])
    context.kill_process()
    context.process.terminate.assert_called_once_with()


def test_reader_ignores_js_noise_and_flags_markers(log):
    reader = talos_process.Reader()
    reader.proc = mock.Mock(pid=7)
    reader(b"plain line\r\n")
    reader("JavaScript error: foo")
    reader("JavaScript warning: foo")
    reader("SyntaxError: foo")
    reader("TypeError: foo")
    assert reader.output == ["plain line"]
    log.process_output.assert_called_once_with(7, "plain line")

    reader("x __endTimestamp")
    assert reader.got_end_timestamp
    reader("TART: TIMEOUT")
    assert reader.got_timeout and reader.timeout_message == "TART"
    reader("TEST-UNEXPECTED-FAIL | something")
    assert reader.got_error


class FakeTimer:
    fire = False

    def __init__(self, timeout, handler):
        self.timeout = timeout
        self.cancel = mock.Mock()
        self.start = handler if self.fire else mock.Mock()


@pytest.fixture
def browser(tmp_path):
    popen = mock.MagicMock(pid=123)
    popen.stdout = [b"line one\n"]
    popen.poll.return_value = 0
    popen.wait.return_value = 0
    process = make_proc(running=False)
    with mock.patch.object(
        talos_process.subprocess, "Popen", return_value=popen
    ) as popen_cls, mock.patch.object(
        talos_process.psutil, "Process", return_value=process
    ), mock.patch.object(talos_process, "Timer", FakeTimer), mock.patch.object(
        talos_process, "dump_screen"
    ) as dump_screen, mock.patch.object(
        talos_process.mozcrash, "kill_and_get_minidump"
    ) as minidump:
        yield mock.Mock(
            popen=popen,
            popen_cls=popen_cls,
            process=process,
            dump_screen=dump_screen,
            minidump=minidump,
            minidump_dir=str(tmp_path),
        )
    FakeTimer.fire = False


def run(browser, command=("firefox",), **kwargs):
    kwargs.setdefault("env", {"A": "1"})
    return talos_process.run_browser(list(command), browser.minidump_dir, **kwargs)


def test_run_browser_success_collects_output_and_timestamps(browser, log):
    on_started = mock.Mock()
    with mock.patch.object(talos_process.time, "time", return_value=5):
        context = run(browser, timeout=10, on_started=on_started)
    on_started.assert_called_once_with(browser.process)
    assert context.output[0] == "line one"
    assert context.output[1:] == [
        "__startBeforeLaunchTimestamp5000__endBeforeLaunchTimestamp",
        "__startAfterTerminationTimestamp5000__endAfterTerminationTimestamp",
    ]
    assert not context.is_launcher
    browser.popen_cls.assert_called_once()
    assert browser.popen_cls.call_args.kwargs["env"] == {"A": "1"}
    browser.popen.wait.assert_called_once_with(1)
    log.process_exit.assert_called_once_with(123, 0)


def test_run_browser_without_exit_code_logs_debug_message(browser, log):
    browser.popen.wait.return_value = None
    run(browser)
    log.process_exit.assert_not_called()
    assert "Unable to detect exit code" in log.debug.call_args[0][0]


def test_run_browser_kill_failure_is_logged(browser, log):
    browser.popen.wait.side_effect = RuntimeError("gone")
    run(browser)
    log.info.assert_any_call("Unable to kill process")


def test_run_browser_launcher_on_windows(browser):
    with mock.patch.object(talos_process.sys, "platform", "win32"):
        context = run(browser, command=("firefox", "-wait-for-browser"))
    assert context.is_launcher


def test_run_browser_not_launcher_elsewhere(browser):
    with mock.patch.object(talos_process.sys, "platform", "linux"):
        context = run(browser, command=("firefox", "-wait-for-browser"))
    assert not context.is_launcher


def test_run_browser_timeout(browser):
    FakeTimer.fire = True
    with pytest.raises(TalosError, match="timeout"):
        run(browser, timeout=1, utility_path="bin")
    browser.dump_screen.assert_called_once()
    browser.minidump.assert_called_once_with(browser.process.pid, browser.minidump_dir)


def test_run_browser_clean_exit_after_end_timestamp_skips_minidump(browser):
    browser.popen.stdout = [b"__endTimestamp\n"]
    run(browser)
    browser.popen.wait.assert_any_call(20)
    browser.minidump.assert_not_called()


def test_run_browser_hang_after_end_timestamp_raises_shutdown_timeout(browser):
    browser.popen.stdout = [b"__endTimestamp\n"]
    browser.popen.poll.return_value = None
    with pytest.raises(TalosError, match="shutdown timed out"):
        run(browser, utility_path="bin")
    browser.minidump.assert_called_once()
    browser.dump_screen.assert_called_once()


def test_run_browser_tart_timeout(browser):
    browser.popen.stdout = [b"TART: TIMEOUT\n"]
    with pytest.raises(TalosError, match="TIMEOUT: TART"):
        run(browser)
    browser.dump_screen.assert_not_called()


def test_run_browser_unexpected_error(browser):
    browser.popen.stdout = [b"TEST-UNEXPECTED-FAIL | boom\n"]
    with pytest.raises(TalosError, match="unexpected error"):
        run(browser, utility_path="bin")
    browser.dump_screen.assert_called_once()


def test_run_browser_debugger_not_found():
    with mock.patch.object(talos_process, "prepend_debugger_args", return_value=None):
        with pytest.raises(TalosError, match="suitable debugger"):
            talos_process.run_browser(["ff"], "dir", debug=True)


def test_run_browser_debug_mode_starts_browser_under_debugger():
    popen = mock.Mock(pid=5)
    popen.wait.return_value = 0
    on_started = mock.Mock()
    with mock.patch.object(
        talos_process, "prepend_debugger_args", return_value=["gdb", "ff"]
    ) as prepend, mock.patch.object(
        talos_process.signal, "signal"
    ) as sig, mock.patch.object(
        talos_process.subprocess, "Popen", return_value=popen
    ) as popen_cls, mock.patch.object(talos_process.psutil, "Process") as process:
        context = talos_process.run_browser(
            ["ff"],
            "dir",
            debugger="gdb",
            debugger_args="-x",
            on_started=on_started,
            env={"E": "1"},
        )
    prepend.assert_called_once_with(["ff"], "gdb", "-x")
    sig.assert_called_once()
    popen_cls.assert_called_once_with(["gdb", "ff"], env={"E": "1"})
    on_started.assert_called_once_with(process.return_value)
    assert context.process is process.return_value


def test_run_in_debug_mode_without_exit_code_does_not_log_exit(log):
    popen = mock.Mock(pid=5)
    popen.wait.return_value = None
    with mock.patch.object(talos_process.signal, "signal"), mock.patch.object(
        talos_process.subprocess, "Popen", return_value=popen
    ), mock.patch.object(talos_process.psutil, "Process"):
        talos_process.run_in_debug_mode(["gdb"])
    log.process_exit.assert_not_called()


def test_kill_and_get_minidump_launcher_child():
    child = mock.Mock(pid=2)
    context = talos_process.ProcessContext(is_launcher=True)
    context.process = make_proc(children=[child])
    with mock.patch.object(talos_process.mozcrash, "kill_and_get_minidump") as kill:
        talos_process.kill_and_get_minidump(context, "dir")
    kill.assert_called_once_with(2, "dir")


def test_kill_and_get_minidump_launcher_several_children():
    context = talos_process.ProcessContext(is_launcher=True)
    context.process = make_proc(children=[mock.Mock(), mock.Mock()])
    with mock.patch.object(talos_process.mozcrash, "kill_and_get_minidump") as kill:
        talos_process.kill_and_get_minidump(context, "dir")
    kill.assert_called_once_with(context.process.pid, "dir")


def test_dump_screen_on_failure_needs_utility_path():
    with mock.patch.object(talos_process, "dump_screen") as dump:
        talos_process.dump_screen_on_failure(None)
        dump.assert_not_called()
        talos_process.dump_screen_on_failure("bin")
        dump.assert_called_once_with("bin", talos_process.LOG)


if __name__ == "__main__":
    mozunit.main()
