"""
This file is originally based on code from https://github.com/nylas/nylas-perftools, which is published under the following license:

The MIT License (MIT)

Copyright (c) 2014 Nylas

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
"""

import atexit
import os
import platform
import random
import sys
import threading
import time
import uuid
from collections import deque
from contextlib import contextmanager

import sentry_sdk
from sentry_sdk._compat import PY33, PY311
from sentry_sdk._types import MYPY
from sentry_sdk.utils import (
    filename_for_module,
    handle_in_app_impl,
    logger,
    nanosecond_time,
)

if MYPY:
    from types import FrameType
    from typing import Any
    from typing import Callable
    from typing import Deque
    from typing import Dict
    from typing import Generator
    from typing import List
    from typing import Optional
    from typing import Set
    from typing import Sequence
    from typing import Tuple
    from typing_extensions import TypedDict
    import sentry_sdk.tracing

    ThreadId = str

    # The exact value of this id is not very meaningful. The purpose
    # of this id is to give us a compact and unique identifier for a
    # raw stack that can be used as a key to a dictionary so that it
    # can be used during the sampled format generation.
    RawStackId = Tuple[int, int]

    RawFrame = Tuple[
        str,  # abs_path
        Optional[str],  # module
        Optional[str],  # filename
        str,  # function
        int,  # lineno
    ]
    RawStack = Tuple[RawFrame, ...]
    RawSample = Sequence[Tuple[str, Tuple[RawStackId, RawStack]]]

    ProcessedSample = TypedDict(
        "ProcessedSample",
        {
            "elapsed_since_start_ns": str,
            "thread_id": ThreadId,
            "stack_id": int,
        },
    )

    ProcessedStack = List[int]

    ProcessedFrame = TypedDict(
        "ProcessedFrame",
        {
            "abs_path": str,
            "filename": Optional[str],
            "function": str,
            "lineno": int,
            "module": Optional[str],
        },
    )

    ProcessedThreadMetadata = TypedDict(
        "ProcessedThreadMetadata",
        {"name": str},
    )

    ProcessedProfile = TypedDict(
        "ProcessedProfile",
        {
            "frames": List[ProcessedFrame],
            "stacks": List[ProcessedStack],
            "samples": List[ProcessedSample],
            "thread_metadata": Dict[ThreadId, ProcessedThreadMetadata],
        },
    )

    ProfileContext = TypedDict(
        "ProfileContext",
        {"profile_id": str},
    )

try:
    from gevent.monkey import is_module_patched  # type: ignore
except ImportError:

    def is_module_patched(*args, **kwargs):
        # type: (*Any, **Any) -> bool
        # unable to import from gevent means no modules have been patched
        return False


_scheduler = None  # type: Optional[Scheduler]


def setup_profiler(options):
    # type: (Dict[str, Any]) -> None

    """
    `buffer_secs` determines the max time a sample will be buffered for
    `frequency` determines the number of samples to take per second (Hz)
    """

    global _scheduler

    if _scheduler is not None:
        logger.debug("profiling is already setup")
        return

    if not PY33:
        logger.warn("profiling is only supported on Python >= 3.3")
        return

    frequency = 101

    if is_module_patched("threading") or is_module_patched("_thread"):
        # If gevent has patched the threading modules then we cannot rely on
        # them to spawn a native thread for sampling.
        # Instead we default to the GeventScheduler which is capable of
        # spawning native threads within gevent.
        default_profiler_mode = GeventScheduler.mode
    else:
        default_profiler_mode = ThreadScheduler.mode

    profiler_mode = options["_experiments"].get("profiler_mode", default_profiler_mode)

    if (
        profiler_mode == ThreadScheduler.mode
        # for legacy reasons, we'll keep supporting sleep mode for this scheduler
        or profiler_mode == "sleep"
    ):
        _scheduler = ThreadScheduler(frequency=frequency)
    elif profiler_mode == GeventScheduler.mode:
        try:
            _scheduler = GeventScheduler(frequency=frequency)
        except ImportError:
            raise ValueError("Profiler mode: {} is not available".format(profiler_mode))
    else:
        raise ValueError("Unknown profiler mode: {}".format(profiler_mode))

    _scheduler.setup()

    atexit.register(teardown_profiler)


def teardown_profiler():
    # type: () -> None

    global _scheduler

    if _scheduler is not None:
        _scheduler.teardown()

    _scheduler = None


# We want to impose a stack depth limit so that samples aren't too large.
MAX_STACK_DEPTH = 128


def extract_stack(
    frame,  # type: Optional[FrameType]
    cwd,  # type: str
    prev_cache=None,  # type: Optional[Tuple[RawStackId, RawStack, Deque[FrameType]]]
    max_stack_depth=MAX_STACK_DEPTH,  # type: int
):
    # type: (...) -> Tuple[RawStackId, RawStack, Deque[FrameType]]
    """
    Extracts the stack starting the specified frame. The extracted stack
    assumes the specified frame is the top of the stack, and works back
    to the bottom of the stack.

    In the event that the stack is more than `MAX_STACK_DEPTH` frames deep,
    only the first `MAX_STACK_DEPTH` frames will be returned.
    """

    frames = deque(maxlen=max_stack_depth)  # type: Deque[FrameType]

    while frame is not None:
        frames.append(frame)
        frame = frame.f_back

    if prev_cache is None:
        stack = tuple(extract_frame(frame, cwd) for frame in frames)
    else:
        _, prev_stack, prev_frames = prev_cache
        prev_depth = len(prev_frames)
        depth = len(frames)

        # We want to match the frame found in this sample to the frames found in the
        # previous sample. If they are the same (using the `is` operator), we can
        # skip the expensive work of extracting the frame information and reuse what
        # we extracted during the last sample.
        #
        # Make sure to keep in mind that the stack is ordered from the inner most
        # from to the outer most frame so be careful with the indexing.
        stack = tuple(
            prev_stack[i]
            if i >= 0 and frame is prev_frames[i]
            else extract_frame(frame, cwd)
            for i, frame in zip(range(prev_depth - depth, prev_depth), frames)
        )

    # Instead of mapping the stack into frame ids and hashing
    # that as a tuple, we can directly hash the stack.
    # This saves us from having to generate yet another list.
    # Additionally, using the stack as the key directly is
    # costly because the stack can be large, so we pre-hash
    # the stack, and use the hash as the key as this will be
    # needed a few times to improve performance.
    #
    # To Reduce the likelihood of hash collisions, we include
    # the stack depth. This means that only stacks of the same
    # depth can suffer from hash collisions.
    stack_id = len(stack), hash(stack)

    return stack_id, stack, frames


def extract_frame(frame, cwd):
    # type: (FrameType, str) -> RawFrame
    abs_path = frame.f_code.co_filename

    try:
        module = frame.f_globals["__name__"]
    except Exception:
        module = None

    # namedtuples can be many times slower when initialing
    # and accessing attribute so we opt to use a tuple here instead
    return (
        # This originally was `os.path.abspath(abs_path)` but that had
        # a large performance overhead.
        #
        # According to docs, this is equivalent to
        # `os.path.normpath(os.path.join(os.getcwd(), path))`.
        # The `os.getcwd()` call is slow here, so we precompute it.
        #
        # Additionally, since we are using normalized path already,
        # we skip calling `os.path.normpath` entirely.
        os.path.join(cwd, abs_path),
        module,
        filename_for_module(module, abs_path) or None,
        get_frame_name(frame),
        frame.f_lineno,
    )


if PY311:

    def get_frame_name(frame):
        # type: (FrameType) -> str
        return frame.f_code.co_qualname  # type: ignore

else:

    def get_frame_name(frame):
        # type: (FrameType) -> str

        f_code = frame.f_code
        co_varnames = f_code.co_varnames

        # co_name only contains the frame name.  If the frame was a method,
        # the class name will NOT be included.
        name = f_code.co_name

        # if it was a method, we can get the class name by inspecting
        # the f_locals for the `self` argument
        try:
            if (
                # the co_varnames start with the frame's positional arguments
                # and we expect the first to be `self` if its an instance method
                co_varnames
                and co_varnames[0] == "self"
                and "self" in frame.f_locals
            ):
                for cls in frame.f_locals["self"].__class__.__mro__:
                    if name in cls.__dict__:
                        return "{}.{}".format(cls.__name__, name)
        except AttributeError:
            pass

        # if it was a class method, (decorated with `@classmethod`)
        # we can get the class name by inspecting the f_locals for the `cls` argument
        try:
            if (
                # the co_varnames start with the frame's positional arguments
                # and we expect the first to be `cls` if its a class method
                co_varnames
                and co_varnames[0] == "cls"
                and "cls" in frame.f_locals
            ):
                for cls in frame.f_locals["cls"].__mro__:
                    if name in cls.__dict__:
                        return "{}.{}".format(cls.__name__, name)
        except AttributeError:
            pass

        # nothing we can do if it is a staticmethod (decorated with @staticmethod)

        # we've done all we can, time to give up and return what we have
        return name


MAX_PROFILE_DURATION_NS = int(3e10)  # 30 seconds


class Profile(object):
    def __init__(
        self,
        scheduler,  # type: Scheduler
        transaction,  # type: sentry_sdk.tracing.Transaction
        hub=None,  # type: Optional[sentry_sdk.Hub]
    ):
        # type: (...) -> None
        self.scheduler = scheduler
        self.transaction = transaction
        self.hub = hub
        self.active_thread_id = None  # type: Optional[int]
        self.start_ns = 0  # type: int
        self.stop_ns = 0  # type: int
        self.active = False  # type: bool
        self.event_id = uuid.uuid4().hex  # type: str

        self.indexed_frames = {}  # type: Dict[RawFrame, int]
        self.indexed_stacks = {}  # type: Dict[RawStackId, int]
        self.frames = []  # type: List[ProcessedFrame]
        self.stacks = []  # type: List[ProcessedStack]
        self.samples = []  # type: List[ProcessedSample]

        transaction._profile = self

    def get_profile_context(self):
        # type: () -> ProfileContext
        return {"profile_id": self.event_id}

    def __enter__(self):
        # type: () -> None
        hub = self.hub or sentry_sdk.Hub.current

        _, scope = hub._stack[-1]
        old_profile = scope.profile
        scope.profile = self

        self._context_manager_state = (hub, scope, old_profile)

        self.start_ns = nanosecond_time()
        self.scheduler.start_profiling(self)

    def __exit__(self, ty, value, tb):
        # type: (Optional[Any], Optional[Any], Optional[Any]) -> None
        self.scheduler.stop_profiling(self)
        self.stop_ns = nanosecond_time()

        _, scope, old_profile = self._context_manager_state
        del self._context_manager_state

        scope.profile = old_profile

    def write(self, ts, sample):
        # type: (int, RawSample) -> None
        if ts < self.start_ns:
            return

        offset = ts - self.start_ns
        if offset > MAX_PROFILE_DURATION_NS:
            return

        elapsed_since_start_ns = str(offset)

        for tid, (stack_id, stack) in sample:
            # Check if the stack is indexed first, this lets us skip
            # indexing frames if it's not necessary
            if stack_id not in self.indexed_stacks:
                for frame in stack:
                    if frame not in self.indexed_frames:
                        self.indexed_frames[frame] = len(self.indexed_frames)
                        self.frames.append(
                            {
                                "abs_path": frame[0],
                                "module": frame[1],
                                "filename": frame[2],
                                "function": frame[3],
                                "lineno": frame[4],
                            }
                        )

                self.indexed_stacks[stack_id] = len(self.indexed_stacks)
                self.stacks.append([self.indexed_frames[frame] for frame in stack])

            self.samples.append(
                {
                    "elapsed_since_start_ns": elapsed_since_start_ns,
                    "thread_id": tid,
                    "stack_id": self.indexed_stacks[stack_id],
                }
            )

    def process(self):
        # type: () -> ProcessedProfile

        # This collects the thread metadata at the end of a profile. Doing it
        # this way means that any threads that terminate before the profile ends
        # will not have any metadata associated with it.
        thread_metadata = {
            str(thread.ident): {
                "name": str(thread.name),
            }
            for thread in threading.enumerate()
        }  # type: Dict[str, ProcessedThreadMetadata]

        return {
            "frames": self.frames,
            "stacks": self.stacks,
            "samples": self.samples,
            "thread_metadata": thread_metadata,
        }

    def to_json(self, event_opt, options):
        # type: (Any, Dict[str, Any]) -> Dict[str, Any]
        profile = self.process()

        handle_in_app_impl(
            profile["frames"], options["in_app_exclude"], options["in_app_include"]
        )

        return {
            "environment": event_opt.get("environment"),
            "event_id": self.event_id,
            "platform": "python",
            "profile": profile,
            "release": event_opt.get("release", ""),
            "timestamp": event_opt["timestamp"],
            "version": "1",
            "device": {
                "architecture": platform.machine(),
            },
            "os": {
                "name": platform.system(),
                "version": platform.release(),
            },
            "runtime": {
                "name": platform.python_implementation(),
                "version": platform.python_version(),
            },
            "transactions": [
                {
                    "id": event_opt["event_id"],
                    "name": self.transaction.name,
                    # we start the transaction before the profile and this is
                    # the transaction start time relative to the profile, so we
                    # hardcode it to 0 until we can start the profile before
                    "relative_start_ns": "0",
                    # use the duration of the profile instead of the transaction
                    # because we end the transaction after the profile
                    "relative_end_ns": str(self.stop_ns - self.start_ns),
                    "trace_id": self.transaction.trace_id,
                    "active_thread_id": str(
                        self.transaction._active_thread_id
                        if self.active_thread_id is None
                        else self.active_thread_id
                    ),
                }
            ],
        }


class Scheduler(object):
    mode = "unknown"

    def __init__(self, frequency):
        # type: (int) -> None
        self.interval = 1.0 / frequency

        self.sampler = self.make_sampler()

        self.new_profiles = deque()  # type: Deque[Profile]
        self.active_profiles = set()  # type: Set[Profile]

    def __enter__(self):
        # type: () -> Scheduler
        self.setup()
        return self

    def __exit__(self, ty, value, tb):
        # type: (Optional[Any], Optional[Any], Optional[Any]) -> None
        self.teardown()

    def setup(self):
        # type: () -> None
        raise NotImplementedError

    def teardown(self):
        # type: () -> None
        raise NotImplementedError

    def start_profiling(self, profile):
        # type: (Profile) -> None
        profile.active = True
        self.new_profiles.append(profile)

    def stop_profiling(self, profile):
        # type: (Profile) -> None
        profile.active = False

    def make_sampler(self):
        # type: () -> Callable[..., None]
        cwd = os.getcwd()

        # In Python3+, we can use the `nonlocal` keyword to rebind the value,
        # but this is not possible in Python2. To get around this, we wrap
        # the value in a list to allow updating this value each sample.
        last_sample = [
            {}
        ]  # type: List[Dict[int, Tuple[RawStackId, RawStack, Deque[FrameType]]]]

        def _sample_stack(*args, **kwargs):
            # type: (*Any, **Any) -> None
            """
            Take a sample of the stack on all the threads in the process.
            This should be called at a regular interval to collect samples.
            """
            # no profiles taking place, so we can stop early
            if not self.new_profiles and not self.active_profiles:
                # make sure to clear the cache if we're not profiling so we dont
                # keep a reference to the last stack of frames around
                last_sample[0] = {}
                return

            # This is the number of profiles we want to pop off.
            # It's possible another thread adds a new profile to
            # the list and we spend longer than we want inside
            # the loop below.
            #
            # Also make sure to set this value before extracting
            # frames so we do not write to any new profiles that
            # were started after this point.
            new_profiles = len(self.new_profiles)

            now = nanosecond_time()

            raw_sample = {
                tid: extract_stack(frame, cwd, last_sample[0].get(tid))
                for tid, frame in sys._current_frames().items()
            }

            # make sure to update the last sample so the cache has
            # the most recent stack for better cache hits
            last_sample[0] = raw_sample

            sample = [
                (str(tid), (stack_id, stack))
                for tid, (stack_id, stack, _) in raw_sample.items()
            ]

            # Move the new profiles into the active_profiles set.
            #
            # We cannot directly add the to active_profiles set
            # in `start_profiling` because it is called from other
            # threads which can cause a RuntimeError when it the
            # set sizes changes during iteration without a lock.
            #
            # We also want to avoid using a lock here so threads
            # that are starting profiles are not blocked until it
            # can acquire the lock.
            for _ in range(new_profiles):
                self.active_profiles.add(self.new_profiles.popleft())

            inactive_profiles = []

            for profile in self.active_profiles:
                if profile.active:
                    profile.write(now, sample)
                else:
                    # If a thread is marked inactive, we buffer it
                    # to `inactive_profiles` so it can be removed.
                    # We cannot remove it here as it would result
                    # in a RuntimeError.
                    inactive_profiles.append(profile)

            for profile in inactive_profiles:
                self.active_profiles.remove(profile)

        return _sample_stack


class ThreadScheduler(Scheduler):
    """
    This scheduler is based on running a daemon thread that will call
    the sampler at a regular interval.
    """

    mode = "thread"
    name = "sentry.profiler.ThreadScheduler"

    def __init__(self, frequency):
        # type: (int) -> None
        super(ThreadScheduler, self).__init__(frequency=frequency)

        # used to signal to the thread that it should stop
        self.event = threading.Event()

        # make sure the thread is a daemon here otherwise this
        # can keep the application running after other threads
        # have exited
        self.thread = threading.Thread(name=self.name, target=self.run, daemon=True)

    def setup(self):
        # type: () -> None
        self.thread.start()

    def teardown(self):
        # type: () -> None
        self.event.set()
        self.thread.join()

    def run(self):
        # type: () -> None
        last = time.perf_counter()

        while True:
            if self.event.is_set():
                break

            self.sampler()

            # some time may have elapsed since the last time
            # we sampled, so we need to account for that and
            # not sleep for too long
            elapsed = time.perf_counter() - last
            if elapsed < self.interval:
                time.sleep(self.interval - elapsed)

            # after sleeping, make sure to take the current
            # timestamp so we can use it next iteration
            last = time.perf_counter()


class GeventScheduler(Scheduler):
    """
    This scheduler is based on the thread scheduler but adapted to work with
    gevent. When using gevent, it may monkey patch the threading modules
    (`threading` and `_thread`). This results in the use of greenlets instead
    of native threads.

    This is an issue because the sampler CANNOT run in a greenlet because
    1. Other greenlets doing sync work will prevent the sampler from running
    2. The greenlet runs in the same thread as other greenlets so when taking
       a sample, other greenlets will have been evicted from the thread. This
       results in a sample containing only the sampler's code.
    """

    mode = "gevent"
    name = "sentry.profiler.GeventScheduler"

    def __init__(self, frequency):
        # type: (int) -> None

        # This can throw an ImportError that must be caught if `gevent` is
        # not installed.
        from gevent.threadpool import ThreadPool  # type: ignore

        super(GeventScheduler, self).__init__(frequency=frequency)

        # used to signal to the thread that it should stop
        self.event = threading.Event()

        # Using gevent's ThreadPool allows us to bypass greenlets and spawn
        # native threads.
        self.pool = ThreadPool(1)

    def setup(self):
        # type: () -> None
        self.pool.spawn(self.run)

    def teardown(self):
        # type: () -> None
        self.event.set()
        self.pool.join()

    def run(self):
        # type: () -> None
        last = time.perf_counter()

        while True:
            if self.event.is_set():
                break

            self.sampler()

            # some time may have elapsed since the last time
            # we sampled, so we need to account for that and
            # not sleep for too long
            elapsed = time.perf_counter() - last
            if elapsed < self.interval:
                time.sleep(self.interval - elapsed)

            # after sleeping, make sure to take the current
            # timestamp so we can use it next iteration
            last = time.perf_counter()


def _should_profile(transaction, hub):
    # type: (sentry_sdk.tracing.Transaction, sentry_sdk.Hub) -> bool

    # The corresponding transaction was not sampled,
    # so don't generate a profile for it.
    if not transaction.sampled:
        return False

    # The profiler hasn't been properly initialized.
    if _scheduler is None:
        return False

    client = hub.client

    # The client is None, so we can't get the sample rate.
    if client is None:
        return False

    options = client.options
    profiles_sample_rate = options["_experiments"].get("profiles_sample_rate")

    # The profiles_sample_rate option was not set, so profiling
    # was never enabled.
    if profiles_sample_rate is None:
        return False

    return random.random() < float(profiles_sample_rate)


@contextmanager
def start_profiling(transaction, hub=None):
    # type: (sentry_sdk.tracing.Transaction, Optional[sentry_sdk.Hub]) -> Generator[None, None, None]
    hub = hub or sentry_sdk.Hub.current

    # if profiling was not enabled, this should be a noop
    if _should_profile(transaction, hub):
        assert _scheduler is not None
        with Profile(_scheduler, transaction, hub):
            yield
    else:
        yield
