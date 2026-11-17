# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import tempfile
import time
import unittest

import mozunit
import pytest

try:
    import psutil
except ImportError:
    psutil = None

from mozsystemmonitor.basemonitor import FORWARD_ENV, BaseResourceMonitor
from mozsystemmonitor.resourcemonitor import (
    SystemResourceMonitor,
    _parse_hg_source_url,
)

UBSAN_FILE = "/builds/worker/checkouts/gecko/third_party/llama.cpp/ggml/src/ggml-c.c"
UBSAN_MESSAGE = "applying non-zero offset 96 to null pointer"
UBSAN_LINES = [
    f"{UBSAN_FILE}:7106:33: runtime error: {UBSAN_MESSAGE}",
    f"    #0 0x7b417858d37c in incr_ptr_aligned {UBSAN_FILE}:7106:33",
    "    #1 0x7f42401b0a33 in clone misc/../sysdeps/unix/sysv/linux/x86_64/clone.S:100",
    f"SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior {UBSAN_FILE}:7106:33",
]

ASAN_FILE = "/builds/worker/checkouts/gecko/dom/base/SpeculationRules.cpp"
ASAN_FUNCTION = "mozilla::dom::SpeculationRules::RemoveLink(mozilla::dom::Element*)"
ASAN_MESSAGE = (
    "SEGV on unknown address 0x00000000030b "
    "(pc 0x7b03e9dd40af bp 0x7fff627fb7f0 sp 0x7fff627fb7e0 T0)"
)
ASAN_LINES = [
    f"==28130==ERROR: AddressSanitizer: {ASAN_MESSAGE}",
    "==28130==The signal is caused by a READ memory access.",
    f"    #0 0x7b03e9dd40af in {ASAN_FUNCTION} {ASAN_FILE}:385:7",
    "    #1 0x561bbfeecf08 in _start (/builds/worker/workspace/build/application/firefox/firefox+0xc1f08)",
    f"SUMMARY: AddressSanitizer: SEGV {ASAN_FILE}:385:7 in {ASAN_FUNCTION}",
    "==28130==ABORTING",
]


# Monitors created by the tests shouldn't forward to the one running the tests
# in automation.
@pytest.fixture(autouse=True)
def no_forwarding(monkeypatch):
    monkeypatch.delenv(FORWARD_ENV, raising=False)


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestResourceMonitor(unittest.TestCase):
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

    def _asan_action(self, message=ASAN_MESSAGE, stack=None, **extra):
        data = {
            "time": int(time.time() * 1000),
            "kind": "SEGV",
            "message": message,
            "pid": 28130,
            "file": ASAN_FILE,
            "lineno": 385,
            "column": 7,
            "function": ASAN_FUNCTION,
        }
        if stack is not None:
            data["stack"] = stack
        data.update(extra)
        SystemResourceMonitor.asan_error(data)

    def test_asan_error(self):
        rev = "56b3cc68b5e7557a3e13fca984f0f8aebc60dd22"
        monitor = SystemResourceMonitor(
            poll_interval=0.25,
            metadata={"sourceURL": f"https://hg.mozilla.org/try/rev/{rev}"},
        )
        monitor.start()
        self._asan_action(
            scope="/css/selectors",
            test="/css/selectors/a.html",
            stack=[
                {
                    "function": ASAN_FUNCTION,
                    "file": ASAN_FILE,
                    "line": 385,
                    "column": 7,
                },
                {"function": "_start", "module": "firefox", "module_offset": 0xC1F08},
            ],
            stacks=[
                {
                    "label": "freed by thread T0 here",
                    "stack": [{"function": "free", "file": ASAN_FILE, "line": 12}],
                }
            ],
        )
        # A report of a run that will be retried is a warning; one without
        # location or stack still produces a marker.
        SystemResourceMonitor.asan_error({
            "time": int(time.time() * 1000),
            "kind": "double-free",
            "message": "attempting double-free on 0x1 in thread T0:",
            "quiet": True,
        })
        monitor.stop()

        events = self._events(monitor, "ASan Error")
        self.assertEqual(len(events), 2)
        first, second = events[0][2], events[1][2]
        self.assertEqual(first["type"], "ASanError")
        self.assertEqual(first["color"], "red")
        self.assertEqual(first["kind"], "SEGV")
        self.assertEqual(first["message"], ASAN_MESSAGE)
        self.assertEqual(first["pid"], 28130)
        self.assertEqual(first["file"], "dom/base/SpeculationRules.cpp")
        self.assertEqual(first["line"], 385)
        self.assertEqual(first["column"], 7)
        self.assertEqual(first["function"], ASAN_FUNCTION)
        self.assertEqual(first["location"], "dom/base/SpeculationRules.cpp:385")
        self.assertEqual(first["scope"], "/css/selectors")
        self.assertEqual(first["test"], "/css/selectors/a.html")
        self.assertEqual(
            first["stack"][0]["file"],
            f"hg:hg.mozilla.org/try:dom/base/SpeculationRules.cpp:{rev}",
        )
        self.assertNotIn("file", first["stack"][1])
        self.assertEqual(first["stacks"][0]["label"], "freed by thread T0 here")
        self.assertEqual(
            first["stacks"][0]["stack"],
            [
                {
                    "function": "free",
                    "file": f"hg:hg.mozilla.org/try:dom/base/SpeculationRules.cpp:{rev}",
                    "line": 12,
                }
            ],
        )
        self.assertEqual(second["color"], "orange")
        self.assertEqual(second["kind"], "double-free")
        self.assertEqual(
            second["location"], "attempting double-free on 0x1 in thread T0:"
        )
        self.assertNotIn("pid", second)
        self.assertNotIn("file", second)
        self.assertNotIn("stack", second)
        self.assertNotIn("stacks", second)

        markers = monitor.as_profile()["threads"][0]["markers"]["data"]
        self.assertEqual(len([m for m in markers if m.get("type") == "ASanError"]), 2)

    def test_asan_error_without_stack_points_at_the_location(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._asan_action()
        monitor.stop()

        events = self._events(monitor, "ASan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(
            events[0][2]["stack"],
            [{"file": "dom/base/SpeculationRules.cpp", "line": 385}],
        )

    def test_process_output_asan_report(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._process_output(
            monitor, "AddressSanitizer:DEADLYSIGNAL", process="GECKO(19529)"
        )
        for line in ASAN_LINES[:2]:
            self._process_output(monitor, line, process="GECKO(19529)")
        # Another process's output interleaves while the stack is symbolized.
        self._process_output(
            monitor,
            "TEST-OK | /css/selectors/a.html | took 1ms",
            process="GECKO(19529)",
        )
        for line in ASAN_LINES[2:]:
            self._process_output(monitor, line, process="GECKO(19529)")
        monitor.stop()

        events = self._events(monitor, "ASan Error")
        self.assertEqual(len(events), 1)
        data = events[0][2]
        self.assertEqual(data["type"], "ASanError")
        self.assertEqual(data["color"], "red")
        self.assertEqual(data["kind"], "SEGV")
        self.assertEqual(data["message"], ASAN_MESSAGE)
        self.assertEqual(data["pid"], 28130)
        self.assertEqual(data["file"], "dom/base/SpeculationRules.cpp")
        self.assertEqual(data["line"], 385)
        self.assertEqual(data["column"], 7)
        self.assertEqual(data["function"], ASAN_FUNCTION)
        self.assertEqual(
            [f["function"] for f in data["stack"]], [ASAN_FUNCTION, "_start"]
        )
        self.assertEqual(data["stack"][0]["file"], "dom/base/SpeculationRules.cpp")
        # The report lines are folded into the marker; only the interleaved
        # line becomes a generic output marker.
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_asan_error_action_supersedes_line_report(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        # Harnesses feed their parser before logging each line, so the
        # action arrives after the header and frames but before SUMMARY.
        for line in ASAN_LINES[:4]:
            self._process_output(monitor, line, process="GECKO(19529)")
        action_stack = [
            {"function": "GetInnerWindow", "file": ASAN_FILE, "line": 1942},
            {"function": ASAN_FUNCTION, "file": ASAN_FILE, "line": 385},
            {"function": "_start"},
        ]
        self._asan_action(stack=action_stack)
        for line in ASAN_LINES[4:]:
            self._process_output(monitor, line, process="GECKO(19529)")
        self._process_output(monitor, "unrelated output", process="GECKO(19529)")
        monitor.stop()

        events = self._events(monitor, "ASan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(len(events[0][2]["stack"]), 3)
        self.assertEqual(len(self._events(monitor, "output")), 1)

    def test_asan_line_report_completing_after_action_is_dropped(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._asan_action()
        for line in ASAN_LINES:
            self._process_output(monitor, line, process="GECKO(19529)")
        self.assertEqual(len(self._events(monitor, "ASan Error")), 1)

        # A second identical report is a new one.
        for line in ASAN_LINES:
            self._process_output(monitor, line, process="GECKO(19529)")
        monitor.stop()

        self.assertEqual(len(self._events(monitor, "ASan Error")), 2)
        self.assertEqual(len(self._events(monitor, "output")), 0)

    def test_process_output_asan_flushed_on_stop(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        for line in ASAN_LINES[:3]:
            self._process_output(monitor, line, process="GECKO(19529)")
        self.assertEqual(len(self._events(monitor, "ASan Error")), 0)
        monitor.stop()

        events = self._events(monitor, "ASan Error")
        self.assertEqual(len(events), 1)
        self.assertEqual(len(events[0][2]["stack"]), 1)
        self.assertNotIn("file", events[0][2])
        self.assertEqual(events[0][2]["location"], ASAN_FUNCTION)

    def test_process_output_asan_without_mozleak(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor._asan_parser = None
        monitor.start()
        for line in ASAN_LINES:
            self._process_output(monitor, line, process="GECKO(19529)")
        monitor.stop()

        self.assertEqual(len(self._events(monitor, "ASan Error")), 0)
        self.assertEqual(len(self._events(monitor, "output")), len(ASAN_LINES))

    def test_process_output_lsan_summary_is_not_an_asan_error(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        for line in (
            "==1689==ERROR: LeakSanitizer: detected memory leaks",
            "SUMMARY: AddressSanitizer: 488 byte(s) leaked in 3 allocation(s).",
        ):
            self._process_output(monitor, line, process="GECKO(1689)")
        monitor.stop()

        self.assertEqual(len(self._events(monitor, "ASan Error")), 0)
        self.assertEqual(len(self._events(monitor, "output")), 2)

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
            will_retry=True,
        )
        self._test_status(
            monitor,
            status="FAIL",
            expected="PASS",
            known_intermittent=["FAIL"],
            message="a known intermittent",
        )
        self._test_status(
            monitor, status="TIMEOUT", expected="PASS", message="a subtest timeout"
        )
        self._test_status(
            monitor, action="log", level="WARNING", message="a harness warning"
        )
        monitor.stop()

        by_message = {
            data["message"]: (name, data)
            for _, name, data in monitor.events
            if data and data.get("type") == "TestStatus"
        }
        self.assertEqual(len(by_message), 8)

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

        name, data = by_message["a known intermittent"]
        self.assertEqual(name, "KNOWN-INTERMITTENT-FAIL")
        self.assertEqual(data["color"], "yellow")

        name, data = by_message["a subtest timeout"]
        self.assertEqual(name, "TIMEOUT")
        self.assertEqual(data["color"], "orange")

        name, data = by_message["a harness warning"]
        self.assertEqual(name, "WARNING")
        self.assertNotIn("color", data)

    def _record_time(self, monitor):
        return (
            time.monotonic() - monitor.start_time
        ) * 1000 + monitor.start_timestamp * 1000

    def _begin_test(self, monitor, test, **kwargs):
        data = {
            "action": "test_start",
            "test": test,
            "time": self._record_time(monitor),
        }
        data.update(kwargs)
        SystemResourceMonitor.begin_test(data)

    def _end_test(self, monitor, test, **kwargs):
        data = {"action": "test_end", "test": test, "time": self._record_time(monitor)}
        data.update(kwargs)
        SystemResourceMonitor.end_test(data)

    def _test_markers(self, monitor):
        return [data for name, _, _, data, _ in monitor.markers if name == "test"]

    def test_end_test_colors(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._begin_test(monitor, "ok.html")
        self._test_status(monitor, test="ok.html", subtest="a", status="PASS")
        self._end_test(monitor, "ok.html", status="OK")
        self._begin_test(monitor, "subtest_fail.html")
        self._test_status(monitor, test="subtest_fail.html", subtest="a", status="PASS")
        self._test_status(
            monitor,
            test="subtest_fail.html",
            subtest="b",
            status="FAIL",
            expected="PASS",
            message="assert_equals failed",
        )
        self._end_test(monitor, "subtest_fail.html", status="OK")
        self._begin_test(monitor, "subtest_intermittent.html")
        self._test_status(
            monitor,
            test="subtest_intermittent.html",
            subtest="a",
            status="FAIL",
            expected="PASS",
            known_intermittent=["FAIL"],
        )
        self._end_test(monitor, "subtest_intermittent.html", status="OK")
        self._begin_test(monitor, "timeout.html")
        self._end_test(
            monitor,
            "timeout.html",
            status="TIMEOUT",
            expected="OK",
            message="Test timed out",
        )
        self._begin_test(monitor, "unexpected_fail.html")
        self._end_test(monitor, "unexpected_fail.html", status="FAIL", expected="PASS")
        self._begin_test(monitor, "expected_fail.html")
        self._end_test(monitor, "expected_fail.html", status="FAIL")
        self._begin_test(monitor, "intermittent.html")
        self._end_test(
            monitor,
            "intermittent.html",
            status="FAIL",
            expected="PASS",
            known_intermittent=["FAIL"],
        )
        self._begin_test(monitor, "retry.js")
        self._end_test(
            monitor,
            "retry.js",
            status="FAIL",
            message="Test failed or timed out, will retry",
            will_retry=True,
        )
        self._begin_test(monitor, "crash.html")
        self._end_test(monitor, "crash.html", status="CRASH", expected="OK")
        monitor.stop()

        by_test = {data["test"]: data for data in self._test_markers(monitor)}
        self.assertEqual(len(by_test), 9)
        self.assertEqual(by_test["ok.html"]["color"], "green")
        self.assertNotIn("unexpected_subtests", by_test["ok.html"])
        self.assertEqual(by_test["subtest_fail.html"]["color"], "orange")
        self.assertEqual(by_test["subtest_fail.html"]["status"], "OK")
        self.assertEqual(by_test["subtest_fail.html"]["unexpected_subtests"], 1)
        self.assertEqual(by_test["subtest_intermittent.html"]["color"], "yellow")
        self.assertNotIn("unexpected_subtests", by_test["subtest_intermittent.html"])
        self.assertEqual(by_test["timeout.html"]["color"], "yellow")
        self.assertEqual(by_test["timeout.html"]["message"], "Test timed out")
        self.assertEqual(by_test["unexpected_fail.html"]["color"], "orange")
        self.assertEqual(by_test["unexpected_fail.html"]["expected"], "PASS")
        self.assertEqual(by_test["expected_fail.html"]["color"], "green")
        self.assertEqual(by_test["intermittent.html"]["color"], "yellow")
        self.assertEqual(by_test["retry.js"]["color"], "orange")
        self.assertEqual(by_test["crash.html"]["color"], "red")

    def test_end_test_keys_runs_by_subsuite_and_test_id(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        reftest = ["a.html", "==", "b.html"]
        self._begin_test(monitor, reftest)
        self._end_test(monitor, reftest, status="PASS")
        self._begin_test(monitor, "x.html", subsuite="")
        self._begin_test(monitor, "x.html", subsuite="private")
        self._test_status(
            monitor,
            test="x.html",
            subsuite="private",
            subtest="a",
            status="FAIL",
            expected="PASS",
        )
        self._end_test(monitor, "x.html", subsuite="", status="OK")
        self._end_test(monitor, "x.html", subsuite="private", status="OK")
        monitor.stop()

        markers = self._test_markers(monitor)
        self.assertEqual(
            [m["test"] for m in markers], ["a.html == b.html", "x.html", "x.html"]
        )
        self.assertEqual(markers[0]["name"], "a.html == b.html")
        self.assertEqual(markers[0]["color"], "green")
        self.assertEqual(markers[1]["color"], "green")
        self.assertEqual(markers[2]["color"], "orange")
        self.assertEqual(markers[2]["unexpected_subtests"], 1)

    def test_status_after_end_fails_a_passed_test(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        start = self._record_time(monitor)
        self._begin_test(monitor, "leak.html", time=start)
        time.sleep(0.01)
        end = self._record_time(monitor)
        self._end_test(monitor, "leak.html", status="PASS", time=end)
        self._test_status(
            monitor,
            test="leak.html",
            subtest="Shutdown",
            status="FAIL",
            expected="PASS",
            message="leaked until shutdown",
            time=(start + end) / 2,
        )
        monitor.stop()

        (marker,) = self._test_markers(monitor)
        self.assertEqual(marker["color"], "orange")
        self.assertEqual(marker["status"], "PASS")
        self.assertEqual(marker["unexpected_subtests"], 1)

    def test_status_after_end_fails_a_known_intermittent_test(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        start = self._record_time(monitor)
        self._begin_test(monitor, "intermittent.html", time=start)
        self._begin_test(monitor, "timeout.html", time=start)
        time.sleep(0.01)
        end = self._record_time(monitor)
        self._end_test(
            monitor,
            "intermittent.html",
            status="FAIL",
            expected="PASS",
            known_intermittent=["FAIL"],
            time=end,
        )
        self._end_test(
            monitor, "timeout.html", status="TIMEOUT", expected="OK", time=end
        )
        for test in ("intermittent.html", "timeout.html"):
            self._test_status(
                monitor,
                test=test,
                subtest="Shutdown",
                status="FAIL",
                expected="PASS",
                time=(start + end) / 2,
            )
        monitor.stop()

        by_test = {data["test"]: data for data in self._test_markers(monitor)}
        self.assertEqual(by_test["intermittent.html"]["color"], "orange")
        self.assertEqual(by_test["intermittent.html"]["status"], "FAIL")
        self.assertEqual(by_test["intermittent.html"]["unexpected_subtests"], 1)
        self.assertEqual(by_test["timeout.html"]["color"], "yellow")
        self.assertNotIn("unexpected_subtests", by_test["timeout.html"])

    def test_status_after_end_matches_the_subsuite(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        start = self._record_time(monitor)
        self._begin_test(monitor, "x.html", subsuite="", time=start)
        self._begin_test(monitor, "x.html", subsuite="private", time=start)
        time.sleep(0.01)
        end = self._record_time(monitor)
        self._end_test(monitor, "x.html", subsuite="", status="PASS", time=end)
        self._end_test(monitor, "x.html", subsuite="private", status="PASS", time=end)
        self._test_status(
            monitor,
            test="x.html",
            subsuite="private",
            subtest="Shutdown",
            status="FAIL",
            expected="PASS",
            time=(start + end) / 2,
        )
        monitor.stop()

        default, private = self._test_markers(monitor)
        self.assertNotIn("subsuite", default)
        self.assertEqual(default["color"], "green")
        self.assertEqual(private["subsuite"], "private")
        self.assertEqual(private["color"], "orange")
        self.assertEqual(private["unexpected_subtests"], 1)

    def test_unexpected_error_subtest_fails_the_test(self):
        monitor = SystemResourceMonitor(poll_interval=0.25)
        monitor.start()
        self._begin_test(monitor, "error.html")
        self._test_status(
            monitor, test="error.html", subtest="a", status="ERROR", expected="PASS"
        )
        self._end_test(monitor, "error.html", status="OK")
        self._begin_test(monitor, "harness_error.js")
        self._test_status(
            monitor,
            test="harness_error.js",
            subtest="",
            status="ERROR",
            expected="ERROR",
        )
        self._end_test(monitor, "harness_error.js", status="PASS")
        monitor.stop()

        by_test = {data["test"]: data for data in self._test_markers(monitor)}
        self.assertEqual(by_test["error.html"]["color"], "orange")
        self.assertEqual(by_test["error.html"]["unexpected_subtests"], 1)
        self.assertEqual(by_test["harness_error.js"]["color"], "green")
        self.assertNotIn("unexpected_subtests", by_test["harness_error.js"])
        self.assertEqual(
            [e[2]["color"] for e in self._events(monitor, "ERROR")], ["red", "red"]
        )

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

    def test_forwarded_marker_schemas(self):
        parent = BaseResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)

        child = SystemResourceMonitor(forward_to=self.forward_path)
        child.start()
        for signature in ("first", "second"):
            child.crash({"signature": signature, "time": time.time() * 1000})
        child.stop()
        parent.stop()

        with open(self.forward_path) as fh:
            kinds = [json.loads(line)[0] for line in fh]
        self.assertEqual(kinds, ["schema", "event", "event"])
        schemas = [s["name"] for s in parent.as_profile()["meta"]["markerSchema"]]
        self.assertEqual(schemas.count("Crash"), 1)
        self.assertNotIn("Test", schemas)


if __name__ == "__main__":
    mozunit.main()
