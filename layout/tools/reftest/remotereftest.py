# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import os
import posixpath
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback
from contextlib import closing
from urllib.request import urlopen

import mozcrash
import reftestcommandline
from mozdevice import ADBDeviceFactory, RemoteProcessMonitor
from output import OutputHandler
from runreftest import RefTest, ReftestResolver, build_obj

# We need to know our current directory so that we can serve our test files from it.
SCRIPT_DIRECTORY = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))


class RemoteReftestResolver(ReftestResolver):
    def absManifestPath(self, path):
        script_abs_path = os.path.join(SCRIPT_DIRECTORY, path)
        if os.path.exists(script_abs_path):
            rv = script_abs_path
        elif os.path.exists(os.path.abspath(path)):
            rv = os.path.abspath(path)
        else:
            print("Could not find manifest %s" % script_abs_path, file=sys.stderr)
            sys.exit(1)
        return os.path.normpath(rv)

    def manifestURL(self, options, path):
        # Dynamically build the reftest URL if possible, beware that
        # args[0] should exist 'inside' webroot. It's possible for
        # this url to have a leading "..", but reftest.js will fix
        # that.  Use the httpdPath to determine if we are running in
        # production or locally.  If we are running the jsreftests
        # locally, strip text up to jsreftest.  We want the docroot of
        # the server to include a link jsreftest that points to the
        # test-stage location of the test files. The desktop oriented
        # setup has already created a link for tests which points
        # directly into the source tree. For the remote tests we need
        # a separate symbolic link to point to the staged test files.
        if "jsreftest" not in path or os.environ.get("MOZ_AUTOMATION"):
            relPath = os.path.relpath(path, SCRIPT_DIRECTORY)
        else:
            relPath = "jsreftest/" + path.split("jsreftest/")[-1]
        return "http://%s:%s/%s" % (options.remoteWebServer, options.httpPort, relPath)


class ReftestServer:
    """Web server used to serve Reftests, for closer fidelity to the real web.
    It is virtually identical to the server used in mochitest and will only
    be used for running reftests remotely.
    Bug 581257 has been filed to refactor this wrapper around httpd.js into
    it's own class and use it in both remote and non-remote testing."""

    def __init__(self, options, scriptDir, log):
        self.log = log
        self.utilityPath = options.utilityPath
        self.xrePath = options.xrePath
        self.profileDir = options.serverProfilePath
        self.webServer = options.remoteWebServer
        self.httpPort = options.httpPort
        self.scriptDir = scriptDir
        self.httpdPath = os.path.abspath(options.httpdPath)
        if options.remoteWebServer == "10.0.2.2":
            # probably running an Android emulator and 10.0.2.2 will
            # not be visible from host
            shutdownServer = "127.0.0.1"
        else:
            shutdownServer = self.webServer
        self.shutdownURL = "http://%(server)s:%(port)s/server/shutdown" % {
            "server": shutdownServer,
            "port": self.httpPort,
        }

    def start(self):
        "Run the Refest server, returning the process ID of the server."

        env = dict(os.environ)
        env["XPCOM_DEBUG_BREAK"] = "warn"
        bin_suffix = ""
        if sys.platform in ("win32", "msys", "cygwin"):
            env["PATH"] = env["PATH"] + ";" + self.xrePath
            bin_suffix = ".exe"
        elif "LD_LIBRARY_PATH" not in env or env["LD_LIBRARY_PATH"] is None:
            env["LD_LIBRARY_PATH"] = self.xrePath
        else:
            env["LD_LIBRARY_PATH"] = ":".join([self.xrePath, env["LD_LIBRARY_PATH"]])

        args = [
            "-g",
            self.xrePath,
            "-e",
            "const _PROFILE_PATH = '%(profile)s';const _SERVER_PORT = "
            "'%(port)s'; const _SERVER_ADDR ='%(server)s'; "
            "const _HTTPD_PATH = '%(httpdPath)s';"
            % {
                "httpdPath": self.httpdPath.replace("\\", "\\\\"),
                "profile": self.profileDir.replace("\\", "\\\\"),
                "port": self.httpPort,
                "server": self.webServer,
            },
            "-f",
            os.path.join(self.scriptDir, "server.js"),
        ]

        xpcshell = os.path.join(self.utilityPath, "xpcshell" + bin_suffix)

        if not os.access(xpcshell, os.F_OK):
            raise Exception("xpcshell not found at %s" % xpcshell)
        if RemoteProcessMonitor.elf_arm(xpcshell):
            raise Exception(
                "xpcshell at %s is an ARM binary; please use "
                "the --utility-path argument to specify the path "
                "to a desktop version." % xpcshell
            )

        self._process = subprocess.Popen([xpcshell] + args, env=env)
        pid = self._process.pid
        if pid < 0:
            self.log.error(
                "TEST-UNEXPECTED-FAIL | remotereftests.py | Error starting server."
            )
            return 2
        self.log.info("INFO | remotereftests.py | Server pid: %d" % pid)

    def ensureReady(self, timeout):
        assert timeout >= 0

        aliveFile = os.path.join(self.profileDir, "server_alive.txt")
        i = 0
        while i < timeout:
            if os.path.exists(aliveFile):
                break
            time.sleep(1)
            i += 1
        else:
            self.log.error(
                "TEST-UNEXPECTED-FAIL | remotereftests.py | "
                "Timed out while waiting for server startup."
            )
            self.stop()
            return 1

    def stop(self):
        if hasattr(self, "_process"):
            try:
                with closing(urlopen(self.shutdownURL)) as c:
                    c.read()

                rtncode = self._process.poll()
                if rtncode is None:
                    self._process.terminate()
            except Exception:
                self.log.info("Failed to shutdown server at %s" % self.shutdownURL)
                traceback.print_exc()
                self._process.kill()


class RemoteReftest(RefTest):
    use_marionette = False
    resolver_cls = RemoteReftestResolver

    def __init__(self, options, scriptDir):
        RefTest.__init__(self, options.suite)
        self.run_by_manifest = False
        self.scriptDir = scriptDir
        self.localLogName = options.localLogName

        verbose = False
        if (
            options.log_mach_verbose
            or options.log_tbpl_level == "debug"
            or options.log_mach_level == "debug"
            or options.log_raw_level == "debug"
        ):
            verbose = True
            print("set verbose!")
        expected = options.app.split("/")[-1]
        self.device = ADBDeviceFactory(
            adb=options.adb_path or "adb",
            device=options.deviceSerial,
            test_root=options.remoteTestRoot,
            verbose=verbose,
            run_as_package=expected,
        )
        if options.remoteTestRoot is None:
            options.remoteTestRoot = posixpath.join(self.device.test_root, "reftest")
        options.remoteProfile = posixpath.join(options.remoteTestRoot, "profile")
        options.remoteLogFile = posixpath.join(options.remoteTestRoot, "reftest.log")
        options.logFile = options.remoteLogFile
        self.remoteProfile = options.remoteProfile
        self.remoteTestRoot = options.remoteTestRoot
        self.isolatedProcess = options.isolated_process

        if not options.ignoreWindowSize:
            parts = self.device.get_info("screen")["screen"][0].split()
            width = int(parts[0].split(":")[1])
            height = int(parts[1].split(":")[1])
            if width < 1366 or height < 1050:
                self.error(
                    "ERROR: Invalid screen resolution %sx%s, "
                    "please adjust to 1366x1050 or higher" % (width, height)
                )

        self._populate_logger(options)
        self.outputHandler = OutputHandler(
            self.log, options.utilityPath, options.symbolsPath
        )

        self.SERVER_STARTUP_TIMEOUT = 90

        self.remoteCache = os.path.join(options.remoteTestRoot, "cache/")

        # Check that Firefox is installed
        expected = options.app.split("/")[-1]
        if not self.device.is_app_installed(expected):
            raise Exception("%s is not installed on this device" % expected)
        self.device.run_as_package = expected
        self.device.clear_logcat()

        self.device.rm(self.remoteCache, force=True, recursive=True)

        procName = options.app.split("/")[-1]
        self.device.stop_application(procName)
        if self.device.process_exist(procName):
            self.log.error("unable to kill %s before starting tests!" % procName)

    def findPath(self, paths, filename=None):
        for path in paths:
            p = path
            if filename:
                p = os.path.join(p, filename)
            if os.path.exists(self.getFullPath(p)):
                return path
        return None

    def startWebServer(self, options):
        """Create the webserver on the host and start it up"""
        remoteXrePath = options.xrePath
        remoteUtilityPath = options.utilityPath

        paths = [options.xrePath]
        if build_obj:
            paths.append(os.path.join(build_obj.topobjdir, "dist", "bin"))
        options.xrePath = self.findPath(paths)
        if options.xrePath is None:
            print(
                "ERROR: unable to find xulrunner path for %s, "
                "please specify with --xre-path" % (os.name)
            )
            return 1
        paths.append("bin")
        paths.append(os.path.join("..", "bin"))

        xpcshell = "xpcshell"
        if os.name == "nt":
            xpcshell += ".exe"

        if options.utilityPath:
            paths.insert(0, options.utilityPath)
        options.utilityPath = self.findPath(paths, xpcshell)
        if options.utilityPath is None:
            print(
                "ERROR: unable to find utility path for %s, "
                "please specify with --utility-path" % (os.name)
            )
            return 1

        options.serverProfilePath = tempfile.mkdtemp()
        self.server = ReftestServer(options, self.scriptDir, self.log)
        retVal = self.server.start()
        if retVal:
            return retVal
        retVal = self.server.ensureReady(self.SERVER_STARTUP_TIMEOUT)
        if retVal:
            return retVal

        options.xrePath = remoteXrePath
        options.utilityPath = remoteUtilityPath
        return 0

    def stopWebServer(self, options):
        self.server.stop()

    def killNamedProc(self, pname, orphans=True):
        """Kill processes matching the given command name"""
        try:
            import psutil
        except ImportError as e:
            self.log.warning("Unable to import psutil: %s" % str(e))
            self.log.warning("Unable to verify that %s is not already running." % pname)
            return

        self.log.info("Checking for %s processes..." % pname)

        for proc in psutil.process_iter():
            try:
                if proc.name() == pname:
                    procd = proc.as_dict(attrs=["pid", "ppid", "name", "username"])
                    if proc.ppid() == 1 or not orphans:
                        self.log.info("killing %s" % procd)
                        try:
                            os.kill(
                                proc.pid, getattr(signal, "SIGKILL", signal.SIGTERM)
                            )
                        except Exception as e:
                            self.log.info(
                                "Failed to kill process %d: %s" % (proc.pid, str(e))
                            )
                    else:
                        self.log.info("NOT killing %s (not an orphan?)" % procd)
            except Exception:
                # may not be able to access process info for all processes
                continue

    def createReftestProfile(self, options, **kwargs):
        profile = RefTest.createReftestProfile(
            self,
            options,
            server=options.remoteWebServer,
            port=options.httpPort,
            **kwargs,
        )
        profileDir = profile.profile
        prefs = {}
        prefs["reftest.remote"] = True
        prefs["datareporting.policy.dataSubmissionPolicyBypassAcceptance"] = True
        # move necko cache to a location that can be cleaned up
        prefs["browser.cache.disk.parent_directory"] = self.remoteCache

        prefs["layout.css.devPixelsPerPx"] = "1.0"
        # Because Fennec is a little wacky (see bug 1156817) we need to load the
        # reftest pages at 1.0 zoom, rather than zooming to fit the CSS viewport.
        prefs["apz.allow_zooming"] = False

        # Set the extra prefs.
        profile.set_preferences(prefs)

        try:
            self.device.push(profileDir, options.remoteProfile)
            # make sure the parent directories of the profile which
            # may have been created by the push, also have their
            # permissions set to allow access.
            self.device.chmod(options.remoteTestRoot, recursive=True)
        except Exception:
            print("Automation Error: Failed to copy profiledir to device")
            raise

        return profile

    def environment(self, env=None, crashreporter=True, **kwargs):
        # Since running remote, do not mimic the local env: do not copy os.environ
        if env is None:
            env = {}

        if crashreporter:
            env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
            env["MOZ_CRASHREPORTER"] = "1"
            env["MOZ_CRASHREPORTER_SHUTDOWN"] = "1"
        else:
            env["MOZ_CRASHREPORTER_DISABLE"] = "1"

        # Crash on non-local network connections by default.
        # MOZ_DISABLE_NONLOCAL_CONNECTIONS can be set to "0" to temporarily
        # enable non-local connections for the purposes of local testing.
        # Don't override the user's choice here.  See bug 1049688.
        env.setdefault("MOZ_DISABLE_NONLOCAL_CONNECTIONS", "1")

        # Send an env var noting that we are in automation. Passing any
        # value except the empty string will declare the value to exist.
        #
        # This may be used to disabled network connections during testing, e.g.
        # Switchboard & telemetry uploads.
        env.setdefault("MOZ_IN_AUTOMATION", "1")

        # Set WebRTC logging in case it is not set yet.
        env.setdefault("R_LOG_LEVEL", "6")
        env.setdefault("R_LOG_DESTINATION", "stderr")
        env.setdefault("R_LOG_VERBOSE", "1")

        if self.isolatedProcess:
            env["MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_PROCESS"] = "1"

        return env

    def buildBrowserEnv(self, options, profileDir):
        browserEnv = RefTest.buildBrowserEnv(self, options, profileDir)
        # remove desktop environment not used on device
        if "XPCOM_MEM_BLOAT_LOG" in browserEnv:
            del browserEnv["XPCOM_MEM_BLOAT_LOG"]
        return browserEnv

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
        **profileArgs,
    ):
        if cmdargs is None:
            cmdargs = []

        if self.use_marionette:
            cmdargs.append("-marionette")

        binary = options.app
        profile = self.createReftestProfile(options, **profileArgs)

        # browser environment
        env = self.buildBrowserEnv(options, profile.profile)

        rpm = RemoteProcessMonitor(
            binary,
            self.device,
            self.log,
            self.outputHandler,
            options.remoteLogFile,
            self.remoteProfile,
        )
        startTime = datetime.datetime.now()
        status = 0
        profileDirectory = self.remoteProfile + "/"
        cmdargs.extend(("-profile", profileDirectory))

        pid = rpm.launch(
            binary,
            debuggerInfo,
            None,
            cmdargs,
            env=env,
            e10s=options.e10s,
        )
        self.log.info("remotereftest.py | Application pid: %d" % pid)
        if not rpm.wait(timeout):
            status = 1
        self.log.info(
            "remotereftest.py | Application ran for: %s"
            % str(datetime.datetime.now() - startTime)
        )
        crashed = self.check_for_crashes(symbolsPath, rpm.last_test_seen)
        if crashed:
            status = 1

        self.cleanup(profile.profile)
        return status

    def check_for_crashes(self, symbols_path, last_test_seen):
        """
        Pull any minidumps from remote profile and log any associated crashes.
        """
        try:
            dump_dir = tempfile.mkdtemp()
            remote_crash_dir = posixpath.join(self.remoteProfile, "minidumps")
            if not self.device.is_dir(remote_crash_dir):
                return False
            self.device.pull(remote_crash_dir, dump_dir)
            crashed = mozcrash.log_crashes(
                self.log, dump_dir, symbols_path, test=last_test_seen
            )
        finally:
            try:
                shutil.rmtree(dump_dir)
            except Exception as e:
                self.log.warning(
                    "unable to remove directory %s: %s" % (dump_dir, str(e))
                )
        return crashed

    def cleanup(self, profileDir):
        self.device.rm(self.remoteTestRoot, force=True, recursive=True)
        self.device.rm(self.remoteProfile, force=True, recursive=True)
        self.device.rm(self.remoteCache, force=True, recursive=True)
        RefTest.cleanup(self, profileDir)


def run_test_harness(parser, options):
    reftest = RemoteReftest(options, SCRIPT_DIRECTORY)
    parser.validate_remote(options)
    parser.validate(options, reftest)

    # Hack in a symbolic link for jsreftest in the SCRIPT_DIRECTORY
    # which is the document root for the reftest web server. This
    # allows a separate redirection for the jsreftests which must
    # run through the web server using the staged tests files and
    # the desktop which will use the tests symbolic link to find
    # the JavaScript tests.
    jsreftest_target = str(os.path.join(SCRIPT_DIRECTORY, "jsreftest"))
    if os.environ.get("MOZ_AUTOMATION"):
        os.system("ln -s ../jsreftest " + jsreftest_target)
    else:
        jsreftest_source = os.path.join(
            build_obj.topobjdir, "dist", "test-stage", "jsreftest"
        )
        if not os.path.islink(jsreftest_target):
            os.symlink(jsreftest_source, jsreftest_target)

    # Despite our efforts to clean up servers started by this script, in practice
    # we still see infrequent cases where a process is orphaned and interferes
    # with future tests, typically because the old server is keeping the port in use.
    # Try to avoid those failures by checking for and killing servers before
    # trying to start new ones.
    reftest.killNamedProc("ssltunnel")
    reftest.killNamedProc("xpcshell")

    # Start the webserver
    retVal = reftest.startWebServer(options)
    if retVal:
        return retVal

    retVal = 0
    try:
        if options.verify:
            retVal = reftest.verifyTests(options.tests, options)
        else:
            retVal = reftest.runTests(options.tests, options)
    except Exception:
        print("Automation Error: Exception caught while running tests")
        traceback.print_exc()
        retVal = 1

    reftest.stopWebServer(options)

    return retVal


if __name__ == "__main__":
    parser = reftestcommandline.RemoteArgumentsParser()
    options = parser.parse_args()
    sys.exit(run_test_harness(parser, options))
