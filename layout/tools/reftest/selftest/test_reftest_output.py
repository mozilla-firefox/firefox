# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

try:
    # Python2
    from cStringIO import StringIO
except ImportError:
    # Python3
    from io import StringIO

from functools import partial

import mozunit
import pytest
from mozharness.base.log import ERROR, INFO, WARNING
from mozharness.mozilla.automation import TBPL_FAILURE, TBPL_SUCCESS, TBPL_WARNING
from moztest.selftest.output import filter_action, get_mozharness_status

here = os.path.abspath(os.path.dirname(__file__))
get_mozharness_status = partial(get_mozharness_status, "reftest")


def test_output_pass(runtests):
    status, lines = runtests("reftest-pass.list")
    assert status == 0

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_SUCCESS
    assert log_level in (INFO, WARNING)

    test_status = filter_action("test_status", lines)
    assert len(test_status) == 3
    assert all(t["status"] == "PASS" for t in test_status)

    test_end = filter_action("test_end", lines)
    assert len(test_end) == 3
    assert all(t["status"] == "PASS" for t in test_end)


def test_output_fail(runtests):
    formatter = pytest.importorskip("output").ReftestFormatter()

    status, lines = runtests("reftest-fail.list")
    assert status == 0

    buf = StringIO()
    tbpl_status, log_level, summary = get_mozharness_status(
        lines, status, formatter=formatter, buf=buf
    )

    assert tbpl_status == TBPL_WARNING
    assert log_level == WARNING

    test_status = filter_action("test_status", lines)
    assert len(test_status) == 3
    assert all(t["status"] == "FAIL" for t in test_status)
    assert all("reftest_screenshots" in t["extra"] for t in test_status)

    test_end = filter_action("test_end", lines)
    assert len(test_end) == 3
    assert all(t["status"] == "FAIL" for t in test_end)

    # ensure screenshots were printed
    formatted = buf.getvalue()
    assert "REFTEST   IMAGE 1" in formatted
    assert "REFTEST   IMAGE 2" in formatted


@pytest.mark.skip_mozinfo("!crashreporter")
def test_output_crash(runtests):
    status, lines = runtests(
        "reftest-crash.list", environment=["MOZ_CRASHREPORTER_SHUTDOWN=1"]
    )
    assert status == 245

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_FAILURE
    assert log_level == ERROR

    crash = filter_action("crash", lines)
    assert len(crash) == 1
    assert crash[0]["action"] == "crash"
    assert crash[0]["signature"]
    assert crash[0]["minidump_path"]
    assert crash[0]["group"].endswith("reftest-crash.list")

    assert len(filter_action("suite_end", lines)) == 1
    assert not any(
        "suite_end message before suite_start" in l["message"]
        for l in filter_action("log", lines)
    )

    lines = filter_action("test_end", lines)
    assert len(lines) == 0


@pytest.mark.skip_mozinfo("!asan")
def test_output_asan(runtests):
    status, lines = runtests(
        "reftest-crash.list", environment=["MOZ_CRASHREPORTER_SHUTDOWN=1"]
    )
    assert status == 245

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_FAILURE
    assert log_level == ERROR

    crash = filter_action("crash", lines)
    assert len(crash) == 0

    process_output = filter_action("process_output", lines)
    assert any("ERROR: AddressSanitizer" in l["data"] for l in process_output)


@pytest.mark.skip_mozinfo("!debug")
def test_output_assertion(runtests):
    status, lines = runtests("reftest-assert.list")
    assert status == 0

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_WARNING
    assert log_level == WARNING

    test_status = filter_action("test_status", lines)
    assert len(test_status) == 1
    assert test_status[0]["status"] == "PASS"

    test_end = filter_action("test_end", lines)
    assert len(test_end) == 1
    assert test_end[0]["status"] == "PASS"

    assertions = filter_action("assertion_count", lines)
    assert len(assertions) == 1
    assert assertions[0]["count"] == 1


@pytest.mark.skip_mozinfo("!debug")
def test_output_leak(monkeypatch, runtests):
    # Monkeypatch mozleak so we always process a failing leak log
    # instead of the actual one.
    import mozleak

    old_process_leak_log = mozleak.process_leak_log

    def process_leak_log(*args, **kwargs):
        return old_process_leak_log(
            os.path.join(here, "files", "leaks.log"), *args[1:], **kwargs
        )

    monkeypatch.setattr("mozleak.process_leak_log", process_leak_log)

    status, lines = runtests("reftest-pass.list")
    assert status == 0

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_WARNING
    assert log_level == WARNING

    leaks = filter_action("mozleak_total", lines)
    assert len(leaks) == 1
    assert leaks[0]["process"] == "default"
    assert leaks[0]["bytes"] == 19915
    assert leaks[0]["scope"]


if __name__ == "__main__":
    mozunit.main()
