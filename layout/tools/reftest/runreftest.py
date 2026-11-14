# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Runs the reftest test harness.
"""

import json
import os
import platform
import posixpath
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
from collections import defaultdict
from datetime import datetime, timedelta

SCRIPT_DIRECTORY = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
if SCRIPT_DIRECTORY not in sys.path:
    sys.path.insert(0, SCRIPT_DIRECTORY)

import mozcrash
import mozdebug
import mozfile
import mozinfo
import mozleak
import mozlog
import mozprocess
import mozprofile
import mozrunner
from manifestparser import TestManifest
from manifestparser import filters as mpf
from mozrunner.utils import get_stack_fixer_function, test_environment
from mozscreenshot import dump_screen, printstatus

try:
    from marionette_driver.addons import Addons
    from marionette_driver.marionette import Marionette
except ImportError as e:  # noqa
    # Defer ImportError until attempt to use Marionette.
    # Python 3 deletes the exception once the except block
    # is exited. Save a version to raise later.
    e_save = ImportError(str(e))

    def reraise_(*args, **kwargs):
        raise (e_save)  # noqa

    Marionette = reraise_

import reftestcommandline
from output import OutputHandler

here = os.path.abspath(os.path.dirname(__file__))

try:
    from mozbuild.base import MozbuildObject
    from mozbuild.util import cpu_count

    build_obj = MozbuildObject.from_environment(cwd=here)
except ImportError:
    build_obj = None
    from multiprocessing import cpu_count


def _get_available_port(preferred):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("127.0.0.1", preferred))
        except OSError:
            s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def categoriesToRegex(categoryList):
    return "\\(" + ", ".join(["(?P<%s>\\d+) %s" % c for c in categoryList]) + "\\)"


summaryLines = [
    ("Successful", [("pass", "pass"), ("loadOnly", "load only")]),
    (
        "Unexpected",
        [
            ("fail", "unexpected fail"),
            ("pass", "unexpected pass"),
            ("asserts", "unexpected asserts"),
            ("fixedAsserts", "unexpected fixed asserts"),
            ("failedLoad", "failed load"),
            ("exception", "exception"),
        ],
    ),
    (
        "Known problems",
        [
            ("knownFail", "known fail"),
            ("knownAsserts", "known asserts"),
            ("random", "random"),
            ("skipped", "skipped"),
            ("slow", "slow"),
        ],
    ),
]


def reraise_(tp_, value_, tb_=None):
    if value_ is None:
        value_ = tp_()
    if value_.__traceback__ is not tb_:
        raise value_.with_traceback(tb_)
    raise value_


def update_mozinfo():
    """walk up directories to find mozinfo.json update the info"""
    # TODO: This should go in a more generic place, e.g. mozinfo

    path = SCRIPT_DIRECTORY
    dirs = set()
    while path != os.path.expanduser("~"):
        if path in dirs:
            break
        dirs.add(path)
        path = os.path.split(path)[0]
    mozinfo.find_and_update_from_json(*dirs)


# Python's print is not threadsafe.
printLock = threading.Lock()


class ReftestThread(threading.Thread):
    def __init__(self, cmdargs):
        threading.Thread.__init__(self)
        self.cmdargs = cmdargs
        self.summaryMatches = {}
        self.retcode = -1
        for text, _ in summaryLines:
            self.summaryMatches[text] = None

    def run(self):
        with printLock:
            print("Starting thread with", self.cmdargs)
            sys.stdout.flush()
        process = subprocess.Popen(self.cmdargs, stdout=subprocess.PIPE)
        for chunk in self.chunkForMergedOutput(process.stdout):
            with printLock:
                print(chunk, end=" ")
                sys.stdout.flush()
        self.retcode = process.wait()

    def chunkForMergedOutput(self, logsource):
        """Gather lines together that should be printed as one atomic unit.
        Individual test results--anything between 'REFTEST TEST-START' and
        'REFTEST TEST-END' lines--are an atomic unit.  Lines with data from
        summaries are parsed and the data stored for later aggregation.
        Other lines are considered their own atomic units and are permitted
        to intermix freely."""
        testStartRegex = re.compile("^REFTEST TEST-START")
        testEndRegex = re.compile("^REFTEST TEST-END")
        summaryHeadRegex = re.compile("^REFTEST INFO \\| Result summary:")
        summaryRegexFormatString = (
            "^REFTEST INFO \\| (?P<message>{text}): (?P<total>\\d+) {regex}"
        )
        summaryRegexStrings = [
            summaryRegexFormatString.format(
                text=text, regex=categoriesToRegex(categories)
            )
            for (text, categories) in summaryLines
        ]
        summaryRegexes = [re.compile(regex) for regex in summaryRegexStrings]

        for line in logsource:
            if testStartRegex.search(line) is not None:
                chunkedLines = [line]
                for lineToBeChunked in logsource:
                    chunkedLines.append(lineToBeChunked)
                    if testEndRegex.search(lineToBeChunked) is not None:
                        break
                yield "".join(chunkedLines)
                continue

            haveSuppressedSummaryLine = False
            for regex in summaryRegexes:
                match = regex.search(line)
                if match is not None:
                    self.summaryMatches[match.group("message")] = match
                    haveSuppressedSummaryLine = True
                    break
            if haveSuppressedSummaryLine:
                continue

            if summaryHeadRegex.search(line) is None:
                yield line


class ReftestResolver:
    def defaultManifest(self, suite):
        return {
            "reftest": "reftest.list",
            "crashtest": "crashtests.list",
            "jstestbrowser": "jstests.list",
        }[suite]

    def directoryManifest(self, suite, path):
        return os.path.join(path, self.defaultManifest(suite))

    def findManifest(self, suite, test_file, subdirs=True):
        """Return a tuple of (manifest-path, filter-string) for running test_file.

        test_file is a path to a test or a manifest file
        """
        rv = []
        default_manifest = self.defaultManifest(suite)
        relative_path = None
        if not os.path.isabs(test_file):
            relative_path = test_file
            test_file = self.absManifestPath(test_file)

        if os.path.isdir(test_file):
            for dirpath, dirnames, filenames in os.walk(test_file):
                if default_manifest in filenames:
                    rv.append((os.path.join(dirpath, default_manifest), None))
                    # We keep recursing into subdirectories which means that in the case
                    # of include directives we get the same manifest multiple times.
                    # However reftest.js will only read each manifest once

            if (
                len(rv) == 0
                and relative_path
                and suite == "jstestbrowser"
                and build_obj
            ):
                # The relative path can be from staging area.
                staged_js_dir = os.path.join(
                    build_obj.topobjdir, "dist", "test-stage", "jsreftest"
                )
                staged_file = os.path.join(staged_js_dir, "tests", relative_path)
                return self.findManifest(suite, staged_file, subdirs)
        elif test_file.endswith(".list"):
            if os.path.exists(test_file):
                rv = [(test_file, None)]
        else:
            dirname, pathname = os.path.split(test_file)
            found = True
            while not os.path.exists(os.path.join(dirname, default_manifest)):
                dirname, suffix = os.path.split(dirname)
                pathname = posixpath.join(suffix, pathname)
                if os.path.dirname(dirname) == dirname:
                    found = False
                    break
            if found:
                rv = [
                    (
                        os.path.join(dirname, default_manifest),
                        r".*%s(?:[#?].*)?$" % pathname.replace("?", r"\?"),
                    )
                ]

        return rv

    def absManifestPath(self, path):
        return os.path.normpath(os.path.abspath(path))

    def manifestURL(self, options, path):
        return "file://%s" % path

    def resolveManifests(self, options, tests):
        suite = options.suite
        manifests = {}
        for testPath in tests:
            for manifest, filter_str in self.findManifest(suite, testPath):
                if manifest not in manifests:
                    manifests[manifest] = set()
                manifests[manifest].add(filter_str)
        manifests_by_url = {}
        for key in manifests.keys():
            id = os.path.relpath(
                os.path.abspath(os.path.dirname(key)), options.topsrcdir
            )
            id = id.replace(os.sep, posixpath.sep)
            if None in manifests[key]:
                manifests[key] = (None, id)
            else:
                manifests[key] = ("|".join(list(manifests[key])), id)
            url = self.manifestURL(options, key)
            manifests_by_url[url] = manifests[key]
        return manifests_by_url


class RefTest:
    oldcwd = os.getcwd()
    resolver_cls = ReftestResolver
    use_marionette = True

    def __init__(self, suite):
        update_mozinfo()
        self.lastTestSeen = None
        self.lastTest = None
        self.haveDumpedScreen = False
        self.resolver = self.resolver_cls()
        self.log = None
        self.outputHandler = None
        self.testDumpFile = os.path.join(tempfile.gettempdir(), "reftests.json")
        self.currentManifest = "No test started"
        self.currentManifestID = None
        self.gtkTheme = self.getGtkTheme()

        self.run_by_manifest = True
        if suite in ("crashtest", "jstestbrowser"):
            self.run_by_manifest = False

    def _populate_logger(self, options):
        if self.log:
            return

        self.log = getattr(options, "log", None)
        if self.log:
            return

        self.log = mozlog.commandline.setup_logging(
            "reftest harness", options, {"raw": sys.stdout}
        )

    def getGtkTheme(self):
        if not platform.system() == "Linux":
            return ""

        try:
            theme_cmd = "gsettings get org.gnome.desktop.interface gtk-theme"
            theme = subprocess.check_output(
                theme_cmd, shell=True, universal_newlines=True
            )
            if theme:
                theme = theme.strip("\n")
                theme = theme.strip("'")
            return theme.strip()
        except subprocess.CalledProcessError:
            return ""

    def getFullPath(self, path):
        "Get an absolute path relative to self.oldcwd."
        return os.path.normpath(os.path.join(self.oldcwd, os.path.expanduser(path)))

    def createReftestProfile(
        self,
        options,
        tests=None,
        manifests=None,
        server="localhost",
        port=0,
        profile_to_clone=None,
        prefs=None,
    ):
        """Sets up a profile for reftest.

        :param options: Object containing command line options
        :param tests: List of test objects to run
        :param manifests: List of manifest files to parse (only takes effect
                          if tests were not passed in)
        :param server: Server name to use for http tests
        :param profile_to_clone: Path to a profile to use as the basis for the
                                 test profile
        :param prefs: Extra preferences to set in the profile
        """
        locations = mozprofile.permissions.ServerLocations()
        locations.add_host(server, scheme="http", port=port)
        locations.add_host(server, scheme="https", port=port)

        sandbox_allowlist_paths = [
            os.path.normpath(p) for p in options.sandboxReadWhitelist
        ]
        if platform.system() == "Linux" or platform.system() in (
            "Windows",
            "Microsoft",
        ):
            # Trailing slashes are needed to indicate directories on Linux and Windows
            sandbox_allowlist_paths = [
                os.path.join(p, "") for p in sandbox_allowlist_paths
            ]

        addons = []
        if not self.use_marionette:
            addons.append(options.reftestExtensionPath)

        if options.specialPowersExtensionPath is not None:
            if not self.use_marionette:
                addons.append(options.specialPowersExtensionPath)

        # Install distributed extensions, if application has any.
        distExtDir = os.path.join(
            options.app[: options.app.rfind(os.sep)], "distribution", "extensions"
        )
        if os.path.isdir(distExtDir):
            for f in os.listdir(distExtDir):
                addons.append(os.path.join(distExtDir, f))

        # Install custom extensions.
        for f in options.extensionsToInstall:
            addons.append(self.getFullPath(f))

        kwargs = {
            "addons": addons,
            "locations": locations,
            "allowlistpaths": sandbox_allowlist_paths,
        }
        if profile_to_clone:
            profile = mozprofile.Profile.clone(profile_to_clone, **kwargs)
        else:
            profile = mozprofile.Profile(**kwargs)

        # First set prefs from the base profiles under testing/profiles.

        # In test packages used in CI, the profile_data directory is installed
        # in the SCRIPT_DIRECTORY.
        profile_data_dir = os.path.join(SCRIPT_DIRECTORY, "profile_data")
        # If possible, read profile data from topsrcdir. This prevents us from
        # requiring a re-build to pick up newly added extensions in the
        # <profile>/extensions directory.
        if build_obj:
            path = os.path.join(build_obj.topsrcdir, "testing", "profiles")
            if os.path.isdir(path):
                profile_data_dir = path
        # Still not found? Look for testing/profiles relative to layout/tools/reftest.
        if not os.path.isdir(profile_data_dir):
            path = os.path.abspath(
                os.path.join(SCRIPT_DIRECTORY, "..", "..", "..", "testing", "profiles")
            )
            if os.path.isdir(path):
                profile_data_dir = path

        with open(os.path.join(profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["reftest"]

        for name in base_profiles:
            path = os.path.join(profile_data_dir, name)
            profile.merge(path)

        # Second set preferences for communication between our command line
        # arguments and the reftest harness. Preferences that are required for
        # reftest to work should instead be set under srcdir/testing/profiles.
        prefs = prefs or {}
        prefs["reftest.timeout"] = options.timeout * 1000
        if options.logFile:
            prefs["reftest.logFile"] = options.logFile
        if options.ignoreWindowSize:
            prefs["reftest.ignoreWindowSize"] = True
        if options.shuffle:
            prefs["reftest.shuffle"] = True
        if options.repeat:
            prefs["reftest.repeat"] = options.repeat
        if options.runUntilFailure:
            prefs["reftest.runUntilFailure"] = True
            if not options.repeat:
                prefs["reftest.repeat"] = 30
        if options.verify:
            prefs["reftest.verify"] = True
        if options.cleanupCrashes:
            prefs["reftest.cleanupPendingCrashes"] = True
        prefs["reftest.focusFilterMode"] = options.focusFilterMode
        prefs["reftest.logLevel"] = options.log_tbpl_level or "info"
        prefs["reftest.suite"] = options.suite
        prefs["sandbox.mozinfo"] = json.dumps(mozinfo.info)

        # Set tests to run or manifests to parse.
        if tests:
            testlist = os.path.join(profile.profile, "reftests.json")
            with open(testlist, "w") as fh:
                json.dump(tests, fh)
            prefs["reftest.tests"] = testlist
        elif manifests:
            prefs["reftest.manifests"] = json.dumps(manifests)

        # Avoid unncessary recursion when MOZHARNESS_TEST_PATHS is set
        prefs["reftest.mozharness_test_paths"] = (
            len(os.environ.get("MOZHARNESS_TEST_PATHS", "")) > 0
        )

        # default fission to True
        prefs["fission.autostart"] = True
        if options.disableFission:
            prefs["fission.autostart"] = False

        if not self.run_by_manifest:
            if options.totalChunks:
                prefs["reftest.totalChunks"] = options.totalChunks
            if options.thisChunk:
                prefs["reftest.thisChunk"] = options.thisChunk

        if options.marionette:
            # options.marionette can specify host:port
            port = options.marionette.split(":")[1]
            prefs["marionette.port"] = int(port)

        # Enable tracing output for detailed failures in case of
        # failing connection attempts, and hangs (bug 1397201)
        prefs["remote.log.level"] = "Trace"

        # Third, set preferences passed in via the command line.
        for v in options.extraPrefs:
            thispref = v.split("=")
            if len(thispref) < 2:
                print("Error: syntax error in --setpref=" + v)
                sys.exit(1)
            prefs[thispref[0]] = thispref[1].strip()

        for pref in prefs:
            prefs[pref] = mozprofile.Preferences.cast(prefs[pref])
        profile.set_preferences(prefs)

        if os.path.join(here, "chrome") not in options.extraProfileFiles:
            options.extraProfileFiles.append(os.path.join(here, "chrome"))

        self.copyExtraFilesToProfile(options, profile)

        self.log.info(f"Running with e10s: {options.e10s}")
        self.log.info("Running with fission: {}".format(prefs["fission.autostart"]))

        return profile

    def environment(self, **kwargs):
        kwargs["log"] = self.log
        return test_environment(**kwargs)

    def buildBrowserEnv(self, options, profileDir):
        browserEnv = self.environment(
            xrePath=options.xrePath, debugger=options.debugger
        )
        browserEnv["XPCOM_DEBUG_BREAK"] = "stack"

        if mozinfo.info["asan"]:
            # Disable leak checking for reftests for now
            if "ASAN_OPTIONS" in browserEnv:
                browserEnv["ASAN_OPTIONS"] += ":detect_leaks=0"
            else:
                browserEnv["ASAN_OPTIONS"] = "detect_leaks=0"

        # Set environment defaults for jstestbrowser. Keep in sync with the
        # defaults used in js/src/tests/lib/tests.py.
        if options.suite == "jstestbrowser":
            browserEnv["TZ"] = "PST8PDT"
            browserEnv["LC_ALL"] = "en_US.UTF-8"

        for v in options.environment:
            ix = v.find("=")
            if ix <= 0:
                print("Error: syntax error in --setenv=" + v)
                return None
            browserEnv[v[:ix]] = v[ix + 1 :]

        # Enable leaks detection to its own log file.
        self.leakLogFile = os.path.join(profileDir, "runreftest_leaks.log")
        browserEnv["XPCOM_MEM_BLOAT_LOG"] = self.leakLogFile

        # TODO: this is always defined (as part of --enable-webrender which is default)
        #       can we make this default in the browser?
        browserEnv["MOZ_ACCELERATED"] = "1"

        if options.headless:
            browserEnv["MOZ_HEADLESS"] = "1"

        if not options.e10s:
            browserEnv["MOZ_FORCE_DISABLE_E10S"] = "1"

        return browserEnv

    def cleanup(self, profileDir):
        if profileDir:
            shutil.rmtree(profileDir, True)

    def verifyTests(self, tests, options):
        """
        Support --verify mode: Run test(s) many times in a variety of
        configurations/environments in an effort to find intermittent
        failures.
        """

        self._populate_logger(options)

        # Number of times to repeat test(s) when running with --repeat
        VERIFY_REPEAT = 10
        # Number of times to repeat test(s) when running test in separate browser
        VERIFY_REPEAT_SINGLE_BROWSER = 5

        def step1():
            options.repeat = VERIFY_REPEAT
            options.runUntilFailure = True
            result = self.runTests(tests, options)
            return result

        def step2():
            options.repeat = 0
            options.runUntilFailure = False
            for i in range(VERIFY_REPEAT_SINGLE_BROWSER):
                result = self.runTests(tests, options)
                if result != 0:
                    break
            return result

        def step3():
            options.repeat = VERIFY_REPEAT
            options.runUntilFailure = True
            options.environment.append("MOZ_CHAOSMODE=0xfb")
            result = self.runTests(tests, options)
            options.environment.remove("MOZ_CHAOSMODE=0xfb")
            return result

        def step4():
            options.repeat = 0
            options.runUntilFailure = False
            options.environment.append("MOZ_CHAOSMODE=0xfb")
            for i in range(VERIFY_REPEAT_SINGLE_BROWSER):
                result = self.runTests(tests, options)
                if result != 0:
                    break
            options.environment.remove("MOZ_CHAOSMODE=0xfb")
            return result

        steps = [
            ("1. Run each test %d times in one browser." % VERIFY_REPEAT, step1),
            (
                "2. Run each test %d times in a new browser each time."
                % VERIFY_REPEAT_SINGLE_BROWSER,
                step2,
            ),
            (
                "3. Run each test %d times in one browser, in chaos mode."
                % VERIFY_REPEAT,
                step3,
            ),
            (
                "4. Run each test %d times in a new browser each time, in chaos mode."
                % VERIFY_REPEAT_SINGLE_BROWSER,
                step4,
            ),
        ]

        stepResults = {}
        for descr, step in steps:
            stepResults[descr] = "not run / incomplete"

        startTime = datetime.now()
        maxTime = timedelta(seconds=options.verify_max_time)
        finalResult = "PASSED"
        for descr, step in steps:
            if (datetime.now() - startTime) > maxTime:
                self.log.info("::: Test verification is taking too long: Giving up!")
                self.log.info(
                    "::: So far, all checks passed, but not all checks were run."
                )
                break
            self.log.info(":::")
            self.log.info('::: Running test verification step "%s"...' % descr)
            self.log.info(":::")
            result = step()
            if result != 0:
                stepResults[descr] = "FAIL"
                finalResult = "FAILED!"
                break
            stepResults[descr] = "Pass"

        self.log.info(":::")
        self.log.info("::: Test verification summary for:")
        self.log.info(":::")
        for test in tests:
            self.log.info("::: " + test)
        self.log.info(":::")
        for descr in sorted(stepResults.keys()):
            self.log.info("::: %s : %s" % (descr, stepResults[descr]))
        self.log.info(":::")
        self.log.info("::: Test verification %s" % finalResult)
        self.log.info(":::")

        return result

    def runTests(self, tests, options, cmdargs=None):
        cmdargs = cmdargs or []
        self._populate_logger(options)
        self.outputHandler = OutputHandler(
            self.log, options.utilityPath, options.symbolsPath
        )

        if options.cleanupCrashes:
            mozcrash.cleanup_pending_crash_reports()

        manifests = self.resolver.resolveManifests(options, tests)
        if options.filter:
            manifests[""] = (options.filter, None)

        if not getattr(options, "runTestsInParallel", False):
            return self.runSerialTests(manifests, options, cmdargs)

        cpuCount = cpu_count()

        # We have the directive, technology, and machine to run multiple test instances.
        # Experimentation says that reftests are not overly CPU-intensive, so we can run
        # multiple jobs per CPU core.
        #
        # Our Windows machines in automation seem to get upset when we run a lot of
        # simultaneous tests on them, so tone things down there.
        if sys.platform == "win32":
            jobsWithoutFocus = cpuCount
        else:
            jobsWithoutFocus = 2 * cpuCount

        totalJobs = jobsWithoutFocus + 1
        perProcessArgs = [sys.argv[:] for i in range(0, totalJobs)]

        host = "localhost"

        # First job is only needs-focus tests.  Remaining jobs are
        # non-needs-focus and chunked.
        perProcessArgs[0].insert(-1, "--focus-filter-mode=needs-focus")
        for chunkNumber, jobArgs in enumerate(perProcessArgs[1:], start=1):
            port = _get_available_port(2828)
            jobArgs[-1:-1] = [
                "--focus-filter-mode=non-needs-focus",
                "--total-chunks=%d" % jobsWithoutFocus,
                "--this-chunk=%d" % chunkNumber,
                "--marionette=%s:%d" % (host, port),
            ]

        for jobArgs in perProcessArgs:
            try:
                jobArgs.remove("--run-tests-in-parallel")
            except Exception:
                pass
            jobArgs[0:0] = [sys.executable, "-u"]

        threads = [ReftestThread(args) for args in perProcessArgs[1:]]
        for t in threads:
            t.start()

        while True:
            # The test harness in each individual thread will be doing timeout
            # handling on its own, so we shouldn't need to worry about any of
            # the threads hanging for arbitrarily long.
            for t in threads:
                t.join(10)
            if not any(t.is_alive() for t in threads):
                break

        # Run the needs-focus tests serially after the other ones, so we don't
        # have to worry about races between the needs-focus tests *actually*
        # needing focus and the dummy windows in the non-needs-focus tests
        # trying to focus themselves.
        focusThread = ReftestThread(perProcessArgs[0])
        focusThread.start()
        focusThread.join()

        # Output the summaries that the ReftestThread filters suppressed.
        summaryObjects = [defaultdict(int) for s in summaryLines]
        for t in threads:
            for summaryObj, (text, categories) in zip(summaryObjects, summaryLines):
                threadMatches = t.summaryMatches[text]
                for attribute, description in categories:
                    amount = int(threadMatches.group(attribute) if threadMatches else 0)
                    summaryObj[attribute] += amount
                amount = int(threadMatches.group("total") if threadMatches else 0)
                summaryObj["total"] += amount

        print("REFTEST INFO | Result summary:")
        for summaryObj, (text, categories) in zip(summaryObjects, summaryLines):
            details = ", ".join([
                "%d %s" % (summaryObj[attribute], description)
                for (attribute, description) in categories
            ])
            print(
                "REFTEST INFO | "
                + text
                + ": "
                + str(summaryObj["total"])
                + " ("
                + details
                + ")"
            )

        return int(any(t.retcode != 0 for t in threads))

    def handleTimeout(self, timeout, proc, utilityPath, debuggerInfo):
        """handle process output timeout"""
        # TODO: bug 913975 : _processOutput should call self.processOutputLine
        # one more time one timeout (I think)
        self.log.error(
            "%s | application timed out after %d seconds with no output"
            % (self.lastTestSeen, int(timeout))
        )
        self.log.warning("Force-terminating active process(es).")
        self.killAndGetStack(
            proc, utilityPath, debuggerInfo, dump_screen=not debuggerInfo
        )

    def dumpScreen(self, utilityPath):
        if self.haveDumpedScreen:
            self.log.info(
                "Not taking screenshot here: see the one that was previously logged"
            )
            return
        self.haveDumpedScreen = True
        dump_screen(utilityPath, self.log)

    def killAndGetStack(self, process, utilityPath, debuggerInfo, dump_screen=False):
        """
        Kill the process, preferrably in a way that gets us a stack trace.
        Also attempts to obtain a screenshot before killing the process
        if specified.
        """

        if dump_screen:
            self.dumpScreen(utilityPath)

        if mozinfo.info.get("crashreporter", True) and not debuggerInfo:
            if mozinfo.isWin:
                # We should have a "crashinject" program in our utility path
                crashinject = os.path.normpath(
                    os.path.join(utilityPath, "crashinject.exe")
                )
                if os.path.exists(crashinject):
                    status = subprocess.Popen([crashinject, str(process.pid)]).wait()
                    printstatus("crashinject", status)
                    if status == 0:
                        return
            else:
                try:
                    process.kill(sig=signal.SIGABRT)
                except OSError:
                    # https://bugzilla.mozilla.org/show_bug.cgi?id=921509
                    self.log.info("Can't trigger Breakpad, process no longer exists")
                return
        self.log.info("Can't trigger Breakpad, just killing process")
        process.kill()

    def runApp(
        self,
        options,
        cmdargs=None,
        timeout=None,
        debuggerInfo=None,
        symbolsPath=None,
        valgrindPath=None,
        valgrindArgs=None,
        valgrindSuppFiles=None,
        checkLeaks=False,
        leakScope=None,
        **profileArgs,
    ):
        if cmdargs is None:
            cmdargs = []
        cmdargs = cmdargs[:]

        if self.use_marionette:
            cmdargs.append("-marionette")

        binary = options.app
        profile = self.createReftestProfile(options, **profileArgs)

        # browser environment
        env = self.buildBrowserEnv(options, profile.profile)

        def timeoutHandler():
            self.handleTimeout(timeout, proc, options.utilityPath, debuggerInfo)

        interactive = False
        debug_args = None
        if debuggerInfo:
            interactive = debuggerInfo.interactive
            debug_args = [debuggerInfo.path] + debuggerInfo.args

        def record_last_test(message):
            """Records the last test seen by this harness for crash logging and
            sanitizer reports."""

            def testid(test):
                if " " in test:
                    return test.split(" ")[0]
                return test

            if message["action"] == "test_start":
                self.lastTestSeen = testid(message["test"])
            elif message["action"] == "test_end":
                if self.lastTest and message["test"] == self.lastTest:
                    self.lastTestSeen = self.currentManifest
                else:
                    self.lastTestSeen = "{} (finished)".format(testid(message["test"]))
            self.outputHandler.lastTestSeen = self.lastTestSeen

        self.log.add_handler(record_last_test)

        kp_kwargs = {
            "kill_on_timeout": False,
            "cwd": SCRIPT_DIRECTORY,
            "onTimeout": [timeoutHandler],
            "processOutputLine": [self.outputHandler],
        }

        if mozinfo.isWin or mozinfo.isMac:
            # Prevents log interleaving on Windows at the expense of losing
            # true log order. See bug 798300 and bug 1324961 for more details.
            kp_kwargs["processStderrLine"] = [self.outputHandler]

        if interactive:
            # If an interactive debugger is attached,
            # don't use timeouts, and don't capture ctrl-c.
            timeout = None
            signal.signal(signal.SIGINT, lambda sigid, frame: None)

        runner_cls = mozrunner.runners.get(
            mozinfo.info.get("appname", "firefox"), mozrunner.Runner
        )
        runner = runner_cls(
            profile=profile,
            binary=binary,
            process_class=mozprocess.ProcessHandlerMixin,
            cmdargs=cmdargs,
            env=env,
            process_args=kp_kwargs,
        )
        runner.start(
            debug_args=debug_args, interactive=interactive, outputTimeout=timeout
        )
        proc = runner.process_handler
        self.outputHandler.proc_name = f"GECKO({proc.pid})"

        # Used to defer a possible IOError exception from Marionette
        marionette_exception = None

        if self.use_marionette:
            marionette_args = {
                "socket_timeout": options.marionette_socket_timeout,
                "startup_timeout": options.marionette_startup_timeout,
                "symbols_path": options.symbolsPath,
            }
            if options.marionette:
                host, port = options.marionette.split(":")
                marionette_args["host"] = host
                marionette_args["port"] = int(port)
            else:
                marionette_port = _get_available_port(preferred=2828)
                if marionette_port != 2828:
                    marionette_args["port"] = marionette_port
                    profile.set_preferences({"marionette.port": marionette_port})

            try:
                marionette = Marionette(**marionette_args)
                marionette.start_session()

                addons = Addons(marionette)
                if options.specialPowersExtensionPath:
                    addons.install(options.specialPowersExtensionPath, temp=True)

                addons.install(options.reftestExtensionPath, temp=True)

                marionette.delete_session()
            except OSError as e:
                # Any IOError as thrown by Marionette means that something is
                # wrong with the process, like a crash or the socket is no
                # longer open. We defer raising this specific error so that
                # post-test checks for leaks and crashes are performed and
                # reported first.
                marionette_exception = e

        status = runner.wait()
        runner.process_handler = None
        self.outputHandler.proc_name = None
        self.outputHandler.finish()

        crashed = mozcrash.log_crashes(
            self.log,
            os.path.join(profile.profile, "minidumps"),
            options.symbolsPath,
            test=self.lastTestSeen,
            group=self.currentManifestID,
        )

        if crashed:
            if not self.outputHandler.results:
                # TODO: while .results is a defaultdict(int), it is proxied via log_actions as data, not type
                self.outputHandler.results = {
                    "Pass": 0,
                    "LoadOnly": 0,
                    "Exception": 0,
                    "FailedLoad": 0,
                    "UnexpectedFail": 1,
                    "UnexpectedPass": 0,
                    "AssertionUnexpected": 0,
                    "AssertionUnexpectedFixed": 0,
                    "KnownFail": 0,
                    "AssertionKnown": 0,
                    "Random": 0,
                    "Skip": 0,
                    "Slow": 0,
                }
            # When running by manifest, runSerialTests logs suite_end after the
            # last manifest. Logging it here would end the suite early.
            if not self.run_by_manifest:
                # log suite_end to wrap up, this is usually done with in in-browser harness
                self.log.suite_end(extra={"results": self.outputHandler.results})

        if not status and crashed:
            status = 1

        if status and not crashed:
            msg = "application terminated with exit code %s" % (status)
            self.log.shutdown_failure(group=self.lastTestSeen, message=msg)

        # The leak logs live in the profile, so process them before removing it.
        # Do not process them when we crash/assert.
        if checkLeaks and status == 0:
            mozleak.process_leak_log(
                self.leakLogFile,
                leak_thresholds=options.leakThresholds,
                stack_fixer=get_stack_fixer_function(
                    options.utilityPath, options.symbolsPath
                ),
                scope=leakScope or options.suite,
            )

        runner.cleanup()
        self.cleanup(profile.profile)

        if marionette_exception is not None:
            raise marionette_exception

        self.log.info("Process mode: {}".format("e10s" if options.e10s else "non-e10s"))
        return status

    def getActiveTests(self, manifests, options, testDumpFile=None):
        # These prefs will cause reftest.sys.mjs to parse the manifests,
        # dump the resulting tests to a file, and exit.
        prefs = {
            "reftest.manifests": json.dumps(manifests),
            "reftest.manifests.dumpTests": testDumpFile or self.testDumpFile,
        }
        cmdargs = []
        self.runApp(options, cmdargs=cmdargs, prefs=prefs)

        if not os.path.isfile(self.testDumpFile):
            print("Error: parsing manifests failed!")
            sys.exit(1)

        with open(self.testDumpFile) as fh:
            tests = json.load(fh)

        if os.path.isfile(self.testDumpFile):
            mozfile.remove(self.testDumpFile)

        for test in tests:
            # Name and path are expected by manifestparser, but not used in reftest.
            test["name"] = test["path"] = test["url1"]

        mp = TestManifest(strict=False)
        mp.tests = tests

        filters = []
        if options.totalChunks:
            filters.append(
                mpf.chunk_by_manifest(options.thisChunk, options.totalChunks)
            )

        tests = mp.active_tests(exists=False, filters=filters)
        return tests

    def runSerialTests(self, manifests, options, cmdargs=None):
        debuggerInfo = None
        if options.debugger:
            debuggerInfo = mozdebug.get_debugger_info(
                options.debugger, options.debuggerArgs, options.debuggerInteractive
            )

        def run(**kwargs):
            if kwargs.get("tests"):
                self.lastTest = kwargs["tests"][-1]["identifier"]
                if not isinstance(self.lastTest, str):
                    self.lastTest = " ".join(self.lastTest)

            status = self.runApp(
                options,
                manifests=manifests,
                cmdargs=cmdargs,
                # We generally want the JS harness or marionette
                # to handle timeouts if they can.
                # The default JS harness timeout is currently
                # 300 seconds (default options.timeout).
                # The default Marionette socket timeout is
                # currently 360 seconds.
                # Give the JS harness extra time to deal with
                # its own timeouts and try to usually exceed
                # the 360 second marionette socket timeout.
                # See bug 479518 and bug 1414063.
                timeout=options.timeout + 70.0,
                debuggerInfo=debuggerInfo,
                symbolsPath=options.symbolsPath,
                checkLeaks=True,
                **kwargs,
            )
            return status

        if not self.run_by_manifest:
            return run()

        tests = self.getActiveTests(manifests, options)
        tests_by_manifest = defaultdict(list)
        ids_by_manifest = defaultdict(list)
        for t in tests:
            tests_by_manifest[t["manifest"]].append(t)
            test_id = t["identifier"]
            if not isinstance(test_id, str):
                test_id = " ".join(test_id)
            ids_by_manifest[t["manifestID"]].append(test_id)

        self.log.suite_start(ids_by_manifest, name=options.suite)

        overall = 0
        status = -1
        for manifest, tests in tests_by_manifest.items():
            if self.getGtkTheme() != self.gtkTheme:
                self.log.error(
                    "Theme (%s) has changed to (%s), terminating job as this is unstable"
                    % (self.gtkTheme, self.getGtkTheme())
                )
                return 1

            manifest_id = tests[0]["manifestID"]
            self.log.group_start(name=manifest_id)
            self.log.info(f"Running tests in {manifest}")
            self.currentManifest = manifest
            self.currentManifestID = manifest_id
            self.lastTestSeen = manifest
            self.outputHandler.lastTestSeen = self.lastTestSeen
            self.outputHandler.scope = manifest_id
            status = run(tests=tests, leakScope=manifest_id)
            overall = overall or status
            self.log.group_end(name=manifest_id)
        if status == -1:
            # we didn't run anything
            overall = 1

        self.log.suite_end(extra={"results": self.outputHandler.results})
        return overall

    def copyExtraFilesToProfile(self, options, profile):
        "Copy extra files or dirs specified on the command line to the testing profile."
        profileDir = profile.profile
        for f in options.extraProfileFiles:
            abspath = self.getFullPath(f)
            if os.path.isfile(abspath):
                if os.path.basename(abspath) == "user.js":
                    extra_prefs = mozprofile.Preferences.read_prefs(abspath)
                    profile.set_preferences(extra_prefs)
                elif os.path.basename(abspath).endswith(".dic"):
                    hyphDir = os.path.join(profileDir, "hyphenation")
                    if not os.path.exists(hyphDir):
                        os.makedirs(hyphDir)
                    shutil.copy2(abspath, hyphDir)
                else:
                    shutil.copy2(abspath, profileDir)
            elif os.path.isdir(abspath):
                dest = os.path.join(profileDir, os.path.basename(abspath))
                shutil.copytree(abspath, dest)
            else:
                self.log.warning(
                    "runreftest.py | Failed to copy %s to profile" % abspath
                )
                continue


def run_test_harness(parser, options):
    reftest = RefTest(options.suite)
    parser.validate(options, reftest)

    # We have to validate options.app here for the case when the mach
    # command is able to find it after argument parsing. This can happen
    # when running from a tests archive.
    if not options.app:
        parser.error("could not find the application path, --appname must be specified")

    options.app = reftest.getFullPath(options.app)
    if not os.path.exists(options.app):
        parser.error(
            "Error: Path %(app)s doesn't exist. Are you executing "
            "$objdir/_tests/reftest/runreftest.py?" % {"app": options.app}
        )

    if options.xrePath is None:
        options.xrePath = os.path.dirname(options.app)

    if options.verify:
        result = reftest.verifyTests(options.tests, options)
    else:
        result = reftest.runTests(options.tests, options)

    return result


if __name__ == "__main__":
    parser = reftestcommandline.DesktopArgumentsParser()
    options = parser.parse_args()
    sys.exit(run_test_harness(parser, options))
