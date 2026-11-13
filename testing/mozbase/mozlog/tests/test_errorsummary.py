import json
import time

import pytest

# flake8: noqa


@pytest.mark.parametrize(
    "logs,expected",
    (
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "PASS", "FAIL"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_baz", "subtest": null, "group": "manifestA", "status": "PASS", "expected": "FAIL", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 8}
                {"group": "manifestA", "status": "ERROR", "duration": 20, "action": "group_result", "line": 9}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 9}
            """.strip(),
            id="basic",
        ),
        pytest.param(
            [
                ("suite_start", {"manifest": ["test_foo"]}),
                ("test_start", "test_foo"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifest"], "action": "test_groups", "line": 0}
                {"group": "manifest", "status": null, "duration": 0, "action": "group_result", "line": 2}
            """.strip(),
            id="missing_test_end",
        ),
        pytest.param(
            [
                ("suite_start", {"manifest": ["test_foo"]}),
                ("test_start", "test_foo"),
                ("test_status", "test_foo", "subtest", "PASS"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifest"], "action": "test_groups", "line": 0}
                {"group": "manifest", "status": "ERROR", "duration": null, "action": "group_result", "line": 3}
            """.strip(),
            id="missing_test_end_with_test_status_ok",
            marks=pytest.mark.xfail,  # status is OK but should be ERROR
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "CRASH"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "FAIL", "FAIL"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_bar", "subtest": null, "group": "manifestA", "status": "CRASH", "expected": "OK", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 4}
                {"group": "manifestA", "status": "ERROR", "duration": 20, "action": "group_result", "line": 9}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 9}
            """.strip(),
            id="crash_and_group_status",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_status", "test_baz", "subtest", "FAIL", "FAIL"),
                ("test_end", "test_baz", "OK"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "OK", "duration": 29, "action": "group_result", "line": 10}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 10}
            """.strip(),
            id="fail_expected_fail",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_status", "test_baz", "Test timed out", "FAIL", "PASS"),
                ("test_status", "test_baz", "", "TIMEOUT", "PASS"),
                ("crash", "", "signature", "manifestA"),
                ("test_end", "test_baz", "OK"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_baz", "subtest": "Test timed out", "group": "manifestA", "status": "FAIL", "expected": "PASS", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 8}
                {"test": "test_baz", "subtest": "", "group": "manifestA", "status": "TIMEOUT", "expected": "PASS", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 9}
                {"test": "manifestA", "group": "manifestA", "signature": "signature", "stackwalk_stderr": null, "stackwalk_stdout": null, "action": "crash", "line": 10}
                {"group": "manifestA", "status": "ERROR", "duration": 49, "action": "group_result", "line": 12}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 12}
            """.strip(),
            id="timeout_and_crash",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "CRASH", "CRASH"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "FAIL", "FAIL"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "OK", "duration": 20, "action": "group_result", "line": 9}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 9}
            """.strip(),
            id="crash_expected_crash",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "FAIL", "FAIL"),
                ("crash", "", "", "manifestA"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "manifestA", "group": "manifestA", "signature": "", "stackwalk_stderr": null, "stackwalk_stdout": null, "action": "crash", "line": 9}
                {"group": "manifestA", "status": "ERROR", "duration": 20, "action": "group_result", "line": 10}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 10}
            """.strip(),
            id="assertion_crash_on_shutdown",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "FAIL"),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_baz", "group": "manifestA", "status": "FAIL", "expected": "OK", "subtest": null, "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 8}
            """.strip(),
            id="timeout_no_group_status",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("group_start", "manifestA"),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "PASS", "FAIL"),
                ("group_end", "manifestA"),
                ("group_start", "manifestB"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("group_end", "manifestB"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_baz", "subtest": null, "group": "manifestA", "status": "PASS", "expected": "FAIL", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 7}
                {"group": "manifestA", "status": "ERROR", "duration": 70, "action": "group_result", "line": 13}
                {"group": "manifestB", "status": "OK", "duration": 30, "action": "group_result", "line": 13}
            """.strip(),
            id="group_start_end_duration",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar"],
                    },
                ),
                ("group_start", "manifestA"),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "SKIP"),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "SKIP"),
                ("group_end", "manifestA"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "SKIP", "duration": 0, "action": "group_result", "line": 7}
            """.strip(),
            id="group_start_end_all_skipped",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("group_start", "parallel"),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "OK"),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("group_end", "parallel"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "OK", "duration": 10, "action": "group_result", "line": 7}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 7}
            """.strip(),
            id="non_manifest_group_names_ignored",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_a1", "test_a2"],
                        "manifestB": ["test_b1"],
                    },
                ),
                ("group_start", "parallel", {"threads": 2}),
                ("test_start", "test_a1"),
                ("test_end", "test_a1", "OK"),
                ("test_start", "test_b1"),
                ("test_end", "test_b1", "OK"),
                ("group_end", "parallel"),
                ("group_start", "sequential"),
                ("test_start", "test_a2"),
                ("test_end", "test_a2", "OK"),
                ("group_end", "sequential"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "OK", "duration": 14, "action": "group_result", "line": 11}
                {"group": "manifestB", "status": "OK", "duration": 5, "action": "group_result", "line": 11}
            """.strip(),
            id="parallel_threads_divide_time",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_a1", "test_a2"],
                    },
                ),
                ("test_start", "test_a1"),
                ("test_start", "test_a2"),
                ("test_end", "test_a2", "SKIP"),
                ("test_end", "test_a1", "OK"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA"], "action": "test_groups", "line": 0}
                {"group": "manifestA", "status": "OK", "duration": 30, "action": "group_result", "line": 5}
            """.strip(),
            id="parallel_skip_does_not_clear_other_test_time",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["test_foo", "test_bar", "test_baz"],
                        "manifestB": ["test_something"],
                    },
                ),
                ("test_start", "test_foo"),
                ("test_end", "test_foo", "OK"),
                ("assertion_count", "test_foo", 1, 0, 0),
                ("test_start", "test_bar"),
                ("test_end", "test_bar", "OK"),
                ("assertion_count", "test_bar", 1, 2, 3),
                ("test_start", "test_baz"),
                ("test_end", "test_baz", "OK"),
                ("assertion_count", "test_baz", 2, 1, 3),
                ("test_start", "test_something"),
                ("test_end", "test_something", "OK"),
                ("assertion_count", "test_something", 0, 0, 0),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "test_foo", "subtest": null, "group": "manifestA", "status": "FAIL", "expected": "PASS", "message": "assertion count 1 is more than expected 0 assertions", "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 3}
                {"test": "test_bar", "subtest": null, "group": "manifestA", "status": "PASS", "expected": "FAIL", "message": "assertion count 1 is less than expected 2 to 3 assertions", "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 6}
                {"group": "manifestA", "status": "ERROR", "duration": 30, "action": "group_result", "line": 13}
                {"group": "manifestB", "status": "OK", "duration": 10, "action": "group_result", "line": 13}
            """.strip(),
            id="unexpected_assertion_count",
        ),
        pytest.param(
            [
                (
                    "suite_start",
                    {
                        "manifestA": ["dir/test_foo.py"],
                        "manifestB": ["dir/test_bar.py"],
                    },
                ),
                (
                    "test_start",
                    "dir/test_foo.py TestFoo.test_foo",
                    None,
                    None,
                    "manifestA",
                ),
                ("test_end", "dir/test_foo.py TestFoo.test_foo", "ERROR", "PASS"),
                (
                    "test_start",
                    "dir/test_bar.py TestBar.test_bar",
                    None,
                    None,
                    "manifestB",
                ),
                ("crash", "", "signature", "dir/test_bar.py TestBar.test_bar"),
                ("test_end", "dir/test_bar.py TestBar.test_bar", "ERROR", "PASS"),
                ("crash", "", "signature", "marionette.py"),
                ("suite_end",),
            ],
            """
                {"groups": ["manifestA", "manifestB"], "action": "test_groups", "line": 0}
                {"test": "dir/test_foo.py TestFoo.test_foo", "subtest": null, "group": "manifestA", "status": "ERROR", "expected": "PASS", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 2}
                {"test": "dir/test_bar.py TestBar.test_bar", "group": "manifestB", "signature": "signature", "stackwalk_stderr": null, "stackwalk_stdout": null, "action": "crash", "line": 4}
                {"test": "dir/test_bar.py TestBar.test_bar", "subtest": null, "group": "manifestB", "status": "ERROR", "expected": "PASS", "message": null, "stack": null, "modifiers": "", "known_intermittent": [], "action": "test_result", "line": 5}
                {"test": "marionette.py", "group": "", "signature": "signature", "stackwalk_stderr": null, "stackwalk_stdout": null, "action": "crash", "line": 6}
                {"group": "manifestA", "status": "ERROR", "duration": 10, "action": "group_result", "line": 7}
                {"group": "manifestB", "status": "ERROR", "duration": 20, "action": "group_result", "line": 7}
            """.strip(),
            id="group_from_test_start",
        ),
    ),
)
def test_errorsummary(monkeypatch, get_logger, logs, expected):
    ts = {"ts": 0.0}  # need to use dict since 'nonlocal' doesn't exist on PY2

    def fake_time():
        ts["ts"] += 0.01
        return ts["ts"]

    monkeypatch.setattr(time, "time", fake_time)
    logger = get_logger("errorsummary")

    for log in logs:
        getattr(logger, log[0])(*log[1:])

    buf = logger.handlers[0].stream
    result = buf.getvalue()
    print("Dumping result for copy/paste:")
    print(result)

    expected = expected.split("\n")
    for i, line in enumerate(result.split("\n")):
        if not line:
            continue

        data = json.loads(line)
        assert data == json.loads(expected[i])


@pytest.mark.parametrize(
    "calls,expected_status",
    (
        pytest.param(
            [
                ("crash", {"test": "test_foo", "quiet": True}),
                ("test_end", "test_foo", "CRASH", "CRASH"),
                ("test_end", "test_foo", "OK"),
            ],
            "OK",
            id="quiet_crash_retry_pass",
        ),
        pytest.param(
            [
                ("crash", {"test": "test_foo", "quiet": True}),
                ("test_end", "test_foo", "TIMEOUT", "TIMEOUT"),
                ("test_end", "test_foo", "OK"),
            ],
            "OK",
            id="quiet_crash_timeout_retry_pass",
        ),
        pytest.param(
            [
                ("crash", {"test": "test_foo"}),
                ("test_end", "test_foo", "OK"),
            ],
            "ERROR",
            id="crash_then_pass",
        ),
        pytest.param(
            [
                ("crash", {"test": "test_foo", "quiet": True}),
                ("test_end", "test_foo", "CRASH", "CRASH"),
                ("crash", {"test": "test_foo"}),
                ("test_end", "test_foo", "CRASH"),
            ],
            "ERROR",
            id="quiet_crash_retry_crash",
        ),
        pytest.param(
            [
                ("crash", {"test": "test_foo", "quiet": True}),
                ("test_end", "test_foo", "CRASH", "CRASH"),
                ("test_end", "test_foo", "FAIL"),
            ],
            "ERROR",
            id="quiet_crash_retry_fail",
        ),
        pytest.param(
            [
                ("test_end", "test_bar", "FAIL"),
                ("crash", {"test": "test_foo", "quiet": True}),
                ("test_end", "test_foo", "CRASH", "CRASH"),
                ("test_end", "test_foo", "OK"),
            ],
            "ERROR",
            id="quiet_crash_other_test_fails",
        ),
        pytest.param(
            [
                ("test_end", "test_foo", "OK"),
                ("crash", {"test": "manifestA"}),
            ],
            "ERROR",
            id="crash_attributed_to_group",
        ),
    ),
)
def test_errorsummary_crash_group_status(get_logger, calls, expected_status):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_foo", "test_bar"]})
    for call in calls:
        if call[0] == "crash":
            logger.crash(signature="signature", **call[1])
        else:
            test, status = call[1], call[2]
            logger.test_start(test)
            if len(call) > 3:
                logger.test_end(test, status, expected=call[3])
            else:
                logger.test_end(test, status)
    logger.suite_end()

    lines = [json.loads(l) for l in logger.handlers[0].stream.getvalue().splitlines()]
    crashes = [l for l in lines if l["action"] == "crash"]
    group_results = [l for l in lines if l["action"] == "group_result"]
    assert len(crashes) == sum(1 for c in calls if c[0] == "crash")
    assert [(g["group"], g["status"]) for g in group_results] == [
        ("manifestA", expected_status)
    ]


def test_errorsummary_crash_with_group(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({
        "manifestA": ["a.html == a-ref.html"],
        "manifestB": ["b.html == b-ref.html"],
    })
    logger.test_start("a.html == a-ref.html")
    logger.test_end("a.html == a-ref.html", "OK")
    logger.test_start("b.html == b-ref.html")
    logger.crash("", "signature", "b.html", group="manifestB")
    logger.suite_end()

    lines = [
        json.loads(line) for line in logger.handlers[0].stream.getvalue().splitlines()
    ]
    crashes = [line for line in lines if line["action"] == "crash"]
    assert len(crashes) == 1
    assert crashes[0]["test"] == "b.html"
    assert crashes[0]["group"] == "manifestB"
    group_results = {
        line["group"]: line["status"]
        for line in lines
        if line["action"] == "group_result"
    }
    assert group_results == {"manifestA": "OK", "manifestB": "ERROR"}


def test_errorsummary_crash_group_overrides_test_mapping(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_a"], "manifestB": ["test_b"]})
    logger.test_start("test_a")
    logger.test_end("test_a", "OK")
    logger.crash("", "signature", "test_a", group="manifestB")
    logger.suite_end()

    lines = [
        json.loads(line) for line in logger.handlers[0].stream.getvalue().splitlines()
    ]
    crashes = [line for line in lines if line["action"] == "crash"]
    assert [(c["test"], c["group"]) for c in crashes] == [("test_a", "manifestB")]
    group_results = {
        line["group"]: line["status"]
        for line in lines
        if line["action"] == "group_result"
    }
    assert group_results == {"manifestA": "OK", "manifestB": "ERROR"}


def _errorsummary_lines(logger):
    return [
        json.loads(line) for line in logger.handlers[0].stream.getvalue().splitlines()
    ]


def _group_results(lines):
    return {
        line["group"]: line["status"]
        for line in lines
        if line["action"] == "group_result"
    }


SEGV_MESSAGE = "SEGV on unknown address 0x00000000030b (pc 0x7b03e9dd40af T0)"


def test_errorsummary_asan_error_attributed_to_running_test(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({
        "/css/selectors": ["/css/selectors/a.html"],
        "/dom": ["/dom/b.html"],
    })
    logger.test_start("/css/selectors/a.html")
    logger.test_end("/css/selectors/a.html", "OK")
    logger.test_start("/dom/b.html")
    logger.asan_error("SEGV", SEGV_MESSAGE, pid=28130, function="GetInnerWindow")
    logger.test_end("/dom/b.html", "OK")
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert crashes == [
        {
            "action": "crash",
            "line": 4,
            "test": "/dom/b.html",
            "signature": "AddressSanitizer: SEGV [@ GetInnerWindow]",
            "group": "/dom",
            "stackwalk_stdout": None,
            "stackwalk_stderr": None,
        }
    ]
    assert _group_results(lines) == {"/css/selectors": "OK", "/dom": "ERROR"}


def test_errorsummary_asan_error_with_test_and_finished_suffix(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_a"], "manifestB": ["test_b"]})
    logger.test_start("test_a")
    logger.test_end("test_a", "OK")
    logger.test_start("test_b")
    stack = [{"module": "libxul.so", "module_offset": 1}, {"function": "f"}]
    logger.asan_error(
        "heap-use-after-free", "use after free", test="test_a (finished)", stack=stack
    )
    logger.test_end("test_b", "OK")
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert [(c["test"], c["group"], c["signature"]) for c in crashes] == [
        ("test_a", "manifestA", "AddressSanitizer: heap-use-after-free [@ f]")
    ]
    assert _group_results(lines) == {"manifestA": "ERROR", "manifestB": "OK"}


@pytest.mark.parametrize(
    ("scope", "group"),
    [
        pytest.param("/dom", "/dom", id="group_name"),
        pytest.param("/dom/b.html", "/dom", id="test_name"),
        pytest.param("/css", "subsuite:/css", id="subsuite_prefixed_group"),
        pytest.param("/unknown", None, id="unknown_scope"),
    ],
)
def test_errorsummary_asan_error_scope_fallback(get_logger, scope, group):
    logger = get_logger("errorsummary")
    logger.suite_start({"/dom": ["/dom/b.html"], "subsuite:/css": ["/css/c.html"]})
    logger.test_start("/dom/b.html")
    logger.test_end("/dom/b.html", "OK")
    logger.asan_error("SEGV", SEGV_MESSAGE, scope=scope)
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert len(crashes) == 1
    assert crashes[0]["test"] is None
    assert crashes[0]["signature"] == "AddressSanitizer: SEGV"
    assert crashes[0].get("group") == group
    # A group nothing touched gets no group_result line.
    expected = {"/dom": "OK"}
    if group:
        expected[group] = "ERROR"
    assert _group_results(lines) == expected


def test_errorsummary_asan_error_quiet_keeps_group_status(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_a"]})
    logger.test_start("test_a")
    logger.asan_error("SEGV", SEGV_MESSAGE, test="test_a", quiet=True)
    logger.test_end("test_a", "CRASH", expected="CRASH")
    logger.test_start("test_a")
    logger.test_end("test_a", "OK")
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert [(c["test"], c["group"]) for c in crashes] == [("test_a", "manifestA")]
    assert _group_results(lines) == {"manifestA": "OK"}


@pytest.mark.parametrize(
    ("end_kwargs", "status"),
    [
        pytest.param(
            {"status": "CRASH", "expected": "CRASH"}, "OK", id="expected_crash"
        ),
        pytest.param(
            {"status": "CRASH", "expected": "OK", "known_intermittent": ["CRASH"]},
            "OK",
            id="known_intermittent_crash",
        ),
        pytest.param(
            {"status": "CRASH", "expected": "OK"}, "ERROR", id="unexpected_crash"
        ),
        pytest.param({"status": "OK"}, "ERROR", id="test_passes"),
    ],
)
def test_errorsummary_asan_error_decided_when_the_test_ends(
    get_logger, end_kwargs, status
):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_a"]})
    logger.test_start("test_a")
    logger.asan_error("SEGV", SEGV_MESSAGE, test="test_a")
    logger.test_end("test_a", **end_kwargs)
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert [(c["test"], c["group"]) for c in crashes] == [("test_a", "manifestA")]
    assert _group_results(lines) == {"manifestA": status}


def test_errorsummary_asan_error_pending_until_suite_end(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({"manifestA": ["test_a"]})
    logger.test_start("test_a")
    logger.asan_error("SEGV", SEGV_MESSAGE)
    logger.suite_end()

    assert _group_results(_errorsummary_lines(logger)) == {"manifestA": "ERROR"}


def test_errorsummary_asan_error_scope_wins_over_running_test(get_logger):
    logger = get_logger("errorsummary")
    logger.suite_start({"/css": ["/css/a.html"], "/dom": ["/dom/b.html"]})
    logger.test_start("/css/a.html")
    logger.test_start("/dom/b.html")
    logger.asan_error("SEGV", SEGV_MESSAGE, scope="/css")
    logger.test_end("/css/a.html", "OK")
    logger.test_end("/dom/b.html", "OK")
    logger.suite_end()

    lines = _errorsummary_lines(logger)
    crashes = [line for line in lines if line["action"] == "crash"]
    assert [(c["test"], c["group"]) for c in crashes] == [("/css/a.html", "/css")]
    assert _group_results(lines) == {"/css": "ERROR", "/dom": "OK"}


if __name__ == "__main__":
    import mozunit

    mozunit.main()
