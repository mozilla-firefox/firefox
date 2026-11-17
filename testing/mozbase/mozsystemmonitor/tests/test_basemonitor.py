# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import multiprocessing
import os
import shutil
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

from mozsystemmonitor import basemonitor
from mozsystemmonitor.basemonitor import (
    FORWARD_ENV,
    BaseResourceMonitor,
    SystemResourceUsage,
    _process_sampling_interval,
)


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
class TestBaseResourceMonitor(unittest.TestCase):
    def test_basic(self):
        monitor = BaseResourceMonitor(poll_interval=0.5)

        monitor.start()
        time.sleep(3)

        monitor.stop()

        data = list(monitor.range_usage())
        self.assertGreater(len(data), 3)

        self.assertIsInstance(data[0], SystemResourceUsage)

    def test_empty(self):
        monitor = BaseResourceMonitor(poll_interval=2.0)
        monitor.start()
        monitor.stop()

        data = list(monitor.range_usage())
        self.assertEqual(len(data), 0)

    def test_phases(self):
        monitor = BaseResourceMonitor(poll_interval=0.25)

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
        monitor = BaseResourceMonitor()

        data = list(monitor.range_usage())
        self.assertEqual(len(data), 0)

    def test_events(self):
        monitor = BaseResourceMonitor(poll_interval=0.25)

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
        monitor = BaseResourceMonitor(poll_interval=0.25)

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
        monitor = BaseResourceMonitor(poll_interval=0.25)

        # There's really no easy way to ensure I/O occurs. For all we know
        # reads and writes will all be serviced by the page cache.
        monitor.start()
        time.sleep(1.0)
        monitor.stop()

        values = monitor.aggregate_io()
        self.assertTrue(hasattr(values, "read_count"))

    def test_memory(self):
        monitor = BaseResourceMonitor(poll_interval=0.25)

        monitor.start()
        time.sleep(1.0)
        monitor.stop()

        v = monitor.min_memory_available()
        self.assertIsInstance(v, int)

        v = monitor.max_memory_percent()
        self.assertIsInstance(v, float)

    def test_as_profile(self):
        monitor = BaseResourceMonitor(poll_interval=0.25)

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


@unittest.skipIf(psutil is None, "Resource monitor requires psutil.")
class TestForwarding(unittest.TestCase):
    def setUp(self):
        fd, self.forward_path = tempfile.mkstemp()
        os.close(fd)
        self.addCleanup(os.unlink, self.forward_path)

    def _child(self):
        with mock.patch.dict(os.environ, {FORWARD_ENV: self.forward_path}):
            return BaseResourceMonitor(poll_interval=0.1)

    def test_forwarding(self):
        parent = BaseResourceMonitor(poll_interval=0.1)
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
            ("marker", start, start + 0.1, "text", BaseResourceMonitor.TASK_CATEGORY),
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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
        parent = BaseResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)
        _run_forwarding_children(self.forward_path)
        parent.stop()

        self.assertEqual(len([m for m in parent.markers if m[0] == "marker"]), 2000)

    def test_accept_forwarded_not_running(self):
        monitor = BaseResourceMonitor(poll_interval=0.1)
        monitor.accept_forwarded(self.forward_path)
        self.assertIsNone(monitor._from_children_path)

    def test_forward_after_stop(self):
        child = self._child()
        child.start()
        child.stop()
        with self.assertWarns(UserWarning):
            child.forward_perfherder("suite", [])

    def test_children_forward_path(self):
        parent = BaseResourceMonitor(poll_interval=0.1)
        self.assertIsNone(parent.children_forward_path)
        parent.start()
        parent.accept_forwarded(self.forward_path)
        self.assertEqual(parent.children_forward_path, self.forward_path)
        parent.stop()

        self.assertEqual(self._child().children_forward_path, self.forward_path)

    def test_forward_to(self):
        parent = BaseResourceMonitor(poll_interval=0.1)
        parent.start()
        parent.accept_forwarded(self.forward_path)

        child = BaseResourceMonitor(forward_to=self.forward_path)
        self.assertTrue(child.forwarding)
        child.start()
        start = time.monotonic()
        child.record_marker("marker", start, start + 0.1, "text")
        child.stop()
        parent.stop()

        self.assertEqual(
            parent.markers[0],
            ("marker", start, start + 0.1, "text", BaseResourceMonitor.TASK_CATEGORY),
        )

    @unittest.skipUnless(hasattr(os, "fork"), "Requires fork.")
    def test_forked_child_forgets_monitor(self):
        monitor = BaseResourceMonitor(poll_interval=0.1)
        monitor.start()
        _wait_for_measurement(monitor)
        pid = os.fork()
        if pid == 0:
            forgotten = BaseResourceMonitor.instance is None and monitor._pipe.closed
            monitor._atexit_stop()
            # Hold the lock as the parent's drain timer could have when forking.
            monitor._pipe_lock.acquire()
            monitor.stop()
            os._exit(0 if forgotten else 1)
        _, status = os.waitpid(pid, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 0)
        child_exit = time.monotonic()
        time.sleep(0.5)
        self.assertIs(BaseResourceMonitor.instance, monitor)
        monitor.stop()
        self.assertGreater(monitor.measurements[-1].end, child_exit)

    def _artifact_filenames(self, monitor):
        return sorted(
            data["filename"] for _, name, data in monitor.events if name == "artifact"
        )

    def test_forwarded_artifacts_in_parent_upload_dir(self):
        with tempfile.TemporaryDirectory() as upload_dir:
            open(os.path.join(upload_dir, "file"), "w").close()
            parent = BaseResourceMonitor(poll_interval=0.1)
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
                parent = BaseResourceMonitor(poll_interval=0.1)
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
            parent = BaseResourceMonitor(poll_interval=0.1)
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
            parent = BaseResourceMonitor(poll_interval=0.1)
            parent.start()
            parent.accept_forwarded(self.forward_path)

            with mock.patch.object(
                BaseResourceMonitor, "_parse_sccache_log"
            ) as parse_sccache_log:
                child = self._child()
                child.start()
                child.stop(upload_dir=upload_dir)
                parent.stop(upload_dir=upload_dir)

            self.assertEqual(parse_sccache_log.call_count, 1)
            self.assertEqual(self._artifact_filenames(parent), ["sccache.log"])

    def test_partial_line(self):
        parent = BaseResourceMonitor(poll_interval=0.1)
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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
        "from mozsystemmonitor.basemonitor import BaseResourceMonitor\n"
        "m = BaseResourceMonitor()\n"
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


def test_base_monitor_standalone(tmp_path):
    # run-task only has basemonitor.py, without the rest of the package.
    package = tmp_path / "mozsystemmonitor"
    package.mkdir()
    (package / "__init__.py").touch()
    shutil.copy(basemonitor.__file__, package)
    forward_path = tmp_path / "forward.jsonl"
    forward_path.touch()
    script = (
        "import sys\n"
        f"sys.path.insert(0, {str(tmp_path)!r})\n"
        "from mozsystemmonitor.basemonitor import BaseResourceMonitor\n"
        f"m = BaseResourceMonitor(forward_to={str(forward_path)!r})\n"
        "m.start()\n"
        "m.record_event('event')\n"
        "m.stop()\n"
        "import json, time\n"
        "m = BaseResourceMonitor(poll_interval=0.1)\n"
        "m.start()\n"
        "m.record_event('event')\n"
        "with m.phase('phase'):\n"
        "    time.sleep(0.3)\n"
        "m.stop()\n"
        "profile = m.as_profile()\n"
        "assert 'mozsystemmonitor.resourcemonitor' not in sys.modules\n"
        "json.dump(profile, sys.stdout)\n"
    )
    env = {k: v for k, v in os.environ.items() if k != FORWARD_ENV}
    result = subprocess.run(
        [sys.executable, "-c", script],
        check=True,
        env=env,
        stdout=subprocess.PIPE,
    )
    records = [json.loads(line) for line in forward_path.read_bytes().splitlines()]
    assert [record[:2] for record in records] == [["event", "event"]]
    profile = json.loads(result.stdout)
    markers = profile["threads"][0]["markers"]["data"]
    assert any(m.get("type") == "Phase" and m.get("phase") == "phase" for m in markers)


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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
            monitor = BaseResourceMonitor(poll_interval=0.1)
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
        monitor = BaseResourceMonitor(poll_interval=0.1)
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
