# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import atexit
import json
import multiprocessing
import os
import sys
import threading
import time
import warnings
from collections import namedtuple
from contextlib import contextmanager

# When set, monitors don't collect resource usage themselves, and instead
# append their events, markers and phases as JSON lines to the file it points
# to, for a monitor in a parent process (e.g. run-task) to merge into its own
# profile.
FORWARD_ENV = "MOZ_RESOURCE_MONITOR_FORWARD"


def _open_for_append(path):
    """Open path for writes that are each appended atomically, even when
    several processes append to it concurrently."""
    if sys.platform != "win32":
        return os.open(path, os.O_WRONLY | os.O_APPEND)

    import _winapi
    import msvcrt

    # O_APPEND seeks to the end before each write on Windows, which isn't
    # atomic. Writes through a handle that may only append go to the end of
    # the file atomically.
    FILE_APPEND_DATA = 0x4
    FILE_SHARE_READ_WRITE_DELETE = 0x7
    handle = _winapi.CreateFile(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ_WRITE_DELETE,
        0,
        _winapi.OPEN_EXISTING,
        0,
        0,
    )
    return msvcrt.open_osfhandle(handle, 0)


class PsutilStub:
    def __init__(self):
        self.sswap = namedtuple(
            "sswap", ["total", "used", "free", "percent", "sin", "sout"]
        )
        self.sdiskio = namedtuple(
            "sdiskio",
            [
                "read_count",
                "write_count",
                "read_bytes",
                "write_bytes",
                "read_time",
                "write_time",
            ],
        )
        self.snetio = namedtuple(
            "snetio", ["bytes_sent", "bytes_recv", "packets_sent", "packets_recv"]
        )
        self.pcputimes = namedtuple("pcputimes", ["user", "system"])
        self.svmem = namedtuple(
            "svmem",
            [
                "total",
                "available",
                "percent",
                "used",
                "free",
                "active",
                "inactive",
                "buffers",
                "cached",
            ],
        )

    def cpu_count(self, logical=True):
        return 0

    def cpu_percent(self, a, b):
        return [0]

    def cpu_times(self, percpu):
        if percpu:
            return [self.pcputimes(0, 0)]
        else:
            return self.pcputimes(0, 0)

    def disk_io_counters(self):
        return self.sdiskio(0, 0, 0, 0, 0, 0)

    def net_io_counters(self):
        return self.snetio(0, 0, 0, 0)

    def swap_memory(self):
        return self.sswap(0, 0, 0, 0, 0, 0)

    def virtual_memory(self):
        return self.svmem(0, 0, 0, 0, 0, 0, 0, 0, 0)


# psutil will raise NotImplementedError if the platform is not supported.
try:
    import psutil

    have_psutil = True
except Exception:
    try:
        # The PsutilStub should get us time intervals, at least
        psutil = PsutilStub()
    except Exception:
        psutil = None

    have_psutil = False


def get_disk_io_counters():
    try:
        io_counters = psutil.disk_io_counters()

        if io_counters is None:
            return PsutilStub().disk_io_counters()
    except RuntimeError:
        io_counters = PsutilStub().disk_io_counters()

    return io_counters


def get_network_io_counters():
    try:
        net_counters = psutil.net_io_counters()

        if net_counters is None:
            return PsutilStub().net_io_counters()
    except (RuntimeError, AttributeError):
        net_counters = PsutilStub().net_io_counters()

    return net_counters


def _poll(pipe, poll_interval=0.1):
    """Wrap multiprocessing.Pipe.poll to hide POLLERR and POLLIN
    exceptions.

    multiprocessing.Pipe is not actually a pipe on at least Linux.
    That has an effect on the expected outcome of reading from it when
    the other end of the pipe dies, leading to possibly hanging on revc()
    below.
    """
    try:
        return pipe.poll(poll_interval)
    except Exception:
        # Poll might throw an exception even though there's still
        # data to read. That happens when the underlying system call
        # returns both POLLERR and POLLIN, but python doesn't tell us
        # about it. So assume there is something to read, and we'll
        # get an exception when trying to read the data.
        return True


def _process_sampling_interval(poll_interval):
    """Return the minimum interval, in seconds, between samples of the running
    processes, or None if they shouldn't be sampled.

    MOZ_PROCESS_SAMPLING sets that interval. Sampling processes is expensive
    when there are many of them, so a larger interval trades the precision of
    their start and end times for less overhead. 0 disables process sampling,
    and a value that isn't a number samples them along with everything else.
    """
    value = os.environ.get("MOZ_PROCESS_SAMPLING")
    if not value:
        return None
    try:
        interval = float(value)
    except ValueError:
        return poll_interval
    return interval if interval > 0 else None


def _collect(pipe, poll_interval):
    """Collects system metrics.

    This is the main function for the background process. It collects
    data then forwards it on a pipe until told to stop.
    """

    processes = []
    process_sampling_interval = _process_sampling_interval(poll_interval)
    known_processes = dict()
    # Processes that were already running when monitoring started.
    initial_processes = set()

    try:
        # Establish initial values.

        io_last = get_disk_io_counters()
        net_io_last = get_network_io_counters()
        swap_last = psutil.swap_memory()
        psutil.cpu_percent(None, True)
        cpu_last = psutil.cpu_times(True)
        last_time = time.monotonic()

        sin_index = swap_last._fields.index("sin")
        sout_index = swap_last._fields.index("sout")

        sleep_interval = poll_interval

        last_process_sample = None

        # On Linux, psutil's boot time comes from the btime field of
        # /proc/stat, which is truncated to whole seconds. This shifts
        # process creation times earlier by up to one second.
        create_time_correction = 0
        if process_sampling_interval is not None and sys.platform.startswith("linux"):
            create_time_correction = min(
                max(
                    time.time()
                    - time.clock_gettime(time.CLOCK_BOOTTIME)
                    - psutil.boot_time(),
                    0,
                ),
                1,
            )

        def update_known_processes(force=False):
            nonlocal known_processes, last_process_sample

            if process_sampling_interval is None:
                return
            now = time.monotonic()
            # Polls are not evenly spaced, so allow for half a poll of slack
            # to not skip polls when the interval is a multiple of it.
            if (
                not force
                and last_process_sample is not None
                and now - last_process_sample + poll_interval / 2
                < process_sampling_interval
            ):
                return
            last_process_sample = now

            updated_known_processes = dict()
            for p in psutil.process_iter():
                pid = p.pid
                try:
                    with p.oneshot():
                        create_time = p.create_time() + create_time_correction
                        # Zombies have exited, they just haven't been waited
                        # for by their parent yet.
                        if p.status() == psutil.STATUS_ZOMBIE:
                            continue
                except psutil.Error:
                    # The process exited or isn't accessible.
                    continue
                # If the process creation time does not match, a new process reused a pid.
                if pid in known_processes and create_time == known_processes[pid][0]:
                    updated_known_processes[pid] = known_processes[pid]
                    del known_processes[pid]
                else:
                    cmd = []
                    try:
                        cmd = p.cmdline()
                    except psutil.AccessDenied:
                        # e.g. processes from other users on Windows and
                        # macOS, whose name is usually still available.
                        try:
                            cmd = [p.name()]
                        except Exception as e:
                            cmd = ["exception", str(e)]
                    except Exception as e:
                        cmd = ["exception", str(e)]
                    ppid = 0
                    try:
                        ppid = p.ppid()
                    except Exception:
                        pass

                    updated_known_processes[pid] = (create_time, cmd, ppid)
            update_time = time.time()
            for pid, (create_time, cmd, ppid) in known_processes.items():
                processes.append((pid, create_time, update_time, cmd, ppid))
            known_processes = updated_known_processes

        update_known_processes()
        initial_processes = {
            (pid, create_time) for pid, (create_time, _, _) in known_processes.items()
        }

        while not _poll(pipe, poll_interval=sleep_interval):
            io = get_disk_io_counters()
            net_io = get_network_io_counters()
            virt_mem = psutil.virtual_memory()
            swap_mem = psutil.swap_memory()
            cpu_percent = psutil.cpu_percent(None, True)
            cpu_times = psutil.cpu_times(True)
            # Take the timestamp as soon as possible after getting cpu_times
            # to reduce the likelihood of our process being interrupted between
            # the two instructions. Having a delayed timestamp would cause the
            # next sample to report more than 100% CPU time.
            measured_end_time = time.monotonic()

            # TODO Does this wrap? At 32 bits? At 64 bits?
            # TODO Consider patching "delta" API to upstream.
            io_diff = [v - io_last[i] for i, v in enumerate(io)]
            io_last = io

            net_io_diff = [
                v - net_io_last[i] for i, v in enumerate(net_io[:4])
            ]  # Only use first 4 fields
            net_io_last = net_io

            cpu_diff = []
            for core, values in enumerate(cpu_times):
                cpu_diff.append([v - cpu_last[core][i] for i, v in enumerate(values)])

            cpu_last = cpu_times

            swap_entry = list(swap_mem)
            swap_entry[sin_index] = swap_mem.sin - swap_last.sin
            swap_entry[sout_index] = swap_mem.sout - swap_last.sout
            swap_last = swap_mem

            pipe.send((
                last_time,
                measured_end_time,
                io_diff,
                net_io_diff,
                cpu_diff,
                cpu_percent,
                list(virt_mem),
                swap_entry,
            ))

            update_known_processes()

            collection_overhead = time.monotonic() - last_time - sleep_interval
            last_time = measured_end_time
            sleep_interval = max(poll_interval / 2, poll_interval - collection_overhead)

        # Take a last sample so that processes started since the previous one
        # are recorded as still running.
        update_known_processes(force=True)

    except Exception as e:
        warnings.warn(f"_collect failed: {e}")

    finally:
        # Record the processes that started while monitoring and are still
        # running.
        stop_time = time.time()
        for pid, (create_time, cmd, ppid) in known_processes.items():
            if (pid, create_time) not in initial_processes:
                processes.append((pid, create_time, stop_time, cmd, ppid))

        for pid, create_time, end_time, cmd, ppid in processes:
            if len(cmd) > 0:
                cmd[0] = os.path.basename(cmd[0])
            cmdline = " ".join([
                arg
                for arg in cmd
                if not arg.startswith("-D")
                and not arg.startswith("-I")
                and not arg.startswith("-W")
                and not arg.startswith("-L")
            ])
            pipe.send((
                "process",
                pid,
                create_time,
                end_time,
                cmdline,
                ppid,
                None,
                None,
            ))

        pipe.send(("done", None, None, None, None, None, None, None))
        pipe.close()

    sys.exit(0)


SystemResourceUsage = namedtuple(
    "SystemResourceUsage",
    ["start", "end", "cpu_times", "cpu_percent", "io", "net_io", "virt", "swap"],
)


class BaseResourceMonitor:
    """Measures system resources.

    Each instance measures system resources from the time it is started
    until it is finished. It does this on a separate process so it doesn't
    impact execution of the main Python process.

    Each instance is a one-shot instance. It cannot be used to record multiple
    durations.

    Aside from basic data gathering, the class supports basic analysis
    capabilities. You can query for data between ranges. You can also tell it
    when certain events occur and later grab data relevant to those events or
    plot those events on a timeline.

    The resource monitor works by periodically polling the state of the
    system. By default, it polls every second. This can be adjusted depending
    on the required granularity of the data and considerations for probe
    overhead. It tries to probe at the interval specified. However, variations
    should be expected. Fast and well-behaving systems should experience
    variations in the 1ms range. Larger variations may exist if the system is
    under heavy load or depending on how accurate socket polling is on your
    system.

    In its current implementation, data is not available until collection has
    stopped. This may change in future iterations.

    Usage
    =====

    monitor = SystemResourceMonitor()
    monitor.start()

    # Record that a single event in time just occurred.
    foo.do_stuff()
    monitor.record_event('foo_did_stuff')

    # Record that we're about to perform a possibly long-running event.
    with monitor.phase('long_job'):
        foo.do_long_running_job()

    # Stop recording. Currently we need to stop before data is available.
    monitor.stop()

    # Obtain the raw data for the entire probed range.
    print('CPU Usage:')
    for core in monitor.aggregate_cpu():
        print(core)

    # We can also request data corresponding to a specific phase.
    for data in monitor.phase_usage('long_job'):
        print(data.cpu_percent)
    """

    # The interprocess communication is complicated enough to warrant
    # explanation. To work around the Python GIL, we launch a separate
    # background process whose only job is to collect metrics. If we performed
    # collection in the main process, the polling interval would be
    # inconsistent if a long-running function were on the stack. Since the
    # child process is independent of the instantiating process, data
    # collection should be evenly spaced.
    #
    # As the child process collects data, it buffers it locally. When
    # collection stops, it flushes all that data to a pipe to be read by
    # the parent process.

    instance = None

    # Marker schemas for the types of markers subclasses record, by name.
    # Monitors forwarding to a parent forward the schema of each type along
    # with its first marker.
    marker_schemas = {}

    # Category indices matching the categories array in _build_meta
    OTHER_CATEGORY = 0
    PHASE_CATEGORY = 1
    TASK_CATEGORY = 2

    @staticmethod
    def _format_percent(value):
        return str(round(value, 1)) + "%"

    def __init__(self, poll_interval=1.0, metadata=None, forward_to=None):
        """Instantiate a system resource monitor instance.

        The instance is configured with a poll interval. This is the interval
        between samples, in float seconds.

        forward_to is the file to forward records to instead of collecting
        resource usage, overriding FORWARD_ENV.
        """
        self.start_time = None
        self.end_time = None

        self.events = []
        self.markers = []
        self.processes = []
        self.measurements = []
        self.phases = {}

        self._active_phases = {}
        self._active_markers = {}

        self._running = False
        self._stopped = False
        self._process = None
        self._stream_file = None
        self._drain_timer = None
        self._pipe_lock = threading.Lock()

        # JSON lines file to forward records to, when a monitor in a parent
        # process accepts them.
        self._to_parent_path = forward_to or os.environ.get(FORWARD_ENV)
        self._to_parent_fd = None
        # JSON lines file into which monitors in child processes forward their
        # records, set by accept_forwarded(), and how far it has been read.
        self._from_children_path = None
        self._from_children_offset = 0
        self._from_children_lock = threading.Lock()
        # Keys in self.phases of phases by (source, name), where source is
        # None for phases recorded by this monitor, or the pid of the child
        # process that forwarded them.
        self._phase_keys = {}
        # (name, extraOptions, start, end, upload_dir, source) tuples forwarded
        # by forward_perfherder() in child processes, see perfherder_data().
        self.perfherder_requests = []
        # (upload_dir, timestamp, data) artifact events forwarded by child
        # processes, added when this monitor stops unless its own scan of the
        # upload directory finds the same file.
        self._forwarded_artifacts = []
        # Names of the marker schemas forwarded to the parent monitor.
        self._forwarded_schemas = set()
        # Marker schemas forwarded by monitors in child processes, by name.
        self._children_marker_schemas = {}

        if metadata is None:
            metadata = {}
        self.metadata = metadata
        self.poll_interval = poll_interval

        if self._to_parent_path:
            self.start_timestamp = time.time()
            self.start_time = time.monotonic()
            return

        if psutil is None:
            return

        # This try..except should not be needed! However, some tools (like
        # |mach build|) attempt to load psutil before properly creating a
        # virtualenv by building psutil. As a result, python/psutil may be in
        # sys.path and its .py files may pick up the psutil C extension from
        # the system install. If the versions don't match, we typically see
        # failures invoking one of these functions.
        try:
            cpu_percent = psutil.cpu_percent(0.0, True)
            cpu_times = psutil.cpu_times(False)
            io = get_disk_io_counters()
            net_io = get_network_io_counters()
            virt = psutil.virtual_memory()
            swap = psutil.swap_memory()
        except Exception as e:
            warnings.warn(f"psutil failed to run: {e}")
            return

        self._cpu_cores = len(cpu_percent)
        self._cpu_times_type = type(cpu_times)
        self._cpu_times_len = len(cpu_times)
        self._io_type = type(io)
        self._io_len = len(io)
        # Only use first 4 fields of net_io (bytes_sent, bytes_recv, packets_sent, packets_recv)
        self._net_io_type = namedtuple("net_io", list(net_io._fields[:4]))
        self._virt_type = type(virt)
        self._virt_len = len(virt)
        self._swap_type = type(swap)
        self._swap_len = len(swap)
        self.start_timestamp = time.time()
        self.start_time = time.monotonic()

        self._pipe, child_pipe = multiprocessing.Pipe(True)

        self._process = multiprocessing.Process(
            target=_collect, args=(child_pipe, poll_interval)
        )

    def __del__(self):
        if self._running:
            self._pipe.send(("terminate",))
            self._process.join()

    def convert_to_monotonic_time(self, timestamp):
        return timestamp - self.start_timestamp + self.start_time

    def get_monotonic_time_from_data(self, data):
        """Convert structured logging timestamp to monotonic time.

        Args:
            data: Dictionary with "time" field in milliseconds

        Returns:
            Monotonic timestamp
        """
        time_sec = data["time"] / 1000
        return self.convert_to_monotonic_time(time_sec)

    # Methods to control monitoring.

    def start(self):
        """Start measuring system-wide CPU resource utilization.

        You should only call this once per instance.
        """
        if self._to_parent_path:
            try:
                self._to_parent_fd = _open_for_append(self._to_parent_path)
            except OSError as e:
                warnings.warn(f"failed to open {self._to_parent_path}: {e}")
                return
            self._running = True
            self.start_time = time.monotonic()
            BaseResourceMonitor.instance = self
            atexit.register(self._atexit_stop)
            return

        if not self._process:
            return

        self._process.start()
        self._running = True
        self.start_time = time.monotonic()
        BaseResourceMonitor.instance = self
        self._schedule_drain_timer()

        # Ensure that stop() is called even if the caller does not do so, to
        # prevent the child from being kept alive forever in that scenario.
        atexit.register(self._atexit_stop)

    def _atexit_stop(self):
        if self._running and not self._stopped:
            self.stop()

    def _flush_pending(self):
        """Record what is still pending when the monitor stops."""

    def stop(self, upload_dir=None):
        """Stop measuring system-wide CPU resource utilization.

        You should call this if and only if you have called start(). You should
        always pair a stop() with a start().

        Currently, data is not available until you call stop().

        Args:
            upload_dir: Optional path to upload directory for artifact markers.
        """
        atexit.unregister(self._atexit_stop)
        if not self._process and self._to_parent_fd is None:
            self._stopped = True
            return

        self.stop_time = time.monotonic()
        assert not self._stopped

        if self._process:
            try:
                self._pipe.send(("terminate",))
            except Exception:
                pass

            self._cancel_drain_timer()

            # Drain remaining data from the child process, including the
            # "done" sentinel and any process entries.
            self._drain_pipe(until_done=True)

            # We establish a timeout so we don't hang forever if the child
            # process has crashed.
            if self._running:
                self._process.join(10)
                if self._process.is_alive():
                    self._process.terminate()
                    self._process.join(10)
        self._stopped = True

        self._running = False
        BaseResourceMonitor.instance = None
        self.end_time = time.monotonic()

        self._flush_pending()
        self._read_forwarded()

        if self._stream_file:
            self._stream_file.close()
            self._stream_file = None

        # Add event markers for files in upload directory
        if upload_dir is None:
            upload_dir = os.environ.get("UPLOAD_DIR") or os.environ.get(
                "MOZ_UPLOAD_DIR"
            )
        if upload_dir:
            upload_dir = os.path.realpath(upload_dir)
        forwarded_artifacts = {
            (child_upload_dir, marker_data["filename"]): (timestamp, marker_data)
            for child_upload_dir, timestamp, marker_data in self._forwarded_artifacts
        }
        self._forwarded_artifacts = []
        if upload_dir and os.path.isdir(upload_dir):
            try:
                for filename in os.listdir(upload_dir):
                    filepath = os.path.join(upload_dir, filename)
                    if os.path.isfile(filepath):
                        stat = os.stat(filepath)
                        timestamp = self.convert_to_monotonic_time(stat.st_mtime)
                        marker_data = {
                            "type": "Artifact",
                            "filename": filename,
                            "size": stat.st_size,
                        }
                        if self._to_parent_path:
                            self._forward([
                                "artifact",
                                upload_dir,
                                timestamp,
                                marker_data,
                            ])
                        else:
                            self._add_event("artifact", timestamp, marker_data)

                        # Parse sccache.log if found, unless a child process
                        # already did.
                        if (
                            filename == "sccache.log"
                            and (upload_dir, filename) not in forwarded_artifacts
                        ):
                            self._parse_sccache_log(filepath)
                        # This monitor's own record of the file supersedes
                        # one forwarded by a child process.
                        forwarded_artifacts.pop((upload_dir, filename), None)
            except Exception as e:
                warnings.warn(f"Failed to scan upload directory: {e}")
        # Files a child process saw that this monitor didn't, because they
        # are in another directory or were moved out of this one.
        for timestamp, marker_data in forwarded_artifacts.values():
            self._add_event("artifact", timestamp, marker_data)

        if self._to_parent_fd is not None:
            os.close(self._to_parent_fd)
            self._to_parent_fd = None

    @property
    def forwarding(self):
        """Whether this monitor forwards its events, markers and phases to a
        monitor in a parent process rather than collecting resource usage."""
        return bool(self._to_parent_path)

    @property
    def children_forward_path(self):
        """The file monitors in child processes should forward their records
        to for them to end up in the same profile as this monitor's, if any."""
        return self._to_parent_path or self._from_children_path

    def start_streaming(self, path):
        """Start streaming profile data to a file as JSON lines.

        The first line contains the meta object, then a thread object,
        then one line per marker. The file is meant to be replaced with
        the full serialized profile on normal shutdown.
        """
        if self._to_parent_path:
            return
        self._stream_file = open(path, "w", encoding="utf-8", newline="\n")
        meta = {"type": "meta"}
        meta.update(self._build_meta())
        self._stream_file.write(json.dumps(meta, separators=(",", ":")) + "\n")
        thread = {"type": "thread"}
        thread.update(self._build_thread())
        thread["processName"] = meta.get("product", "mach")
        self._stream_file.write(json.dumps(thread, separators=(",", ":")) + "\n")

        for name, start, end, data, category in self.markers:
            markerData = (
                data if isinstance(data, dict) else {"type": "Text", "text": str(data)}
            )
            self._stream_marker(name, start, end, markerData, category)
        for event in self.events:
            if len(event) == 3:
                timestamp, name, data = event
                self._stream_marker(name, timestamp, None, data)
            else:
                timestamp, name = event
                self._stream_marker(
                    name, timestamp, None, {"type": "Text", "text": name}
                )

        self._drain_pipe()

    def _drain_pipe(self, until_done=False):
        """Read available measurement data from the child process pipe.

        If until_done is True, block until the "done" sentinel is received.
        Otherwise, only read data that is immediately available.
        """
        if not self._pipe:
            return

        with self._pipe_lock:
            self._drain_pipe_locked(until_done)

    def _drain_pipe_locked(self, until_done):
        poll_interval = 0.1 if until_done else 0
        deadline = time.monotonic() + 10
        while True:
            # The collector may take a while to send the remaining data, e.g.
            # when it receives the termination request while enumerating
            # processes, so keep waiting for it as long as it's alive.
            alive = (
                until_done and self._process.is_alive() and time.monotonic() < deadline
            )
            if not _poll(self._pipe, poll_interval=poll_interval):
                if alive:
                    continue
                break
            try:
                (
                    start_time,
                    end_time,
                    io_diff,
                    net_io_diff,
                    cpu_diff,
                    cpu_percent,
                    virt_mem,
                    swap_mem,
                ) = self._pipe.recv()
            except Exception as e:
                if not self._stopped:
                    warnings.warn(f"failed to receive data: {e}")
                break

            if start_time == "process":
                pid = end_time
                start = self.convert_to_monotonic_time(io_diff)
                end = self.convert_to_monotonic_time(net_io_diff)
                cmd = cpu_diff
                ppid = cpu_percent
                self.processes.append((pid, start, end, cmd, ppid))
                continue

            if start_time == "done":
                break

            try:
                io = self._io_type(*io_diff)
                net_io = self._net_io_type(*net_io_diff)
                virt = self._virt_type(*virt_mem)
                swap = self._swap_type(*swap_mem)
                cpu_times = [self._cpu_times_type(*v) for v in cpu_diff]

                m = SystemResourceUsage(
                    start_time, end_time, cpu_times, cpu_percent, io, net_io, virt, swap
                )
                self.measurements.append(m)
                self._stream_measurement(m)
            except Exception:
                warnings.warn(
                    "failed to read the received data: {}".format((
                        start_time,
                        end_time,
                        io_diff,
                        cpu_diff,
                        cpu_percent,
                        virt_mem,
                        swap_mem,
                    ))
                )
                break

    def _stream_measurement(self, m):
        if not self._stream_file:
            return
        if m.end - m.start < self.poll_interval / 10:
            return
        for name, start, end, data in self._measurement_markers(m):
            self._stream_marker(name, start, end, data)

    def _schedule_drain_timer(self):
        if self._running:
            self._drain_timer = threading.Timer(
                self.poll_interval * 10, self._on_drain_timer
            )
            self._drain_timer.daemon = True
            self._drain_timer.start()

    def _cancel_drain_timer(self):
        if self._drain_timer:
            self._drain_timer.cancel()
            self._drain_timer = None

    def _on_drain_timer(self):
        self._drain_pipe()
        self._read_forwarded()
        self._schedule_drain_timer()

    def accept_forwarded(self, path):
        """Merge the events, markers and phases that monitors in child
        processes append to the given file, which should be exported to them
        as FORWARD_ENV. Does nothing if this monitor isn't running."""
        if not self._running:
            return
        self._from_children_path = path

    def _forward(self, record):
        if self._to_parent_fd is None:
            if self._to_parent_path and self._stopped:
                warnings.warn(f"failed to forward {record[0]}: monitor is stopped")
            return
        line = json.dumps(record, separators=(",", ":")) + "\n"
        try:
            os.write(self._to_parent_fd, line.encode("utf-8"))
        except (OSError, TypeError, ValueError) as e:
            warnings.warn(f"failed to forward {record[0]}: {e}")

    def _read_forwarded(self):
        if not self._from_children_path:
            return
        with self._from_children_lock:
            try:
                with open(self._from_children_path, "rb") as fh:
                    fh.seek(self._from_children_offset)
                    data = fh.read()
            except OSError:
                return
            # Leave incomplete lines to the next read.
            data = data[: data.rfind(b"\n") + 1]
            self._from_children_offset += len(data)
            for line in data.splitlines():
                try:
                    kind, *args = json.loads(line)
                    if kind == "event":
                        self._add_event(*args)
                    elif kind == "marker":
                        self._add_marker(*args)
                    elif kind == "phase":
                        self._add_phase(*args)
                    elif kind == "perfherder":
                        self.perfherder_requests.append(tuple(args))
                    elif kind == "artifact":
                        self._forwarded_artifacts.append(tuple(args))
                    elif kind == "schema":
                        (schema,) = args
                        self._children_marker_schemas.setdefault(schema["name"], schema)
                except Exception as e:
                    warnings.warn(f"failed to read forwarded record {line!r}: {e}")

    def _add_phase(self, name, start, end, source=None):
        key = name
        n = 1
        while key in self.phases or key in self._active_phases:
            n += 1
            key = f"{name} ({n})"
        self.phases[key] = (start, end)
        self._phase_keys[(source, name)] = key
        self._stream_marker(
            "Phase",
            start,
            end,
            {"type": "Phase", "phase": key},
            self.PHASE_CATEGORY,
        )

    def _stream_marker(self, name, start, end, data, category=0):
        if not self._stream_file:
            return

        start_ms = round((start - self.start_time) * 1000, 3)
        end_ms = round((end - self.start_time) * 1000, 3) if end is not None else None
        obj = {
            "type": "marker",
            "name": name,
            "startTime": start_ms,
            "endTime": end_ms,
            "data": data,
        }
        if category != 0:
            obj["category"] = category
        line = json.dumps(obj, separators=(",", ":"))
        self._stream_file.write(line + "\n")
        self._stream_file.flush()

    def _parse_sccache_log(self, filepath):
        """Parse sccache.log and add profiler markers for cache hits and misses."""
        import re
        from datetime import datetime

        parse_start = time.monotonic()

        try:
            # Track compilation entries: file -> {hash_time, lookup_time, write_time, hit}
            compilations = {}

            # Compile regex pattern outside the loop
            # Matches: [timestamp DEBUG ...] [filename]: message ([\d.]+) s[,$]
            # Examples:
            #   [timestamp] [file.o]: generate_hash_key took 0.123 s
            #   [timestamp] [file.o]: Cache hit in 0.456 s
            #   [timestamp] [file.o]: Compiled in 2.580 s, storing in cache
            pattern = re.compile(
                r"\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z) .*\] \[([^\]]+)\]: (.+) ([\d.]+) s(?:,|$)"
            )

            with open(filepath) as f:
                for line in f:
                    match = pattern.match(line)
                    if not match:
                        continue

                    timestamp_str = match.group(1)
                    filename = match.group(2)
                    message = match.group(3)
                    duration = float(match.group(4)) * 1000  # Convert to milliseconds

                    # Parse ISO 8601 timestamp and convert to monotonic time
                    dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S.%fZ")
                    timestamp = self.convert_to_monotonic_time(dt.timestamp())

                    # Get or create compilation entry
                    entry = compilations.setdefault(filename, {})

                    # Track hash generation time
                    if message == "generate_hash_key took":
                        entry["hash_time"] = duration
                        # timestamp is in seconds, duration in milliseconds
                        entry["start_time"] = timestamp - (duration / 1000)

                    # Track cache hit/miss and lookup time
                    elif message == "Cache hit in":
                        entry["lookup_time"] = duration
                        entry["hit"] = True
                        entry["end_time"] = timestamp

                    elif message == "Cache miss in":
                        entry["lookup_time"] = duration
                        entry["hit"] = False

                    # Track cache write time (only for misses)
                    elif message == "Cache write finished in":
                        entry["write_time"] = duration
                        entry["end_time"] = timestamp

                    # Track compilation time (for misses)
                    elif message == "Compiled in":
                        entry["compile_time"] = duration

                    # Track cache artifact creation time (for misses)
                    elif message == "Created cache artifact in":
                        entry["artifact_time"] = duration

            # Add markers for each compilation
            for filename, data in compilations.items():
                if "start_time" not in data or "end_time" not in data:
                    continue

                marker_data = {
                    "type": "sccache",
                    "file": filename,
                }

                if "hash_time" in data:
                    marker_data["hash_time"] = data["hash_time"]
                if "lookup_time" in data:
                    marker_data["lookup_time"] = data["lookup_time"]

                if data.get("hit"):
                    marker_data["status"] = "hit"
                    marker_data["color"] = "green"
                else:
                    marker_data["status"] = "miss"
                    marker_data["color"] = "yellow"
                    if "compile_time" in data:
                        marker_data["compile_time"] = data["compile_time"]
                    if "artifact_time" in data:
                        marker_data["artifact_time"] = data["artifact_time"]
                    if "write_time" in data:
                        marker_data["write_time"] = data["write_time"]

                self._add_marker(
                    "sccache",
                    data["start_time"],
                    data["end_time"],
                    marker_data,
                    self.TASK_CATEGORY,
                )

        except Exception as e:
            warnings.warn(f"Failed to parse sccache.log: {e}")
        else:
            # Add a duration marker showing parsing time
            parse_end = time.monotonic()
            num_markers = len(compilations)
            # Add as a duration marker
            if num_markers > 0:
                self._add_marker(
                    "sccache parsing",
                    parse_start,
                    parse_end,
                    {
                        "type": "Text",
                        "text": f"Parsed {num_markers} sccache entries from log",
                    },
                    self.TASK_CATEGORY,
                )

    # Methods to record events alongside the monitored data.

    def _forward_schema(self, data):
        name = data.get("type") if isinstance(data, dict) else None
        if name in self.marker_schemas and name not in self._forwarded_schemas:
            self._forwarded_schemas.add(name)
            self._forward(["schema", self.marker_schemas[name]])

    def _add_event(self, name, timestamp, data=None):
        if self._to_parent_path:
            self._forward_schema(data)
            self._forward(["event", name, timestamp, data])
            return
        if data:
            self.events.append((timestamp, name, data))
            self._stream_marker(name, timestamp, None, data)
        else:
            self.events.append((timestamp, name))
            self._stream_marker(name, timestamp, None, {"type": "Text", "text": name})

    def _add_marker(self, name, start, end, data, category=TASK_CATEGORY):
        if self._to_parent_path:
            self._forward_schema(data)
            self._forward(["marker", name, start, end, data, category])
            return
        self.markers.append((name, start, end, data, category))
        markerData = (
            data if isinstance(data, dict) else {"type": "Text", "text": str(data)}
        )
        self._stream_marker(name, start, end, markerData, category)

    @staticmethod
    def record_event(name, timestamp=None, data=None):
        """Record an event as occuring now.

        Events are actions that occur at a specific point in time. If you are
        looking for an action that has a duration, see the phase API below.

        Args:
            name: Name of the event (string)
            timestamp: Optional timestamp (monotonic time). If not provided, uses current time.
            data: Optional marker payload dictionary (e.g., {"type": "TestStatus", ...})
        """
        if BaseResourceMonitor.instance:
            if timestamp is None:
                timestamp = time.monotonic()
            BaseResourceMonitor.instance._add_event(name, timestamp, data)

    @staticmethod
    def record_marker(name, start, end, data):
        """Record a marker with a duration and optional data payload

        Markers are typically used to record when a single command happened.
        For actions with a longer duration that justifies tracking resource use
        see the phase API below.

        The data parameter can be either a dictionary containing a marker
        payload (e.g., {"type": "Text", "text": "description"}) or a string.
        """
        if BaseResourceMonitor.instance:
            BaseResourceMonitor.instance._add_marker(name, start, end, data)

    @staticmethod
    def forward_marker(name, start, end, data, forward_to=None):
        """Like record_marker(), but when no monitor runs in this process,
        forward the marker to the monitor of a parent process accepting
        records through FORWARD_ENV, if any. This allows recording markers
        before a process starts its own monitor, or without starting one.

        When forward_to is given, always forward the marker to that path
        instead."""
        path = forward_to
        if path is None:
            if BaseResourceMonitor.instance:
                BaseResourceMonitor.instance._add_marker(name, start, end, data)
                return
            path = os.environ.get(FORWARD_ENV)
        if not path:
            return
        record = ["marker", name, start, end, data, BaseResourceMonitor.TASK_CATEGORY]
        line = json.dumps(record, separators=(",", ":")) + "\n"
        try:
            fd = _open_for_append(path)
            try:
                os.write(fd, line.encode("utf-8"))
            finally:
                os.close(fd)
        except (OSError, TypeError, ValueError) as e:
            warnings.warn(f"failed to forward marker: {e}")

    @staticmethod
    def begin_marker(name, text, disambiguator=None, timestamp=None):
        if BaseResourceMonitor.instance:
            id = name + ":" + text
            if disambiguator:
                id += ":" + disambiguator
            BaseResourceMonitor.instance._active_markers[id] = (
                BaseResourceMonitor.instance.convert_to_monotonic_time(timestamp)
                if timestamp
                else time.monotonic()
            )

    @staticmethod
    def end_marker(name, text, disambiguator=None, timestamp=None):
        if not BaseResourceMonitor.instance:
            return
        end = time.monotonic()
        if timestamp:
            end = BaseResourceMonitor.instance.convert_to_monotonic_time(timestamp)
        id = name + ":" + text
        if disambiguator:
            id += ":" + disambiguator
        if not id in BaseResourceMonitor.instance._active_markers:
            return
        start = BaseResourceMonitor.instance._active_markers.pop(id)
        # Convert text to new data format for backward compatibility
        data = {"type": "Text", "text": text}
        BaseResourceMonitor.instance.record_marker(name, start, end, data)

    @contextmanager
    def phase(self, name):
        """Context manager for recording an active phase."""
        self.begin_phase(name)
        yield
        self.finish_phase(name)

    def begin_phase(self, name):
        """Record the start of a phase.

        Phases are actions that have a duration. Multiple phases can be active
        simultaneously. Phases can be closed in any order.

        Keep in mind that if phases occur in parallel, it will become difficult
        to isolate resource utilization specific to individual phases.
        """
        assert name not in self._active_phases

        self._active_phases[name] = time.monotonic()

    def finish_phase(self, name):
        """Record the end of a phase."""

        assert name in self._active_phases

        start = self._active_phases.pop(name)
        end = time.monotonic()
        self._add_phase(name, start, end)
        self._forward(["phase", name, start, end, os.getpid()])

        return end - start

    def forward_perfherder(self, name, extra_options, upload_dir=None):
        """Ask the parent monitor to report job_resource_usage Perfherder
        data covering the time this monitor ran, see perfherder_data().
        This must be called before stop().

        The data is written to perfherder-data-resource-usage.json in
        upload_dir if given."""
        self._forward([
            "perfherder",
            name,
            extra_options,
            self.start_time,
            time.monotonic(),
            upload_dir,
            os.getpid(),
        ])

    # Methods to query data.

    def perfherder_data(self, name, extra_options, start=None, end=None, source=None):
        """Build job_resource_usage Perfherder data for the given range, with
        an overall suite, and a suite for each phase within that range that
        was recorded by this monitor, or forwarded by the child process whose
        pid is source."""
        if start is None:
            start = self.start_time
        if end is None:
            end = self.end_time

        overall = []
        cpu_percent = self.aggregate_cpu_percent(start, end, per_cpu=False)
        if cpu_percent:
            overall.append({"name": "cpu_percent", "value": cpu_percent})

        io = self.aggregate_io(start, end)
        overall.extend([
            {"name": "io_write_bytes", "value": io.write_bytes},
            {"name": "io.read_bytes", "value": io.read_bytes},
            {"name": "io_write_time", "value": io.write_time},
            {"name": "io_read_time", "value": io.read_time},
        ])

        suites = [
            {
                "name": f"{name}.overall",
                "extraOptions": extra_options,
                "subtests": overall,
            }
        ]

        for (phase_source, phase), key in self._phase_keys.items():
            if phase_source != source:
                continue
            phase_start, phase_end = self.phases[key]
            if phase_start < start or phase_end > end:
                continue
            subtests = [{"name": "time", "value": phase_end - phase_start}]
            cpu_percent = self.aggregate_cpu_percent(phase=key, per_cpu=False)
            if cpu_percent is not None:
                subtests.append({"name": "cpu_percent", "value": cpu_percent})

            # We don't report I/O during each step because measured I/O
            # is system I/O and that I/O can be delayed (e.g. writes will
            # buffer before being flushed and recorded in our metrics).
            suites.append({
                "name": f"{name}.{phase}",
                "subtests": subtests,
            })

        return {
            "framework": {"name": "job_resource_usage"},
            "suites": suites,
        }

    def range_usage(self, start=None, end=None):
        """Obtain the usage data falling within the given time range.

        This is a generator of SystemResourceUsage.

        If no time range bounds are given, all data is returned.
        """
        if not self._stopped or self.start_time is None:
            return

        if start is None:
            start = self.start_time

        if end is None:
            end = self.end_time

        for entry in self.measurements:
            if entry.start < start:
                continue

            if entry.end > end:
                break

            yield entry

    def phase_usage(self, phase):
        """Obtain usage data for a specific phase.

        This is a generator of SystemResourceUsage.
        """
        time_start, time_end = self.phases[phase]

        return self.range_usage(time_start, time_end)

    def between_events_usage(self, start_event, end_event):
        """Obtain usage data between two point events.

        This is a generator of SystemResourceUsage.
        """
        start_time = None
        end_time = None

        for t, name in self.events:
            if name == start_event:
                start_time = t
            elif name == end_event:
                end_time = t

        if start_time is None:
            raise Exception(f"Could not find start event: {start_event}")

        if end_time is None:
            raise Exception(f"Could not find end event: {end_event}")

        return self.range_usage(start_time, end_time)

    def aggregate_cpu_percent(self, start=None, end=None, phase=None, per_cpu=True):
        """Obtain the aggregate CPU percent usage for a range.

        Returns a list of floats representing average CPU usage percentage per
        core if per_cpu is True (the default). If per_cpu is False, return a
        single percentage value.

        By default this will return data for the entire instrumented interval.
        If phase is defined, data for a named phase will be returned. If start
        and end are defined, these times will be fed into range_usage().
        """
        cpu = [[] for i in range(0, self._cpu_cores)]

        if phase:
            data = self.phase_usage(phase)
        else:
            data = self.range_usage(start, end)

        for usage in data:
            for i, v in enumerate(usage.cpu_percent):
                cpu[i].append(v)

        samples = len(cpu[0])

        if not samples:
            return 0

        if per_cpu:
            # pylint --py3k W1619
            return [sum(x) / samples for x in cpu]

        cores = [sum(x) for x in cpu]

        # pylint --py3k W1619
        return sum(cores) / len(cpu) / samples

    def aggregate_cpu_times(self, start=None, end=None, phase=None, per_cpu=True):
        """Obtain the aggregate CPU times for a range.

        If per_cpu is True (the default), this returns a list of named tuples.
        Each tuple is as if it were returned by psutil.cpu_times(). If per_cpu
        is False, this returns a single named tuple of the aforementioned type.
        """
        empty = [0 for i in range(0, self._cpu_times_len)]
        cpu = [list(empty) for i in range(0, self._cpu_cores)]

        if phase:
            data = self.phase_usage(phase)
        else:
            data = self.range_usage(start, end)

        for usage in data:
            for i, core_values in enumerate(usage.cpu_times):
                for j, v in enumerate(core_values):
                    cpu[i][j] += v

        if per_cpu:
            return [self._cpu_times_type(*v) for v in cpu]

        sums = list(empty)
        for core in cpu:
            for i, v in enumerate(core):
                sums[i] += v

        return self._cpu_times_type(*sums)

    def aggregate_io(self, start=None, end=None, phase=None):
        """Obtain aggregate I/O counters for a range.

        Returns an iostat named tuple from psutil.
        """

        io = [0 for i in range(self._io_len)]

        if phase:
            data = self.phase_usage(phase)
        else:
            data = self.range_usage(start, end)

        for usage in data:
            for i, v in enumerate(usage.io):
                io[i] += v

        return self._io_type(*io)

    def min_memory_available(self, start=None, end=None, phase=None):
        """Return the minimum observed available memory number from a range.

        Returns long bytes of memory available.

        See psutil for notes on how this is calculated.
        """
        if phase:
            data = self.phase_usage(phase)
        else:
            data = self.range_usage(start, end)

        values = []

        for usage in data:
            values.append(usage.virt.available)

        return min(values)

    def max_memory_percent(self, start=None, end=None, phase=None):
        """Returns the maximum percentage of system memory used.

        Returns a float percentage. 1.00 would mean all system memory was in
        use at one point.
        """
        if phase:
            data = self.phase_usage(phase)
        else:
            data = self.range_usage(start, end)

        values = []

        for usage in data:
            values.append(usage.virt.percent)

        return max(values)

    def _build_cpu_schema(self):
        cpu_times = psutil.cpu_times(False)
        schema = {
            "name": "CPU",
            "tooltipLabel": "{marker.name}",
            "display": [],
            "data": [
                {"key": "cpuPercent", "label": "CPU Percent", "format": "string"},
            ],
            "graphs": [],
        }
        for field, label in {
            "user": "User %",
            "iowait": "IO Wait %",
            "system": "System %",
            "nice": "Nice %",
            "idle": "Idle %",
        }.items():
            if hasattr(cpu_times, field):
                schema["data"].append({
                    "key": field + "_pct",
                    "label": label,
                    "format": "string",
                })
        for field, color in {
            "softirq": "orange",
            "iowait": "red",
            "system": "grey",
            "user": "yellow",
            "nice": "blue",
        }.items():
            if hasattr(cpu_times, field):
                schema["graphs"].append({"key": field, "color": color, "type": "bar"})
        return schema

    def _build_meta(self):
        """Build the profile metadata dict."""
        meta = {
            "processType": 0,
            "product": "mach",
            "stackwalk": 0,
            "version": 27,
            "preprocessedProfileVersion": 47,
            "symbolicationNotSupported": True,
            "interval": self.poll_interval * 1000,
            "startTime": self.start_timestamp * 1000,
            "profilingStartTime": 0,
            "logicalCPUs": psutil.cpu_count(logical=True),
            "physicalCPUs": psutil.cpu_count(logical=False),
            "mainMemory": psutil.virtual_memory()[0],
            "categories": [
                {
                    "name": "Other",
                    "color": "grey",
                    "subcategories": ["Other"],
                },
                {
                    "name": "Phases",
                    "color": "grey",
                    "subcategories": ["Other"],
                },
                {
                    "name": "Tasks",
                    "color": "grey",
                    "subcategories": ["Other"],
                },
            ],
            "markerSchema": [
                self._build_cpu_schema(),
                {
                    "name": "Phase",
                    "tooltipLabel": "{marker.data.phase}",
                    "tableLabel": "{marker.name} — {marker.data.phase} — CPU time: {marker.data.cpuTime} ({marker.data.cpuPercent})",
                    "chartLabel": "{marker.data.phase}",
                    "display": [
                        "marker-chart",
                        "marker-table",
                        "timeline-overview",
                    ],
                    "data": [
                        {
                            "key": "cpuTime",
                            "label": "CPU Time",
                            "format": "duration",
                        },
                        {
                            "key": "cpuPercent",
                            "label": "CPU Percent",
                            "format": "string",
                        },
                    ],
                },
                {
                    "name": "Text",
                    "tooltipLabel": "{marker.name}",
                    "tableLabel": "{marker.name} — {marker.data.text}",
                    "chartLabel": "{marker.data.text}",
                    "display": ["marker-chart", "marker-table"],
                    "data": [
                        {
                            "key": "text",
                            "label": "Description",
                            "format": "string",
                        }
                    ],
                },
                {
                    "name": "Artifact",
                    "tableLabel": "{marker.data.filename} — {marker.data.size}",
                    "display": ["marker-chart", "marker-table"],
                    "data": [
                        {
                            "key": "filename",
                            "label": "Filename",
                            "format": "string",
                        },
                        {
                            "key": "size",
                            "label": "Size",
                            "format": "bytes",
                        },
                    ],
                },
                {
                    "name": "Mem",
                    "tooltipLabel": "{marker.name}",
                    "display": [],
                    "data": [
                        {"key": "used", "label": "Memory Used", "format": "bytes"},
                        {
                            "key": "cached",
                            "label": "Memory cached",
                            "format": "bytes",
                        },
                        {
                            "key": "buffers",
                            "label": "Memory buffers",
                            "format": "bytes",
                        },
                    ],
                    "graphs": [
                        {"key": "used", "color": "orange", "type": "line-filled"}
                    ],
                },
                {
                    "name": "IO",
                    "tooltipLabel": "{marker.name}",
                    "display": [],
                    "data": [
                        {
                            "key": "write_bytes",
                            "label": "Written",
                            "format": "bytes",
                        },
                        {
                            "key": "write_count",
                            "label": "Write count",
                            "format": "integer",
                        },
                        {"key": "read_bytes", "label": "Read", "format": "bytes"},
                        {
                            "key": "read_count",
                            "label": "Read count",
                            "format": "integer",
                        },
                    ],
                    "graphs": [
                        {"key": "read_bytes", "color": "green", "type": "bar"},
                        {"key": "write_bytes", "color": "red", "type": "bar"},
                    ],
                },
                {
                    "name": "NetIO",
                    "tooltipLabel": "{marker.name}",
                    "display": [],
                    "data": [
                        {
                            "key": "sent_bytes",
                            "label": "Sent",
                            "format": "bytes",
                        },
                        {
                            "key": "sent_count",
                            "label": "Packets sent",
                            "format": "integer",
                        },
                        {
                            "key": "recv_bytes",
                            "label": "Received",
                            "format": "bytes",
                        },
                        {
                            "key": "recv_count",
                            "label": "Packets received",
                            "format": "integer",
                        },
                    ],
                    "graphs": [
                        {"key": "recv_bytes", "color": "blue", "type": "bar"},
                        {"key": "sent_bytes", "color": "orange", "type": "bar"},
                    ],
                },
                {
                    "name": "Process",
                    "chartLabel": "{marker.data.cmd}",
                    "tooltipLabel": "{marker.name}",
                    "tableLabel": "{marker.data.cmd}",
                    "display": ["marker-chart", "marker-table"],
                    "data": [
                        {
                            "key": "cmd",
                            "label": "Command line",
                            "format": "string",
                        },
                        {
                            "key": "pid",
                            "label": "Process ID",
                            "format": "pid",
                        },
                        {
                            "key": "ppid",
                            "label": "Parent process ID",
                            "format": "pid",
                        },
                    ],
                },
                {
                    "name": "Interval",
                    "tooltipLabel": "{marker.name}",
                    "display": [],
                    "data": [
                        {
                            "key": "interval",
                            "label": "Interval",
                            "format": "duration",
                        }
                    ],
                    "graphs": [{"key": "interval", "color": "purple", "type": "line"}],
                },
                {
                    "name": "sccache",
                    "tooltipLabel": "{marker.data.status}: {marker.data.file}",
                    "tableLabel": "{marker.data.status}: {marker.data.file}",
                    "chartLabel": "{marker.data.file}",
                    "display": ["marker-chart", "marker-table"],
                    "colorField": "color",
                    "data": [
                        {
                            "key": "file",
                            "label": "File",
                            "format": "string",
                        },
                        {
                            "key": "status",
                            "label": "Status",
                            "format": "string",
                        },
                        {
                            "key": "hash_time",
                            "label": "Hash Time",
                            "format": "duration",
                        },
                        {
                            "key": "lookup_time",
                            "label": "Lookup Time",
                            "format": "duration",
                        },
                        {
                            "key": "compile_time",
                            "label": "Compile Time",
                            "format": "duration",
                        },
                        {
                            "key": "artifact_time",
                            "label": "Artifact Creation Time",
                            "format": "duration",
                        },
                        {
                            "key": "write_time",
                            "label": "Cache Write Time",
                            "format": "duration",
                        },
                        {
                            "key": "color",
                            "hidden": True,
                        },
                    ],
                },
            ],
            "usesOnlyOneStackType": True,
        }
        names = {schema["name"] for schema in meta["markerSchema"]}
        for schema in [
            *self.marker_schemas.values(),
            *self._children_marker_schemas.values(),
        ]:
            if schema["name"] not in names:
                names.add(schema["name"])
                meta["markerSchema"].append(schema)
        for key in self.metadata:
            meta[key] = self.metadata[key]
        return meta

    def _build_thread(self):
        """Build the base thread dict for the profile."""
        return {
            "processType": "default",
            "processName": "mach",
            "processStartupTime": 0,
            "processShutdownTime": None,
            "registerTime": 0,
            "unregisterTime": None,
            "pausedRanges": [],
            "showMarkersInTimeline": True,
            "name": "",
            "isMainThread": False,
            "pid": "0",
            "tid": 0,
            "samples": {
                "weightType": "samples",
                "weight": None,
                "stack": [],
                "time": [],
                "length": 0,
            },
            "stackTable": {
                "frame": [0],
                "prefix": [None],
                "category": [0],
                "subcategory": [0],
                "length": 1,
            },
            "frameTable": {
                "address": [-1],
                "inlineDepth": [0],
                "category": [None],
                "subcategory": [0],
                "func": [0],
                "nativeSymbol": [None],
                "innerWindowID": [0],
                "implementation": [None],
                "line": [None],
                "column": [None],
                "length": 1,
            },
            "funcTable": {
                "isJS": [False],
                "relevantForJS": [False],
                "name": [0],
                "resource": [-1],
                "fileName": [None],
                "lineNumber": [None],
                "columnNumber": [None],
                "length": 1,
            },
            "resourceTable": {
                "lib": [],
                "name": [],
                "host": [],
                "type": [],
                "length": 0,
            },
            "nativeSymbols": {
                "libIndex": [],
                "address": [],
                "name": [],
                "functionSize": [],
                "length": 0,
            },
        }

    def _measurement_markers(self, m):
        """Yield (name, start, end, data) tuples for a single measurement."""
        fp = self._format_percent

        # CPU
        cpu_data = {
            "type": "CPU",
            "cpuPercent": fp(sum(list(m.cpu_percent)) / len(m.cpu_percent)),
        }
        # Due to inconsistencies in the sampling rate, sometimes the
        # cpu_times add up to more than 100%, causing annoying
        # spikes in the CPU use charts. Avoid them by dividing the
        # values by the total if it is above 1.
        total = 0
        for field in ["nice", "user", "system", "iowait", "softirq", "idle"]:
            if hasattr(m.cpu_times[0], field):
                total += sum(getattr(core, field) for core in m.cpu_times) / (
                    m.end - m.start
                )
        divisor = total if total > 1 else 1
        total = 0
        for field in ["nice", "user", "system", "iowait", "softirq"]:
            if hasattr(m.cpu_times[0], field):
                total += (
                    sum(getattr(core, field) for core in m.cpu_times)
                    / (m.end - m.start)
                    / divisor
                )
                cpu_data[field] = round(total, 3)
        for field in ["nice", "user", "system", "iowait", "idle"]:
            if hasattr(m.cpu_times[0], field):
                cpu_data[field + "_pct"] = fp(
                    100
                    * sum(getattr(core, field) for core in m.cpu_times)
                    / (m.end - m.start)
                    / len(m.cpu_times)
                )
        yield ("CPU Use", m.start, m.end, cpu_data)

        # Memory
        mem_data = {"type": "Mem", "used": m.virt.used}
        if hasattr(m.virt, "cached"):
            mem_data["cached"] = m.virt.cached
        if hasattr(m.virt, "buffers"):
            mem_data["buffers"] = m.virt.buffers
        yield ("Memory", m.start, m.end, mem_data)

        # IO
        yield (
            "IO",
            m.start,
            m.end,
            {
                "type": "IO",
                "read_count": m.io.read_count,
                "read_bytes": m.io.read_bytes,
                "write_count": m.io.write_count,
                "write_bytes": m.io.write_bytes,
            },
        )

        # Network IO
        yield (
            "NetIO",
            m.start,
            m.end,
            {
                "type": "NetIO",
                "recv_count": m.net_io.packets_recv,
                "recv_bytes": m.net_io.bytes_recv,
                "sent_count": m.net_io.packets_sent,
                "sent_bytes": m.net_io.bytes_sent,
            },
        )

        # Sampling interval
        yield (
            "Sampling Interval",
            m.end,
            None,
            {
                "type": "Interval",
                "interval": round((m.end - m.start) * 1000),
            },
        )

    def as_profile(self):
        """Convert the recorded data to an object suitable for import into the firefox profiler"""
        profile_time = time.monotonic()
        start_time = self.start_time
        firstThread = self._build_thread()
        firstThread["stringArray"] = ["(root)"]
        firstThread["markers"] = {
            "data": [],
            "name": [],
            "startTime": [],
            "endTime": [],
            "phase": [],
            "category": [],
            "stack": [],
            "length": 0,
        }
        profile = {
            "meta": self._build_meta(),
            "libs": [],
            "threads": [firstThread],
            "counters": [],
        }
        markers = firstThread["markers"]

        def get_string_index(string):
            stringArray = firstThread["stringArray"]
            try:
                return stringArray.index(string)
            except ValueError:
                stringArray.append(string)
                return len(stringArray) - 1

        def parse_stack(stack_string):
            """Parse a JavaScript stack trace into structured format.

            Supports two formats:
            1. JavaScript Error.stack format: "func@file:line:col\nfunc@file:line:col\n..."
            2. Normalized nsIStackFrame format: "file:func:line\nfile:func:line\n..."
            Returns an array of frame dicts.
            """
            if not stack_string:
                return None

            frames = []
            for line in stack_string.strip().split("\n"):
                if not line:
                    continue

                file_name = None
                func_part = None
                line_num = None
                col_num = None

                # Parse "func@file:line:col" (JavaScript Error.stack format)
                if "@" in line:
                    func_part, location = line.rsplit("@", 1)
                    func_part = func_part.strip()

                    # Parse "file:line:col"
                    parts = location.rsplit(":", 2)
                    if len(parts) == 3:
                        file_name, line_str, col_str = parts
                        try:
                            line_num = int(line_str)
                            col_num = int(col_str)
                        except ValueError:
                            pass
                    elif len(parts) == 2:
                        file_name, line_str = parts
                        try:
                            line_num = int(line_str)
                        except ValueError:
                            pass
                    else:
                        file_name = location
                else:
                    # Parse "file:func:line" (normalized nsIStackFrame format)
                    parts = line.rsplit(":", 2)
                    if len(parts) == 3:
                        file_name, func_part, line_str = parts
                        try:
                            line_num = int(line_str)
                        except ValueError:
                            func_part = line.strip()
                            file_name = None
                    else:
                        func_part = line.strip()

                frame_dict = {"is_js": True}
                if func_part:
                    frame_dict["function"] = func_part
                if file_name:
                    frame_dict["file"] = file_name
                if line_num is not None:
                    frame_dict["line"] = line_num
                if col_num is not None:
                    frame_dict["column"] = col_num
                frames.append(frame_dict)

            return frames

        def get_stack_index(stack_frames):
            """Get a stack index from a structured stack (array of frame dicts).

            Each frame dict contains:
            - function: function name (optional)
            - module: module/library name (optional)
            - file: source file path (optional)
            - line: line number (optional)
            - column: column number (optional)
            - offset: hex offset for unsymbolicated frames (optional)
            - inlined: boolean indicating if this is an inlined frame (optional)
            - is_js: boolean indicating if this is a JavaScript frame (optional)

            Returns the index of the innermost stack frame, or None if stack_frames is empty.
            """
            if not stack_frames:
                return None

            stackTable = firstThread["stackTable"]
            frameTable = firstThread["frameTable"]
            funcTable = firstThread["funcTable"]
            resourceTable = firstThread["resourceTable"]
            nativeSymbols = firstThread["nativeSymbols"]

            # Build stack from outermost to innermost
            stack_index = None
            inline_depth = 0

            for frame_data in reversed(stack_frames):
                # Handle inline depth tracking
                if frame_data.get("inlined"):
                    inline_depth += 1
                else:
                    inline_depth = 0

                # Get frame components
                module_name = frame_data.get("module")
                file_name = frame_data.get("file")
                line_num = frame_data.get("line")
                col_num = frame_data.get("column")
                is_js = frame_data.get("is_js", False)

                # Get offsets - different handling for native vs JIT frames
                module_offset = frame_data.get("module_offset")
                function_offset = frame_data.get("function_offset")
                raw_offset = frame_data.get("offset")  # For JIT frames without module

                func_name = frame_data.get("function")
                if not func_name and (offset := module_offset or raw_offset):
                    func_name = hex(offset)

                # Get or create resource for the module or file. Native frames
                # with a function but no module (e.g. LSan log frames) get an
                # "unknown" resource so the front-end treats them as resolved.
                resource_index = -1
                if is_js:
                    resource_name = file_name
                else:
                    resource_name = module_name or "unknown"
                if resource_name:
                    # Find existing resource
                    for i, name_idx in enumerate(resourceTable["name"]):
                        if firstThread["stringArray"][name_idx] == resource_name:
                            resource_index = i
                            break
                    else:
                        # Create new resource if not found
                        resource_index = resourceTable["length"]
                        resourceTable["lib"].append(None)
                        resourceTable["name"].append(get_string_index(resource_name))
                        resourceTable["host"].append(None)
                        # Possible resourceTypes:
                        # 0 = unknown, 1 = library, 2 = addon, 3 = webhost, 4 = otherhost, 5 = url
                        # https://github.com/firefox-devtools/profiler/blob/32cb6672c7ed47311e9d84963023d51f5147042b/src/profile-logic/data-structures.ts#L322
                        resource_type = 5 if is_js else 1
                        resourceTable["type"].append(resource_type)
                        resourceTable["length"] += 1

                # Create native symbol for unsymbolicated frames
                # nativeSymbols.address = module_offset - function_offset
                native_symbol_index = None
                if (
                    module_offset is not None
                    and function_offset is not None
                    and module_name
                ):
                    symbol_address = module_offset - function_offset

                    # Check if this native symbol already exists
                    for i in range(nativeSymbols["length"]):
                        if (
                            nativeSymbols["libIndex"][i] == resource_index
                            and nativeSymbols["address"][i] == symbol_address
                        ):
                            native_symbol_index = i
                            break
                    else:
                        # Create new native symbol if not found
                        native_symbol_index = nativeSymbols["length"]
                        nativeSymbols["libIndex"].append(resource_index)
                        nativeSymbols["address"].append(symbol_address)
                        nativeSymbols["name"].append(get_string_index(func_name))
                        nativeSymbols["functionSize"].append(None)
                        nativeSymbols["length"] += 1

                # Get or create func index
                func_name_index = get_string_index(func_name)
                file_name_index = get_string_index(file_name) if file_name else None

                for i, name_idx in enumerate(funcTable["name"]):
                    if (
                        name_idx == func_name_index
                        and funcTable["resource"][i] == resource_index
                        and funcTable["fileName"][i] == file_name_index
                        and funcTable["lineNumber"][i] == line_num
                    ):
                        func_index = i
                        break
                else:
                    func_index = funcTable["length"]
                    funcTable["isJS"].append(is_js)
                    funcTable["relevantForJS"].append(is_js)
                    funcTable["name"].append(func_name_index)
                    funcTable["resource"].append(resource_index)
                    funcTable["fileName"].append(file_name_index)
                    funcTable["lineNumber"].append(line_num)
                    funcTable["columnNumber"].append(col_num)
                    funcTable["length"] += 1

                # Get or create frame index
                # frameTable.address = module_offset for native frames, or offset for JIT frames
                frame_address = module_offset or raw_offset or -1
                for i, func_idx in enumerate(frameTable["func"]):
                    if (
                        func_idx == func_index
                        and frameTable["line"][i] == line_num
                        and frameTable["column"][i] == col_num
                        and frameTable["inlineDepth"][i] == inline_depth
                        and frameTable["nativeSymbol"][i] == native_symbol_index
                        and frameTable["address"][i] == frame_address
                    ):
                        frame_index = i
                        break
                else:
                    frame_index = frameTable["length"]
                    frameTable["address"].append(frame_address)
                    frameTable["inlineDepth"].append(inline_depth)
                    frameTable["category"].append(self.OTHER_CATEGORY)
                    frameTable["subcategory"].append(0)
                    frameTable["func"].append(func_index)
                    frameTable["nativeSymbol"].append(native_symbol_index)
                    frameTable["innerWindowID"].append(0)
                    frameTable["implementation"].append(None)
                    frameTable["line"].append(line_num)
                    frameTable["column"].append(col_num)
                    frameTable["length"] += 1

                # Create stack entry
                new_stack_index = stackTable["length"]
                stackTable["frame"].append(frame_index)
                stackTable["prefix"].append(stack_index)
                stackTable["category"].append(0)
                stackTable["subcategory"].append(0)
                stackTable["length"] += 1
                stack_index = new_stack_index

            return stack_index

        def add_marker(
            name_index,
            start,
            end,
            data,
            category_index=self.OTHER_CATEGORY,
            precision=None,
        ):
            # The precision argument allows setting how many digits after the
            # decimal point are desired.
            # For resource use samples where we sample with a timer, an integer
            # number of ms is good enough.
            # For short duration markers, the profiler front-end may show up to
            # 3 digits after the decimal point (ie. µs precision).
            markers["startTime"].append(round((start - start_time) * 1000, precision))
            if end is None:
                markers["endTime"].append(None)
                # 0 = Instant marker
                markers["phase"].append(0)
            else:
                markers["endTime"].append(round((end - start_time) * 1000, precision))
                # 1 = marker with start and end times, 2 = start but no end.
                markers["phase"].append(1)
            markers["category"].append(category_index)
            markers["name"].append(name_index)

            # Extract and process stack if present
            stack_index = None
            if isinstance(data, dict) and "stack" in data:
                stack = data["stack"]
                del data["stack"]

                # Convert string stack to structured format if needed
                if isinstance(stack, str):
                    stack = parse_stack(stack)

                stack_index = get_stack_index(stack)

                # Add cause object to marker data for processed profile format
                if stack_index is not None:
                    data["cause"] = {
                        "time": markers["startTime"][-1],
                        "stack": stack_index,
                    }

            markers["data"].append(data)
            markers["stack"].append(stack_index)
            markers["length"] = markers["length"] + 1

        for m in self.measurements:
            if m.end - m.start < self.poll_interval / 10:
                continue
            for name, start, end, markerData in self._measurement_markers(m):
                add_marker(get_string_index(name), start, end, markerData)

        # Create markers for phases
        phase_string_index = get_string_index("Phase")
        for phase, v in self.phases.items():
            markerData = {"type": "Phase", "phase": phase}

            cpu_percent_cores = self.aggregate_cpu_percent(phase=phase)
            if cpu_percent_cores:
                markerData["cpuPercent"] = self._format_percent(
                    sum(cpu_percent_cores) / len(cpu_percent_cores)
                )

            cpu_times = [list(c) for c in self.aggregate_cpu_times(phase=phase)]
            cpu_times_sum = [0.0] * self._cpu_times_len
            for i in range(0, self._cpu_times_len):
                cpu_times_sum[i] = sum(core[i] for core in cpu_times)
            total_cpu_time_ms = sum(cpu_times_sum) * 1000
            if total_cpu_time_ms > 0:
                markerData["cpuTime"] = total_cpu_time_ms

            add_marker(
                phase_string_index, v[0], v[1], markerData, self.PHASE_CATEGORY, 3
            )

        process_string_index = get_string_index("process")
        for pid, start, end, cmd, ppid in self.processes:
            markerData = {"type": "Process", "pid": pid, "ppid": ppid, "cmd": cmd}
            add_marker(process_string_index, start, end, markerData)
        # Add generic markers
        for name, start, end, data, category in self.markers:
            # data can be a dictionary containing the marker payload or a plain text value
            markerData = (
                data if isinstance(data, dict) else {"type": "Text", "text": str(data)}
            )
            add_marker(get_string_index(name), start, end, markerData, category, 3)
        if self.events:
            event_string_index = get_string_index("Event")
            for event in self.events:
                if len(event) == 3:
                    # Event with payload: (time, name, data)
                    event_time, name, data = event
                    add_marker(
                        get_string_index(name),
                        event_time,
                        None,
                        data,
                        self.OTHER_CATEGORY,
                        3,
                    )
                elif len(event) == 2:
                    # Simple event: (time, text)
                    event_time, text = event
                    add_marker(
                        event_string_index,
                        event_time,
                        None,
                        {"type": "Text", "text": text},
                        self.OTHER_CATEGORY,
                        3,
                    )

        # We may have spent some time generating this profile, and there might
        # also have been some time elapsed between stopping the resource
        # monitor, and the profile being created. These are hidden costs that
        # we should account for as best as possible, and the best we can do
        # is to make the profile contain information about this cost somehow.
        # We extend the profile end time up to now rather than self.end_time,
        # and add a phase covering that period of time.
        now = time.monotonic()
        profile["meta"]["profilingEndTime"] = round(
            (now - self.start_time) * 1000 + 0.0005, 3
        )
        markerData = {
            "type": "Phase",
            "phase": "teardown",
        }
        add_marker(
            phase_string_index, self.stop_time, now, markerData, self.PHASE_CATEGORY, 3
        )
        teardown_string_index = get_string_index("resourcemonitor")
        markerData = {
            "type": "Text",
            "text": "stop",
        }
        add_marker(
            teardown_string_index,
            self.stop_time,
            self.end_time,
            markerData,
            self.TASK_CATEGORY,
            3,
        )
        markerData = {
            "type": "Text",
            "text": "as_profile",
        }
        add_marker(
            teardown_string_index, profile_time, now, markerData, self.TASK_CATEGORY, 3
        )

        # Unfortunately, whatever the caller does with the profile (e.g. json)
        # or after that (hopefully, exit) is not going to be counted, but we
        # assume it's fast enough.
        return profile


def _forget_monitor_after_fork():
    # The process collecting resource usage, and the pipe to it, belong to the
    # parent. Stopping the inherited monitor would stop the parent's collection,
    # or deadlock on a lock held by one of the parent's threads, and keeping the
    # pipe open would keep the collection going if the parent exits without
    # stopping it.
    monitor = BaseResourceMonitor.instance
    if monitor is not None:
        monitor._running = False
        if monitor._process:
            monitor._pipe.close()
            monitor._process = None
        BaseResourceMonitor.instance = None


if hasattr(os, "register_at_fork"):
    os.register_at_fork(after_in_child=_forget_monitor_after_fork)
