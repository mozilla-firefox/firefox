#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import json
import os
import platform
import random
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback
from argparse import Namespace
from collections import defaultdict, deque, namedtuple
from datetime import datetime, timedelta
from functools import partial
from multiprocessing import cpu_count
from subprocess import PIPE, STDOUT, Popen
from tempfile import gettempdir, mkdtemp
from threading import Event, Lock, Thread, Timer, current_thread

import mozdebug
import six
from mozgeckoprofiler import (
    symbolicate_profile_json,
    symbolicate_profiles,
    view_gecko_profile,
)
from mozserve import Http3Server, MozHttp2Server

try:
    import psutil

    HAVE_PSUTIL = True
except Exception:
    HAVE_PSUTIL = False

from xpcshellcommandline import parser_desktop

SCRIPT_DIR = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))

try:
    from mozbuild.base import MozbuildObject

    build = MozbuildObject.from_environment(cwd=SCRIPT_DIR)
except ImportError:
    build = None

HARNESS_TIMEOUT = 30
# How long to wait for a force-killed process to exit on its own after
# kill_and_get_minidump asked it for a minidump (a SIGABRT the process handles
# itself on Linux/macOS), before giving up and letting postCheck SIGKILL it.
TIMEOUT_MINIDUMP_WAIT = 30
TBPL_RETRY = 4  # defined in mozharness

# Based on recent benchmarking on highcpu pools, this value gives the best
# balance between runtime and memory usage
#
# Note:
# - NUM_THREADS defines the maximum number of tests that can run in parallel
# - With e10s/fission enabled, the actual number of underlying processes/threads
#   can be much higher, so memory pressure may vary accordingly
# - For ASan/TSan variants, the thread count is reduced by half to avoid OOM
#
# This value can be overridden via the --threadCount CLI option if adjustments
# are needed for custom CPU/memory configurations
NUM_THREADS = int(cpu_count() * 2.5)

EXPECTED_LOG_ACTIONS = set([
    "crash_reporter_init",
    "test_status",
    "log",
])

# --------------------------------------------------------------
# TODO: this is a hack for mozbase without virtualenv, remove with bug 849900
#
here = os.path.dirname(__file__)
mozbase = os.path.realpath(os.path.join(os.path.dirname(here), "mozbase"))

if os.path.isdir(mozbase):
    for package in os.listdir(mozbase):
        sys.path.append(os.path.join(mozbase, package))

import mozcrash
import mozfile
import mozinfo
from manifestparser import TestManifest
from manifestparser.expression import parse
from manifestparser.filters import failures, pathprefix, tags
from manifestparser.util import normsep, split_manifest_list
from mozlog import commandline
from mozprofile import Profile
from mozprofile.cli import parse_key_value, parse_preferences
from mozrunner.utils import get_stack_fixer_function
from moztest.assertions import AssertionFailureParser
from moztest.ubsan import UBSanErrorParser

# --------------------------------------------------------------

# TODO: perhaps this should be in a more generally shared location?
# This regex matches all of the C0 and C1 control characters
# (U+0000 through U+001F; U+007F; U+0080 through U+009F),
# except TAB (U+0009), CR (U+000D), LF (U+000A) and backslash (U+005C).
# A raw string is deliberately not used.
_cleanup_encoding_re = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f\\\\]")


def get_full_group_name(test):
    group = test["manifest"]
    if "ancestor_manifest" in test:
        ancestor_manifest = normsep(test["ancestor_manifest"])
        # Only change the group id if ancestor is not the generated root manifest.
        if "/" in ancestor_manifest:
            group = f"{ancestor_manifest}:{group}"
    return group


def _cleanup_encoding_repl(m):
    c = m.group(0)
    return "\\\\" if c == "\\" else f"\\x{ord(c):02X}"


def cleanup_encoding(s):
    """S is either a byte or unicode string.  Either way it may
    contain control characters, unpaired surrogates, reserved code
    points, etc.  If it is a byte string, it is assumed to be
    UTF-8, but it may not be *correct* UTF-8.  Return a
    sanitized unicode object."""
    if not isinstance(s, str):
        if isinstance(s, bytes):
            return six.ensure_str(s)
        else:
            return str(s)
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    # Replace all C0 and C1 control characters with \xNN escapes.
    return _cleanup_encoding_re.sub(_cleanup_encoding_repl, s)


""" Control-C handling """
gotSIGINT = False


def markGotSIGINT(signum, stackFrame):
    global gotSIGINT
    gotSIGINT = True


class XPCShellTestThread(Thread):
    def __init__(
        self,
        test_object,
        retry=None,
        verbose=False,
        usingTSan=False,
        usingCrashReporter=False,
        **kwargs,
    ):
        Thread.__init__(self)
        self.daemon = True

        self.test_object = test_object
        self.retry = retry
        if retry is None:
            # Retry in CI, but report results without retry when run locally to
            # avoid confusion and ease local debugging.
            self.retry = os.environ.get("MOZ_AUTOMATION") is not None
        # True only for the re-run of a test that failed the first time, so it
        # can name artifacts differently from the initial run.
        self.is_retry = kwargs.get("is_retry", False)
        self.verbose = verbose
        self.usingTSan = usingTSan
        self.usingCrashReporter = usingCrashReporter

        self.appPath = kwargs.get("appPath")
        self.xrePath = kwargs.get("xrePath")
        self.utility_path = kwargs.get("utility_path")
        self.testingModulesDir = kwargs.get("testingModulesDir")
        self.debuggerInfo = kwargs.get("debuggerInfo")
        self.jsDebuggerInfo = kwargs.get("jsDebuggerInfo")
        self.headJSPath = kwargs.get("headJSPath")
        self.testharnessdir = kwargs.get("testharnessdir")
        self.profileName = kwargs.get("profileName")
        self.singleFile = kwargs.get("singleFile")
        self.env = copy.deepcopy(kwargs.get("env"))
        self.symbolsPath = kwargs.get("symbolsPath")
        self.logfiles = kwargs.get("logfiles")
        self.app_binary = kwargs.get("app_binary")
        self.xpcshell = kwargs.get("xpcshell")
        self.xpcsRunArgs = kwargs.get("xpcsRunArgs")
        self.failureManifest = kwargs.get("failureManifest")
        self.jscovdir = kwargs.get("jscovdir")
        self.stack_fixer_function = kwargs.get("stack_fixer_function")
        self._rootTempDir = kwargs.get("tempDir")
        self.cleanup_dir_list = kwargs.get("cleanup_dir_list")
        self.pStdout = kwargs.get("pStdout")
        self.pStderr = kwargs.get("pStderr")
        self.keep_going = kwargs.get("keep_going")
        self.log = kwargs.get("log")
        self.assertion_parser = AssertionFailureParser(self.log)
        self.ubsan_parser = UBSanErrorParser(self.log)
        self.app_dir_key = kwargs.get("app_dir_key")
        self.interactive = kwargs.get("interactive")
        self.rootPrefsFile = kwargs.get("rootPrefsFile")
        self.extraPrefs = kwargs.get("extraPrefs")
        self.verboseIfFails = kwargs.get("verboseIfFails")
        self.headless = kwargs.get("headless")
        self.selfTest = kwargs.get("selfTest")
        self.runFailures = kwargs.get("runFailures")
        self.timeoutAsPass = kwargs.get("timeoutAsPass")
        self.crashAsPass = kwargs.get("crashAsPass")
        self.conditionedProfileDir = kwargs.get("conditionedProfileDir")
        self.profiler = kwargs.get("profiler")
        if self.runFailures:
            self.retry = False

        # Default the test prefsFile to the rootPrefsFile.
        self.prefsFile = self.rootPrefsFile

        # only one of these will be set to 1. adding them to the totals in
        # the harness
        self.passCount = 0
        self.todoCount = 0
        self.failCount = 0

        # Context for output processing
        self.output_lines = []
        self.has_failure_output = False
        self.saw_crash_reporter_init = False
        self.saw_proc_start = False
        self.saw_proc_end = False
        self.command = None
        self.harness_timeout = kwargs.get("harness_timeout")
        self.timedout = False
        self.infra = False

        # Set by run() when the thread finishes; testTimeout can mark a test done
        # while its thread is still blocked, so they need a value from the start.
        self.exception = None
        self.traceback = None

        # event from main thread to signal work done
        self.event = kwargs.get("event")
        self.done = False  # explicitly set flag so we don't rely on thread.isAlive
        self.timer = None  # Timer used to detect test timeouts
        self.lock = Lock()  # lock used to serialize access to the timer

    def run(self):
        try:
            self.run_test()
        except PermissionError as e:
            self.infra = True
            self.exception = e
            self.traceback = traceback.format_exc()
        except Exception as e:
            self.exception = e
            self.traceback = traceback.format_exc()
        else:
            self.exception = None
            self.traceback = None
        if self.retry:
            self.log.info(
                "%s failed or timed out, will retry." % self.test_object["id"]
            )
        self.done = True
        self.event.set()

    def kill(self, proc):
        """
        Simple wrapper to kill a process.
        On a remote system, this is overloaded to handle remote process communication.
        """
        return proc.kill()

    def removeDir(self, dirname):
        """
        Simple wrapper to remove (recursively) a given directory.
        On a remote system, we need to overload this to work on the remote filesystem.
        """
        mozfile.remove(dirname)

    def poll(self, proc):
        """
        Simple wrapper to check if a process has terminated.
        On a remote system, this is overloaded to handle remote process communication.
        """
        return proc.poll()

    def createLogFile(self, test_file, stdout):
        """
        For a given test file and stdout buffer, create a log file.
        On a remote system we have to fix the test name since it can contain directories.
        """
        with open(test_file + ".log", "w") as f:
            f.write(stdout)

    def getReturnCode(self, proc):
        """
        Simple wrapper to get the return code for a given process.
        On a remote system we overload this to work with the remote process management.
        """
        if proc is not None and hasattr(proc, "returncode"):
            return proc.returncode
        return -1

    def communicate(self, proc):
        """
        Simple wrapper to communicate with a process.
        On a remote system, this is overloaded to handle remote process communication.
        """
        # Processing of incremental output put here to
        # sidestep issues on remote platforms, where what we know
        # as proc is a file pulled off of a device.
        if proc.stdout:
            while True:
                line = proc.stdout.readline()
                if not line:
                    break
                self.process_line(line)

            if self.saw_proc_start and not self.saw_proc_end:
                self.has_failure_output = True

        return proc.communicate()

    def launchProcess(
        self, cmd, stdout, stderr, env, cwd, timeout=None, test_name=None
    ):
        """
        Simple wrapper to launch a process.
        On a remote system, this is more complex and we need to overload this function.
        """
        # timeout is needed by remote xpcshell to extend the
        # remote device timeout. It is not used in this function.
        if six.PY3:
            cwd = six.ensure_str(cwd)
            for i in range(len(cmd)):
                cmd[i] = six.ensure_str(cmd[i])

        if HAVE_PSUTIL:
            popen_func = psutil.Popen
        else:
            popen_func = Popen

        return popen_func(cmd, stdout=stdout, stderr=stderr, env=env, cwd=cwd)

    def checkForCrashes(self, dump_directory, symbols_path, test_name=None):
        """
        Simple wrapper to check for crashes.
        On a remote system, this is more complex and we need to overload this function.
        """
        quiet = self.crashAsPass or self.retry
        # For selftests, set dump_save_path to prevent crash dumps from being saved
        # (they intentionally crash and the dumps aren't useful artifacts)
        dump_save_path = "" if self.selfTest else None
        return mozcrash.log_crashes(
            self.log,
            dump_directory,
            symbols_path,
            test=test_name,
            quiet=quiet,
            dump_save_path=dump_save_path,
        )

    def logCommand(self, name, completeCmd, testdir):
        self.log.info("%s | full command: %r" % (name, completeCmd))
        self.log.info("%s | current directory: %r" % (name, testdir))
        # Show only those environment variables that are changed from
        # the ambient environment.
        changedEnv = set("%s=%s" % i for i in self.env.items()) - set(
            "%s=%s" % i for i in os.environ.items()
        )
        self.log.info("%s | environment: %s" % (name, list(changedEnv)))
        shell_command_tokens = [
            shlex.quote(tok) for tok in list(changedEnv) + completeCmd
        ]
        self.log.info(
            "%s | as shell command: (cd %s; %s)"
            % (name, shlex.quote(testdir), " ".join(shell_command_tokens))
        )

    def killTimeout(self, proc):
        if proc is not None and hasattr(proc, "pid"):
            mozcrash.kill_and_get_minidump(
                proc.pid, self.tempDir, utility_path=self.utility_path
            )
        else:
            self.log.info("not killing -- proc or pid unknown")

    def postCheck(self, proc):
        """Checks for a still-running test process, kills it and fails the test if found.
        We can sometimes get here before the process has terminated, which would
        cause removeDir() to fail - so check for the process and kill it if needed.
        """
        if proc and self.poll(proc) is None:
            if HAVE_PSUTIL:
                try:
                    self.kill(proc)
                except psutil.NoSuchProcess:
                    pass
            else:
                self.kill(proc)
            message = "%s | Process still running after test!" % self.test_object["id"]
            if self.retry:
                self.log.info(message)
                return

            self.log.error(message)
            self.log_full_output()
            self.failCount = 1

    def _readTimeoutProfileProgress(self, profile_path):
        # Read the 0..1 streaming progress the profiler writes to the
        # "<profile>.progress" sidecar while a scheduled dump is in flight.
        # Returns None if it isn't there yet or can't be parsed.
        if not profile_path:
            return None
        try:
            with open(profile_path + ".progress") as f:
                return float(f.read().strip())
        except (OSError, ValueError):
            return None

    def testTimeout(self, proc):
        # Ensure that we didn't race the test finishing execution between when
        # the timeout timer fired and this code started executing.
        self.lock.acquire()

        # If `self.timer` has been set to None it means that the test actually
        # finished execution before we managed to get the lock and the code in
        # this timer shouldn't run, so return right away.
        if self.timer is None:
            self.lock.release()
            return

        # While a scheduled profile dump (see scheduleDumpToFile in head.js) is
        # still writing, defer the kill so we don't upload truncated JSON. The
        # profiler reports the dump's 0..1 progress in a "<profile>.progress"
        # sidecar; keep deferring while it advances, and kill once it stalls (a
        # genuine hang) or the budget runs out (30 deferrals of 10s, or 3 stalls).
        profile_path = self.env.get("MOZ_TEST_TIMEOUT_PROFILE_PATH")
        progress = self._readTimeoutProfileProgress(profile_path)
        if progress is not None and self._timeout_defer_count < 30:
            if progress > self._last_timeout_profile_progress + 1e-6:
                self._last_timeout_profile_progress = progress
                self._timeout_stall_count = 0
            else:
                self._timeout_stall_count += 1
            if progress < 1.0 and self._timeout_stall_count < 3:
                self._timeout_defer_count += 1
                self.log.info(
                    f"{self.test_object['id']} | timeout profile dump "
                    f"progressing ({progress * 100:.1f}%), deferring kill"
                )
                self.timer = Timer(10, lambda: self.testTimeout(proc))
                self.timer.start()
                self.lock.release()
                return

        # Mark timed out so run_test reports the timeout instead of a result for
        # the process we're killing.
        self.timedout = True

        try:
            # Kill the test process before calling log_full_output that can take a
            # a while due to stack fixing.
            self.killTimeout(proc)

            # On Linux/macOS kill_and_get_minidump only sends SIGABRT, so wait for
            # the process to finish writing its minidump before postCheck's SIGKILL
            # cuts it off, then symbolicate it -- this timeout path returns before
            # run_test's own checkForCrashes.
            if proc is not None and hasattr(proc, "pid"):
                deadline = time.time() + TIMEOUT_MINIDUMP_WAIT
                while self.poll(proc) is None and time.time() < deadline:
                    time.sleep(0.1)
            self.checkForCrashes(
                self.tempDir, self.symbolsPath, test_name=self.test_object["id"]
            )

            # Note that the crash above is this force-killed process, not a real
            # crash. Buffered as a test message so the retry replay demotes it out of
            # the failure summary, like the crash itself.
            self.report_message({
                "action": "log",
                "level": "ERROR",
                "message": (
                    f"{self.test_object['id']} | Timed out and was force-killed by "
                    "the harness; the crash dump reported for this test is that "
                    "force-killed process, not an actual crash."
                ),
            })

            self.reportTimeoutResult()

            self.log.info(f"xpcshell return code: {self.getReturnCode(proc)}")
            self.postCheck(proc)
            self.clean_temp_dirs(self.test_object["path"])
        finally:
            # Relinquish the lock to allow run_test() to finish, and mark the test
            # done. Both have to happen even if the cleanup above raised -- this
            # runs on the timer thread, and the test thread it leaves behind can be
            # blocked forever on a stdout pipe a surviving child process holds open,
            # so nothing else will ever set this and the scheduler would wait for
            # the test until mozharness kills the job. Reached after the TIMEOUT
            # test_end above, so a retry can't interleave its test_start with our
            # output.
            self.lock.release()
            self.done = True

    def reportTimeoutResult(self):
        """Log the structured failure for a timed-out test: a FAIL test_status
        pointing at the uploaded profile (when one was written), followed by a
        TIMEOUT test_end. Shared by the harness timer (testTimeout) and the path
        where a profiled test dumps its profile and exits on its own."""
        if self.test_object["expected"] == "pass":
            expected = "PASS"
        else:
            expected = "FAIL"

        extra = None
        if self.timeout_factor > 1:
            extra = {"timeoutfactor": self.timeout_factor}

        # If the profiler dumped a profile from its sampler thread (scheduled by
        # head.js via scheduleDumpToFile since the main thread can't write one
        # once it stops returning to the event loop), report it here as a
        # structured test_status, logged while the test is still in progress so
        # the artifact is linked to this test. A structured message (unlike a
        # raw log line) is downgraded to expected on a retried run, and is
        # recorded as a FAIL marker in the resource-usage profile the dashboards
        # read.
        profile_name = self.timeout_profile_name
        upload_dir = self.env.get("MOZ_UPLOAD_DIR")
        if (
            profile_name
            and upload_dir
            and os.path.isfile(os.path.join(upload_dir, profile_name))
        ):
            self.log.test_status(
                self.test_object["id"],
                "",
                "FAIL",
                expected="FAIL" if (self.retry or self.timeoutAsPass) else expected,
                message=(
                    "Test timed out; profile uploaded in "
                    f"{self.timeout_profile_artifact_name}"
                ),
            )

        if self.retry:
            self.log.test_end(
                self.test_object["id"],
                "TIMEOUT",
                expected="TIMEOUT",
                message="Test timed out",
                extra=extra,
            )
            self.log_full_output(mark_failures_as_expected=True)
        else:
            result = "TIMEOUT"
            if self.timeoutAsPass:
                expected = "FAIL"
                result = "FAIL"
            self.failCount = 1
            self.log.test_end(
                self.test_object["id"],
                result,
                expected=expected,
                message="Test timed out",
                extra=extra,
            )
            self.log_full_output()

    def updateTestPrefsFile(self):
        # If the Manifest file has some additional prefs, merge the
        # prefs set in the user.js file stored in the _rootTempdir
        # with the prefs from the manifest and the prefs specified
        # in the extraPrefs option.
        if "prefs" in self.test_object:
            # Merge the user preferences in a fake profile dir in a
            # local temporary dir (self.tempDir is the remoteTmpDir
            # for the RemoteXPCShellTestThread subclass and so we
            # can't use that tempDir here).
            localTempDir = mkdtemp(prefix="xpc-other-", dir=self._rootTempDir)

            filename = "user.js"
            interpolation = {"server": "dummyserver"}
            profile = Profile(profile=localTempDir, restore=False)
            # _rootTempDir contains a user.js file, generated by buildPrefsFile
            profile.merge(self._rootTempDir, interpolation=interpolation)

            prefs = split_manifest_list(self.test_object["prefs"])
            name = self.test_object["id"]
            if self.verbose:
                self.log.info(
                    "%s: Per-test extra prefs will be set:\n  {}".format(
                        "\n  ".join(prefs)
                    )
                    % name
                )

            profile.set_preferences(parse_preferences(prefs), filename=filename)
            # Make sure that the extra prefs form the command line are overriding
            # any prefs inherited from the shared profile data or the manifest prefs.
            profile.set_preferences(
                parse_preferences(self.extraPrefs), filename=filename
            )
            return os.path.join(profile.profile, filename)

        # Return the root prefsFile if there is no other prefs to merge.
        # This is the path set by buildPrefsFile.
        return self.rootPrefsFile

    def updateTestEnvironment(self):
        # Add additional environment variables from the Manifest file
        extraEnv = {}
        if "environment" in self.test_object:
            extraEnv = split_manifest_list(self.test_object["environment"])
            self.log.info(
                "The following extra environment variables will be set:\n  {}".format(
                    "\n  ".join(extraEnv)
                )
            )
            self.env.update(
                dict(
                    parse_key_value(
                        extraEnv, context="environment variables in manifest"
                    )
                )
            )

    @property
    def conditioned_profile_copy(self):
        """Returns a copy of the original conditioned profile that was created."""

        condprof_copy = os.path.join(tempfile.mkdtemp(), "profile")
        shutil.copytree(
            self.conditionedProfileDir,
            condprof_copy,
            ignore=shutil.ignore_patterns("lock"),
        )
        self.log.info("Created a conditioned-profile copy: %s" % condprof_copy)
        return condprof_copy

    def buildCmdTestFile(self, name):
        """
        Build the command line arguments for the test file.
        On a remote system, this may be overloaded to use a remote path structure.
        """
        return ["-e", 'const _TEST_FILE = ["%s"];' % name.replace("\\", "/")]

    def setupTempDir(self):
        tempDir = mkdtemp(prefix="xpc-other-", dir=self._rootTempDir)
        self.env["XPCSHELL_TEST_TEMP_DIR"] = tempDir
        if self.interactive:
            self.log.info("temp dir is %s" % tempDir)

        if "MOZ_APP_DATA" not in os.environ:
            appdata_dir = os.path.join(tempDir, "moz-appdata")
            os.makedirs(appdata_dir, exist_ok=True)
            self.env["MOZ_APP_DATA"] = os.path.normpath(
                os.path.join(appdata_dir, "AppData", "Roaming")
            )
            self.env["MOZ_LOCAL_APP_DATA"] = os.path.normpath(
                os.path.join(appdata_dir, "Local")
            )

        return tempDir

    def setupProfileDir(self):
        """
        Create a temporary folder for the profile and set appropriate environment variables.
        When running check-interactive and check-one, the directory is well-defined and
        retained for inspection once the tests complete.

        On a remote system, this may be overloaded to use a remote path structure.
        """
        if self.conditionedProfileDir:
            profileDir = self.conditioned_profile_copy
        elif self.interactive or (self.singleFile and not self.selfTest):
            profileDir = os.path.join(gettempdir(), self.profileName, "xpcshellprofile")
            try:
                # This could be left over from previous runs
                self.removeDir(profileDir)
            except Exception:
                pass
            os.makedirs(profileDir)
        else:
            profileDir = mkdtemp(prefix="xpc-profile-", dir=self._rootTempDir)
        self.env["XPCSHELL_TEST_PROFILE_DIR"] = profileDir
        if self.interactive or self.singleFile:
            self.log.info("profile dir is %s" % profileDir)
        return profileDir

    def setupMozinfoJS(self):
        mozInfoJSPath = os.path.join(self.profileDir, "mozinfo.json")
        mozInfoJSPath = mozInfoJSPath.replace("\\", "\\\\")
        mozinfo.output_to_file(mozInfoJSPath)
        return mozInfoJSPath

    def buildCmdHead(self):
        """
        Build the command line arguments for the head files,
        along with the address of the webserver which some tests require.

        On a remote system, this is overloaded to resolve quoting issues over a
        secondary command line.
        """
        headfiles = self.getHeadFiles(self.test_object)
        cmdH = ", ".join(['"' + f.replace("\\", "/") + '"' for f in headfiles])

        dbgport = 0 if self.jsDebuggerInfo is None else self.jsDebuggerInfo.port

        return [
            "-e",
            "const _HEAD_FILES = [%s];" % cmdH,
            "-e",
            "const _JSDEBUGGER_PORT = %d;" % dbgport,
        ]

    def getHeadFiles(self, test):
        """Obtain lists of head- files.  Returns a list of head files."""

        def sanitize_list(s, kind):
            for f in s.strip().split(" "):
                f = f.strip()
                if len(f) < 1:
                    continue

                path = os.path.normpath(os.path.join(test["here"], f))
                if not os.path.exists(path):
                    raise Exception("%s file does not exist: %s" % (kind, path))

                if not os.path.isfile(path):
                    raise Exception("%s file is not a file: %s" % (kind, path))

                yield path

        headlist = test.get("head", "")
        return list(sanitize_list(headlist, "head"))

    def buildXpcsCmd(self):
        """
        Load the root head.js file as the first file in our test path, before other head,
        and test files. On a remote system, we overload this to add additional command
        line arguments, so this gets overloaded.
        """
        # - NOTE: if you rename/add any of the constants set here, update
        #   do_load_child_test_harness() in head.js
        if not self.appPath:
            self.appPath = self.xrePath

        if self.app_binary:
            xpcsCmd = [
                self.app_binary,
                "--xpcshell",
            ]
        else:
            xpcsCmd = [
                self.xpcshell,
            ]

        xpcsCmd += [
            "-g",
            self.xrePath,
            "-a",
            self.appPath,
            "-m",
            "-e",
            'const _HEAD_JS_PATH = "%s";' % self.headJSPath,
            "-e",
            'const _MOZINFO_JS_PATH = "%s";' % self.mozInfoJSPath,
            "-e",
            'const _PREFS_FILE = "%s";' % self.prefsFile.replace("\\", "\\\\"),
        ]

        if self.testingModulesDir:
            # Escape backslashes in string literal.
            sanitized = self.testingModulesDir.replace("\\", "\\\\")
            xpcsCmd.extend(["-e", 'const _TESTING_MODULES_DIR = "%s";' % sanitized])

        xpcsCmd.extend(["-f", os.path.join(self.testharnessdir, "head.js")])

        if self.debuggerInfo:
            xpcsCmd = [self.debuggerInfo.path] + self.debuggerInfo.args + xpcsCmd

        return xpcsCmd

    def cleanupDir(self, directory, name):
        if not os.path.exists(directory):
            return

        # up to TRY_LIMIT attempts (one every second), because
        # the Windows filesystem is slow to react to the changes
        TRY_LIMIT = 25
        try_count = 0
        while try_count < TRY_LIMIT:
            try:
                self.removeDir(directory)
            except OSError:
                self.log.info("Failed to remove directory: %s. Waiting." % directory)
                # We suspect the filesystem may still be making changes. Wait a
                # little bit and try again.
                time.sleep(1)
                try_count += 1
            else:
                # removed fine
                return

        # we try cleaning up again later at the end of the run
        self.cleanup_dir_list.append(directory)

    def clean_temp_dirs(self, name):
        # We don't want to delete the profile when running check-interactive
        # or check-one.
        if self.profileDir and not self.interactive and not self.singleFile:
            self.cleanupDir(self.profileDir, name)

        self.cleanupDir(self.tempDir, name)

    def parse_output(self, output):
        """Parses process output for structured messages and saves output as it is
        read. Sets self.has_failure_output in case of evidence of a failure"""
        for line_string in output.splitlines():
            self.process_line(line_string)
        self.assertion_parser.flush()
        self.ubsan_parser.flush()

        if self.saw_proc_start and not self.saw_proc_end:
            self.has_failure_output = True

    def fix_text_output(self, line):
        line = cleanup_encoding(line)
        if self.stack_fixer_function is not None:
            line = self.stack_fixer_function(line)

        if isinstance(line, bytes):
            line = line.decode("utf-8")
        return line

    def log_line(self, line, time=None):
        """Log a line of output (either a parser json object or text output from
        the test process"""
        if isinstance(line, (str, bytes)):
            line = self.fix_text_output(line).rstrip("\r\n")
            kwargs = {"command": self.command, "test": self.test_object["id"]}
            if time is not None:
                kwargs["time"] = time
            self.assertion_parser.log(
                line, pid=self.proc_ident, test=self.test_object["id"], time=time
            )
            self.ubsan_parser.log(
                line, pid=self.proc_ident, test=self.test_object["id"], time=time
            )
            self.log.process_output(self.proc_ident, line, **kwargs)
        else:
            if "message" in line:
                line["message"] = self.fix_text_output(line["message"])
            if "xpcshell_process" in line:
                line["thread"] = " ".join([
                    current_thread().name,
                    line["xpcshell_process"],
                ])
            else:
                line["thread"] = current_thread().name
            self.log.log_raw(line)

    def log_full_output(self, mark_failures_as_expected=False):
        """Logs any buffered output from the test process, and clears the buffer.

        Args:
            mark_failures_as_expected: If True, failures will be marked as expected
                (TEST-EXPECTED-FAIL instead of TEST-UNEXPECTED-FAIL). This is used
                when a test will be retried.
        """
        if not self.output_lines:
            return
        log_message = f"full log for {self.test_object['id']}"
        self.log.info(f">>>>>>> Begin of {log_message}")
        self.log.group_start("replaying " + log_message)
        for timestamp, line in self.output_lines:
            if isinstance(line, dict):
                # Always ensure the 'test' field is present for resource usage profiles
                if "test" not in line:
                    line["test"] = self.test_object["id"]

                if mark_failures_as_expected:
                    if line.get("action") == "test_status" and "expected" in line:
                        # Ensure the 'expected' field matches the 'status' to avoid failing the job
                        line["expected"] = line.get("status")
                        line.setdefault("extra", {})["will_retry"] = True
                    elif line.get("action") == "log" and line.get("level") == "ERROR":
                        # Convert ERROR log to test_status so it gets colored
                        # properly without causing a test failure.
                        line["action"] = "test_status"
                        line["status"] = "ERROR"
                        line["expected"] = "ERROR"
                        line["subtest"] = ""
                        del line["level"]
                self.log_line(line)
            else:
                # For text lines, replace text matching error patterns to avoid
                # mozharness log parsing forcing an error job exit code
                line = re.sub(
                    r"ERROR: ((Address|Leak)Sanitizer)", r"ERROR (will retry): \1", line
                )
                # Treeherder's log parser catches "fatal error" as an error
                line = re.sub(r"fatal error", r"error", line)
                # For text lines, we need to provide the timestamp that was
                # recorded when appending the message to self.output_lines
                self.log_line(line, time=timestamp)
        self.log.info(f"<<<<<<< End of {log_message}")
        self.log.group_end("replaying " + log_message)
        self.assertion_parser.flush()
        self.ubsan_parser.flush()
        self.output_lines = []

    def report_message(self, message):
        """Stores or logs a json log message in mozlog format."""
        if self.verbose:
            self.log_line(message)
        else:
            # Store timestamp only for string messages (dicts already have timestamps)
            # We need valid timestamps to replay messages correctly in resource
            # usage profiles.
            import time

            timestamp = (
                int(time.time() * 1000) if isinstance(message, (str, bytes)) else None
            )
            self.output_lines.append((timestamp, message))

    def process_line(self, line_string):
        """Parses a single line of output, determining its significance and
        reporting a message.
        """
        if isinstance(line_string, bytes):
            # Transform binary to string representation
            line_string = line_string.decode(sys.stdout.encoding, errors="replace")

        if not line_string.strip():
            return

        try:
            line_object = json.loads(line_string)
            if not isinstance(line_object, dict):
                self.report_message(line_string)
                return
        except ValueError:
            self.report_message(line_string)
            return

        if (
            "action" not in line_object
            or line_object["action"] not in EXPECTED_LOG_ACTIONS
        ):
            # The test process output JSON.
            self.report_message(line_string)
            return

        if line_object["action"] == "crash_reporter_init":
            self.saw_crash_reporter_init = True
            return

        action = line_object["action"]

        self.has_failure_output = (
            self.has_failure_output
            or "expected" in line_object
            or action == "log"
            and line_object["level"] == "ERROR"
        )

        self.report_message(line_object)

        if action == "log" and line_object["message"] == "CHILD-TEST-STARTED":
            self.saw_proc_start = True
        elif action == "log" and line_object["message"] == "CHILD-TEST-COMPLETED":
            self.saw_proc_end = True

    def run_test(self):
        """Run an individual xpcshell test."""
        global gotSIGINT

        name = self.test_object["id"]
        path = self.test_object["path"]
        group = get_full_group_name(self.test_object)

        # Check for skipped tests
        if "disabled" in self.test_object:
            message = self.test_object["disabled"]
            if not message:
                message = "disabled from xpcshell manifest"
            self.log.test_start(name, group=group)
            self.log.test_end(name, "SKIP", message=message, group=group)

            self.retry = False
            self.keep_going = True
            return

        self.log.test_start(name, group=group)

        # Check for known-fail tests
        expect_pass = self.test_object["expected"] == "pass"

        # By default self.appPath will equal the gre dir. If specified in the
        # xpcshell.toml file, set a different app dir for this test.
        if self.app_dir_key and self.app_dir_key in self.test_object:
            rel_app_dir = self.test_object[self.app_dir_key]
            rel_app_dir = os.path.join(self.xrePath, rel_app_dir)
            self.appPath = os.path.abspath(rel_app_dir)
        else:
            self.appPath = None

        test_dir = os.path.dirname(path)

        # Create a profile and a temp dir that the JS harness can stick
        # a profile and temporary data in
        self.profileDir = self.setupProfileDir()
        self.tempDir = self.setupTempDir()
        self.mozInfoJSPath = self.setupMozinfoJS()

        # Setup per-manifest prefs and write them into the tempdir.
        self.prefsFile = self.updateTestPrefsFile()

        # Setup per-manifest env variables
        self.updateTestEnvironment()

        # The order of the command line is important:
        # 1) Arguments for xpcshell itself
        self.command = self.buildXpcsCmd()

        # 2) Arguments for the head files
        self.command.extend(self.buildCmdHead())

        # 3) Arguments for the test file
        self.command.extend(self.buildCmdTestFile(path))
        self.command.extend(["-e", 'const _TEST_NAME = "%s";' % name])
        self.command.extend([
            "-e",
            'const _EXPECTED = "%s";' % self.test_object["expected"],
        ])

        # 4) Arguments for code coverage
        if self.jscovdir:
            self.command.extend([
                "-e",
                'const _JSCOV_DIR = "%s";' % self.jscovdir.replace("\\", "/"),
            ])

        # 5) Runtime arguments
        if "debug" in self.test_object:
            self.command.append("-d")

        self.command.extend(self.xpcsRunArgs)

        if self.test_object.get("dmd") == "true":
            self.env["PYTHON"] = sys.executable
            self.env["BREAKPAD_SYMBOLS_PATH"] = self.symbolsPath

        if self.test_object.get("subprocess") == "true":
            self.env["PYTHON"] = sys.executable

        if self.profiler:
            if not self.singleFile:
                self.log.error(
                    "The --profiler flag is currently only supported when running a single test"
                )
            else:
                self.env["MOZ_PROFILER_STARTUP"] = "1"
                profile_path = os.path.join(
                    self.profileDir, "profile_" + os.path.basename(name) + ".json"
                )
                self.env["MOZ_PROFILER_SHUTDOWN"] = profile_path
                # The user explicitly asked for a profile, so use the normal
                # profiler feature set and sampling interval instead of the
                # low-overhead defaults applied in buildCoreEnvironment,
                # unless they have already picked values themselves.
                if "MOZ_PROFILER_STARTUP_FEATURES" not in os.environ:
                    self.env["MOZ_PROFILER_STARTUP_FEATURES"] = "default"
                if "MOZ_PROFILER_STARTUP_INTERVAL" not in os.environ:
                    self.env.pop("MOZ_PROFILER_STARTUP_INTERVAL", None)

        if (
            self.test_object.get("headless", "true" if self.headless else None)
            == "true"
        ):
            self.env["MOZ_HEADLESS"] = "1"
            self.env["DISPLAY"] = "77"  # Set a fake display.

        # Allow a test to request a multiple of the timeout if it is expected to take long
        self.timeout_factor = 1
        if "requesttimeoutfactor" in self.test_object:
            self.timeout_factor = int(self.test_object["requesttimeoutfactor"])

        testTimeoutInterval = self.harness_timeout * self.timeout_factor

        self.timeout_profile_name = None
        self.timeout_profile_artifact_name = None
        if not self.interactive and not self.debuggerInfo and not self.jsDebuggerInfo:
            # When the profiler runs by default, have it dump a profile from its
            # sampler thread once this timeout is reached (armed by head.js), so
            # a test blocked in a synchronous run (where the main thread can
            # never write one) still leaves a profile. We pick the artifact name here
            # so the retry of a test that timed out doesn't overwrite the initial
            # run's profile: the retry gets a "_retry" suffix, and a numeric
            # counter is only added on an actual name collision (the same test
            # listed in two manifests). The suffixes go before the test
            # extension, so the profile is still named after the test.
            # testTimeout reports this name.
            upload_dir = self.env.get("MOZ_UPLOAD_DIR")
            timeout_dump_armed = (
                upload_dir
                and self.env.get("MOZ_PROFILER_STARTUP")
                and "MOZ_PROFILER_SHUTDOWN" not in self.env
            )
            if timeout_dump_armed:
                root, ext = os.path.splitext(os.path.basename(name))
                if self.is_retry:
                    root += "_retry"
                filename = f"profile_{root}{ext}.json"
                i = 2
                while any(
                    os.path.exists(os.path.join(upload_dir, checked_filename))
                    for checked_filename in (filename, filename + ".gz")
                ):
                    filename = f"profile_{root}-{i}{ext}.json"
                    i += 1
                self.timeout_profile_name = filename
                # Symbolication gzips every uploaded profile and renames it
                # accordingly, so the surviving artifact is the ".json.gz" one.
                self.timeout_profile_artifact_name = filename + ".gz"
                self.env["MOZ_TEST_TIMEOUT_PROFILE_PATH"] = os.path.join(
                    upload_dir, filename
                )

            # head.js dumps the profile from the sampler thread once the timeout
            # is reached, so when that's armed this timer is only a safety net;
            # give it 50% more time so the sampler thread can finish writing the
            # profile before we kill the process.
            kill_interval = testTimeoutInterval
            if timeout_dump_armed:
                kill_interval = testTimeoutInterval * 1.5
            # Tracks the scheduled profile dump's streaming progress so
            # testTimeout can defer the kill while the dump is still making
            # progress, and kill it once progress stalls.
            self._last_timeout_profile_progress = -1.0
            self._timeout_stall_count = 0
            self._timeout_defer_count = 0
            self.timer = Timer(kill_interval, lambda: self.testTimeout(proc))
            self.timer.start()
            self.env["MOZ_TEST_TIMEOUT_INTERVAL"] = str(testTimeoutInterval)

        proc = None
        process_output = None

        try:
            if self.verbose:
                self.logCommand(name, self.command, test_dir)

            launch_time = time.monotonic()
            proc = self.launchProcess(
                self.command,
                stdout=self.pStdout,
                stderr=self.pStderr,
                env=self.env,
                cwd=test_dir,
                timeout=testTimeoutInterval,
                test_name=name,
            )

            if hasattr(proc, "pid"):
                self.proc_ident = proc.pid
            else:
                # On mobile, "proc" is just a file.
                self.proc_ident = name

            if self.interactive:
                self.log.info("%s | Process ID: %d" % (name, self.proc_ident))

            # Communicate returns a tuple of (stdout, stderr), however we always
            # redirect stderr to stdout, so the second element is ignored.
            process_output, _ = self.communicate(proc)
            elapsed = time.monotonic() - launch_time

            if self.interactive:
                # Not sure what else to do here...
                self.keep_going = True
                return

            # Cancel the test timeout timer, note that this must happen under
            # lock to prevent this code from racing with the code within the
            # timer itself.
            self.lock.acquire()

            if self.timer:
                self.timer.cancel()
                self.timer = None

            self.lock.release()

            # Drop the scheduled-dump progress sidecar so it isn't uploaded as a
            # stray artifact (it lives next to the profile inside MOZ_UPLOAD_DIR).
            # This is the only cleanup site, reached on both the self-exit and
            # kill paths; the C++ exit-after path _exit()s the process without
            # removing it, so this is what keeps it out of the upload.
            timeout_profile = self.env.get("MOZ_TEST_TIMEOUT_PROFILE_PATH")
            if timeout_profile:
                try:
                    os.remove(timeout_profile + ".progress")
                except OSError:
                    pass

            # A profiled test that overran its expected timeout dumps a profile
            # from the sampler thread and exits the process itself (see
            # scheduleDumpToFile in head.js), rather than waiting for the
            # harness's safety-net kill. Recognize that here -- the process
            # ended on its own past the expected timeout and left a profile at
            # the agreed path -- and report it as a timeout, not a normal result.
            if (
                not self.timedout
                and self.timeout_profile_name
                and self.env.get("MOZ_UPLOAD_DIR")
                and elapsed > testTimeoutInterval
                and os.path.isfile(
                    os.path.join(self.env["MOZ_UPLOAD_DIR"], self.timeout_profile_name)
                )
            ):
                self.timedout = True
                self.reportTimeoutResult()
                return

            if process_output:
                # For the remote case, stdout is not yet depleted, so we parse
                # it here all at once.
                self.parse_output(process_output)

            return_code = self.getReturnCode(proc)

            # TSan'd processes return 66 if races are detected.  This isn't
            # good in the sense that there's no way to distinguish between
            # a process that would normally have returned zero but has races,
            # and a race-free process that returns 66.  But I don't see how
            # to do better.  This ambiguity is at least constrained to the
            # with-TSan case.  It doesn't affect normal builds.
            #
            # This also assumes that the magic value 66 isn't overridden by
            # a TSAN_OPTIONS=exitcode=<number> environment variable setting.
            #
            TSAN_EXIT_CODE_WITH_RACES = 66

            return_code_ok = return_code == 0 or (
                self.usingTSan and return_code == TSAN_EXIT_CODE_WITH_RACES
            )

            # Due to the limitation on the remote xpcshell test, the process
            # return code does not represent the process crash.
            # If crash_reporter_init log has not been seen and the return code
            # is 0, it means the process crashed before setting up the crash
            # reporter.
            #
            # NOTE: Crash reporter is not enabled on some configuration, such
            #       as ASAN and TSAN. Those configuration shouldn't be using
            #       remote xpcshell test, and the crash should be caught by
            #       the process return code.
            # NOTE: self.saw_crash_reporter_init is False also when adb failed
            #       to launch process, and in that case the return code is
            #       not 0.
            #       (see launchProcess in remotexpcshelltests.py)
            ended_before_crash_reporter_init = (
                return_code_ok
                and self.usingCrashReporter
                and not self.saw_crash_reporter_init
                and len(process_output) > 0
            )

            passed = (
                (not self.has_failure_output)
                and not ended_before_crash_reporter_init
                and return_code_ok
            )

            status = "PASS" if passed else "FAIL"
            expected = "PASS" if expect_pass else "FAIL"
            message = "xpcshell return code: %d" % return_code

            if self.timedout:
                return

            # Check for crashes before logging test_end
            found_crash = self.checkForCrashes(
                self.tempDir, self.symbolsPath, test_name=name
            )
            if found_crash:
                status = "CRASH"
                message = "Test crashed"

            # Include timeout factor in extra data if not default
            extra = None
            if self.timeout_factor > 1:
                extra = {"timeoutfactor": self.timeout_factor}

            if status != expected or ended_before_crash_reporter_init:
                if ended_before_crash_reporter_init:
                    self.log.test_end(
                        name,
                        "CRASH",
                        expected=expected,
                        message="Test ended before setting up the crash reporter",
                        group=group,
                        extra=extra,
                    )
                elif self.retry:
                    retry_message = (
                        "Test crashed, will retry"
                        if status == "CRASH"
                        else "Test failed or timed out, will retry"
                    )
                    self.log.test_end(
                        name,
                        status,
                        expected=status,
                        message=retry_message,
                        group=group,
                        extra=extra,
                    )
                    self.clean_temp_dirs(path)
                    if self.verboseIfFails and not self.verbose:
                        self.log_full_output()
                    return
                else:
                    self.log.test_end(
                        name,
                        status,
                        expected=expected,
                        message=message,
                        group=group,
                        extra=extra,
                    )
                self.log_full_output()

                self.failCount += 1

                if self.failureManifest:
                    with open(self.failureManifest, "a") as f:
                        f.write("[%s]\n" % self.test_object["path"])
                        for k, v in self.test_object.items():
                            f.write("%s = %s\n" % (k, v))

            else:
                # If TSan reports a race, dump the output, else we can't
                # diagnose what the problem was.  See comments above about
                # the significance of TSAN_EXIT_CODE_WITH_RACES.
                if self.usingTSan and return_code == TSAN_EXIT_CODE_WITH_RACES:
                    self.log_full_output()

                self.log.test_end(
                    name,
                    status,
                    expected=expected,
                    message=message,
                    group=group,
                    extra=extra,
                )
                if self.verbose:
                    self.log_full_output()

                self.retry = False

                if expect_pass:
                    self.passCount = 1
                else:
                    self.todoCount = 1

            if self.logfiles and process_output:
                self.createLogFile(name, process_output)

        finally:
            self.postCheck(proc)
            if self.profiler and self.singleFile:
                # Symbolication gzips the profile, which moves it, so open the
                # path it returns.
                profile_path = symbolicate_profile_json(profile_path, self.symbolsPath)
                view_gecko_profile(profile_path)
            self.clean_temp_dirs(path)

        if gotSIGINT:
            self.log.error("Received SIGINT (control-C) during test execution")
            if self.keep_going:
                gotSIGINT = False
            else:
                self.keep_going = False
                return

        self.keep_going = True


class XPCShellTests:
    def __init__(self, log=None):
        """Initializes node status and logger."""
        self.log = log
        self.harness_timeout = HARNESS_TIMEOUT
        self.nodeProc = {}
        self.http3Server = None
        self.mozHttp2Server = None
        self.conditioned_profile_dir = None

    def getTestManifest(self, manifest):
        if isinstance(manifest, TestManifest):
            return manifest
        elif manifest is not None:
            manifest = os.path.normpath(os.path.abspath(manifest))
            if os.path.isfile(manifest):
                return TestManifest([manifest], strict=True)
            else:
                toml_path = os.path.join(manifest, "xpcshell.toml")
        else:
            toml_path = os.path.join(SCRIPT_DIR, "tests", "xpcshell.toml")

        if os.path.exists(toml_path):
            return TestManifest([toml_path], strict=True)
        else:
            self.log.error(
                "Failed to find manifest at %s; use --manifest "
                "to set path explicitly." % toml_path
            )
            sys.exit(1)

    def normalizeTest(self, root, test_object):
        path = test_object.get("file_relpath", test_object["relpath"])
        if "dupe-manifest" in test_object and "ancestor_manifest" in test_object:
            # Use same logic as get_full_group_name() to determine which manifest to use
            ancestor_manifest = normsep(test_object["ancestor_manifest"])
            # If ancestor is not the generated root (has path separator), use it
            manifest_for_id = (
                test_object["ancestor_manifest"]
                if "/" in ancestor_manifest
                else test_object["manifest"]
            )
            test_object["id"] = "%s:%s" % (os.path.basename(manifest_for_id), path)
        else:
            test_object["id"] = path

        if root:
            test_object["manifest"] = os.path.relpath(test_object["manifest"], root)

        if os.sep != "/":
            for key in ("id", "manifest"):
                test_object[key] = test_object[key].replace(os.sep, "/")

        return test_object

    def buildTestList(self, test_tags=None, test_paths=None, verify=False):
        """Reads the xpcshell.toml manifest and set self.alltests to an array.

        Given the parameters, this method compiles a list of tests to be run
        that matches the criteria set by parameters.

        If any chunking of tests are to occur, it is also done in this method.

        If no tests are added to the list of tests to be run, an error
        is logged. A sys.exit() signal is sent to the caller.

        Args:
            test_tags (list, optional): list of strings.
            test_paths (list, optional): list of strings derived from the command
                                         line argument provided by user, specifying
                                         tests to be run.
            verify (bool, optional): boolean value.
        """
        if test_paths is None:
            test_paths = []

        mp = self.getTestManifest(self.manifest)

        root = mp.rootdir
        if build and not root:
            root = build.topsrcdir
        normalize = partial(self.normalizeTest, root)

        filters = []
        if test_tags:
            filters.extend([tags(x) for x in test_tags])

        path_filter = None
        if test_paths:
            path_filter = pathprefix(test_paths)
            filters.append(path_filter)

        noDefaultFilters = False
        if self.runFailures:
            filters.append(failures(self.runFailures))
            noDefaultFilters = True

        try:
            self.alltests = list(
                map(
                    normalize,
                    mp.active_tests(
                        filters=filters,
                        noDefaultFilters=noDefaultFilters,
                        strictExpressions=True,
                        **mozinfo.info,
                    ),
                )
            )
        except TypeError:
            sys.stderr.write("*** offending mozinfo.info: %s\n" % repr(mozinfo.info))
            raise

        # Store missing manifests for later use in structured logging
        self.missing_manifests = set()
        if path_filter and path_filter.missing:
            self.missing_manifests = path_filter.missing
            self.log.warning(
                "The following path(s) didn't resolve any tests:\n  {}".format(
                    "  \n".join(sorted(path_filter.missing))
                )
            )

        if len(self.alltests) == 0:
            if (
                test_paths
                and path_filter.missing == set(test_paths)
                and os.environ.get("MOZ_AUTOMATION") == "1"
            ):
                # This can happen in CI when a manifest doesn't exist due to a
                # build config variable in moz.build traversal. Don't generate
                # an error in this case. Adding a todo count avoids mozharness
                # raising an error.
                self.todoCount += len(path_filter.missing)
            else:
                self.log.error(
                    "no tests to run using specified "
                    f"combination of filters: {mp.fmt_filters()}"
                )
                sys.exit(1)

        # Count non-disabled tests for --profiler validation
        enabled_tests = [t for t in self.alltests if "disabled" not in t]
        if len(enabled_tests) == 1 and not verify:
            self.singleFile = os.path.basename(enabled_tests[0]["path"])
        else:
            self.singleFile = None

        if self.dump_tests:
            self.dump_tests = os.path.expanduser(self.dump_tests)
            assert os.path.exists(os.path.dirname(self.dump_tests))
            with open(self.dump_tests, "w") as dumpFile:
                dumpFile.write(json.dumps({"active_tests": self.alltests}))

            self.log.info("Dumping active_tests to %s file." % self.dump_tests)
            sys.exit()

    def setAbsPath(self):
        """
        Set the absolute path for xpcshell and xrepath. These 3 variables
        depend on input from the command line and we need to allow for absolute paths.
        This function is overloaded for a remote solution as os.path* won't work remotely.
        """
        self.testharnessdir = os.path.dirname(os.path.abspath(__file__))
        self.headJSPath = self.testharnessdir.replace("\\", "/") + "/head.js"
        if self.xpcshell is not None:
            self.xpcshell = os.path.abspath(self.xpcshell)

        if self.app_binary is not None:
            self.app_binary = os.path.abspath(self.app_binary)

        if self.xrePath is None:
            binary_path = self.app_binary or self.xpcshell
            self.xrePath = os.path.dirname(binary_path)
            if mozinfo.isMac:
                # Check if we're run from an OSX app bundle and override
                # self.xrePath if we are.
                appBundlePath = os.path.join(
                    os.path.dirname(os.path.dirname(self.xpcshell)), "Resources"
                )
                if os.path.exists(os.path.join(appBundlePath, "application.ini")):
                    self.xrePath = appBundlePath
        else:
            self.xrePath = os.path.abspath(self.xrePath)

        if self.mozInfo is None:
            self.mozInfo = os.path.join(self.testharnessdir, "mozinfo.json")

    def buildPrefsFile(self, extraPrefs):
        # Create the prefs.js file

        # In test packages used in CI, the profile_data directory is installed
        # in the SCRIPT_DIR.
        profile_data_dir = os.path.join(SCRIPT_DIR, "profile_data")
        # If possible, read profile data from topsrcdir. This prevents us from
        # requiring a re-build to pick up newly added extensions in the
        # <profile>/extensions directory.
        if build:
            path = os.path.join(build.topsrcdir, "testing", "profiles")
            if os.path.isdir(path):
                profile_data_dir = path
        # Still not found? Look for testing/profiles relative to testing/xpcshell.
        if not os.path.isdir(profile_data_dir):
            path = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "profiles"))
            if os.path.isdir(path):
                profile_data_dir = path

        with open(os.path.join(profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["xpcshell"]

        # values to use when interpolating preferences
        interpolation = {
            "server": "dummyserver",
        }

        profile = Profile(profile=self.tempDir, restore=False)
        prefsFile = os.path.join(profile.profile, "user.js")

        # Empty the user.js file in case the file existed before.
        with open(prefsFile, "w"):
            pass

        for name in base_profiles:
            path = os.path.join(profile_data_dir, name)
            profile.merge(path, interpolation=interpolation)

        # add command line prefs
        prefs = parse_preferences(extraPrefs)
        profile.set_preferences(prefs)

        self.prefsFile = prefsFile
        return prefs

    def buildCoreEnvironment(self):
        """
        Add environment variables likely to be used across all platforms, including
        remote systems.
        """
        # Make assertions fatal
        self.env["XPCOM_DEBUG_BREAK"] = "stack-and-abort"
        # Crash reporting interferes with debugging
        if not self.debuggerInfo:
            self.env["MOZ_CRASHREPORTER"] = "1"
        # Don't launch the crash reporter client
        self.env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
        # Don't permit remote connections by default.
        # MOZ_DISABLE_NONLOCAL_CONNECTIONS can be set to "0" to temporarily
        # enable non-local connections for the purposes of local testing.
        # Don't override the user's choice here.  See bug 1049688.
        self.env.setdefault("MOZ_DISABLE_NONLOCAL_CONNECTIONS", "1")
        if self.mozInfo.get("topsrcdir") is not None:
            self.env["MOZ_DEVELOPER_REPO_DIR"] = self.mozInfo["topsrcdir"]
        if self.mozInfo.get("topobjdir") is not None:
            self.env["MOZ_DEVELOPER_OBJ_DIR"] = self.mozInfo["topobjdir"]

        # Disable the content process sandbox for the xpcshell tests. They
        # currently attempt to do things like bind() sockets, which is not
        # compatible with the sandbox.
        self.env["MOZ_DISABLE_CONTENT_SANDBOX"] = "1"
        if os.getenv("MOZ_FETCHES_DIR", None):
            self.env["MOZ_FETCHES_DIR"] = os.getenv("MOZ_FETCHES_DIR", None)

        # Self-tests run many sub-processes whose tests fail on purpose, so
        # profiling them only wastes overhead and uploads useless artifacts.
        # Strip any startup-profiling request inherited from the environment
        # (e.g. a `mach try fuzzy --profiler` push) and don't enable it by
        # default.
        if self.selfTest:
            self.env.pop("MOZ_PROFILER_STARTUP", None)
            return

        # Enable the profiler by default with a feature set chosen to keep
        # overhead low while still producing useful profiles: the platform
        # defaults minus `stackwalk` and `fileioall` (both too expensive),
        # plus `ipcmessages` and `memory` so IPC and memory tracks show up.
        #
        # The profiler is left disabled under ThreadSanitizer, where it causes
        # too many failures.
        if not self.mozInfo.get("tsan"):
            self.env.setdefault("MOZ_PROFILER_STARTUP", "1")
            self.env.setdefault(
                "MOZ_PROFILER_STARTUP_FEATURES",
                "java,js,screenshots,processcpu,ipcmessages,memory",
            )

            # Set the sampling interval to 10ms to reduce the sampling overhead
            # and avoid triggering the timer resolution change on Windows.
            self.env.setdefault("MOZ_PROFILER_STARTUP_INTERVAL", "10")

    def buildEnvironment(self):
        """
        Create and returns a dictionary of self.env to include all the appropriate env
        variables and values. On a remote system, we overload this to set different
        values and are missing things like os.environ and PATH.
        """
        self.env = dict(os.environ)
        self.buildCoreEnvironment()
        if sys.platform == "win32":
            self.env["PATH"] = self.env["PATH"] + ";" + self.xrePath
        elif sys.platform in ("os2emx", "os2knix"):
            os.environ["BEGINLIBPATH"] = self.xrePath + ";" + self.env["BEGINLIBPATH"]
            os.environ["LIBPATHSTRICT"] = "T"
        elif sys.platform == "osx" or sys.platform == "darwin":
            self.env["DYLD_LIBRARY_PATH"] = os.path.join(
                os.path.dirname(self.xrePath), "MacOS"
            )
        elif "LD_LIBRARY_PATH" not in self.env or self.env["LD_LIBRARY_PATH"] is None:
            self.env["LD_LIBRARY_PATH"] = self.xrePath
        else:
            self.env["LD_LIBRARY_PATH"] = ":".join([
                self.xrePath,
                self.env["LD_LIBRARY_PATH"],
            ])

        usingASan = "asan" in self.mozInfo and self.mozInfo["asan"]
        usingTSan = "tsan" in self.mozInfo and self.mozInfo["tsan"]
        if usingASan or usingTSan:
            # symbolizer support
            if "ASAN_SYMBOLIZER_PATH" in self.env and os.path.isfile(
                self.env["ASAN_SYMBOLIZER_PATH"]
            ):
                llvmsym = self.env["ASAN_SYMBOLIZER_PATH"]
            else:
                llvmsym = os.path.join(
                    self.xrePath, "llvm-symbolizer" + self.mozInfo["bin_suffix"]
                )
            if os.path.isfile(llvmsym):
                if usingASan:
                    self.env["ASAN_SYMBOLIZER_PATH"] = llvmsym
                else:
                    oldTSanOptions = self.env.get("TSAN_OPTIONS", "")
                    self.env["TSAN_OPTIONS"] = (
                        f"external_symbolizer_path={llvmsym} {oldTSanOptions}"
                    )
                self.log.info("runxpcshelltests.py | using symbolizer at %s" % llvmsym)
            else:
                self.log.error(
                    "TEST-UNEXPECTED-FAIL | runxpcshelltests.py | "
                    "Failed to find symbolizer at %s" % llvmsym
                )

        return self.env

    def getPipes(self):
        """
        Determine the value of the stdout and stderr for the test.
        Return value is a list (pStdout, pStderr).
        """
        if self.interactive:
            pStdout = None
            pStderr = None
        elif self.debuggerInfo and self.debuggerInfo.interactive:
            pStdout = None
            pStderr = None
        else:
            if sys.platform == "os2emx":
                pStdout = None
            else:
                pStdout = PIPE
            pStderr = STDOUT
        return pStdout, pStderr

    def verifyDirPath(self, dirname):
        """
        Simple wrapper to get the absolute path for a given directory name.
        On a remote system, we need to overload this to work on the remote filesystem.
        """
        return os.path.abspath(dirname)

    def buildNodeEnvironment(self):
        """
        Return the environment to use for the node process. This can be overridden
        by subclasses to filter or modify the environment.
        """
        return self.env

    def trySetupNode(self):
        """
        Run node for HTTP/2 tests, if available, and updates mozinfo as appropriate.
        """
        if os.getenv("MOZ_ASSUME_NODE_RUNNING", None):
            self.log.info("Assuming required node servers are already running")
            if not os.getenv("MOZHTTP2_PORT", None):
                self.log.warning(
                    "MOZHTTP2_PORT environment variable not set. "
                    "Tests requiring http/2 will fail."
                )
            return

        nodeBin = os.getenv("MOZ_NODE_PATH", None)
        if not nodeBin and build:
            nodeBin = build.substs.get("NODEJS")
        if not nodeBin:
            self.log.warning(
                "MOZ_NODE_PATH environment variable not set. "
                "Tests requiring http/2 will fail."
            )
            return

        if not os.path.exists(nodeBin) or not os.path.isfile(nodeBin):
            error = "node not found at MOZ_NODE_PATH %s" % (nodeBin)
            self.log.error(error)
            raise OSError(error)

        self.log.info("Found node at %s" % (nodeBin,))

        node_env = self.buildNodeEnvironment()
        myDir = os.path.split(os.path.abspath(__file__))[0]
        serverPath = os.path.join(myDir, "moz-http2", "moz-http2.js")

        serverOptions = {}
        serverOptions["nodeBin"] = nodeBin
        serverOptions["serverPath"] = serverPath
        serverOptions["isWin"] = sys.platform == "win32"

        self.mozHttp2Server = MozHttp2Server(serverOptions, node_env, self.log)
        self.mozHttp2Server.start()

        for key, value in self.mozHttp2Server.ports().items():
            self.env[key] = value

    def removeOwnedTempDir(self):
        """
        Remove the temp dir this run created because the caller gave none.
        """
        if not self.ownsTempDir:
            return
        try:
            shutil.rmtree(self.tempDir)
        except Exception:
            self.log.info(f"{self.tempDir} could not be cleaned up.")

    def shutdownNode(self):
        """
        Shut down our node process, if it exists
        """
        if self.mozHttp2Server is not None:
            self.mozHttp2Server.stop()
            self.mozHttp2Server = None

    def startHttp3Server(self):
        """
        Start a Http3 test server.
        """
        binSuffix = ""
        if sys.platform == "win32":
            binSuffix = ".exe"
        http3ServerPath = self.http3ServerPath
        serverEnv = self.env.copy()
        if not http3ServerPath:
            if self.mozInfo["buildapp"] == "mobile/android":
                # For android, use binary from host utilities.
                http3ServerPath = os.path.join(self.xrePath, "http3server" + binSuffix)
                if sys.platform == "darwin":
                    serverEnv["DYLD_LIBRARY_PATH"] = self.xrePath
                else:
                    serverEnv["LD_LIBRARY_PATH"] = self.xrePath
            elif build:
                http3ServerPath = os.path.join(
                    build.topobjdir, "dist", "bin", "http3server" + binSuffix
                )
            else:
                http3ServerPath = os.path.join(
                    SCRIPT_DIR, "http3server", "http3server" + binSuffix
                )

        # Treat missing http3server as a non-fatal error, because tests that do not
        # depend on http3server may work just fine.
        if not os.path.exists(http3ServerPath):
            self.log.error("Cannot find http3server at path %s" % (http3ServerPath))
            return

        dbPath = os.path.join(SCRIPT_DIR, "http3server", "http3serverDB")
        if build:
            dbPath = os.path.join(build.topsrcdir, "netwerk", "test", "http3serverDB")
        options = {}
        options["http3ServerPath"] = http3ServerPath
        options["profilePath"] = dbPath
        options["isMochitest"] = False
        options["isWin"] = sys.platform == "win32"
        serverLog = self.env.get("MOZHTTP3_SERVER_LOG")
        if serverLog is not None:
            serverEnv["RUST_LOG"] = serverLog
        self.http3Server = Http3Server(options, serverEnv, self.log)
        self.http3Server.start()
        for key, value in self.http3Server.ports().items():
            self.env[key] = value
        self.env["MOZHTTP3_ECH"] = self.http3Server.echConfig()
        self.env["MOZ_HTTP3_SERVER_PATH"] = http3ServerPath
        self.env["MOZ_HTTP3_CERT_DB_PATH"] = dbPath

    def shutdownHttp3Server(self):
        if self.http3Server is None:
            return
        self.http3Server.stop()
        self.http3Server = None

    def buildXpcsRunArgs(self):
        """
        Add arguments to run the test or make it interactive.
        """
        if self.interactive:
            self.xpcsRunArgs = [
                "-e",
                'print("To start the test, type |_execute_test();|.");',
                "-i",
            ]
        else:
            self.xpcsRunArgs = ["-e", "_execute_test(); quit(0);"]

    def addTestResults(self, test):
        self.passCount += test.passCount
        self.failCount += test.failCount
        self.todoCount += test.todoCount

    def updateMozinfo(self, prefs, options):
        # Handle filenames in mozInfo
        if not isinstance(self.mozInfo, dict):
            mozInfoFile = self.mozInfo
            if not os.path.isfile(mozInfoFile):
                self.log.error(
                    "Error: couldn't find mozinfo.json at '%s'. Perhaps you "
                    "need to use --build-info-json?" % mozInfoFile
                )
                return False
            self.mozInfo = json.load(open(mozInfoFile))

        # mozinfo.info is used as kwargs.  Some builds are done with
        # an older Python that can't handle Unicode keys in kwargs.
        # All of the keys in question should be ASCII.
        fixedInfo = {}
        for k, v in self.mozInfo.items():
            if isinstance(k, bytes):
                k = k.decode("utf-8")
            fixedInfo[k] = v
        self.mozInfo = fixedInfo

        self.mozInfo["fission"] = prefs.get("fission.autostart", True)
        self.mozInfo["sessionHistoryInParent"] = True

        self.mozInfo["verify"] = options.get("verify", False)

        self.mozInfo["inc_origin_init"] = (
            os.environ.get("MOZ_ENABLE_INC_ORIGIN_INIT") == "1"
        )

        self.mozInfo["privateBrowsing"] = os.environ.get("MOZ_PRIVATE_BROWSING") == "1"

        self.mozInfo["condprof"] = options.get("conditionedProfile", False)
        self.mozInfo["msix"] = options.get("variant", "") == "msix"

        self.mozInfo["is_ubuntu"] = "Ubuntu" in platform.version()

        # TODO: remove this when crashreporter is fixed on mac via bug 1910777
        if self.mozInfo["os"] == "mac":
            (release, versioninfo, machine) = platform.mac_ver()
            versionNums = release.split(".")[:2]
            os_version = "%s.%s" % (versionNums[0], versionNums[1].ljust(2, "0"))
            if os_version.split(".", 1)[0] in ["14", "15"]:
                self.mozInfo["crashreporter"] = False

        # we default to false for e10s on xpcshell
        self.mozInfo["e10s"] = self.mozInfo.get("e10s", False)

        # isolated_process is used in manifest but this is Android only
        self.mozInfo["isolated_process"] = options.get("isolated_process", False)

        mozinfo.update(self.mozInfo)
        return True

    @property
    def conditioned_profile_copy(self):
        """Returns a copy of the original conditioned profile that was created."""
        condprof_copy = os.path.join(tempfile.mkdtemp(), "profile")
        shutil.copytree(
            self.conditioned_profile_dir,
            condprof_copy,
            ignore=shutil.ignore_patterns("lock"),
        )
        self.log.info("Created a conditioned-profile copy: %s" % condprof_copy)
        return condprof_copy

    def downloadConditionedProfile(self, profile_scenario, app):
        from condprof.client import get_profile
        from condprof.util import get_current_platform, get_version

        if self.conditioned_profile_dir:
            # We already have a directory, so provide a copy that
            # will get deleted after it's done with
            return self.conditioned_profile_dir

        # create a temp file to help ensure uniqueness
        temp_download_dir = tempfile.mkdtemp()
        self.log.info(
            f"Making temp_download_dir from inside get_conditioned_profile {temp_download_dir}"
        )
        # call condprof's client API to yield our platform-specific
        # conditioned-profile binary
        platform = get_current_platform()
        version = None
        if isinstance(app, str):
            version = get_version(app)

        if not profile_scenario:
            profile_scenario = "settled"
        try:
            cond_prof_target_dir = get_profile(
                temp_download_dir,
                platform,
                profile_scenario,
                repo="mozilla-central",
                version=version,
                retries=2,
            )
        except Exception:
            if version is None:
                # any other error is a showstopper
                self.log.critical("Could not get the conditioned profile")
                traceback.print_exc()
                raise
            version = None
            try:
                self.log.info("Retrying a profile with no version specified")
                cond_prof_target_dir = get_profile(
                    temp_download_dir,
                    platform,
                    profile_scenario,
                    repo="mozilla-central",
                    version=version,
                )
            except Exception:
                self.log.critical("Could not get the conditioned profile")
                traceback.print_exc()
                raise

        # now get the full directory path to our fetched conditioned profile
        self.conditioned_profile_dir = os.path.join(
            temp_download_dir, cond_prof_target_dir
        )
        if not os.path.exists(cond_prof_target_dir):
            self.log.critical(
                f"Can't find target_dir {cond_prof_target_dir}, from get_profile()"
                f"temp_download_dir {temp_download_dir}, platform {platform}, scenario {profile_scenario}"
            )
            raise OSError

        self.log.info(
            f"Original self.conditioned_profile_dir is now set: {self.conditioned_profile_dir}"
        )
        return self.conditioned_profile_copy

    def runSelfTest(self):
        import unittest
        from concurrent.futures import ThreadPoolExecutor, as_completed

        import selftest

        this = self

        class XPCShellTestsTests(selftest.XPCShellTestsTests):
            def __init__(self, name):
                unittest.TestCase.__init__(self, name)
                self.testing_modules = this.testingModulesDir
                self.xpcshellBin = this.xpcshell
                self.app_binary = this.app_binary
                self.utility_path = this.utility_path
                self.symbols_path = this.symbolsPath

        old_info = dict(mozinfo.info)
        try:
            suite = unittest.TestLoader().loadTestsFromTestCase(XPCShellTestsTests)
            test_cases = list(suite)
            group = "xpcshell-selftest"
            tests_by_manifest = {
                "xpcshell-selftest": [tc._testMethodName for tc in test_cases]
            }
            self.log.suite_start(tests_by_manifest, name=group)
            self.log.group_start(name="selftests")

            if self.sequential or len(test_cases) <= 1:
                return unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful()

            def run_single_test(test_case):
                result = unittest.TestResult()
                test_name = test_case._testMethodName
                this.log.test_start(test_name, group=group)
                status = "PASS"
                try:
                    test_case.run(result)
                    if not result.wasSuccessful():
                        status = "FAIL"
                except Exception as e:
                    result.addError(test_case, (type(e), e, None))
                    status = "ERROR"
                finally:
                    this.log.test_end(test_name, status, expected="PASS", group=group)
                    return {
                        "result": result,
                        "name": test_name,
                    }

            success = True

            # Limit parallel self-tests to 32 on macOS to avoid "too many open files" error.
            max_workers = (
                min(32, self.threadCount)
                if sys.platform == "darwin"
                else self.threadCount
            )

            self.log.info(
                f"Running {len(test_cases)} self-tests in parallel with up to {max_workers} workers..."
            )
            with ThreadPoolExecutor(max_workers=max_workers) as executor:
                # Submit all tests
                future_to_test = {
                    executor.submit(run_single_test, test): test for test in test_cases
                }

                # Print the status of tests as they finish
                for future in as_completed(future_to_test):
                    try:
                        test_result = future.result()
                        result_obj = test_result["result"]

                        if not result_obj.wasSuccessful():
                            success = False
                            test_name = test_result["name"]
                            if result_obj.failures:
                                self.log.error(f"FAIL: {test_name}")
                                for test, traceback in result_obj.failures:
                                    self.log.error(f"  Failure: {traceback}")
                            if result_obj.errors:
                                self.log.error(f"ERROR: {test_name}")
                                for test, traceback in result_obj.errors:
                                    self.log.error(f"  Error: {traceback}")

                    except Exception as e:
                        self.log.error(f"Exception in test execution: {e}")
                        success = False

            return success

        finally:
            # The self tests modify mozinfo, so we need to reset it.
            mozinfo.info.clear()
            mozinfo.update(old_info)

            self.log.group_end(name="selftests")
            self.log.suite_end()

    def runTests(self, options, testClass=XPCShellTestThread, mobileArgs=None):
        """
        Run xpcshell tests.
        """
        if isinstance(options, Namespace):
            options = vars(options)

        self.tempDir = options.get("tempDir")
        self.ownsTempDir = not self.tempDir
        if self.ownsTempDir:
            self.tempDir = mkdtemp(prefix="xpc-")
        self.tempDir = os.path.normpath(self.tempDir)
        try:
            return self._runTests(options, testClass, mobileArgs)
        finally:
            self.removeOwnedTempDir()

    def _runTests(self, options, testClass, mobileArgs):
        # Number of times to repeat test(s) in --verify mode
        VERIFY_REPEAT = 10

        # Try to guess modules directory.
        # This somewhat grotesque hack allows the buildbot machines to find the
        # modules directory without having to configure the buildbot hosts. This
        # code path should never be executed in local runs because the build system
        # should always set this argument.
        if not options.get("testingModulesDir"):
            possible = os.path.join(here, os.path.pardir, "modules")

            if os.path.isdir(possible):
                testingModulesDir = possible

        if options.get("rerun_failures"):
            if os.path.exists(options.get("failure_manifest")):
                rerun_manifest = os.path.join(
                    os.path.dirname(options["failure_manifest"]), "rerun.toml"
                )
                shutil.copyfile(options["failure_manifest"], rerun_manifest)
                os.remove(options["failure_manifest"])
            else:
                self.log.error("No failures were found to re-run.")
                sys.exit(1)

        if options.get("testingModulesDir"):
            # The resource loader expects native paths. Depending on how we were
            # invoked, a UNIX style path may sneak in on Windows. We try to
            # normalize that.
            testingModulesDir = os.path.normpath(options["testingModulesDir"])

            if not os.path.isabs(testingModulesDir):
                testingModulesDir = os.path.abspath(testingModulesDir)

            if not testingModulesDir.endswith(os.path.sep):
                testingModulesDir += os.path.sep

        self.debuggerInfo = None

        if options.get("debugger"):
            self.debuggerInfo = mozdebug.get_debugger_info(
                options.get("debugger"),
                options.get("debuggerArgs"),
                options.get("debuggerInteractive"),
            )

        self.jsDebuggerInfo = None
        if options.get("jsDebugger"):
            # A namedtuple let's us keep .port instead of ['port']
            JSDebuggerInfo = namedtuple("JSDebuggerInfo", ["port"])
            self.jsDebuggerInfo = JSDebuggerInfo(port=options["jsDebuggerPort"])

        # Apply timeout factor
        timeout_factor = options.get("timeoutFactor", 1.0)
        self.harness_timeout = int(HARNESS_TIMEOUT * timeout_factor)
        self.log.info(
            f"Using harness timeout of {self.harness_timeout}s "
            f"(base={HARNESS_TIMEOUT}s, factor={timeout_factor})"
        )

        self.app_binary = options.get("app_binary")
        self.xpcshell = options.get("xpcshell")
        self.http3ServerPath = options.get("http3server")
        self.xrePath = options.get("xrePath")
        self.utility_path = options.get("utility_path")
        self.appPath = options.get("appPath")
        self.symbolsPath = options.get("symbolsPath")
        self.manifest = options.get("manifest")
        self.dump_tests = options.get("dump_tests")
        self.interactive = options.get("interactive")
        self.verbose = options.get("verbose")
        self.verboseIfFails = options.get("verboseIfFails")
        self.keepGoing = options.get("keepGoing")
        self.logfiles = options.get("logfiles")
        self.profileName = options.get("profileName") or "xpcshell"
        self.mozInfo = options.get("mozInfo")
        self.testingModulesDir = testingModulesDir
        self.sequential = options.get("sequential")
        self.failure_manifest = options.get("failure_manifest")
        self.threadCount = options.get("threadCount") or NUM_THREADS
        self.jscovdir = options.get("jscovdir")
        self.headless = options.get("headless")
        self.selfTest = options.get("selfTest")
        self.runFailures = options.get("runFailures")
        self.timeoutAsPass = options.get("timeoutAsPass")
        self.crashAsPass = options.get("crashAsPass")
        self.conditionedProfile = options.get("conditionedProfile")
        self.repeat = options.get("repeat", 0)
        self.variant = options.get("variant", "")
        self.profiler = options.get("profiler")

        if self.variant == "msix":
            self.appPath = options.get("msixAppPath")
            self.xrePath = options.get("msixXrePath")
            self.app_binary = options.get("msix_app_binary")
            self.xpcshell = None

        self.testCount = 0
        self.passCount = 0
        self.failCount = 0
        self.todoCount = 0

        if self.conditionedProfile:
            self.conditioned_profile_dir = self.downloadConditionedProfile(
                "full", self.appPath
            )
            options["self_test"] = False

        self.setAbsPath()

        eprefs = options.get("extraPrefs") or []
        # enable fission by default
        if options.get("disableFission"):
            eprefs.append("fission.autostart=false")
        else:
            # should be by default, just in case
            eprefs.append("fission.autostart=true")

        prefs = self.buildPrefsFile(eprefs)
        self.buildXpcsRunArgs()

        self.event = Event()

        if not self.updateMozinfo(prefs, options):
            return False

        self.log.info(
            "These variables are available in the mozinfo environment and "
            "can be used to skip tests conditionally:"
        )
        for info in sorted(self.mozInfo.items(), key=lambda item: item[0]):
            self.log.info(f"    {info[0]}: {info[1]}")

        if options.get("self_test"):
            if not self.runSelfTest():
                return False

        if (
            ("tsan" in self.mozInfo and self.mozInfo["tsan"])
            or ("asan" in self.mozInfo and self.mozInfo["asan"])
        ) and not options.get("threadCount"):
            # TSan/ASan require significantly more memory, so reduce the amount of parallel
            # tests we run to avoid OOMs and timeouts. We always keep a minimum of 2 for
            # non-sequential execution.
            # pylint --py3k W1619
            self.threadCount = max(self.threadCount / 2, 2)

        self.stack_fixer_function = None
        if self.utility_path and os.path.exists(self.utility_path):
            self.stack_fixer_function = get_stack_fixer_function(
                self.utility_path, self.symbolsPath
            )

        # buildEnvironment() needs mozInfo, so we call it after mozInfo is initialized.
        self.buildEnvironment()
        extraEnv = parse_key_value(options.get("extraEnv") or [], context="--setenv")
        for k, v in extraEnv:
            if k in self.env:
                self.log.info(
                    "Using environment variable %s instead of %s." % (v, self.env[k])
                )
            self.env[k] = v

        # The appDirKey is a optional entry in either the default or individual test
        # sections that defines a relative application directory for test runs. If
        # defined we pass 'grePath/$appDirKey' for the -a parameter of the xpcshell
        # test harness.
        appDirKey = None
        if "appname" in self.mozInfo:
            appDirKey = self.mozInfo["appname"] + "-appdir"

        # We have to do this before we run tests that depend on having the node
        # http/2 server.
        self.trySetupNode()

        self.startHttp3Server()

        pStdout, pStderr = self.getPipes()

        self.buildTestList(
            options.get("test_tags"), options.get("testPaths"), options.get("verify")
        )
        if self.singleFile and not self.selfTest:
            self.sequential = True

        if options.get("shuffle"):
            random.shuffle(self.alltests)

        self.cleanup_dir_list = []

        # If any of the tests that are about to be run uses npm packages
        # we should install them now. It would also be possible for tests
        # to define the location where they want the npm modules to be
        # installed, but for now only netwerk xpcshell tests use it.
        installNPM = False
        for test in self.alltests:
            if "usesNPM" in test:
                installNPM = True
                break

        if installNPM:
            env = os.environ.copy()
            nodePath = os.environ.get("MOZ_NODE_PATH", "")
            if nodePath:
                node_bin_path = os.path.dirname(nodePath)
                env["PATH"] = f"{node_bin_path}{os.pathsep}{env.get('PATH', '')}"

            # Try to find npm in PATH
            npm_executable = shutil.which("npm", path=env.get("PATH"))

            if npm_executable:
                command = [npm_executable, "ci"]
                working_directory = os.path.join(SCRIPT_DIR, "moz-http2")
                result = subprocess.run(
                    command,
                    cwd=working_directory,
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                )

                # Print the output
                self.log.info("npm output: " + result.stdout)
                self.log.info("npm error: " + result.stderr)
                self.log.info("npm return code: " + str(result.returncode))
            else:
                self.log.warning(
                    "npm step was skipped because no executable could be resolved."
                )

        kwargs = {
            "appPath": self.appPath,
            "xrePath": self.xrePath,
            "utility_path": self.utility_path,
            "testingModulesDir": self.testingModulesDir,
            "debuggerInfo": self.debuggerInfo,
            "jsDebuggerInfo": self.jsDebuggerInfo,
            "headJSPath": self.headJSPath,
            "tempDir": self.tempDir,
            "testharnessdir": self.testharnessdir,
            "profileName": self.profileName,
            "singleFile": self.singleFile,
            "env": self.env,  # making a copy of this in the testthreads
            "symbolsPath": self.symbolsPath,
            "logfiles": self.logfiles,
            "app_binary": self.app_binary,
            "xpcshell": self.xpcshell,
            "xpcsRunArgs": self.xpcsRunArgs,
            "failureManifest": self.failure_manifest,
            "jscovdir": self.jscovdir,
            "harness_timeout": self.harness_timeout,
            "stack_fixer_function": self.stack_fixer_function,
            "event": self.event,
            "cleanup_dir_list": self.cleanup_dir_list,
            "pStdout": pStdout,
            "pStderr": pStderr,
            "keep_going": self.keepGoing,
            "log": self.log,
            "interactive": self.interactive,
            "app_dir_key": appDirKey,
            "rootPrefsFile": self.prefsFile,
            "extraPrefs": options.get("extraPrefs") or [],
            "verboseIfFails": self.verboseIfFails,
            "headless": self.headless,
            "selfTest": self.selfTest,
            "runFailures": self.runFailures,
            "timeoutAsPass": self.timeoutAsPass,
            "crashAsPass": self.crashAsPass,
            "conditionedProfileDir": self.conditioned_profile_dir,
            "repeat": self.repeat,
            "profiler": self.profiler,
        }

        # Only set retry if explicitly provided (avoid overriding default behavior)
        if options.get("retry") is not None:
            kwargs["retry"] = options.get("retry")

        if self.sequential:
            # Allow user to kill hung xpcshell subprocess with SIGINT
            # when we are only running tests sequentially.
            signal.signal(signal.SIGINT, markGotSIGINT)

        if self.debuggerInfo:
            # Force a sequential run
            self.sequential = True

            # If we have an interactive debugger, disable SIGINT entirely.
            if self.debuggerInfo.interactive:
                signal.signal(signal.SIGINT, lambda signum, frame: None)

            if "lldb" in self.debuggerInfo.path:
                # Ask people to start debugging using 'process launch', see bug 952211.
                self.log.info(
                    "It appears that you're using LLDB to debug this test.  "
                    + "Please use the 'process launch' command instead of "
                    "the 'run' command to start xpcshell."
                )

        if self.jsDebuggerInfo:
            # The js debugger magic needs more work to do the right thing
            # if debugging multiple files.
            if len(self.alltests) != 1:
                self.log.error(
                    "Error: --jsdebugger can only be used with a single test!"
                )
                return False

        # The test itself needs to know whether it is a tsan build, since
        # that has an effect on interpretation of the process return value.
        usingTSan = "tsan" in self.mozInfo and self.mozInfo["tsan"]

        usingCrashReporter = (
            "crashreporter" in self.mozInfo and self.mozInfo["crashreporter"]
        )

        # create a queue of all tests that will run
        tests_queue = deque()
        # also a list for the tests that need to be run sequentially
        sequential_tests = []
        status = None

        if options.get("repeat", 0) > 0:
            self.sequential = True

        def _match_run_sequentially(value, **values):
            """Helper function to evaluate run-sequentially conditions like skip-if/run-if"""
            return any(parse(e, strict=True, **values) for e in value.splitlines() if e)

        if not options.get("verify"):
            for test_object in self.alltests:
                # Test identifiers are provided for the convenience of logging. These
                # start as path names but are rewritten in case tests from the same path
                # are re-run.

                path = test_object["path"]

                if self.singleFile and not path.endswith(self.singleFile):
                    continue

                # if we have --repeat, duplicate the tests as needed
                for i in range(0, options.get("repeat", 0) + 1):
                    self.testCount += 1

                    test = testClass(
                        test_object,
                        verbose=self.verbose or test_object.get("verbose") == "true",
                        usingTSan=usingTSan,
                        usingCrashReporter=usingCrashReporter,
                        mobileArgs=mobileArgs,
                        **kwargs,
                    )
                    if (
                        "run-sequentially" in test_object
                        and _match_run_sequentially(
                            test_object["run-sequentially"], **mozinfo.info
                        )
                    ) or self.sequential:
                        sequential_tests.append(test)
                    else:
                        tests_queue.append(test)

            # Sort parallel tests by timeout factor (descending) to start slower tests first
            # This helps optimize parallel execution by avoiding long-running tests at the end
            if tests_queue:
                tests_queue = deque(
                    sorted(
                        tests_queue,
                        key=lambda t: int(t.test_object.get("requesttimeoutfactor", 1)),
                        reverse=True,
                    )
                )

            status = self.runTestList(
                tests_queue, sequential_tests, testClass, mobileArgs, **kwargs
            )
        else:
            #
            # Test verification: Run each test many times, in various configurations,
            # in hopes of finding intermittent failures.
            #

            # A retry would hide the intermittent failures we are looking for.
            kwargs["retry"] = False

            def step1():
                # Run tests sequentially. Parallel mode would also work, except that
                # the logging system gets confused when 2 or more tests with the same
                # name run at the same time.
                sequential_tests = []
                for i in range(VERIFY_REPEAT):
                    self.testCount += 1
                    test = testClass(test_object, mobileArgs=mobileArgs, **kwargs)
                    sequential_tests.append(test)
                status = self.runTestList(
                    tests_queue, sequential_tests, testClass, mobileArgs, **kwargs
                )
                return status

            def step2():
                # Run tests sequentially, with MOZ_CHAOSMODE enabled.
                sequential_tests = []
                self.env["MOZ_CHAOSMODE"] = "0xfb"

                # for android, adjust flags to avoid slow down
                if self.env.get("MOZ_ANDROID_DATA_DIR", ""):
                    self.env["MOZ_CHAOSMODE"] = "0x3b"

                # chaosmode runs really slow, allow tests extra time to pass
                kwargs["harness_timeout"] = self.harness_timeout * 2
                for i in range(VERIFY_REPEAT):
                    self.testCount += 1
                    test = testClass(test_object, mobileArgs=mobileArgs, **kwargs)
                    sequential_tests.append(test)
                status = self.runTestList(
                    tests_queue, sequential_tests, testClass, mobileArgs, **kwargs
                )
                kwargs["harness_timeout"] = self.harness_timeout
                return status

            steps = [
                ("1. Run each test %d times, sequentially." % VERIFY_REPEAT, step1),
                (
                    "2. Run each test %d times, sequentially, in chaos mode."
                    % VERIFY_REPEAT,
                    step2,
                ),
            ]
            startTime = datetime.now()
            maxTime = timedelta(seconds=options["verifyMaxTime"])
            for test_object in self.alltests:
                stepResults = {}
                for descr, step in steps:
                    stepResults[descr] = "not run / incomplete"
                finalResult = "PASSED"
                for descr, step in steps:
                    if (datetime.now() - startTime) > maxTime:
                        self.log.info(
                            "::: Test verification is taking too long: Giving up!"
                        )
                        self.log.info(
                            "::: So far, all checks passed, but not "
                            "all checks were run."
                        )
                        break
                    self.log.info(":::")
                    self.log.info('::: Running test verification step "%s"...' % descr)
                    self.log.info(":::")
                    status = step()
                    if status is not True:
                        stepResults[descr] = "FAIL"
                        finalResult = "FAILED!"
                        break
                    stepResults[descr] = "Pass"
                self.log.info(":::")
                self.log.info(
                    "::: Test verification summary for: %s" % test_object["path"]
                )
                self.log.info(":::")
                for descr in sorted(stepResults.keys()):
                    self.log.info("::: %s : %s" % (descr, stepResults[descr]))
                self.log.info(":::")
                self.log.info("::: Test verification %s" % finalResult)
                self.log.info(":::")

        self.shutdownNode()
        self.shutdownHttp3Server()

        return status

    def start_test(self, test):
        test.start()

    def test_ended(self, test):
        pass

    def join_test(self, test):
        """Wait for a test to be done rather than for its thread to exit: a test
        that timed out leaving a child process holding its stdout pipe open
        never returns from communicate(), and testTimeout marks it done for us.
        The thread is a daemon, so leaving it blocked behind is harmless."""
        while not test.done:
            test.join(1)

    def runTestList(
        self, tests_queue, sequential_tests, testClass, mobileArgs, **kwargs
    ):
        if self.sequential:
            self.log.info("Running tests sequentially.")
        else:
            self.log.info("Using at most %d threads." % self.threadCount)

        # keep a set of threadCount running tests and start running the
        # tests in the queue at most threadCount at a time
        running_tests = set()
        keep_going = True
        infra_abort = False
        exceptions = []
        tracebacks = []
        self.try_again_list = []

        tests_by_manifest = defaultdict(list)
        for test in self.alltests:
            group = get_full_group_name(test)
            tests_by_manifest[group].append(test["id"])

        # Add missing manifests with empty test lists so they appear in
        # group_result output with SKIP status
        for missing_path in self.missing_manifests:
            tests_by_manifest[missing_path] = []

        self.log.suite_start(tests_by_manifest, name="xpcshell")

        # Start group for parallel test execution
        parallel_group_started = False
        if tests_queue:
            self.log.group_start(name="parallel", extra={"threads": self.threadCount})
            parallel_group_started = True

        while tests_queue or running_tests:
            # if we're not supposed to continue and all of the running tests
            # are done, stop
            if not keep_going and not running_tests:
                break

            # if there's room to run more tests, start running them
            while (
                keep_going and tests_queue and (len(running_tests) < self.threadCount)
            ):
                test = tests_queue.popleft()
                running_tests.add(test)
                self.start_test(test)

            # queue is full (for now) or no more new tests,
            # process the finished tests so far

            # wait for at least one of the tests to finish
            self.event.wait(1)
            self.event.clear()

            # find what tests are done (might be more than 1)
            done_tests = set()
            for test in running_tests:
                if test.done:
                    self.test_ended(test)
                    done_tests.add(test)
                    test.join(
                        1
                    )  # join with timeout so we don't hang on blocked threads
                    # if the test had trouble, we will try running it again
                    # at the end of the run
                    if test.retry or test.is_alive():
                        # if the join call timed out, test.is_alive => True
                        self.try_again_list.append(test.test_object)
                        # Print the failure output now, marking failures as expected
                        # since we'll retry sequentially
                        test.log_full_output(mark_failures_as_expected=True)
                        continue
                    # did the test encounter any exception?
                    if test.exception:
                        exceptions.append(test.exception)
                        tracebacks.append(test.traceback)
                        # we won't add any more tests, will just wait for
                        # the currently running ones to finish
                        keep_going = False
                    infra_abort = infra_abort and test.infra
                    keep_going = keep_going and test.keep_going
                    self.addTestResults(test)

            # make room for new tests to run
            running_tests.difference_update(done_tests)

        # End group for parallel test execution
        if parallel_group_started:
            self.log.group_end(name="parallel")

        if infra_abort:
            return TBPL_RETRY  # terminate early

        if keep_going:
            # run the other tests sequentially
            if sequential_tests:
                self.log.group_start(name="sequential")
            for test in sequential_tests:
                if not keep_going:
                    self.log.error(
                        "TEST-UNEXPECTED-FAIL | Received SIGINT (control-C), so "
                        "stopped run. (Use --keep-going to keep running tests "
                        "after killing one with SIGINT)"
                    )
                    break
                self.start_test(test)
                self.join_test(test)
                self.test_ended(test)
                if (test.failCount > 0 or test.passCount <= 0) and test.retry:
                    self.try_again_list.append(test.test_object)
                    # Print the failure output now, marking failures as expected
                    # since we'll retry sequentially
                    test.log_full_output(mark_failures_as_expected=True)
                    continue
                self.addTestResults(test)
                # did the test encounter any exception?
                if test.exception:
                    exceptions.append(test.exception)
                    tracebacks.append(test.traceback)
                    break
                keep_going = test.keep_going

            if sequential_tests:
                self.log.group_end(name="sequential")

        # retry tests that failed when run in parallel
        if self.try_again_list:
            self.log.info("Retrying tests that failed when run in parallel.")
            self.log.group_start(name="retry")

        try_again_kwargs = kwargs.copy()
        try_again_kwargs["retry"] = False
        try_again_kwargs["is_retry"] = True
        for test_object in self.try_again_list:
            test = testClass(
                test_object,
                verbose=self.verbose,
                mobileArgs=mobileArgs,
                **try_again_kwargs,
            )
            self.start_test(test)
            self.join_test(test)
            self.test_ended(test)
            self.addTestResults(test)
            # did the test encounter any exception?
            if test.exception:
                exceptions.append(test.exception)
                tracebacks.append(test.traceback)
                break
            keep_going = test.keep_going

        if self.try_again_list:
            self.log.group_end(name="retry")

        # restore default SIGINT behaviour
        if self.sequential:
            signal.signal(signal.SIGINT, signal.SIG_DFL)

        # Clean up any slacker directories that might be lying around
        # Some might fail because of windows taking too long to unlock them.
        # We don't do anything if this fails because the test machines will have
        # their $TEMP dirs cleaned up on reboot anyway.
        for directory in self.cleanup_dir_list:
            try:
                shutil.rmtree(directory)
            except Exception:
                self.log.info("%s could not be cleaned up." % directory)

        if exceptions:
            self.log.info("Following exceptions were raised:")
            for t in tracebacks:
                self.log.error(t)
            raise exceptions[0]

        if self.testCount == 0 and os.environ.get("MOZ_AUTOMATION") != "1":
            self.log.error("No tests run. Did you pass an invalid --test-path?")
            self.failCount = 1

        # doing this allows us to pass the mozharness parsers that
        # report an orange job for failCount>0
        if self.runFailures:
            passed = self.passCount
            self.passCount = self.failCount
            self.failCount = passed

        self.log.info("INFO | Result summary:")
        self.log.info("INFO | Passed: %d" % self.passCount)
        self.log.info("INFO | Failed: %d" % self.failCount)
        self.log.info("INFO | Todo: %d" % self.todoCount)
        self.log.info("INFO | Retried: %d" % len(self.try_again_list))

        if gotSIGINT and not keep_going:
            self.log.error(
                "TEST-UNEXPECTED-FAIL | Received SIGINT (control-C), so stopped run. "
                "(Use --keep-going to keep running tests after "
                "killing one with SIGINT)"
            )
            return False

        self.log.suite_end()
        return self.runFailures or self.failCount == 0


def main():
    parser = parser_desktop()
    options = parser.parse_args()

    log = commandline.setup_logging("XPCShell", options, {"raw": sys.stdout})

    if options.xpcshell is None and options.app_binary is None:
        log.error(
            "Must provide path to xpcshell using --xpcshell or Firefox using --app-binary"
        )
        sys.exit(1)

    if options.xpcshell is not None and options.app_binary is not None:
        log.error(
            "Cannot provide --xpcshell and --app-binary - they are mutually exclusive options. Choose one."
        )
        sys.exit(1)

    xpcsh = XPCShellTests(log)

    if options.interactive and not options.testPath:
        log.error("Error: You must specify a test filename in interactive mode!")
        sys.exit(1)

    result = xpcsh.runTests(options)

    if "MOZ_AUTOMATION" in os.environ:
        symbolicate_profiles(symbol_dir=options.symbolsPath)

    if result == TBPL_RETRY:
        sys.exit(4)

    if not result:
        sys.exit(1)


if __name__ == "__main__":
    main()
