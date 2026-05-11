# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import multiprocessing
import time
import unittest

import mozunit

try:
    import psutil
except ImportError:
    psutil = None

from mozsystemmonitor.resourcemonitor import SystemResourceMonitor, SystemResourceUsage


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


if __name__ == "__main__":
    mozunit.main()
