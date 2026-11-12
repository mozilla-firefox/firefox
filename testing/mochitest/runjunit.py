# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import os
import posixpath
import re
import shutil
import sys
import tempfile
import traceback

import mozcrash
import mozinfo
import mozlog
import moznetwork
from mozdevice import ADBDeviceFactory, ADBError, ADBTimeoutError
from mozprofile import DEFAULT_PORTS, Profile
from mozprofile.cli import parse_preferences
from mozprofile.permissions import ServerLocations
from runtests import MochitestDesktop, update_mozinfo

here = os.path.abspath(os.path.dirname(__file__))

try:
    from mach.util import UserError
    from mozbuild.base import MachCommandConditions as conditions
    from mozbuild.base import MozbuildObject

    build_obj = MozbuildObject.from_environment(cwd=here)
except ImportError:
    build_obj = None
    conditions = None
    UserError = Exception


class JavaTestHarnessException(Exception):
    pass


class JUnitTestRunner(MochitestDesktop):
    """
    A test harness to run geckoview junit tests on a remote device.
    """

    def __init__(self, log, options):
        self.log = log
        self.verbose = False
        self.http3Server = None
        self.http2Server = None
        self.mozHttp2Server = None
        self.dohServer = None
        if (
            options.log_tbpl_level == "debug"
            or options.log_mach_level == "debug"
            or options.verbose
        ):
            self.verbose = True
        self.device = ADBDeviceFactory(
            adb=options.adbPath or "adb",
            device=options.deviceSerial,
            test_root=options.remoteTestRoot,
            verbose=self.verbose,
            run_as_package=options.app,
        )
        self.options = options
        self.log.debug("options=%s" % vars(options))
        update_mozinfo()
        self.remote_profile = posixpath.join(self.device.test_root, "junit-profile")
        self.remote_filter_list = posixpath.join(
            self.device.test_root, "junit-filters.list"
        )

        if self.options.coverage and not self.options.coverage_output_dir:
            raise UserError(
                "--coverage-output-dir is required when using --enable-coverage"
            )
        if self.options.coverage:
            self.remote_coverage_output_file = posixpath.join(
                self.device.test_root, "junit-coverage.ec"
            )
            self.coverage_output_file = os.path.join(
                self.options.coverage_output_dir, "junit-coverage.ec"
            )

        self.server_init()

        self.cleanup()
        self.device.clear_logcat()
        self.build_profile()
        if self.startServers(self.options, debuggerInfo=None, public=True) is False:
            raise RuntimeError(
                "Failed to start servers: a required port is already in use"
            )
        self.log.debug("Servers started")

    def collectLogcatForCurrentTest(self):
        # These are unique start and end markers logged by GeckoSessionTestRule.java
        START_MARKER = "1f0befec-3ff2-40ff-89cf-b127eb38b1ec"
        END_MARKER = "c5ee677f-bc83-49bd-9e28-2d35f3d0f059"
        logcat = self.device.get_logcat()
        test_logcat = ""
        started = False
        for l in logcat:
            if START_MARKER in l and self.test_name in l:
                started = True
            if started:
                test_logcat += l + "\n"
            if started and END_MARKER in l:
                return test_logcat

    def needsWebsocketProcessBridge(self, options):
        """
        Overrides MochitestDesktop.needsWebsocketProcessBridge and always
        returns False as the junit tests do not use the websocket process
        bridge. This is needed to satisfy MochitestDesktop.startServers.
        """
        return False

    def needsParakeetModelServer(self, options):
        """
        Overrides MochitestDesktop.needsParakeetModelServer and always returns
        False as the junit tests do not use the parakeet model server. This is
        needed to satisfy MochitestDesktop.startServers.
        """
        return False

    def server_init(self):
        """
        Additional initialization required to satisfy MochitestDesktop.startServers
        """
        self._locations = None
        self.server = None
        self.wsserver = None
        self.websocketProcessBridge = None
        self.parakeetModelServer = None
        self.SERVER_STARTUP_TIMEOUT = 180 if mozinfo.info.get("debug") else 90
        if self.options.remoteWebServer is None:
            self.options.remoteWebServer = moznetwork.get_ip()
        self.options.webServer = self.options.remoteWebServer
        self.options.webSocketPort = "9988"
        self.options.httpdPath = None
        self.options.http3ServerPath = None
        self.options.http2ServerPath = None
        self.options.keep_open = False
        self.options.pidFile = ""
        self.options.subsuite = None
        self.options.xrePath = None
        self.options.useHttp3Server = False
        self.options.useHttp2Server = False
        if build_obj and "MOZ_HOST_BIN" in os.environ:
            self.options.xrePath = os.environ["MOZ_HOST_BIN"]
            if not self.options.utilityPath:
                self.options.utilityPath = self.options.xrePath
        if not self.options.xrePath:
            self.options.xrePath = self.options.utilityPath
        if build_obj:
            self.options.certPath = os.path.join(
                build_obj.topsrcdir, "build", "pgo", "certs"
            )

    def build_profile(self):
        """
        Create a local profile with test prefs and proxy definitions and
        push it to the remote device.
        """

        self.profile = Profile(locations=self.locations, proxy=self.proxy(self.options))
        self.options.profilePath = self.profile.profile

        # Set preferences
        self.merge_base_profiles(self.options, "geckoview-junit")

        if self.options.web_content_isolation_strategy is not None:
            self.options.extraPrefs.append(
                "fission.webContentIsolationStrategy=%s"
                % self.options.web_content_isolation_strategy
            )
        self.options.extraPrefs.append("fission.autostart=true")
        if self.options.disable_fission:
            self.options.extraPrefs.pop()
            self.options.extraPrefs.append("fission.autostart=false")
        prefs = parse_preferences(self.options.extraPrefs)
        self.profile.set_preferences(prefs)

        if self.fillCertificateDB(self.options):
            self.log.error("Certificate integration failed")

        self.device.push(self.profile.profile, self.remote_profile)
        self.log.debug(
            "profile %s -> %s" % (str(self.profile.profile), str(self.remote_profile))
        )

    def cleanup(self):
        try:
            self.stopServers()
            self.log.debug("Servers stopped")
            self.device.stop_application(self.options.app)
            self.device.rm(self.remote_profile, force=True, recursive=True)
            if hasattr(self, "profile"):
                del self.profile
            self.device.rm(self.remote_filter_list, force=True)
        except Exception:
            traceback.print_exc()
            self.log.info("Caught and ignored an exception during cleanup")

    def build_command_line(self, test_filters_file, test_filters):
        """
        Construct and return the 'am instrument' command line.
        """
        cmd = "am instrument -w -r"
        # profile location
        cmd = cmd + " -e args '-profile %s'" % self.remote_profile
        # chunks (shards)
        shards = self.options.totalChunks
        shard = self.options.thisChunk
        if shards is not None and shard is not None:
            shard -= 1  # shard index is 0 based
            cmd = cmd + " -e numShards %d -e shardIndex %d" % (shards, shard)

        # test filters: limit run to specific test(s)
        # filter can be class-name or 'class-name#method-name' (single test)
        # Multiple filters must be specified as a line-separated text file
        # and then pushed to the device.
        filter_list_name = None

        if test_filters_file:
            # We specified a pre-existing file, so use that
            filter_list_name = test_filters_file
        elif test_filters:
            if len(test_filters) > 1:
                # Generate the list file from test_filters
                with tempfile.NamedTemporaryFile(delete=False, mode="w") as filter_list:
                    for f in test_filters:
                        print(f, file=filter_list)
                    filter_list_name = filter_list.name
            else:
                # A single filter may be directly appended to the command line
                cmd = cmd + " -e class %s" % test_filters[0]

        if filter_list_name:
            self.device.push(filter_list_name, self.remote_filter_list)

            if test_filters:
                # We only remove the filter list if we generated it as a
                # temporary file.
                os.remove(filter_list_name)

            cmd = cmd + " -e testFile %s" % self.remote_filter_list

        # enable code coverage reports
        if self.options.coverage:
            cmd = cmd + " -e coverage true"
            cmd = cmd + " -e coverageFile %s" % self.remote_coverage_output_file
        # environment
        env = {}
        env["MOZ_CRASHREPORTER"] = "1"
        env["MOZ_CRASHREPORTER_SHUTDOWN"] = "1"
        env["XPCOM_DEBUG_BREAK"] = "stack"
        env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "1"
        env["MOZ_IN_AUTOMATION"] = "1"
        env["R_LOG_VERBOSE"] = "1"
        env["R_LOG_LEVEL"] = "6"
        env["R_LOG_DESTINATION"] = "stderr"
        # webrender needs gfx.webrender.all=true, gtest doesn't use prefs
        env["MOZ_WEBRENDER"] = "1"
        # FIXME: When android switches to using Fission by default,
        # MOZ_FORCE_DISABLE_FISSION will need to be configured correctly.
        if self.options.disable_fission:
            env["MOZ_FORCE_DISABLE_FISSION"] = "1"
        else:
            env["MOZ_FORCE_ENABLE_FISSION"] = "1"
        if self.options.enable_isolated_zygote_process:
            env["MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_WITH_ZYGOTE"] = "1"
        else:
            env["MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_WITH_ZYGOTE"] = "0"
        if self.options.isolated_process:
            env["MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_PROCESS"] = "1"

        # Add additional env variables
        for [key, value] in [p.split("=", 1) for p in self.options.add_env]:
            env[key] = value

        env.update(self.device.target_ccov_env)

        for env_count, (env_key, env_val) in enumerate(env.items()):
            cmd = cmd + " -e env%d %s=%s" % (env_count, env_key, env_val)
        # runner
        cmd = cmd + " %s/%s" % (self.options.app, self.options.runner)
        return cmd

    @property
    def locations(self):
        if self._locations is not None:
            return self._locations
        locations_file = os.path.join(here, "server-locations.txt")
        self._locations = ServerLocations(locations_file)
        return self._locations

    def need_more_runs(self):
        if self.options.run_until_failure and (self.fail_count == 0):
            return True
        if self.runs <= self.options.repeat:
            return True
        return False

    def run_tests(self, test_filters_file=None, test_filters=None):
        """
        Run the tests.
        """
        if not self.device.is_app_installed(self.options.app):
            raise UserError("%s is not installed" % self.options.app)
        if self.device.process_exist(self.options.app):
            raise UserError(
                "%s already running before starting tests" % self.options.app
            )
        # test_filters_file and test_filters must be mutually-exclusive
        if test_filters_file and test_filters:
            raise UserError(
                "Test filters may not be specified when test-filters-file is provided"
            )

        self.test_started = False
        self.pass_count = 0
        self.fail_count = 0
        self.todo_count = 0
        self.total_count = 0
        self.runs = 0
        self.seen_last_test = False

        def callback(line):
            # Output callback: Parse the raw junit log messages, translating into
            # treeherder-friendly test start/pass/fail messages.

            line = line.decode("utf-8")
            self.log.process_output(self.options.app, line)
            # Expect per-test info like: "INSTRUMENTATION_STATUS: class=something"
            match = re.match(r"INSTRUMENTATION_STATUS:\s*class=(.*)", line)
            if match:
                self.class_name = match.group(1)
            # Expect per-test info like: "INSTRUMENTATION_STATUS: test=something"
            match = re.match(r"INSTRUMENTATION_STATUS:\s*test=(.*)", line)
            if match:
                self.test_name = match.group(1)
            match = re.match(r"INSTRUMENTATION_STATUS:\s*numtests=(.*)", line)
            if match:
                self.total_count = int(match.group(1))
            match = re.match(r"INSTRUMENTATION_STATUS:\s*current=(.*)", line)
            if match:
                self.current_test_id = int(match.group(1))
            match = re.match(r"INSTRUMENTATION_STATUS:\s*stack=(.*)", line)
            if match:
                self.exception_message = match.group(1)
            if (
                "org.mozilla.geckoview.test.rule.TestHarnessException"
                in self.exception_message
            ):
                # This is actually a problem in the test harness itself
                raise JavaTestHarnessException(self.exception_message)

            # Expect per-test info like: "INSTRUMENTATION_STATUS_CODE: 0|1|..."
            match = re.match(r"INSTRUMENTATION_STATUS_CODE:\s*([+-]?\d+)", line)
            if match:
                status = match.group(1)
                full_name = "%s#%s" % (self.class_name, self.test_name)
                if full_name == self.current_full_name:
                    # A crash in the test harness might cause us to ignore tests,
                    # so we double check that we've actually ran all the tests
                    if self.total_count == self.current_test_id:
                        self.seen_last_test = True

                    if status == "0":
                        message = ""
                        status = "PASS"
                        expected = "PASS"
                        self.pass_count += 1
                        if self.verbose:
                            self.log.info("Printing logcat for test:")
                            print(self.collectLogcatForCurrentTest())
                    elif status == "-3":  # ignored (skipped)
                        message = ""
                        status = "SKIP"
                        expected = "SKIP"
                        self.todo_count += 1
                    elif status == "-4":  # known fail
                        message = ""
                        status = "FAIL"
                        expected = "FAIL"
                        self.todo_count += 1
                    else:
                        if self.exception_message:
                            message = self.exception_message
                        else:
                            message = "status %s" % status
                        status = "FAIL"
                        expected = "PASS"
                        self.fail_count += 1
                        self.log.info("Printing logcat for test:")
                        print(self.collectLogcatForCurrentTest())
                    self.log.test_end(full_name, status, expected, message)
                    self.test_started = False
                else:
                    if self.test_started:
                        # next test started without reporting previous status
                        self.fail_count += 1
                        status = "FAIL"
                        expected = "PASS"
                        self.log.test_end(
                            self.current_full_name,
                            status,
                            expected,
                            "missing test completion status",
                        )
                    self.log.test_start(full_name)
                    self.test_started = True
                    self.current_full_name = full_name

        # Ideally all test names should be reported to suite_start, but these test
        # names are not known in advance.
        self.log.suite_start(["geckoview-junit"])
        try:
            self.device.grant_runtime_permissions(self.options.app)
            self.device.add_change_device_settings(self.options.app)
            self.device.add_mock_location(self.options.app)
            cmd = self.build_command_line(
                test_filters_file=test_filters_file, test_filters=test_filters
            )
            while self.need_more_runs():
                self.class_name = ""
                self.exception_message = ""
                self.test_name = ""
                self.current_full_name = ""
                self.current_test_id = 0
                self.runs += 1
                self.log.info("launching %s" % cmd)
                p = self.device.shell(
                    cmd, timeout=self.options.max_time, stdout_callback=callback
                )
                if p.timedout:
                    self.log.error(
                        "TEST-UNEXPECTED-TIMEOUT | runjunit.py | "
                        "Timed out after %d seconds" % self.options.max_time
                    )
            self.log.info("Passed: %d" % self.pass_count)
            self.log.info("Failed: %d" % self.fail_count)
            self.log.info("Todo: %d" % self.todo_count)
            if not self.seen_last_test:
                self.log.error(
                    "TEST-UNEXPECTED-FAIL | runjunit.py | "
                    "Some tests did not run (probably due to a crash in the harness)"
                )
        finally:
            self.log.suite_end()

        if self.check_for_crashes():
            self.fail_count = 1

        if self.options.coverage:
            try:
                self.device.pull(
                    self.remote_coverage_output_file, self.coverage_output_file
                )
            except ADBError:
                # Avoid a task retry in case the code coverage file is not found.
                self.log.error(
                    "No code coverage file (%s) found on remote device"
                    % self.remote_coverage_output_file
                )
                return -1

        return 1 if self.fail_count else 0

    def check_for_crashes(self):
        symbols_path = self.options.symbolsPath
        try:
            dump_dir = tempfile.mkdtemp()
            remote_dir = posixpath.join(self.remote_profile, "minidumps")
            if not self.device.is_dir(remote_dir):
                return False
            self.device.pull(remote_dir, dump_dir)
            crashed = mozcrash.log_crashes(
                self.log, dump_dir, symbols_path, test=self.current_full_name
            )
        finally:
            try:
                shutil.rmtree(dump_dir)
            except Exception:
                self.log.warning("unable to remove directory: %s" % dump_dir)
        return crashed


class JunitArgumentParser(argparse.ArgumentParser):
    """
    An argument parser for geckoview-junit.
    """

    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        self.add_argument(
            "--appname",
            action="store",
            type=str,
            dest="app",
            default="org.mozilla.geckoview.test",
            help="Test package name.",
        )
        self.add_argument(
            "--adbpath",
            action="store",
            type=str,
            dest="adbPath",
            default=None,
            help="Path to adb binary.",
        )
        self.add_argument(
            "--deviceSerial",
            action="store",
            type=str,
            dest="deviceSerial",
            help="adb serial number of remote device. This is required "
            "when more than one device is connected to the host. "
            "Use 'adb devices' to see connected devices. ",
        )
        self.add_argument(
            "--setenv",
            dest="add_env",
            action="append",
            default=[],
            help="Set target environment variable, like FOO=BAR",
        )
        self.add_argument(
            "--remoteTestRoot",
            action="store",
            type=str,
            dest="remoteTestRoot",
            help="Remote directory to use as test root "
            "(eg. /data/local/tmp/test_root).",
        )
        self.add_argument(
            "--max-time",
            action="store",
            type=int,
            dest="max_time",
            default="3000",
            help="Max time in seconds to wait for tests (default 3000s).",
        )
        self.add_argument(
            "--runner",
            action="store",
            type=str,
            dest="runner",
            default="androidx.test.runner.AndroidJUnitRunner",
            help="Test runner name.",
        )
        self.add_argument(
            "--symbols-path",
            action="store",
            type=str,
            dest="symbolsPath",
            default=None,
            help="Path to directory containing breakpad symbols, "
            "or the URL of a zip file containing symbols.",
        )
        self.add_argument(
            "--utility-path",
            action="store",
            type=str,
            dest="utilityPath",
            default=None,
            help="Path to directory containing host utility programs.",
        )
        self.add_argument(
            "--total-chunks",
            action="store",
            type=int,
            dest="totalChunks",
            default=None,
            help="Total number of chunks to split tests into.",
        )
        self.add_argument(
            "--this-chunk",
            action="store",
            type=int,
            dest="thisChunk",
            default=None,
            help="If running tests by chunks, the chunk number to run.",
        )
        self.add_argument(
            "--verbose",
            "-v",
            action="store_true",
            dest="verbose",
            default=False,
            help="Verbose output - enable debug log messages",
        )
        self.add_argument(
            "--enable-coverage",
            action="store_true",
            dest="coverage",
            default=False,
            help="Enable code coverage collection.",
        )
        self.add_argument(
            "--coverage-output-dir",
            action="store",
            type=str,
            dest="coverage_output_dir",
            default=None,
            help="If collecting code coverage, save the report file in this dir.",
        )
        self.add_argument(
            "--disable-fission",
            action="store_true",
            dest="disable_fission",
            default=False,
            help="Run the tests without Fission (site isolation) enabled.",
        )
        self.add_argument(
            "--enable-isolated-zygote-process",
            action="store_true",
            dest="enable_isolated_zygote_process",
            default=False,
            help="Run with app Zygote preloading enabled.",
        )
        self.add_argument(
            "--web-content-isolation-strategy",
            type=int,
            dest="web_content_isolation_strategy",
            help="Strategy used to determine whether or not a particular site should load into "
            "a webIsolated content process, see fission.webContentIsolationStrategy.",
        )
        self.add_argument(
            "--enable-isolated-process",
            action="store_true",
            dest="isolated_process",
            default=False,
            help="Run the tests with content service isolated process enabled.",
        )
        self.add_argument(
            "--repeat",
            type=int,
            default=0,
            help="Repeat the tests the given number of times.",
        )
        self.add_argument(
            "--run-until-failure",
            action="store_true",
            dest="run_until_failure",
            default=False,
            help="Run tests repeatedly but stop the first time a test fails.",
        )
        self.add_argument(
            "--setpref",
            action="append",
            dest="extraPrefs",
            default=[],
            metavar="PREF=VALUE",
            help="Defines an extra user preference.",
        )
        # Additional options for server.
        (
            self.add_argument(
                "--certificate-path",
                action="store",
                type=str,
                dest="certPath",
                default=None,
                help="Path to directory containing certificate store.",
            ),
        )
        self.add_argument(
            "--http-port",
            action="store",
            type=str,
            dest="httpPort",
            default=DEFAULT_PORTS["http"],
            help="http port of the remote web server.",
        )
        self.add_argument(
            "--remote-webserver",
            action="store",
            type=str,
            dest="remoteWebServer",
            help="IP address of the remote web server.",
        )
        self.add_argument(
            "--ssl-port",
            action="store",
            type=str,
            dest="sslPort",
            default=DEFAULT_PORTS["https"],
            help="ssl port of the remote web server.",
        )
        self.add_argument(
            "--test-filters-file",
            action="store",
            type=str,
            dest="test_filters_file",
            default=None,
            help="Line-delimited file containing test filter(s)",
        )
        # Remaining arguments are test filters.
        self.add_argument(
            "test_filters",
            nargs="*",
            help="Test filter(s): class and/or method names of test(s) to run.",
        )

        mozlog.commandline.add_logging_group(self)


def run_test_harness(parser, options):
    if hasattr(options, "log"):
        log = options.log
    else:
        log = mozlog.commandline.setup_logging("runjunit", options, {"raw": sys.stdout})
    runner = JUnitTestRunner(log, options)
    result = -1
    try:
        device_exception = False
        result = runner.run_tests(
            test_filters_file=options.test_filters_file,
            test_filters=options.test_filters,
        )
    except KeyboardInterrupt:
        log.info("runjunit.py | Received keyboard interrupt")
        result = -1
    except JavaTestHarnessException as e:
        log.error(
            "TEST-UNEXPECTED-FAIL | runjunit.py | The previous test failed because "
            "of an error in the test harness | %s" % (str(e))
        )
    except Exception as e:
        traceback.print_exc()
        log.error("runjunit.py | Received unexpected exception while running tests")
        result = 1
        if isinstance(e, ADBTimeoutError):
            device_exception = True
    finally:
        if not device_exception:
            runner.cleanup()
    return result


def main(args=sys.argv[1:]):
    parser = JunitArgumentParser()
    options = parser.parse_args()
    return run_test_harness(parser, options)


if __name__ == "__main__":
    sys.exit(main())
