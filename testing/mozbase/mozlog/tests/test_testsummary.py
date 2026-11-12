import json

import pytest
from mozlog.formatters import TestSummaryFormatter

# flake8: noqa


@pytest.mark.parametrize(
    "record,kept",
    (
        pytest.param(
            {"action": "log", "level": "INFO", "message": "noise"},
            False,
            id="log_info_dropped",
        ),
        pytest.param(
            {"action": "log", "level": "ERROR", "message": "boom"},
            True,
            id="log_error_kept",
        ),
        pytest.param(
            {"action": "log", "level": "CRITICAL", "message": "crit"},
            True,
            id="log_critical_kept",
        ),
        pytest.param(
            {"action": "log", "level": "WARNING", "message": "warn"},
            False,
            id="log_warning_dropped",
        ),
        pytest.param(
            {"action": "log", "level": "DEBUG", "message": "dbg"},
            False,
            id="log_debug_dropped",
        ),
        pytest.param(
            {"action": "suite_start", "tests": {"manifestA": ["test_foo"]}},
            True,
            id="suite_start_kept",
        ),
        pytest.param({"action": "suite_end"}, True, id="suite_end_kept"),
        pytest.param(
            {"action": "assertion_count", "count": 3},
            False,
            id="assertion_count_dropped",
        ),
        pytest.param(
            {
                "action": "lsan_leak",
                "frames": ["Ensure", "nsFoo::Bar"],
                "kind": "Direct",
                "bytes": 168,
                "objects": 1,
                "scope": "browser.toml",
                "allowed_match": None,
            },
            True,
            id="lsan_leak_kept",
        ),
        pytest.param(
            {
                "action": "lsan_leak",
                "frames": ["PR_LoadLibrary", "nsFoo::Bar"],
                "kind": "Direct",
                "bytes": 64,
                "objects": 1,
                "scope": "browser.toml",
                "allowed_match": "PR_LoadLibrary",
            },
            False,
            id="lsan_leak_allowed_match_dropped",
        ),
        pytest.param(
            {
                "action": "lsan_summary",
                "bytes": 488,
                "allocations": 3,
                "allowed": False,
            },
            True,
            id="lsan_summary_kept",
        ),
        pytest.param(
            {"action": "lsan_summary", "bytes": 64, "allocations": 1, "allowed": True},
            False,
            id="lsan_summary_allowed_dropped",
        ),
        pytest.param(
            {"action": "process_exit", "process": 1234, "exitcode": 0},
            False,
            id="process_exit_dropped",
        ),
        pytest.param(
            {"action": "process_start", "process": 1234},
            False,
            id="process_start_dropped",
        ),
        pytest.param(
            {
                "action": "tsan_error",
                "kind": "data race",
                "signature": "pk11slot.c:1350:17 in PK11_InitToken",
                "pid": 1596,
                "stacks": [{"label": "Write of size 2 by thread T9", "stack": []}],
                "scope": "browser_syncedtabs_errors_firefoxview.js",
            },
            True,
            id="tsan_error_kept",
        ),
        pytest.param(
            {"action": "assertion_failure", "kind": "MOZ_ASSERT", "stack": []},
            False,
            id="assertion_failure_dropped",
        ),
        pytest.param(
            {"action": "ubsan_error", "kind": "undefined-behavior", "message": "m"},
            True,
            id="ubsan_error_kept",
        ),
        pytest.param(
            {"action": "group_start", "name": "manifestA"}, True, id="group_start_kept"
        ),
        pytest.param(
            {"action": "group_end", "name": "manifestA"}, True, id="group_end_kept"
        ),
        pytest.param(
            {"action": "test_start", "test": "test_foo", "group": "manifestA"},
            True,
            id="test_start_kept",
        ),
        pytest.param(
            {"action": "test_end", "test": "test_foo", "status": "OK"},
            True,
            id="test_end_kept",
        ),
        pytest.param(
            {
                "action": "test_status",
                "test": "test_foo",
                "subtest": "subtest1",
                "status": "FAIL",
                "expected": "PASS",
            },
            True,
            id="test_status_unexpected_kept",
        ),
        pytest.param(
            {
                "action": "test_status",
                "test": "test_foo",
                "subtest": "subtest1",
                "status": "PASS",
            },
            False,
            id="test_status_no_expected_dropped",
        ),
        pytest.param(
            {
                "action": "test_status",
                "test": "test_foo",
                "subtest": "subtest1",
                "status": "PASS",
                "expected": "PASS",
            },
            False,
            id="test_status_matching_expected_dropped",
        ),
        pytest.param(
            {"action": "crash", "test": "test_foo", "signature": "sig"},
            True,
            id="crash_kept",
        ),
        pytest.param({"action": "shutdown"}, False, id="shutdown_dropped"),
        pytest.param(
            {
                "action": "process_output",
                "process": 1234,
                "command": "xpcshell",
                "data": "stdout chatter",
            },
            False,
            id="process_output_dropped",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "default",
                "bytes": 856,
                "threshold": 0,
                "objects": ["CondVar"],
            },
            True,
            id="mozleak_total_over_threshold_kept",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "tab",
                "bytes": 0,
                "threshold": 0,
                "objects": [],
            },
            False,
            id="mozleak_total_no_leak_dropped",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "gmplugin",
                "bytes": 20000,
                "threshold": 20000,
                "objects": ["nsFoo"],
            },
            False,
            id="mozleak_total_within_threshold_dropped",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "default",
                "bytes": None,
                "threshold": 0,
                "objects": [],
            },
            True,
            id="mozleak_total_missing_total_kept",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "tab",
                "bytes": None,
                "threshold": 0,
                "objects": [],
                "ignore_missing": True,
            },
            False,
            id="mozleak_total_ignored_missing_total_dropped",
        ),
        pytest.param(
            {
                "action": "mozleak_total",
                "process": "tab",
                "bytes": None,
                "threshold": 0,
                "objects": [],
                "induced_crash": True,
            },
            False,
            id="mozleak_total_induced_crash_dropped",
        ),
    ),
)
def test_testsummary_filters(record, kept):
    fmt = TestSummaryFormatter()
    out = fmt(record)
    if kept:
        assert out is not None
        assert out.endswith("\n")
    else:
        assert out is None or out == ""


def test_testsummary_strips_noise_fields():
    fmt = TestSummaryFormatter()
    record = {
        "action": "test_status",
        "time": 1780300269164,
        "thread": "Thread-11",
        "pid": None,
        "source": "xpcshell/head.js",
        "test": "xpcom/tests/unit/test_bug476919.js",
        "subtest": "run_test",
        "status": "FAIL",
        "expected": "PASS",
        "message": "[run_test : 25] force fail - false == true",
        "stack": "stack/trace\nlines",
        "js_source": "head.js",
        "minidump_path": "/tmp/foo.dmp",
        "crashing_thread_stack": "frame\nframe",
        "tests": {"manifestA": ["test_foo"]},
        "stackwalk_stdout": "...",
        "stackwalk_stderr": "...",
        "extra": {"key": "value"},
    }
    out = fmt(record)
    result = json.loads(out)
    for stripped in (
        "thread",
        "pid",
        "source",
        "extra",
        "tests",
        "js_source",
        "crashing_thread_stack",
        "stackwalk_stdout",
        "stackwalk_stderr",
    ):
        assert stripped not in result
    assert result["action"] == "test_status"
    assert result["test"] == "xpcom/tests/unit/test_bug476919.js"
    assert result["status"] == "FAIL"
    assert result["expected"] == "PASS"
    assert result["stack"] == "stack/trace\nlines"
    assert result["minidump_path"] == "/tmp/foo.dmp"


def test_testsummary_log_error_keeps_only_level_and_message():
    fmt = TestSummaryFormatter()
    message = (
        "TEST-UNEXPECTED-FAIL | LeakSanitizer leak at nsTimer, NS_NewTimer"
        " | netwerk/test/browser/browser.toml"
    )
    record = {
        "action": "log",
        "time": 1787844653959,
        "thread": "MainThread",
        "pid": 9594,
        "source": "mochitest",
        "level": "ERROR",
        "message": message,
    }
    out = fmt(record)
    result = json.loads(out)
    assert result == {
        "action": "log",
        "time": 1787844653959,
        "level": "ERROR",
        "message": message,
    }


def test_testsummary_mozleak_total_keeps_the_leak_fields():
    fmt = TestSummaryFormatter()
    objects = ["CondVar", "MozPromiseRefcountable", "Mutex", "nsThread"]
    scope = "browser/components/aiwindow/ui/test/browser/browser.toml"
    record = {
        "action": "mozleak_total",
        "time": 1789627855830,
        "thread": "MainThread",
        "pid": 1517,
        "source": "mochitest",
        "process": "default",
        "bytes": 856,
        "threshold": 0,
        "objects": objects,
        "scope": scope,
        "induced_crash": False,
        "ignore_missing": False,
    }
    out = fmt(record)
    result = json.loads(out)
    assert result == {
        "action": "mozleak_total",
        "time": 1789627855830,
        "process": "default",
        "bytes": 856,
        "threshold": 0,
        "objects": objects,
        "scope": scope,
        "induced_crash": False,
        "ignore_missing": False,
    }


def test_testsummary_lsan_summary_keeps_the_counts():
    fmt = TestSummaryFormatter()
    record = {
        "action": "lsan_summary",
        "time": 1791156513865,
        "thread": "MainThread",
        "pid": 1652,
        "source": "mochitest",
        "bytes": 488,
        "allocations": 3,
    }
    out = fmt(record)
    result = json.loads(out)
    assert result == {
        "action": "lsan_summary",
        "time": 1791156513865,
        "bytes": 488,
        "allocations": 3,
    }


def test_testsummary_lsan_leak_keeps_the_stack():
    fmt = TestSummaryFormatter()
    frames = [
        "Ensure",
        "mozilla::dom::FileSystemBackgroundRequestHandler::CreateFileSystemManagerChild",
        "mozilla::dom::FileSystemManager::BeginRequest",
        "mozilla::dom::fs::FileSystemRequestHandler::GetRootHandle",
    ]
    stack = [{"function": "Ensure", "file": "MozPromise.h", "line": 1550}]
    scope = "browser/components/aiwindow/ui/test/browser/browser_smartwindow.toml"
    record = {
        "action": "lsan_leak",
        "time": 1791156513878,
        "thread": "MainThread",
        "pid": 1652,
        "source": "mochitest",
        "frames": frames,
        "kind": "Direct",
        "bytes": 168,
        "objects": 1,
        "stack": stack,
        "scope": scope,
    }
    out = fmt(record)
    result = json.loads(out)
    assert result == {
        "action": "lsan_leak",
        "time": 1791156513878,
        "frames": frames,
        "kind": "Direct",
        "bytes": 168,
        "objects": 1,
        "stack": stack,
        "scope": scope,
    }


def test_testsummary_crash_keeps_stack():
    fmt = TestSummaryFormatter()
    record = {
        "action": "crash",
        "test": "test_foo",
        "signature": "sig",
        "stack": "frame1\nframe2",
        "thread": "main",
        "pid": 1234,
    }
    out = fmt(record)
    result = json.loads(out)
    assert result["stack"] == "frame1\nframe2"
    assert "thread" not in result
    assert "pid" not in result


def test_testsummary_crash_keeps_the_minidump_path():
    fmt = TestSummaryFormatter()
    minidump_path = (
        "/var/folders/ss/l_yg30rn4777np9phmknlrgw000014/T/xpc-vmxo7brg"
        "/xpc-other-p9w3v69p/33672F0C-6F69-4499-AAE6-85A13340212B.dmp"
    )
    reason = (
        "MOZ_ASSERT(staticPrefValue == preferenceValue) (Preference"
        " 'dom.ipc.processLaunch.useMacPlists' got modified since"
        " StaticPrefs::dom_ipc_processLaunch_useMacPlists_AtStartup was"
        " initialized. Consider using an `always` mirror kind instead)"
    )
    record = {
        "action": "crash",
        "time": 1792157632661,
        "thread": "MainThread",
        "pid": 13322,
        "source": "xpcshell",
        "process": None,
        "test": "extensions/pref/autoconfig/test/unit/test_autoconfig_profile_prefs.js",
        "signature": "@ mozilla::StaticPrefs::InitOncePrefs()::$_13::operator()",
        "minidump_path": minidump_path,
        "stackwalk_stdout": "Operating system: Mac OS X\n...",
        "stackwalk_stderr": None,
        "stackwalk_retcode": 0,
        "stackwalk_errors": [],
        "crashing_thread_stack": [{"frame": 0}],
        "process_type": "main",
        "reason": reason,
        "quiet": False,
    }
    result = json.loads(fmt(record))
    assert result == {
        "action": "crash",
        "time": 1792157632661,
        "process": None,
        "test": "extensions/pref/autoconfig/test/unit/test_autoconfig_profile_prefs.js",
        "signature": "@ mozilla::StaticPrefs::InitOncePrefs()::$_13::operator()",
        "minidump_path": minidump_path,
        "process_type": "main",
        "reason": reason,
        "quiet": False,
    }


def test_testsummary_ubsan_error_keeps_stack():
    fmt = TestSummaryFormatter()
    stack = [{"function": "incr_ptr_aligned", "file": "ggml-c.c", "line": 7106}]
    record = {
        "action": "ubsan_error",
        "kind": "undefined-behavior",
        "message": "applying non-zero offset 96 to null pointer",
        "file": "ggml-c.c",
        "lineno": 7106,
        "column": 33,
        "stack": stack,
        "thread": "main",
        "pid": 1234,
    }
    result = json.loads(fmt(record))
    assert result["stack"] == stack
    assert result["message"] == "applying non-zero offset 96 to null pointer"
    assert result["column"] == 33
    assert "thread" not in result
    assert "pid" not in result


def test_testsummary_tsan_error_keeps_pid_and_stacks():
    fmt = TestSummaryFormatter()
    stacks = [
        {
            "label": "Write of size 2 at 0x725c002ac828 by thread T9",
            "stack": [
                {
                    "function": "PK11_InitToken",
                    "module": "libnss3.so",
                    "module_offset": "0xd9c2f",
                    "file": "/builds/worker/checkouts/gecko/security/nss/lib/pk11wrap/pk11slot.c",
                    "line": 1350,
                    "column": 17,
                }
            ],
        }
    ]
    scope = "browser/components/firefoxview/tests/browser/browser_syncedtabs_errors_firefoxview.js"
    record = {
        "action": "tsan_error",
        "time": 1793990929939,
        "thread": "MainThread",
        # The racing process, not the harness that logged the record.
        "pid": 1596,
        "source": "mochitest",
        "kind": "data race",
        "signature": "pk11slot.c:1350:17 in PK11_InitToken",
        "description": None,
        "stacks": stacks,
        "scope": scope,
    }
    result = json.loads(fmt(record))
    assert result == {
        "action": "tsan_error",
        "time": 1793990929939,
        "pid": 1596,
        "kind": "data race",
        "signature": "pk11slot.c:1350:17 in PK11_InitToken",
        "description": None,
        "stacks": stacks,
        "scope": scope,
    }


def test_testsummary_log_error_keeps_stack():
    fmt = TestSummaryFormatter()
    message = (
        "Unexpected exception NS_ERROR_FAILURE: Component returned failure code:"
        " 0x80004005 (NS_ERROR_FAILURE) [nsIGIOService.createHandlerAppFromAppId]"
    )
    stack = (
        "promiseStartLegacyDownload@head.js:282:8\n"
        "test_launch_id@common_test_Download.js:2629:26\n"
        "@-e:1:1\n"
    )
    record = {
        "action": "log",
        "time": 1791156063780,
        "thread": None,
        "pid": None,
        "source": "xpcshell/head.js",
        "level": "ERROR",
        "message": message,
        "exc_info": False,
        "extra": {"source_file": "head.js", "stack": stack},
        "stack": stack,
        "test": "toolkit/components/downloads/test/unit/test_DownloadLegacy.js",
    }
    result = json.loads(fmt(record))
    assert result["stack"] == stack
    assert result["message"] == message
    for stripped in ("thread", "pid", "source", "extra"):
        assert stripped not in result


def test_testsummary_emits_test_start_and_end_separately():
    fmt = TestSummaryFormatter()
    out_start = fmt({"action": "test_start", "test": "test_foo", "time": 1000})
    result_start = json.loads(out_start)
    assert result_start["action"] == "test_start"
    assert result_start["time"] == 1000

    out_end = fmt({
        "action": "test_end",
        "test": "test_foo",
        "status": "OK",
        "time": 2000,
    })
    result_end = json.loads(out_end)
    assert result_end["action"] == "test_end"
    assert result_end["time"] == 2000
    assert result_end["test"] == "test_foo"
    assert result_end["status"] == "OK"


def test_testsummary_keeps_a_harness_abort_as_log_and_test_end():
    """
    A harness abort reaches the summary as an ERROR log line, but only the
    test_end RemoteProcessMonitor emits alongside it carries a status and the
    test it is attributed to.
    """
    fmt = TestSummaryFormatter()
    message = "application timed out after 370 seconds with no output"

    log_record = json.loads(
        fmt({
            "action": "log",
            "level": "ERROR",
            "message": f"TEST-UNEXPECTED-FAIL | test_foo | {message}",
        })
    )
    assert log_record["level"] == "ERROR"
    assert "test" not in log_record
    assert "status" not in log_record

    result = json.loads(
        fmt({
            "action": "test_end",
            "test": "test_foo",
            "status": "TIMEOUT",
            "expected": "PASS",
            "message": message,
            "source": "mozdevice",
        })
    )
    assert result["status"] == "TIMEOUT"
    assert result["test"] == "test_foo"
    assert result["message"] == message
    assert "source" not in result


if __name__ == "__main__":
    import mozunit

    mozunit.main()
