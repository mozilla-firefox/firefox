# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import atexit
import copy
import logging
import os
import signal
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from concurrent.futures import wait as futures_wait
from concurrent.futures.process import _python_exit as futures_atexit
from itertools import chain
from math import ceil
from multiprocessing import get_context
from multiprocessing.queues import Queue
from subprocess import CalledProcessError

import mozpack.path as mozpath
from mozbuild.util import cpu_count
from mozversioncontrol import (
    InvalidRepoPath,
    MissingUpstreamRepo,
    get_repository_object,
)

from .errors import LintersNotConfigured, NoValidLinter
from .parser import Parser
from .pathutils import filterpaths, findobject
from .result import ResultSummary
from .types import supported_types

SHUTDOWN = False
orig_sigint = signal.getsignal(signal.SIGINT)

# Logger passed down into subprocesses
logger = logging.getLogger("lint")
handler = logging.StreamHandler()
formatter = logging.Formatter("%(levelname)s: %(message)s")
handler.setFormatter(formatter)
logger.addHandler(handler)

# Adapter for parent process.
log = logging.LoggerAdapter(logger, {"lintname": "mozlint", "pid": os.getpid()})


def _setup_logger(log, show_verbose):
    if show_verbose:
        formatter = logging.Formatter(
            "%(asctime)s.%(msecs)03d %(lintname)s (%(pid)s) | %(message)s", "%H:%M:%S"
        )
        logger.handlers[0].setFormatter(formatter)
        log.setLevel(logging.DEBUG)
    else:
        log.setLevel(logging.WARNING)


def _run_worker(config, paths, **lintargs):
    log = logging.LoggerAdapter(
        logger, {"lintname": config.get("name"), "pid": os.getpid()}
    )
    _setup_logger(log, lintargs.get("show_verbose"))
    lintargs["log"] = log
    result = ResultSummary(lintargs["root"])

    if SHUTDOWN:
        return result

    # Override warnings setup for code review
    # Only disactivating when code_review_warnings is set to False on a linter.yml in use
    if os.environ.get("CODE_REVIEW") == "1" and config.get(
        "code_review_warnings", True
    ):
        lintargs["show_warnings"] = True

    func = supported_types[config["type"]]
    start_time = time.monotonic()
    try:
        res = func(paths, config, **lintargs)
        # Some linters support fixed operations
        # dict returned - {"results":results,"fixed":fixed}
        if isinstance(res, dict):
            result.fixed += res["fixed"]
            res = res["results"] or []
        elif isinstance(res, list):
            res = res or []
        else:
            log.error(f"Unexpected result type received: {type(res)}")
            assert False
    except Exception:
        log.exception(f"{config['name']} failed")
        res = 1
    except (KeyboardInterrupt, SystemExit):
        return result
    finally:
        end_time = time.monotonic()
        log.debug(f"Finished in {end_time - start_time:.2f} seconds")
        sys.stdout.flush()

    if not isinstance(res, (list, tuple)):
        if res:
            result.failed_run.add(config["name"])
    else:
        for r in res:
            if not lintargs.get("show_warnings") and r.level == "warning":
                result.suppressed_warnings[r.path] += 1
                continue

            result.issues[r.path].append(r)

    return result


def _ancestors(path):
    parent = os.path.dirname(path)
    while parent != path:
        yield parent
        path, parent = parent, os.path.dirname(parent)


def _group_jobs(jobs):
    groups = []  # list of (job_list, path_set, ancestor_set) triples
    for job in jobs:
        paths = set(job[1])
        ancestors = {a for p in paths for a in _ancestors(p)}
        for group_jobs, group_paths, group_ancestors in groups:
            if (
                paths.isdisjoint(group_paths)
                and paths.isdisjoint(group_ancestors)
                and ancestors.isdisjoint(group_paths)
            ):
                break
        else:
            group_jobs, group_paths, group_ancestors = [], set(), set()
            groups.append((group_jobs, group_paths, group_ancestors))
        group_jobs.append(job)
        group_paths |= paths
        group_ancestors |= ancestors
    return [group_jobs for group_jobs, _, _ in groups]


class InterruptableQueue(Queue):
    """A multiprocessing.Queue that catches KeyboardInterrupt when a worker is
    blocking on it and returns None.

    This is needed to gracefully handle KeyboardInterrupts when a worker is
    blocking on ProcessPoolExecutor's call queue.
    """

    def __init__(self, *args, **kwargs):
        kwargs["ctx"] = get_context()
        super().__init__(*args, **kwargs)

    def get(self, *args, **kwargs):
        try:
            return Queue.get(self, *args, **kwargs)
        except KeyboardInterrupt:
            return None


def _worker_sigint_handler(signum, frame):
    """Sigint handler for the worker subprocesses.

    Tells workers not to process the extra jobs on the call queue that couldn't
    be canceled by the parent process.
    """
    global SHUTDOWN  # noqa PLW0603
    SHUTDOWN = True
    orig_sigint(signum, frame)


def wrap_futures_atexit():
    """Sometimes futures' atexit handler can spew tracebacks. This wrapper
    suppresses them."""
    try:
        futures_atexit()
    except Exception:
        # Generally `atexit` handlers aren't supposed to raise exceptions, but the
        # futures' handler can sometimes raise when the user presses `CTRL-C`. We
        # suppress all possible exceptions here so users have a nice experience
        # when canceling their lint run. Any exceptions raised by this function
        # won't be useful anyway.
        pass


atexit.unregister(futures_atexit)
atexit.register(wrap_futures_atexit)


class LintRoller:
    """Registers and runs linters.

    :param root: Path to which relative paths will be joined. If
                 unspecified, root will either be determined from
                 version control or cwd.
    :param lintargs: Arguments to pass to the underlying linter(s).
    """

    MAX_PATHS_PER_JOB = (
        50  # set a max size to prevent command lines that are too long on Windows
    )

    def __init__(
        self,
        root,
        exclude=None,
        third_party_exclude=None,
        include_thiry_party=False,
        setupargs=None,
        **lintargs,
    ):
        self.parse = Parser(root)
        try:
            self.vcs = get_repository_object(root)
        except InvalidRepoPath:
            self.vcs = None

        self.linters = []
        self._setup_done = set()
        self.lintargs = lintargs
        self.lintargs["root"] = root
        self._setupargs = setupargs or {}

        # result state
        self.result = ResultSummary(
            root,
            # Prevent failing on warnings when the --warnings parameter is set to "soft"
            fail_on_warnings=lintargs.get("show_warnings") != "soft",
        )

        self.root = root
        self.exclude = exclude or []
        self.third_party_exclude = third_party_exclude or []
        self.include_third_party = include_thiry_party

        _setup_logger(log, lintargs.get("show_verbose"))

    def read(self, paths):
        """Parse one or more linters and add them to the registry.

        :param paths: A path or iterable of paths to linter definitions.
        """
        if isinstance(paths, str):
            paths = (paths,)

        for linter in chain(*[self.parse(p) for p in paths]):
            # Add only the excludes present in paths
            linter["local_exclude"] = linter.get("exclude", [])[:]
            linter["global_exclude"] = self.exclude[:]
            # Add in our global excludes
            exclude = linter.setdefault("exclude", [])
            exclude.extend(self.exclude)
            # Add in third-party excludes if not configured otherwise
            if not self.include_third_party and not linter.get(
                "include_third_party", False
            ):
                exclude.extend(self.third_party_exclude)
            self.linters.append(linter)

    def setup(self, virtualenv_manager=None, names=None):
        """Run setup for applicable linters

        :param names: Only set up the linters with these names (optional).
        """
        if not self.linters:
            raise NoValidLinter

        for linter in self.linters:
            if "setup" not in linter or linter["name"] in self._setup_done:
                continue
            if names is not None and linter["name"] not in names:
                continue
            self._setup_done.add(linter["name"])

            try:
                setupargs = copy.deepcopy(self.lintargs)
                setupargs.update(self._setupargs)
                setupargs["name"] = linter["name"]
                setupargs["log"] = logging.LoggerAdapter(
                    logger, {"lintname": linter["name"], "pid": os.getpid()}
                )
                if virtualenv_manager is not None:
                    setupargs["virtualenv_manager"] = virtualenv_manager
                start_time = time.monotonic()
                res = (
                    findobject(
                        linter["setup"],
                        linter["path"],
                        self.lintargs.get("linter_paths"),
                    )(
                        **setupargs,
                    )
                    or 0
                )
                log.debug(
                    f"setup for {linter['name']} finished in "
                    f"{round(time.monotonic() - start_time, 2)} seconds"
                )
            except Exception:
                log.exception(f"{linter['name']} setup failed")
                res = 1

            if res > 0:
                self.result.failed_setup.add(linter["name"])
                self.result.skipped.add(linter["name"])
            elif res < 0:
                # Negative return code means the linter should be skipped for
                # reasons other than a failure.
                self.result.skipped.add(linter["name"])

        self.linters = [l for l in self.linters if l["name"] not in self.result.skipped]

        if self.result.failed_setup:
            log.error(
                "problem with lint setup, skipping {}".format(
                    ", ".join(sorted(self.result.failed_setup))
                )
            )
            return 1

        if not self.linters:
            log.error("all linters were skipped due to setup, nothing to do!")
            return 1

        return 0

    def should_lint_entire_tree(self, vcs_paths: set[str], linter: dict) -> bool:
        """Return `True` if the linter should be run on the entire tree."""
        # Don't lint the entire tree when `--fix` is passed to linters.
        if "fix" in self.lintargs and self.lintargs["fix"]:
            return False

        if self.lintargs.get("stdin_filename"):
            return False

        # Lint the whole tree when a `support-file` is modified.
        return any(
            os.path.isfile(p)
            and (
                mozpath.match(p, pattern)
                or mozpath.match(mozpath.relpath(p, self.root), pattern)
            )
            for pattern in linter.get("support-files", [])
            for p in vcs_paths
        )

    def _generate_jobs(self, paths, vcs_paths, num_procs):
        def __get_current_paths(path=self.root):
            return [os.path.join(path, p) for p in os.listdir(path)]

        """A job is of the form (<linter:dict>, <paths:list>)."""
        for linter in self.linters:
            if self.should_lint_entire_tree(vcs_paths, linter):
                lpaths = __get_current_paths()
                print(
                    "warning: {} support-file modified, linting entire tree "
                    "(press ctrl-c to cancel)".format(linter["name"])
                )
            elif paths == {os.path.abspath(self.root)}:
                # If the command line is ".", the path will match with the root
                # directory. In this case, get all the paths, so that we can
                # benefit from chunking below.
                lpaths = __get_current_paths()
            else:
                lpaths = paths.union(vcs_paths)

            lpaths = list(lpaths) or __get_current_paths(os.getcwd())
            if self.lintargs.get("use_filters", True):
                # Only the paths to lint are needed here. Workers filter their
                # own chunk again and compute the excludes at that point, so
                # skip the expensive glob expansion over the whole tree.
                lpaths, _ = filterpaths(
                    self.root,
                    lpaths,
                    include=linter["include"],
                    exclude=linter.get("exclude", []),
                    extensions=linter.get("extensions", []),
                    exclude_extensions=linter.get("exclude_extensions", []),
                    expand_excludes=False,
                )
                if not lpaths:
                    continue

            chunk_size = (
                min(self.MAX_PATHS_PER_JOB, int(ceil(len(lpaths) / num_procs))) or 1
            )
            if linter["type"] == "global":
                # Global linters lint the entire tree in one job.
                chunk_size = len(lpaths) or 1
            assert chunk_size > 0

            while lpaths:
                yield linter, lpaths[:chunk_size]
                lpaths = lpaths[chunk_size:]

    def _collect_results(self, future):
        if future.cancelled():
            return

        # Merge this job's results with our global ones.
        try:
            self.result.update(future.result())
        except Exception:
            log.exception("Sub-process raised an error:")

    def roll(
        self,
        paths=None,
        outgoing=None,
        workdir=None,
        rev=None,
        num_procs=None,
        virtualenv_manager=None,
    ):
        """Run all of the registered linters against the specified file paths.

        Linters that have files to lint are set up first, unless `setup` has
        already run for them.

        :param paths: An iterable of files and/or directories to lint.
        :param outgoing: Lint files touched by commits that are not on the remote repository.
        :param workdir: Lint all files touched in the working directory.
        :param num_procs: The number of processes to use. Default: cpu count
        :param virtualenv_manager: Passed to the linters' setup functions.
        :return: A :class:`~result.ResultSummary` instance.
        """
        if not self.linters:
            raise LintersNotConfigured

        self.result.reset()

        # Need to use a set in case vcs operations specify the same file
        # more than once.
        paths = paths or set()
        if isinstance(paths, str):
            paths = set([paths])
        elif isinstance(paths, (list, tuple)):
            paths = set(paths)

        if not self.vcs and (workdir or outgoing):
            log.error(
                "'{}' is not a known repository, can't use --workdir or --outgoing".format(
                    self.lintargs["root"]
                )
            )

        # Calculate files from VCS
        vcs_paths = set()
        try:
            if workdir:
                vcs_paths.update(self.vcs.get_changed_files("AM", mode=workdir))
            if rev:
                vcs_paths.update(self.vcs.get_changed_files("AM", rev=rev))
            if outgoing:
                upstream = outgoing if isinstance(outgoing, str) else None
                try:
                    vcs_paths.update(
                        self.vcs.get_outgoing_files("AM", upstream=upstream)
                    )
                except MissingUpstreamRepo:
                    log.warning(
                        "could not find default push, specify a remote for --outgoing"
                    )
        except CalledProcessError as e:
            msg = f"command failed: {' '.join(e.cmd)}"
            if e.output:
                msg = f"{msg}\n{e.output}"
            log.exception(msg)

        if not (paths or vcs_paths) and (workdir or outgoing):
            if os.environ.get("MOZ_AUTOMATION") and not os.environ.get(
                "PYTEST_CURRENT_TEST"
            ):
                raise Exception(
                    "Despite being a CI lint job, no files were linted. Is the task "
                    "missing explicit paths?"
                )

            log.warning("no files linted")
            return self.result

        # Make sure all paths are absolute. Join `paths` to cwd and `vcs_paths` to root.
        paths = set(map(os.path.abspath, paths))
        vcs_paths = set([
            os.path.join(self.root, p) if not os.path.isabs(p) else p for p in vcs_paths
        ])

        num_procs = num_procs or cpu_count()
        jobs = list(self._generate_jobs(paths, vcs_paths, num_procs))

        if jobs:
            self.setup(virtualenv_manager, names={linter["name"] for linter, _ in jobs})
            jobs = [job for job in jobs if job[0]["name"] not in self.result.skipped]

        # Make sure we never spawn more processes than we have jobs.
        num_procs = min(len(jobs), num_procs) or 1
        if sys.platform == "win32":
            # https://github.com/python/cpython/pull/13132
            num_procs = min(num_procs, 61)

        signal.signal(signal.SIGINT, _worker_sigint_handler)
        executor = ProcessPoolExecutor(num_procs)
        executor._call_queue = InterruptableQueue(executor._call_queue._maxsize)

        # Submit jobs to the worker pool. The _collect_results method will be
        # called when a job is finished. We store the futures so that they can
        # be canceled in the event of a KeyboardInterrupt.
        #
        # When fixing, partition jobs into groups where no job in a group
        # shares a path with, or lies inside a directory of, another job in
        # that group. Jobs within a group run in parallel safely; a
        # barrier between groups prevents concurrent writes to the same file by
        # different linters.
        futures = []

        def _parent_sigint_handler(signum, frame):
            """Sigint handler for the parent process.

            Cancels all jobs that have not yet been placed on the call queue.
            The parent process won't exit until all workers have terminated.
            Assuming the linters are implemented properly, this shouldn't take
            more than a couple seconds.
            """
            [f.cancel() for f in futures]
            executor.shutdown(wait=True)
            log.warning("\nnot all files were linted")
            signal.signal(signal.SIGINT, signal.SIG_IGN)

        signal.signal(signal.SIGINT, _parent_sigint_handler)

        if self.lintargs.get("fix"):
            for group_jobs in _group_jobs(jobs):
                group_futures = []
                for job in group_jobs:
                    future = executor.submit(_run_worker, *job, **self.lintargs)
                    future.add_done_callback(self._collect_results)
                    futures.append(future)
                    group_futures.append(future)
                futures_wait(group_futures)
        else:
            for job in jobs:
                future = executor.submit(_run_worker, *job, **self.lintargs)
                future.add_done_callback(self._collect_results)
                futures.append(future)
        executor.shutdown()
        signal.signal(signal.SIGINT, orig_sigint)
        return self.result
