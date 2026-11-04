# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import multiprocessing
import os
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import mozunit
import pytest

try:
    import psutil
except ImportError:
    psutil = None

from mozsystemmonitor.resourcemonitor import (
    FORWARD_ENV,
    SystemResourceMonitor,
    SystemResourceUsage,
    _parse_hg_source_url,
    _process_sampling_interval,
)

UBSAN_FILE = "/builds/worker/checkouts/gecko/third_party/llama.cpp/ggml/src/ggml-c.c"
UBSAN_MESSAGE = "applying non-zero offset 96 to null pointer"
UBSAN_LINES = [
    f"{UBSAN_FILE}:7106:33: runtime error: {UBSAN_MESSAGE}",
    f"    #0 0x7b417858d37c in incr_ptr_aligned {UBSAN_FILE}:7106:33",
    "    #1 0x7f42401b0a33 in clone misc/../sysdeps/unix/sysv/linux/x86_64/clone.S:100",
    f"SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior {UBSAN_FILE}:7106:33",
]


# Monitors created by the tests shouldn't forward to the one running the tests
# in automation.
@pytest.fixture(autouse=True)
def no_forwarding(monkeypatch):
    monkeypatch.delenv(FORWARD_ENV, raising=False)


def _wait_for_measurement(monitor, timeout=30):
    """Wait for the first measurement, which takes a while when the
    collecting process is spawned rather than forked, e.g. on macOS."""
    deadline = time.monotonic() + timeout
    while not monitor.measurements:
        assert time.monotonic() < deadline, "no measurement received"
        monitor._drain_pipe()
        time.sleep(0.05)


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestResourceMonitor(unittest.TestCase):
    def test_basic(self):
        monitor = SystemResourceMonitor(poll_interval=0.5)

        monitor.start()
        time.sleep(3)

        monitor.stop()

        data = list(monitor.range_usage())
        self.assertGreater(len(data), 3)

        self.assertIsInstance(data[0], SystemResourceUsage)

    def test_empty(self):
        monitor = SystemResourceMonitor(poll_interval=2.0)
        monitor.start()
        monitor.stop()

        data = list(monitor.range_usage())
        self.assertEqual(len(data), 0)

    def test_phases(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(1)

        with monitor.phase("phase1"):
            time.sleep(1)

            with monitor.phase("phase2"):
                time.sleep(1)

        monitor.stop()

        self.assertEqual(len(monitor.phases), 2)
        self.assertEqual(["phase2", "phase1"], list(monitor.phases.keys()))

        all = list(monitor.range_usage())
        data1 = list(monitor.phase_usage("phase1"))
        data2 = list(monitor.phase_usage("phase2"))

        self.assertGreater(len(all), len(data1))
        self.assertGreater(len(data1), len(data2))

        # This could fail if time.monotonic() takes more than 0.1s. It really
        # shouldn't.
        self.assertAlmostEqual(data1[-1].end, data2[-1].end, delta=0.25)

    def test_no_data(self):
        monitor = SystemResourceMonitor()

        data = list(monitor.range_usage())
        self.assertEqual(len(data), 0)

    def test_events(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(0.5)

        t0 = time.monotonic()
        monitor.record_event("t0")
        time.sleep(2)

        monitor.record_event("t1")
        time.sleep(0.5)
        monitor.stop()

        events = monitor.events
        self.assertEqual(len(events), 2)

        event = events[0]

        self.assertEqual(event[1], "t0")
        self.assertAlmostEqual(event[0], t0, delta=0.25)

        data = list(monitor.between_events_usage("t0", "t1"))
        self.assertGreater(len(data), 0)

    def test_aggregate_cpu(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        _wait_for_measurement(monitor)
        time.sleep(1)
        monitor.stop()

        values = monitor.aggregate_cpu_percent()
        self.assertIsInstance(values, list)
        self.assertEqual(len(values), multiprocessing.cpu_count())
        for v in values:
            self.assertIsInstance(v, float)

        value = monitor.aggregate_cpu_percent(per_cpu=False)
        self.assertIsInstance(value, float)

        values = monitor.aggregate_cpu_times()
        self.assertIsInstance(values, list)
        self.assertGreater(len(values), 0)
        self.assertTrue(hasattr(values[0], "user"))

        t = type(values[0])

        value = monitor.aggregate_cpu_times(per_cpu=False)
        self.assertIsInstance(value, t)

    def test_aggregate_io(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        # There's really no easy way to ensure I/O occurs. For all we know
        # reads and writes will all be serviced by the page cache.
        monitor.start()
        time.sleep(1.0)
        monitor.stop()

        values = monitor.aggregate_io()
        self.assertTrue(hasattr(values, "read_count"))

    def test_memory(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(1.0)
        monitor.stop()

        v = monitor.min_memory_available()
        self.assertIsInstance(v, int)

        v = monitor.max_memory_percent()
        self.assertIsInstance(v, float)

    def test_lsan_events(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(0.1)

        # Direct leak with a stack and an allow-list match -> yellow.
        SystemResourceMonitor.lsan_leak({
            "time": int(time.time() * 1000),
            "kind": "Direct",
            "bytes": 128,
            "objects": 2,
            "stack": [{"function": "Foo::Bar"}, {"function": "main"}],
            "scope": "browser/foo",
            "allowed_match": "Foo::Bar",
        })

        # Indirect leak without allow-list match -> orange, no stack/scope.
        SystemResourceMonitor.lsan_leak({
            "time": int(time.time() * 1000),
            "kind": "Indirect",
            "bytes": 32,
            "objects": 1,
        })

        # Allow-listed summary -> yellow.
        SystemResourceMonitor.lsan_summary({
            "time": int(time.time() * 1000),
            "bytes": 160,
            "allocations": 3,
            "allowed": True,
        })

        time.sleep(0.1)
        monitor.stop()

        leak_events = [e for e in monitor.events if e[1] == "LSan Leak"]
        summary_events = [e for e in monitor.events if e[1] == "LSan Summary"]
        self.assertEqual(len(leak_events), 2)
        self.assertEqual(len(summary_events), 1)

        direct, indirect = leak_events[0][2], leak_events[1][2]
        self.assertEqual(direct["type"], "LSanLeak")
        self.assertEqual(direct["kind"], "Direct")
        self.assertEqual(direct["bytes"], 128)
        self.assertEqual(direct["objects"], 2)
        self.assertEqual(direct["scope"], "browser/foo")
        self.assertEqual(direct["allowed_match"], "Foo::Bar")
        self.assertEqual(direct["color"], "yellow")
        self.assertEqual(direct["stack"][0]["function"], "Foo::Bar")

        self.assertEqual(indirect["kind"], "Indirect")
        self.assertEqual(indirect["color"], "orange")
        self.assertNotIn("stack", indirect)
        self.assertNotIn("scope", indirect)
        self.assertNotIn("allowed_match", indirect)

        summary = summary_events[0][2]
        self.assertEqual(summary["type"], "LSanSummary")
        self.assertEqual(summary["bytes"], 160)
        self.assertEqual(summary["allocations"], 3)
        self.assertEqual(summary["color"], "yellow")
        self.assertTrue(summary["allowed"])

        markers = monitor.as_profile()["threads"][0]["markers"]["data"]
        self.assertTrue(any(m.get("type") == "LSanLeak" for m in markers))
        self.assertTrue(any(m.get("type") == "LSanSummary" for m in markers))

    def test_tsan_error(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(0.1)

        now = int(time.time() * 1000)
        # A lock-order inversion with two labeled acquisition stacks: it should
        # produce two markers sharing a report_index, each carrying one stack.
        SystemResourceMonitor.tsan_error({
            "time": now,
            "kind": "lock-order-inversion (potential deadlock)",
            "signature": "Mutex_posix.cpp:91:3 in mutexLock",
            "pid": 1234,
            "description": "Cycle in lock order graph: M0 => M1 => M0",
            "scope": "browser/foo",
            "stacks": [
                {
                    "label": "Mutex M1 acquired here while holding mutex M0",
                    "stack": [
                        {
                            "function": "mutexLock",
                            "module": "firefox",
                            "file": "/builds/worker/checkouts/gecko/mozglue/misc/Mutex_posix.cpp",
                            "line": 91,
                        },
                        {"function": "main", "module": "firefox"},
                    ],
                },
                {
                    "label": "Mutex M0 acquired here while holding mutex M1",
                    "stack": [{"function": "other", "module": "libxul.so"}],
                },
            ],
        })

        # A report with no stacks should still produce a single marker.
        SystemResourceMonitor.tsan_error({
            "time": now,
            "kind": "data race",
            "signature": "Activation.cpp:16 in registerProfiling",
            "stacks": [],
        })

        time.sleep(0.1)
        monitor.stop()

        events = [e[2] for e in monitor.events if e[1] == "TSan Error"]
        # 2 markers for the lock-order report + 1 for the stackless data race.
        self.assertEqual(len(events), 3)

        lock_order = [e for e in events if e["kind"].startswith("lock-order-inversion")]
        self.assertEqual(len(lock_order), 2)
        first, second = lock_order
        self.assertEqual(first["type"], "TSanError")
        self.assertEqual(first["color"], "orange")
        self.assertEqual(first["pid"], 1234)
        self.assertEqual(first["scope"], "browser/foo")
        # Both markers of one report share a report_index.
        self.assertEqual(first["report_index"], second["report_index"])
        self.assertNotEqual(first["label"], second["label"])
        self.assertEqual(first["stack"][0]["function"], "mutexLock")
        # checkouts/gecko paths are rewritten to a repo-relative/hg view.
        self.assertNotIn("/builds/worker/checkouts/gecko/", first["stack"][0]["file"])

        data_race = [e for e in events if e["kind"] == "data race"]
        self.assertEqual(len(data_race), 1)
        self.assertNotIn("stack", data_race[0])
        # The two reports get distinct report_index values.
        self.assertNotEqual(first["report_index"], data_race[0]["report_index"])

        markers = monitor.as_profile()["threads"][0]["markers"]["data"]
        self.assertTrue(any(m.get("type") == "TSanError" for m in markers))

    def _ubsan_action(self, message=UBSAN_MESSAGE, stack=None, **extra):
        data = {
            "time": int(time.time() * 1000),
            "kind": "undefined-behavior",
            "message": message,
            "file": UBSAN_FILE,
            "lineno": 7106,
            "column": 33,
        }
        if stack is not None:
            data["stack"] = stack
        data.update(extra)
        SystemResourceMonitor.ubsan_error(data)

    def test_ubsan_error(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        self._ubsan_action(
            scope="browser/foo",
            test="test_foo",
            stack=[
                {
                    "function": "incr_ptr_aligned",
                    "file": UBSAN_FILE,
                    "line": 7106,
                    "column": 33,
                },
                {"function": "clone", "module": "libc.so.6", "module_offset": 0x1000},
            ],
        )
        # A report without location or stack still produces a marker.
        SystemResourceMonitor.ubsan_error({
            "time": int(time.time() * 1000),
            "kind": "undefined-behavior",
            "message": "division by zero",
        })
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(len(events), 2)
        first, second = events[0][2], events[1][2]
        self.assertEqual(first["type"], "UBSanError")
        self.assertEqual(first["color"], "orange")
        self.assertEqual(first["kind"], "undefined-behavior")
        self.assertEqual(first["message"], UBSAN_MESSAGE)
        self.assertEqual(first["file"], "third_party/llama.cpp/ggml/src/ggml-c.c")
        self.assertEqual(first["line"], 7106)
        self.assertEqual(first["column"], 33)
        self.assertEqual(first["scope"], "browser/foo")
        self.assertEqual(first["test"], "test_foo")
        self.assertEqual(
            first["stack"][0]["file"],
            f"hg:hg.mozilla.org/try:third_party/llama.cpp/ggml/src/ggml-c.c:{rev}",
        )
        self.assertNotIn("file", first["stack"][1])
        self.assertEqual(second["message"], "division by zero")
        self.assertNotIn("file", second)
        self.assertNotIn("stack", second)

        markers = monitor.as_profile()["threads"][0]["markers"]["data"]
        self.assertEqual(len([m for m in markers if m.get("type") == "UBSanError"]), 2)

    def test_ubsan_error_without_stack_points_at_the_location(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._ubsan_action()
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(
            events[0][2]["stack"],
            [{"file": "third_party/llama.cpp/ggml/src/ggml-c.c", "line": 7106}],
        )

    def test_process_output_ubsan_report(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        for line in UBSAN_LINES:
            self._process_output(monitor, line, process="GECKO(1717)")
        self._process_output(monitor, "unrelated output", process="GECKO(1717)")
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(len(events), 1)
        data = events[0][2]
        self.assertEqual(data["type"], "UBSanError")
        self.assertEqual(data["message"], UBSAN_MESSAGE)
        self.assertEqual(data["file"], "third_party/llama.cpp/ggml/src/ggml-c.c")
        self.assertEqual(data["line"], 7106)
        self.assertEqual(data["column"], 33)
        self.assertEqual(
            [f["function"] for f in data["stack"]], ["incr_ptr_aligned", "clone"]
        )
        self.assertEqual(
            data["stack"][0]["file"], "third_party/llama.cpp/ggml/src/ggml-c.c"
        )
        # The report lines are folded into the marker; only the unrelated
        # line becomes a generic output marker.
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_ubsan_error_action_supersedes_line_report(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        # Harnesses feed their parser before logging each line, so the
        # action arrives after the header and frames but before SUMMARY.
        for line in UBSAN_LINES[:3]:
            self._process_output(monitor, line, process="GECKO(1717)")
        action_stack = [
            {"function": "incr_ptr_aligned", "file": UBSAN_FILE, "line": 7106},
            {"function": "ggml_graph_nbytes", "file": UBSAN_FILE, "line": 7113},
            {"function": "clone"},
        ]
        self._ubsan_action(stack=action_stack)
        self._process_output(monitor, UBSAN_LINES[3], process="GECKO(1717)")
        self._process_output(monitor, "unrelated output", process="GECKO(1717)")
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(len(events[0][2]["stack"]), 3)
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_ubsan_line_report_completing_after_action_is_dropped(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._ubsan_action()
        for line in UBSAN_LINES:
            self._process_output(monitor, line, process="GECKO(1717)")
        self.assertEqual(len(self._events(monitor, "UBSan Error")), 1)

        # A second identical report is a new one.
        for line in UBSAN_LINES:
            self._process_output(monitor, line, process="GECKO(1717)")
        monitor.stop()

        self.assertEqual(len(self._events(monitor, "UBSan Error")), 2)
        self.assertEqual(len(self._events(monitor, "output")), 0)

    def test_process_output_ubsan_flushed_on_stop(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        for line in UBSAN_LINES[:2]:
            self._process_output(monitor, line, process="GECKO(1717)")
        self.assertEqual(len(self._events(monitor, "UBSan Error")), 0)
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(len(events[0][2]["stack"]), 1)

    def test_process_output_ubsan_keyed_by_process(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        other = [
            "/src/a.c:12:3: runtime error: division by zero",
            "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /src/a.c:12:3",
        ]
        self._process_output(monitor, UBSAN_LINES[0], process="A")
        self._process_output(monitor, other[0], process="B")
        self._process_output(monitor, UBSAN_LINES[1], process="A")
        self._process_output(monitor, other[1], process="B")
        for line in UBSAN_LINES[2:]:
            self._process_output(monitor, line, process="A")
        monitor.stop()

        events = self._events(monitor, "UBSan Error")
        self.assertEqual(
            [e[2]["message"] for e in events], ["division by zero", UBSAN_MESSAGE]
        )
        self.assertEqual(len(events[1][2]["stack"]), 2)
        self.assertEqual(len(self._events(monitor, "output")), 0)

    def test_process_output_ubsan_without_moztest(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor._ubsan_parser = None
        monitor.start()
        for line in UBSAN_LINES:
            self._process_output(monitor, line, process="GECKO(1717)")
        monitor.stop()

        self.assertEqual(len(self._events(monitor, "UBSan Error")), 0)
        self.assertEqual(len(self._events(monitor, "output")), 4)

    def test_as_profile(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(0.1)
        monitor.begin_phase("phase1")
        monitor.record_event("foo")
        time.sleep(0.1)
        monitor.begin_phase("phase2")
        monitor.record_event("bar")
        time.sleep(0.2)
        monitor.finish_phase("phase1")
        time.sleep(0.2)
        monitor.finish_phase("phase2")
        time.sleep(0.4)
        monitor.stop()

        d = monitor.as_profile()

        self.assertEqual(len(d["threads"]), 1)
        self.assertIn("markers", d["threads"][0])
        self.assertIn("data", d["threads"][0]["markers"])
        markers = d["threads"][0]["markers"]["data"]
        self.assertTrue(
            any(m["type"] == "Phase" and m["phase"] == "phase1" for m in markers)
        )
        self.assertTrue(
            any(m["type"] == "Phase" and m["phase"] == "phase2" for m in markers)
        )
        self.assertIn({"type": "Text", "text": "foo"}, markers)
        self.assertIn({"type": "Text", "text": "bar"}, markers)

    def _test_status(self, monitor, **kwargs):
        data = {
            "action": "test_status",
            "test": "test_foo.js",
            "time": (time.monotonic() - monitor.start_time) * 1000
            + monitor.start_timestamp * 1000,
        }
        data.update(kwargs)
        SystemResourceMonitor.test_status(data)

    def test_status_expectations(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        # mozlog drops "expected" when it matches "status", so an expected
        # failure arrives without it.
        self._test_status(monitor, status="FAIL", message="a todo")
        self._test_status(monitor, status="FAIL", expected="PASS", message="a failure")
        self._test_status(monitor, status="PASS", message="a pass")
        self._test_status(
            monitor, status="PASS", expected="FAIL", message="a todo that passed"
        )
        self._test_status(
            monitor,
            status="FAIL",
            message="a failure that will be retried",
            extra={"will_retry": True},
        )
        monitor.stop()

        by_message = {
            data["message"]: (name, data)
            for _, name, data in monitor.events
            if data and data.get("type") == "TestStatus"
        }
        self.assertEqual(len(by_message), 5)

        name, data = by_message["a todo"]
        self.assertEqual(name, "KNOWN-FAIL")
        self.assertEqual(data["color"], "yellow")

        name, data = by_message["a failure"]
        self.assertEqual(name, "FAIL")
        self.assertEqual(data["color"], "orange")

        name, data = by_message["a pass"]
        self.assertEqual(name, "PASS")
        self.assertEqual(data["color"], "green")

        name, data = by_message["a todo that passed"]
        self.assertEqual(name, "UNEXPECTED-PASS")
        self.assertEqual(data["color"], "orange")

        name, data = by_message["a failure that will be retried"]
        self.assertEqual(name, "FAIL")
        self.assertEqual(data["color"], "orange")

    def _process_output(self, monitor, line, process=None):
        data = {
            "action": "process_output",
            "data": line,
            "time": (time.monotonic() - monitor.start_time) * 1000
            + monitor.start_timestamp * 1000,
        }
        if process is not None:
            data["process"] = process
        SystemResourceMonitor.test_status(data)

    def _events(self, monitor, name):
        return [e for e in monitor.events if len(e) == 3 and e[1] == name]

    def test_process_output_docshell(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "[Child 4208: Main Thread]: I/DocShellAndDOMWindowLeak "
            "++DOCSHELL 2f804b00 == 2 [pid = 4208] [id = 37]",
        )
        time.sleep(0.05)
        self._process_output(
            monitor,
            "[Child 4208: Main Thread]: I/DocShellAndDOMWindowLeak "
            "--DOCSHELL 2f804b00 == 0 [pid = 4208] [id = 37] "
            "[url = about:aichatcontent]",
        )
        monitor.stop()

        docshell = [m for m in monitor.markers if m[0] == "DocShell"]
        self.assertEqual(len(docshell), 1)
        name, start, end, data, _ = docshell[0]
        self.assertEqual(data["type"], "DocShell")
        self.assertEqual(data["url"], "about:aichatcontent")
        self.assertEqual(data["id"], 37)
        self.assertEqual(data["pid"], 4208)
        self.assertEqual(data["process"], "Child")
        self.assertEqual(data["thread"], "Main Thread")
        self.assertEqual(data["pointer"], "2f804b00")
        self.assertLess(start, end)

    def test_process_output_domwindow(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "[Child 3444: Main Thread]: I/DocShellAndDOMWindowLeak "
            "++DOMWINDOW == 2 (b3bc400) [pid = 3444] [serial = 2] "
            "[outer = 4f60940]",
        )
        time.sleep(0.05)
        self._process_output(
            monitor,
            "[Child 3444: Main Thread]: I/DocShellAndDOMWindowLeak "
            "--DOMWINDOW == 1 (b3bc400) [pid = 3444] [serial = 2] "
            "[outer = 4f60940] [url = about:blank]",
        )
        monitor.stop()

        windows = [m for m in monitor.markers if m[0] == "DOMWindow"]
        self.assertEqual(len(windows), 1)
        _, start, end, data, _ = windows[0]
        self.assertEqual(data["type"], "DOMWindow")
        self.assertEqual(data["url"], "about:blank")
        self.assertEqual(data["serial"], 2)
        self.assertEqual(data["pointer"], "b3bc400")
        self.assertEqual(data["outer"], "4f60940")
        self.assertLess(start, end)

    def test_process_output_javascript_error(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "JavaScript error: chrome://browser/content/places/browserPlacesViews.js,"
            " line 118: Error: No DOM node set for aPlacesNode.",
        )
        self._process_output(
            monitor,
            "JavaScript warning: resource://gre/foo.js, line 12: deprecation warning",
        )
        monitor.stop()

        errors = [
            e for e in monitor.events if len(e) == 3 and e[1] == "JavaScript error"
        ]
        warns = [
            e for e in monitor.events if len(e) == 3 and e[1] == "JavaScript warning"
        ]
        self.assertEqual(len(errors), 1)
        self.assertEqual(len(warns), 1)
        _, _, data = errors[0]
        self.assertEqual(data["type"], "jsError")
        self.assertNotIn("level", data)
        self.assertNotIn("color", data)
        self.assertEqual(
            data["file"], "chrome://browser/content/places/browserPlacesViews.js"
        )
        self.assertEqual(data["line"], 118)
        self.assertIn("No DOM node set", data["message"])
        self.assertEqual(data["stack"][0]["is_js"], True)

    def test_process_output_cpp_warning(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        # Both ": file" and ", file" separator forms occur in practice.
        self._process_output(
            monitor,
            "[Parent 72612, Main Thread] WARNING: NS_ENSURE_TRUE(uri) failed: "
            "file caps/BasePrincipal.cpp:1511",
        )
        self._process_output(
            monitor,
            "[Child 19675, Main Thread] WARNING: 'NS_FAILED(rv)', "
            "file checkouts/gecko/xpcom/threads/nsThreadUtils.cpp:238",
        )
        monitor.stop()

        warnings_ = [e for e in monitor.events if len(e) == 3 and e[1] == "C++ warning"]
        self.assertEqual(len(warnings_), 2)
        _, _, data = warnings_[0]
        self.assertEqual(data["type"], "cppDebug")
        self.assertEqual(data["message"], "NS_ENSURE_TRUE(uri) failed")
        self.assertEqual(data["file"], "caps/BasePrincipal.cpp")
        self.assertEqual(data["line"], 1511)
        self.assertEqual(data["pid"], 72612)
        self.assertEqual(data["process"], "Parent")
        self.assertEqual(data["thread"], "Main Thread")
        self.assertNotIn("color", data)
        # Comma-separator form preserves the quoted message verbatim.
        _, _, data = warnings_[1]
        self.assertEqual(data["message"], "'NS_FAILED(rv)'")

    # NS_DebugBreak rings the bell before the assertion text.
    NS_ASSERTION_LINE = (
        "\x07[Parent 5900, Main Thread] ###!!! ASSERTION: Out-of-flow frame got "
        "reflowed before its placeholder: 'Error', file "
        "layout/generic/nsPlaceholderFrame.cpp:131"
    )
    NS_ASSERTION_FRAMES = (
        "#01: NS_DebugBreak(unsigned int, char const*, char const*, char const*, int) "
        "[xpcom/base/nsDebugImpl.cpp:516]",
        "#02: nsPlaceholderFrame::Reflow(nsPresContext*) "
        "[layout/generic/nsPlaceholderFrame.cpp:131]",
    )
    MOZ_ASSERT_LINE = (
        "[6136] Assertion failure: false (unreachable), at "
        "/builds/worker/checkouts/gecko/dom/workers/RuntimeService.cpp:2229"
    )

    def test_process_output_cpp_assertion(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, self.NS_ASSERTION_LINE)
        for frame in self.NS_ASSERTION_FRAMES:
            self._process_output(monitor, frame)
        self._process_output(monitor, "totally random output line that we don't parse")
        monitor.stop()

        asserts = self._events(monitor, "NS_ASSERTION")
        self.assertEqual(len(asserts), 1)
        _, _, data = asserts[0]
        self.assertEqual(data["type"], "AssertionFailure")
        self.assertEqual(data["kind"], "NS_ASSERTION")
        self.assertEqual(data["color"], "orange")
        self.assertNotIn("fatal", data)
        self.assertIn("Out-of-flow frame", data["message"])
        self.assertEqual(data["file"], "layout/generic/nsPlaceholderFrame.cpp")
        self.assertEqual(data["line"], 131)
        self.assertEqual(data["pid"], 5900)
        self.assertEqual(data["process"], "Parent")
        self.assertEqual(data["thread"], "Main Thread")
        self.assertEqual(
            [f["function"] for f in data["stack"]],
            [
                "NS_DebugBreak(unsigned int, char const*, char const*, char const*, int)",
                "nsPlaceholderFrame::Reflow(nsPresContext*)",
            ],
        )
        self.assertEqual(data["stack"][1]["line"], 131)
        # The frames were folded into the assertion, only the trailing line
        # is a generic output marker.
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_process_output_assertion_flushed_on_stop(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, self.MOZ_ASSERT_LINE)
        self._process_output(monitor, "#01: f() [dom/workers/RuntimeService.cpp:2229]")
        self.assertEqual(self._events(monitor, "MOZ_ASSERT"), [])
        monitor.stop()

        asserts = self._events(monitor, "MOZ_ASSERT")
        self.assertEqual(len(asserts), 1)
        _, _, data = asserts[0]
        self.assertEqual(data["color"], "red")
        self.assertTrue(data["fatal"])
        self.assertEqual(data["file"], "dom/workers/RuntimeService.cpp")
        self.assertEqual(data["pid"], 6136)
        self.assertNotIn("process", data)
        self.assertEqual(
            data["stack"],
            [
                {
                    "function": "f()",
                    "file": "dom/workers/RuntimeService.cpp",
                    "line": 2229,
                }
            ],
        )

    def test_process_output_xpcshell_assertion_without_frames(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "[123, Main Thread] ###!!! ASSERTION: boom: 'expr', file xpcom/a.cpp:7",
        )
        self._process_output(monitor, "unrelated")
        monitor.stop()

        asserts = self._events(monitor, "NS_ASSERTION")
        self.assertEqual(len(asserts), 1)
        _, _, data = asserts[0]
        self.assertEqual(data["pid"], 123)
        self.assertNotIn("process", data)
        # Without frames the source location still gives the marker a cause.
        self.assertEqual(data["stack"], [{"file": "xpcom/a.cpp", "line": 7}])

    def test_process_output_assertions_keyed_by_process(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, self.MOZ_ASSERT_LINE, process="A")
        self._process_output(monitor, self.NS_ASSERTION_LINE, process="B")
        self._process_output(monitor, "#01: a() [a.cpp:1]", process="A")
        self._process_output(monitor, "#01: b() [b.cpp:1]", process="B")
        monitor.stop()

        moz_assert = self._events(monitor, "MOZ_ASSERT")[0][2]
        ns_assertion = self._events(monitor, "NS_ASSERTION")[0][2]
        self.assertEqual([f["function"] for f in moz_assert["stack"]], ["a()"])
        self.assertEqual([f["function"] for f in ns_assertion["stack"]], ["b()"])

    def test_process_output_cpp_assertion_without_moztest(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor._assertion_parser = None
        monitor.start()
        self._process_output(monitor, self.NS_ASSERTION_LINE)
        monitor.stop()

        asserts = self._events(monitor, "C++ assertion")
        self.assertEqual(len(asserts), 1)
        _, _, data = asserts[0]
        self.assertEqual(data["type"], "cppDebug")
        self.assertEqual(data["color"], "red")
        self.assertEqual(data["line"], 131)

    def _assertion_failure(self, message="x", stack=None, **extra):
        SystemResourceMonitor.assertion_failure({
            "time": int(time.time() * 1000),
            "kind": "MOZ_ASSERT",
            "message": message,
            "file": "/builds/worker/checkouts/gecko/dom/workers/RuntimeService.cpp",
            "lineno": 2229,
            "fatal": True,
            "stack": stack,
            **extra,
        })

    def test_assertion_failure_action(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        self._assertion_failure(
            message="false (unreachable)",
            pid=6136,
            process_type="Child",
            thread="DOM Worker",
            test="dom/workers/test/test_bar.html",
            stack=[
                {
                    "function": "Run()",
                    "file": "/builds/worker/checkouts/gecko/dom/workers/RuntimeService.cpp",
                    "line": 2229,
                },
                {"module": "libxul.so", "module_offset": 0x43A0},
            ],
        )
        monitor.stop()

        asserts = self._events(monitor, "MOZ_ASSERT")
        self.assertEqual(len(asserts), 1)
        _, _, data = asserts[0]
        self.assertEqual(data["type"], "AssertionFailure")
        self.assertEqual(data["kind"], "MOZ_ASSERT")
        self.assertEqual(data["color"], "red")
        self.assertTrue(data["fatal"])
        self.assertEqual(data["message"], "false (unreachable)")
        self.assertEqual(data["file"], "dom/workers/RuntimeService.cpp")
        self.assertEqual(data["line"], 2229)
        self.assertEqual(data["pid"], 6136)
        self.assertEqual(data["process"], "Child")
        self.assertEqual(data["thread"], "DOM Worker")
        self.assertEqual(data["test"], "dom/workers/test/test_bar.html")
        self.assertEqual(
            data["stack"][0]["file"],
            f"hg:hg.mozilla.org/try:dom/workers/RuntimeService.cpp:{rev}",
        )
        self.assertEqual(
            data["stack"][1], {"module": "libxul.so", "module_offset": 0x43A0}
        )

        markers = monitor.as_profile()["threads"][0]["markers"]["data"]
        marker = next(m for m in markers if m.get("type") == "AssertionFailure")
        self.assertIn("cause", marker)

    def test_assertion_failure_action_supersedes_line_report(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, self.MOZ_ASSERT_LINE)
        self._process_output(monitor, "#01: f() [dom/workers/RuntimeService.cpp:2229]")
        # The harness parser emits the action when it sees the terminating
        # line, before that line is logged.
        self._assertion_failure(
            message="false (unreachable)",
            stack=[{"function": "g()", "file": "dom/a.cpp", "line": 1}],
        )
        self._process_output(monitor, "terminating line")
        monitor.stop()

        asserts = self._events(monitor, "MOZ_ASSERT")
        self.assertEqual(len(asserts), 1)
        self.assertEqual([f["function"] for f in asserts[0][2]["stack"]], ["g()"])
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_assertion_failure_action_with_other_key_keeps_both(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, self.MOZ_ASSERT_LINE)
        self._process_output(monitor, "#01: f() [dom/workers/RuntimeService.cpp:2229]")
        self._assertion_failure(message="something else")
        self._process_output(monitor, "terminating line")
        monitor.stop()

        messages = sorted(e[2]["message"] for e in self._events(monitor, "MOZ_ASSERT"))
        self.assertEqual(messages, ["false (unreachable)", "something else"])

    def test_line_report_completing_after_action_is_dropped(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._assertion_failure(message="false (unreachable)")
        self._process_output(monitor, self.MOZ_ASSERT_LINE)
        self._process_output(monitor, "#01: f() [dom/workers/RuntimeService.cpp:2229]")
        self._process_output(monitor, "terminating line")
        # The same assertion firing again is a new event.
        self._process_output(monitor, self.MOZ_ASSERT_LINE)
        self._process_output(monitor, "terminating line")
        monitor.stop()

        asserts = self._events(monitor, "MOZ_ASSERT")
        self.assertEqual(len(asserts), 2)
        self.assertIsNone(asserts[0][2].get("stack", [{}])[0].get("function"))
        self.assertEqual(
            asserts[1][2]["stack"],
            [{"file": "dom/workers/RuntimeService.cpp", "line": 2229}],
        )

    def test_process_output_console(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            'console.error: (new Error("Unable to retrieve the translation models.",'
            ' "resource://gre/actors/TranslationsParent.sys.mjs", 2674))',
        )
        self._process_output(
            monitor, 'console.warn: "No view for invalid view, switching to default"'
        )
        self._process_output(
            monitor, "console.log: Downloads: Closing the downloads panel."
        )
        monitor.stop()

        names = sorted(e[1] for e in monitor.events if len(e) == 3)
        self.assertEqual(names, ["console.error", "console.log", "console.warn"])
        log_event = next(
            e for e in monitor.events if len(e) == 3 and e[1] == "console.log"
        )
        self.assertEqual(log_event[2]["type"], "console")
        self.assertEqual(
            log_event[2]["message"], "Downloads: Closing the downloads panel."
        )

    def test_process_output_docshell_unmatched(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "[Child 4208: Main Thread]: I/DocShellAndDOMWindowLeak "
            "++DOCSHELL 2f804b00 == 2 [pid = 4208] [id = 37]",
        )
        # No matching --DOCSHELL: simulates a process that crashed or
        # never tore down its docshells before profiling stopped.
        monitor.stop()

        docshell = [m for m in monitor.markers if m[0] == "DocShell"]
        self.assertEqual(len(docshell), 1)
        _, start, end, data, _ = docshell[0]
        self.assertIsNone(end)
        self.assertEqual(data["id"], 37)
        self.assertEqual(data["pid"], 4208)
        self.assertEqual(data["pointer"], "2f804b00")
        self.assertNotIn("url", data)

    def test_process_output_console_trace_with_stack(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor, 'console.trace: AboutHomeStartupCache: "Preloaded was updated."'
        )
        self._process_output(
            monitor,
            "resource:///modules/AboutHomeStartupCache.sys.mjs 775 onPreloadedNewTabMessage",
        )
        self._process_output(
            monitor,
            "resource://newtab/lib/ActivityStreamMessageChannel.sys.mjs 79 middleware/</<",
        )
        # A second console.trace flushes the first.
        self._process_output(
            monitor, 'console.trace: AboutHomeStartupCache: "Preloaded was updated."'
        )
        self._process_output(
            monitor, "resource:///modules/AboutHomeStartupCache.sys.mjs 775 onFoo"
        )
        monitor.stop()

        traces = [e for e in monitor.events if len(e) == 3 and e[1] == "console.trace"]
        self.assertEqual(len(traces), 2)
        _, _, data = traces[0]
        self.assertEqual(data["type"], "console")
        self.assertEqual(len(data["stack"]), 2)
        self.assertEqual(
            data["stack"][0]["file"],
            "resource:///modules/AboutHomeStartupCache.sys.mjs",
        )
        self.assertEqual(data["stack"][0]["line"], 775)
        self.assertEqual(data["stack"][0]["function"], "onPreloadedNewTabMessage")
        self.assertEqual(data["stack"][1]["function"], "middleware/</<")
        # The second trace was flushed at stop with one frame collected.
        self.assertEqual(len(traces[1][2]["stack"]), 1)

    def test_process_output_console_trace_flushed_by_other_line(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.trace: foo")
        self._process_output(monitor, "some/file.mjs 42 doStuff")
        # Non-frame line should flush the pending trace and be processed normally.
        self._process_output(monitor, "totally random output line")
        monitor.stop()

        traces = [e for e in monitor.events if len(e) == 3 and e[1] == "console.trace"]
        self.assertEqual(len(traces), 1)
        self.assertEqual(len(traces[0][2]["stack"]), 1)
        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_process_output_console_multiline_error(self):
        """console.error from Console.sys.mjs's createMultiLineDumper produces an
        empty header followed by indented Message:/Stack: lines and JS frames.
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error: ")
        self._process_output(
            monitor, "  Message: Error: Cannot attach ID to a tab in a closed window."
        )
        self._process_output(monitor, "  Stack:")
        self._process_output(
            monitor, "    setId@chrome://browser/content/parent/ext-browser.js:387:13"
        )
        self._process_output(
            monitor, "getId@chrome://browser/content/parent/ext-browser.js:363:10"
        )
        self._process_output(
            monitor, "wrapTab@chrome://browser/content/parent/ext-browser.js:1293:58"
        )
        # An unrelated line flushes the pending marker and is processed normally.
        self._process_output(monitor, "totally random output line")
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        data = errors[0][2]
        self.assertEqual(data["type"], "console")
        self.assertEqual(
            data["message"], "Error: Cannot attach ID to a tab in a closed window."
        )
        self.assertEqual(len(data["stack"]), 3)
        self.assertEqual(data["stack"][0]["function"], "setId")
        self.assertEqual(
            data["stack"][0]["file"],
            "chrome://browser/content/parent/ext-browser.js",
        )
        self.assertEqual(data["stack"][0]["line"], 387)
        self.assertEqual(data["stack"][0]["column"], 13)
        self.assertTrue(data["stack"][0]["is_js"])
        self.assertEqual(data["stack"][2]["function"], "wrapTab")
        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_process_output_console_multiline_no_trailing_space(self):
        """The header line may or may not carry a trailing space depending on
        how the harness rstrips lines; both shapes start the multi-line body.
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error:")
        self._process_output(monitor, "  Message: Boom")
        self._process_output(monitor, "  Stack:")
        self._process_output(monitor, "    foo@resource:///x.sys.mjs:10:1")
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        data = errors[0][2]
        self.assertEqual(data["message"], "Boom")
        self.assertEqual(len(data["stack"]), 1)

    def test_process_output_console_multiline_flushed_at_stop(self):
        """A multi-line console body without follow-up still gets flushed."""
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error: ")
        self._process_output(monitor, "  Message: Boom")
        # No Stack: follow-up; stop() must still emit the marker.
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        self.assertEqual(errors[0][2]["message"], "Boom")
        self.assertNotIn("stack", errors[0][2])

    def test_process_output_console_multiline_no_message(self):
        """When the line after the header isn't 'Message:', we flush an empty
        marker and let the next line be processed normally.
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error: ")
        self._process_output(monitor, "totally random output line")
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_process_output_console_multiline_with_prefix(self):
        """Console.sys.mjs's createConsole({prefix}) prepends "<prefix>: " to
        the header, so the multi-line body opens with "console.<m>: <prefix>:"
        rather than just "console.<m>: ".
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error: services.settings:")
        self._process_output(
            monitor,
            '  Message: EmptyDatabaseError: "main/nimbus-desktop-experiments"'
            " has not been synced yet",
        )
        self._process_output(monitor, "  Stack:")
        self._process_output(
            monitor,
            "    EmptyDatabaseError@resource://services-settings/Database.sys.mjs:19:5",
        )
        self._process_output(
            monitor,
            "async*get@resource://services-settings/RemoteSettingsClient.sys.mjs:573:28",
        )
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        data = errors[0][2]
        # The prefix is stitched back in front of the Message: text.
        self.assertEqual(
            data["message"],
            'services.settings: EmptyDatabaseError: "main/nimbus-desktop-experiments"'
            " has not been synced yet",
        )
        self.assertEqual(len(data["stack"]), 2)
        self.assertEqual(data["stack"][0]["function"], "EmptyDatabaseError")
        self.assertEqual(data["stack"][1]["function"], "async*get")

    def test_process_output_console_speculative_falls_back_to_single_line(self):
        """A multi-line method whose body happens to end with ":" but isn't a
        multi-line header still emits a single-line marker, and the next line
        is processed normally.
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.error: oops, ends in colon:")
        self._process_output(monitor, "totally random output line")
        monitor.stop()

        errors = [e for e in monitor.events if len(e) == 3 and e[1] == "console.error"]
        self.assertEqual(len(errors), 1)
        self.assertEqual(errors[0][2]["message"], "oops, ends in colon:")
        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_process_output_console_warn_empty_is_single_line(self):
        """console.warn uses createDumper(), not the multi-line dumper, so an
        empty body must NOT trigger the multi-line state machine.
        """
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "console.warn: ")
        self._process_output(monitor, "  Message: not a follow-up")
        monitor.stop()

        warns = [e for e in monitor.events if len(e) == 3 and e[1] == "console.warn"]
        self.assertEqual(len(warns), 1)
        # The "  Message: ..." line wasn't consumed by a multi-line body and
        # should fall back to a generic output marker.
        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_process_output_unrecognized_falls_back(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(monitor, "totally random output line that we don't parse")
        monitor.stop()

        outputs = [e for e in monitor.events if len(e) == 3 and e[1] == "output"]
        self.assertEqual(len(outputs), 1)

    def test_cpp_warning_frame_uses_hg_source_url(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        self._process_output(
            monitor,
            "[Parent 100, Main Thread] WARNING: oops: file "
            "/builds/worker/workspace/obj-build/foo/./../../../../../checkouts/gecko/"
            "netwerk/protocol/http/Http2Compression.cpp:42",
        )
        monitor.stop()

        warnings_ = [e for e in monitor.events if len(e) == 3 and e[1] == "C++ warning"]
        self.assertEqual(len(warnings_), 1)
        _, _, data = warnings_[0]
        self.assertEqual(data["file"], "netwerk/protocol/http/Http2Compression.cpp")
        self.assertEqual(
            data["stack"][0]["file"],
            f"hg:hg.mozilla.org/try:netwerk/protocol/http/Http2Compression.cpp:{rev}",
        )

    def test_cpp_warning_frame_no_source_url(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor,
            "[Parent 100, Main Thread] WARNING: oops: file "
            "/builds/worker/workspace/obj-build/foo/./../../../../../checkouts/gecko/"
            "netwerk/protocol/http/Http2Compression.cpp:42",
        )
        monitor.stop()

        warnings_ = [e for e in monitor.events if len(e) == 3 and e[1] == "C++ warning"]
        self.assertEqual(len(warnings_), 1)
        _, _, data = warnings_[0]
        self.assertEqual(data["file"], "netwerk/protocol/http/Http2Compression.cpp")
        self.assertEqual(
            data["stack"][0]["file"], "netwerk/protocol/http/Http2Compression.cpp"
        )

    def test_cpp_warning_frame_sysroot_path_not_wrapped(self):
        # Even with a sourceURL set, a path that isn't repo-relative (sysroot,
        # fetches, rust stdlib) must pass through unchanged so the source view
        # doesn't try to fetch it from hg.mozilla.org and 404.
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        sysroot = (
            "/builds/worker/fetches/sysroot-x86_64-linux-gnu/usr/lib/gcc/"
            "x86_64-linux-gnu/10/../../../../include/c++/10/bits/std_function.h"
        )
        self._process_output(
            monitor,
            f"[Parent 100, Main Thread] WARNING: oops: file {sysroot}:42",
        )
        monitor.stop()

        warnings_ = [e for e in monitor.events if len(e) == 3 and e[1] == "C++ warning"]
        self.assertEqual(len(warnings_), 1)
        _, _, data = warnings_[0]
        self.assertEqual(data["file"], sysroot)
        self.assertEqual(data["stack"][0]["file"], sysroot)


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestCleanFrameFile(unittest.TestCase):
    def _monitor(self, source_url=None):
        metadata = {"sourceURL": source_url} if source_url else {}
        return SystemResourceMonitor(poll_interval=0.25, metadata=metadata)

    def test_ci_path_no_hg_prefix(self):
        self.assertEqual(
            self._monitor()._clean_frame_file(
                "/builds/worker/workspace/obj-build/netwerk/protocol/http/./../../../"
                "../../checkouts/gecko/netwerk/protocol/http/Http2Compression.cpp"
            ),
            (
                "netwerk/protocol/http/Http2Compression.cpp",
                "netwerk/protocol/http/Http2Compression.cpp",
            ),
        )

    def test_ci_path_with_hg_prefix(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        self.assertEqual(
            self._monitor(f"https://hg.mozilla.org/try/rev/{rev}")._clean_frame_file(
                "/builds/worker/workspace/obj-build/netwerk/protocol/http/./../../../"
                "../../checkouts/gecko/netwerk/protocol/http/Http2Compression.cpp"
            ),
            (
                "netwerk/protocol/http/Http2Compression.cpp",
                f"hg:hg.mozilla.org/try:netwerk/protocol/http/Http2Compression.cpp:{rev}",
            ),
        )

    def test_path_without_marker_unchanged(self):
        self.assertEqual(
            self._monitor()._clean_frame_file("/home/user/firefox/foo/bar.cpp"),
            ("/home/user/firefox/foo/bar.cpp", "/home/user/firefox/foo/bar.cpp"),
        )

    def test_sysroot_path_not_wrapped_even_with_hg_prefix(self):
        # Sysroot/fetches paths must not be wrapped as "hg:..." URLs; the
        # source view would 404 trying to fetch them from hg.mozilla.org.
        rev = "deadbeef"
        sysroot = (
            "/builds/worker/fetches/sysroot-x86_64-linux-gnu/usr/lib/gcc/"
            "x86_64-linux-gnu/10/../../../../include/c++/10/bits/std_function.h"
        )
        monitor = self._monitor(f"https://hg.mozilla.org/try/rev/{rev}")
        self.assertEqual(monitor._clean_frame_file(sysroot), (sysroot, sysroot))

    def test_last_occurrence_wins(self):
        self.assertEqual(
            self._monitor()._clean_frame_file(
                "/x/checkouts/gecko/y/../../checkouts/gecko/foo/bar.cpp"
            ),
            ("foo/bar.cpp", "foo/bar.cpp"),
        )

    def test_falsy_passthrough(self):
        monitor = self._monitor()
        self.assertEqual(monitor._clean_frame_file(""), ("", ""))
        self.assertEqual(monitor._clean_frame_file(None), (None, None))


class TestParseHgSourceUrl(unittest.TestCase):
    def test_try(self):
        self.assertEqual(
            _parse_hg_source_url(
                "https://hg.mozilla.org/try/rev/56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
            ),
            (
                "hg:hg.mozilla.org/try:",
                "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22",
            ),
        )

    def test_mozilla_central(self):
        self.assertEqual(
            _parse_hg_source_url(
                "https://hg.mozilla.org/mozilla-central/rev/abcdef0123456789"
            ),
            ("hg:hg.mozilla.org/mozilla-central:", "abcdef0123456789"),
        )

    def test_multi_segment_repo(self):
        self.assertEqual(
            _parse_hg_source_url(
                "https://hg.mozilla.org/integration/autoland/rev/abcdef0123456789"
            ),
            ("hg:hg.mozilla.org/integration/autoland:", "abcdef0123456789"),
        )
        self.assertEqual(
            _parse_hg_source_url(
                "https://hg.mozilla.org/releases/mozilla-beta/rev/abcdef0123456789"
            ),
            ("hg:hg.mozilla.org/releases/mozilla-beta:", "abcdef0123456789"),
        )

    def test_non_hg_returns_none(self):
        self.assertEqual(
            _parse_hg_source_url("https://github.com/mozilla/gecko-dev/commit/abc"),
            (None, None),
        )

    def test_empty_returns_none(self):
        self.assertEqual(_parse_hg_source_url(None), (None, None))
        self.assertEqual(_parse_hg_source_url(""), (None, None))


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestLsanLeakFrameRewrite(unittest.TestCase):
    def _record_lsan_leak(self, monitor, stack):
        SystemResourceMonitor.lsan_leak({
            "kind": "Direct",
            "bytes": 16,
            "objects": 1,
            "stack": stack,
            "time": int((time.time() - monitor.start_timestamp) * 1000),
        })

    def test_lsan_leak_rewrites_frame_files_with_source_url(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        original_stack = [
            {
                "function": "Foo",
                "file": "/builds/worker/workspace/obj-build/./../../../../../"
                "checkouts/gecko/xpcom/ds/nsAtomTable.cpp",
                "line": 90,
            },
            {"function": "Bar", "module": "libxul.so"},
        ]
        self._record_lsan_leak(monitor, original_stack)
        monitor.stop()

        leaks = [e for e in monitor.events if len(e) == 3 and e[1] == "LSan Leak"]
        self.assertEqual(len(leaks), 1)
        _, _, data = leaks[0]
        self.assertEqual(
            data["stack"][0]["file"],
            f"hg:hg.mozilla.org/try:xpcom/ds/nsAtomTable.cpp:{rev}",
        )
        self.assertNotIn("file", data["stack"][1])
        # Original frame dicts must not be mutated.
        self.assertEqual(
            original_stack[0]["file"],
            "/builds/worker/workspace/obj-build/./../../../../../"
            "checkouts/gecko/xpcom/ds/nsAtomTable.cpp",
        )

    def test_lsan_leak_empty_stack(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._record_lsan_leak(monitor, [])
        monitor.stop()

        leaks = [e for e in monitor.events if len(e) == 3 and e[1] == "LSan Leak"]
        self.assertEqual(len(leaks), 1)
        _, _, data = leaks[0]
        self.assertNotIn("stack", data)


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestForwarding(unittest.TestCase):
    def setUp(self):
        fd, self.forward_path = tempfile.mkstemp()
        os.close(fd)
        self.addCleanup(os.unlink, self.forward_path)

    def _child(self):
        with mock.patch.dict(os.environ, {FORWARD_ENV: self.forward_path}):
            return SystemResourceMonitor(poll_interval=0.1)

    def test_forwarding(self):
        parent = SystemResourceMonitor(poll_interval=0.1)
        self.assertFalse(parent.forwarding)
        parent.start()
        parent.accept_forwarded(self.forward_path)

        child = self._child()
        self.assertTrue(child.forwarding)
        child.start()
        self.assertEqual(child.measurements, [])
        child.record_event("event", data={"type": "Text", "text": "data"})
        start = time.monotonic()
        child.record_marker("marker", start, start + 0.1, "text")
        with child.phase("phase"):
            time.sleep(0.3)
        child.forward_perfherder("suite", ["option"], "/upload")
        child.stop()
        parent.stop()

        self.assertEqual(len(parent.events), 1)
        self.assertEqual(
            parent.events[0][1:], ("event", {"type": "Text", "text": "data"})
        )
        self.assertEqual(
            parent.markers[0],
            ("marker", start, start + 0.1, "text", SystemResourceMonitor.TASK_CATEGORY),
        )
        self.assertEqual(list(parent.phases), ["phase"])
        self.assertEqual(len(parent.perfherder_requests), 1)
        name, options, req_start, req_end, upload_dir, source = (
            parent.perfherder_requests[0]
        )
        self.assertEqual((name, options, upload_dir), ("suite", ["option"], "/upload"))
        self.assertEqual(req_start, child.start_time)
        self.assertEqual(source, os.getpid())

        data = parent.perfherder_data(name, options, req_start, req_end, source)
        self.assertEqual(data["framework"], {"name": "job_resource_usage"})
        self.assertEqual(
            [suite["name"] for suite in data["suites"]],
            ["suite.overall", "suite.phase"],
        )
        self.assertEqual(data["suites"][0]["extraOptions"], ["option"])

    def test_forwarded_phase_name_conflict(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        with monitor.phase("phase"):
            pass
        monitor._add_phase("phase", 1.0, 2.0, 1)
        monitor._add_phase("phase", 3.0, 4.0, 2)
        monitor.stop()
        self.assertEqual(monitor.phases["phase (2)"], (1.0, 2.0))
        self.assertEqual(monitor.phases["phase (3)"], (3.0, 4.0))

        data = monitor.perfherder_data("suite", [], 1.0, 2.0, 1)
        self.assertEqual(
            [suite["name"] for suite in data["suites"]],
            ["suite.overall", "suite.phase"],
        )

    def test_forwarded_phase_before_local_phase(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        monitor._add_phase("phase", 1.0, 2.0, 1)
        with monitor.phase("phase"):
            pass
        monitor.stop()
        self.assertEqual(monitor.phases["phase"], (1.0, 2.0))
        self.assertIn("phase (2)", monitor.phases)

        data = monitor.perfherder_data("suite", [])
        self.assertEqual(
            [suite["name"] for suite in data["suites"]],
            ["suite.overall", "suite.phase"],
        )
        self.assertNotEqual(data["suites"][1]["subtests"][0]["value"], 1.0)

    def test_perfherder_data_source_phases(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        start = time.monotonic()
        monitor._add_phase("outer", start, start + 0.3, 1)
        monitor._add_phase("inner", start + 0.1, start + 0.2, 2)
        monitor.stop()

        for source, phase in ((1, "outer"), (2, "inner")):
            data = monitor.perfherder_data("suite", [], start, start + 0.3, source)
            self.assertEqual(
                [suite["name"] for suite in data["suites"]],
                ["suite.overall", f"suite.{phase}"],
            )
        data = monitor.perfherder_data("suite", [])
        self.assertEqual([suite["name"] for suite in data["suites"]], ["suite.overall"])

    def test_concurrent_forwarding(self):
        parent = SystemResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)
        _run_forwarding_children(self.forward_path)
        parent.stop()

        self.assertEqual(len([m for m in parent.markers if m[0] == "marker"]), 2000)

    def test_accept_forwarded_not_running(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.accept_forwarded(self.forward_path)
        self.assertIsNone(monitor._from_children_path)

    def test_forward_after_stop(self):
        child = self._child()
        child.start()
        child.stop()
        with self.assertWarns(UserWarning):
            child.forward_perfherder("suite", [])

    def test_children_forward_path(self):
        parent = SystemResourceMonitor(poll_interval=0.1)
        self.assertIsNone(parent.children_forward_path)
        parent.start()
        parent.accept_forwarded(self.forward_path)
        self.assertEqual(parent.children_forward_path, self.forward_path)
        parent.stop()

        self.assertEqual(self._child().children_forward_path, self.forward_path)

    def test_forward_to(self):
        parent = SystemResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)

        child = SystemResourceMonitor(forward_to=self.forward_path)
        self.assertTrue(child.forwarding)
        child.start()
        start = time.monotonic()
        child.record_marker("marker", start, start + 0.1, "text")
        child.stop()
        parent.stop()

        self.assertEqual(
            parent.markers[0],
            ("marker", start, start + 0.1, "text", SystemResourceMonitor.TASK_CATEGORY),
        )

    @unittest.skipUnless(hasattr(os, "fork"), "Requires fork.")
    def test_forked_child_forgets_monitor(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        _wait_for_measurement(monitor)
        pid = os.fork()
        if pid == 0:
            forgotten = SystemResourceMonitor.instance is None and monitor._pipe.closed
            monitor._atexit_stop()
            # Hold the lock as the parent's drain timer could have when forking.
            monitor._pipe_lock.acquire()
            monitor.stop()
            os._exit(0 if forgotten else 1)
        _, status = os.waitpid(pid, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 0)
        child_exit = time.monotonic()
        time.sleep(0.5)
        self.assertIs(SystemResourceMonitor.instance, monitor)
        monitor.stop()
        self.assertGreater(monitor.measurements[-1].end, child_exit)

    def _artifact_filenames(self, monitor):
        return sorted(
            data["filename"] for _, name, data in monitor.events if name == "artifact"
        )

    def test_forwarded_artifacts_in_parent_upload_dir(self):
        with tempfile.TemporaryDirectory() as upload_dir:
            open(os.path.join(upload_dir, "file"), "w").close()
            parent = SystemResourceMonitor(poll_interval=0.1)
            parent.start()
            parent.accept_forwarded(self.forward_path)

            child = self._child()
            child.start()
            child.stop(upload_dir=upload_dir)
            parent.stop(upload_dir=upload_dir)

            self.assertEqual(self._artifact_filenames(parent), ["file"])

    def test_forwarded_artifacts_in_other_upload_dir(self):
        with tempfile.TemporaryDirectory() as upload_dir:
            with tempfile.TemporaryDirectory() as child_upload_dir:
                open(os.path.join(upload_dir, "parent"), "w").close()
                open(os.path.join(child_upload_dir, "child"), "w").close()
                parent = SystemResourceMonitor(poll_interval=0.1)
                parent.start()
                parent.accept_forwarded(self.forward_path)

                child = self._child()
                child.start()
                child.stop(upload_dir=child_upload_dir)
                parent.stop(upload_dir=upload_dir)

                self.assertEqual(self._artifact_filenames(parent), ["child", "parent"])

    def test_forwarded_artifacts_moved_out_of_parent_upload_dir(self):
        with tempfile.TemporaryDirectory() as upload_dir:
            path = os.path.join(upload_dir, "file")
            open(path, "w").close()
            parent = SystemResourceMonitor(poll_interval=0.1)
            parent.start()
            parent.accept_forwarded(self.forward_path)

            child = self._child()
            child.start()
            child.stop(upload_dir=upload_dir)
            os.unlink(path)
            parent.stop(upload_dir=upload_dir)

            self.assertEqual(self._artifact_filenames(parent), ["file"])

    def test_forwarded_sccache_log_parsed_once(self):
        with tempfile.TemporaryDirectory() as upload_dir:
            open(os.path.join(upload_dir, "sccache.log"), "w").close()
            parent = SystemResourceMonitor(poll_interval=0.1)
            parent.start()
            parent.accept_forwarded(self.forward_path)

            with mock.patch.object(
                SystemResourceMonitor, "_parse_sccache_log"
            ) as parse_sccache_log:
                child = self._child()
                child.start()
                child.stop(upload_dir=upload_dir)
                parent.stop(upload_dir=upload_dir)

            self.assertEqual(parse_sccache_log.call_count, 1)
            self.assertEqual(self._artifact_filenames(parent), ["sccache.log"])

    def test_partial_line(self):
        parent = SystemResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)
        with open(self.forward_path, "a") as fh:
            fh.write('["event", "complete", 1.0, null]\n["event", "incompl')
        parent._read_forwarded()
        self.assertEqual(parent.events, [(1.0, "complete")])
        with open(self.forward_path, "a") as fh:
            fh.write('ete", 2.0, null]\n')
        parent.stop()
        self.assertEqual(parent.events, [(1.0, "complete"), (2.0, "incomplete")])

    def test_perfherder_data_all_phases(self):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        with monitor.phase("phase1"):
            time.sleep(0.3)
        monitor.stop()

        data = monitor.perfherder_data("suite", [])
        self.assertEqual(
            [suite["name"] for suite in data["suites"]],
            ["suite.overall", "suite.phase1"],
        )
        overall = {s["name"] for s in data["suites"][0]["subtests"]}
        self.assertLessEqual(
            {"io_write_bytes", "io.read_bytes", "io_write_time", "io_read_time"},
            overall,
        )


def _run_forwarding_children(forward_path):
    script = (
        "import time\n"
        "from mozsystemmonitor.resourcemonitor import SystemResourceMonitor\n"
        "m = SystemResourceMonitor()\n"
        "m.start()\n"
        "for i in range(500):\n"
        "    t = time.monotonic()\n"
        "    m.record_marker('marker', t, t, 'x' * 200)\n"
        "m.stop()\n"
    )
    env = dict(os.environ, **{FORWARD_ENV: forward_path})
    env["PYTHONPATH"] = os.pathsep.join(sys.path)
    children = [
        subprocess.Popen([sys.executable, "-c", script], env=env) for _ in range(4)
    ]
    for child in children:
        assert child.wait() == 0


def test_concurrent_forwarding_lines(tmp_path):
    # Forwarding children don't need psutil, so this also runs where the
    # parent can't monitor.
    forward_path = tmp_path / "forward.jsonl"
    forward_path.touch()
    _run_forwarding_children(str(forward_path))

    data = forward_path.read_bytes()
    assert data.endswith(b"\n")
    records = [json.loads(line) for line in data.splitlines()]
    assert len(records) == 2000
    assert all(record[0] == "marker" for record in records)


@pytest.mark.parametrize(
    "value,expected",
    [
        (None, None),
        ("", None),
        ("0", None),
        ("2.5", 2.5),
        ("yes", 0.1),
    ],
)
def test_process_sampling_interval(value, expected):
    env = {} if value is None else {"MOZ_PROCESS_SAMPLING": value}
    with mock.patch.dict(os.environ, env):
        if value is None:
            os.environ.pop("MOZ_PROCESS_SAMPLING", None)
        assert _process_sampling_interval(0.1) == expected


@pytest.mark.skipif(psutil is None, reason="Resource monitor requires psutil.")
def test_process_sampling_with_interval():
    with mock.patch.dict(os.environ, {"MOZ_PROCESS_SAMPLING": "0.5"}):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        _wait_for_measurement(monitor)
        subprocess.run([sys.executable, "-c", "import time; time.sleep(2)"], check=True)
        time.sleep(0.6)
        monitor.stop()
    cmds = [cmd for pid, start, end, cmd, ppid in monitor.processes]
    assert any("time.sleep(2)" in cmd for cmd in cmds), cmds


@pytest.mark.skipif(psutil is None, reason="Resource monitor requires psutil.")
def test_process_sampling_still_running():
    sleep = [sys.executable, "-c", "import time; time.sleep(60)"]
    before = subprocess.Popen(sleep + ["before"])
    try:
        with mock.patch.dict(os.environ, {"MOZ_PROCESS_SAMPLING": "0.1"}):
            monitor = SystemResourceMonitor(poll_interval=0.1)
            monitor.start()
            # Let the monitor take its first sample, which is what it
            # considers to be running before it started.
            _wait_for_measurement(monitor)
            during = subprocess.Popen(sleep + ["during"])
            try:
                time.sleep(1)
                monitor.stop()
            finally:
                during.kill()
                during.wait()
    finally:
        before.kill()
        before.wait()
    cmds = [cmd for pid, start, end, cmd, ppid in monitor.processes]
    assert any(cmd.endswith(" during") for cmd in cmds), cmds
    assert not any(cmd.endswith(" before") for cmd in cmds), cmds


@pytest.mark.skipif(psutil is None, reason="Resource monitor requires psutil.")
@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="Linux only")
def test_process_sampling_start_time():
    with mock.patch.dict(os.environ, {"MOZ_PROCESS_SAMPLING": "0.1"}):
        monitor = SystemResourceMonitor(poll_interval=0.1)
        monitor.start()
        _wait_for_measurement(monitor)
        before = time.monotonic()
        child = subprocess.Popen([
            sys.executable,
            "-c",
            "import time; time.sleep(60)",
            "start-time",
        ])
        after = time.monotonic()
        try:
            time.sleep(1)
            monitor.stop()
        finally:
            child.kill()
            child.wait()
    starts = [
        start for pid, start, end, cmd, ppid in monitor.processes if pid == child.pid
    ]
    assert len(starts) == 1, monitor.processes
    # /proc/<pid>/stat has a resolution of one clock tick, usually 10ms, and
    # converting between clocks adds a little imprecision.
    assert before - 0.02 <= starts[0] <= after + 0.01, (before, starts[0], after)


if __name__ == "__main__":
    mozunit.main()
