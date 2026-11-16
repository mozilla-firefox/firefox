import argparse
import os
import sys
from urllib.parse import urlparse

import mozlog

here = os.path.abspath(os.path.dirname(__file__))


class ReftestArgumentsParser(argparse.ArgumentParser):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        # Try to import a MozbuildObject. Success indicates that we are
        # running from a source tree. This allows some defaults to be set
        # from the source tree.
        try:
            from mozbuild.base import MozbuildObject

            self.build_obj = MozbuildObject.from_environment(cwd=here)
        except ImportError:
            self.build_obj = None

        self.add_argument(
            "--xre-path",
            action="store",
            type=str,
            dest="xrePath",
            # individual scripts will set a sane default
            default=None,
            help="absolute path to directory containing XRE (probably xulrunner)",
        )

        self.add_argument(
            "--symbols-path",
            action="store",
            type=str,
            dest="symbolsPath",
            default=None,
            help="absolute path to directory containing breakpad symbols, "
            "or the URL of a zip file containing symbols",
        )

        self.add_argument(
            "--debugger",
            action="store",
            dest="debugger",
            help="use the given debugger to launch the application",
        )

        self.add_argument(
            "--debugger-args",
            action="store",
            dest="debuggerArgs",
            help="pass the given args to the debugger _before_ "
            "the application on the command line",
        )

        self.add_argument(
            "--debugger-interactive",
            action="store_true",
            dest="debuggerInteractive",
            help="prevents the test harness from redirecting "
            "stdout and stderr for interactive debuggers",
        )

        self.add_argument(
            "--appname",
            action="store",
            type=str,
            dest="app",
            default=None,
            help="absolute path to application, overriding default",
        )

        self.add_argument(
            "--extra-profile-file",
            action="append",
            dest="extraProfileFiles",
            default=[],
            help="copy specified files/dirs to testing profile",
        )

        self.add_argument(
            "--timeout",
            action="store",
            dest="timeout",
            type=int,
            default=300,  # 5 minutes per bug 479518
            help="reftest will timeout in specified number of seconds. "
            "[default %(default)s].",
        )

        self.add_argument(
            "--leak-threshold",
            action="store",
            type=int,
            dest="defaultLeakThreshold",
            default=0,
            help="fail if the number of bytes leaked in default "
            "processes through refcounted objects (or bytes "
            "in classes with MOZ_COUNT_CTOR and MOZ_COUNT_DTOR) "
            "is greater than the given number",
        )

        self.add_argument(
            "--utility-path",
            action="store",
            type=str,
            dest="utilityPath",
            default=self.build_obj.bindir if self.build_obj else None,
            help="absolute path to directory containing utility "
            "programs (xpcshell, ssltunnel, certutil)",
        )

        self.add_argument(
            "--total-chunks",
            type=int,
            dest="totalChunks",
            help="how many chunks to split the tests up into",
        )

        self.add_argument(
            "--this-chunk",
            type=int,
            dest="thisChunk",
            help="which chunk to run between 1 and --total-chunks",
        )

        self.add_argument(
            "--log-file",
            action="store",
            type=str,
            dest="logFile",
            default=None,
            help="file to log output to in addition to stdout",
        )

        self.add_argument(
            "--skip-slow-tests",
            dest="skipSlowTests",
            action="store_true",
            default=False,
            help="skip tests marked as slow when running",
        )

        self.add_argument(
            "--ignore-window-size",
            dest="ignoreWindowSize",
            action="store_true",
            default=False,
            help="ignore the window size, which may cause spurious failures and passes",
        )

        self.add_argument(
            "--install-extension",
            action="append",
            dest="extensionsToInstall",
            default=[],
            help="install the specified extension in the testing profile. "
            "The extension file's name should be <id>.xpi where <id> is "
            "the extension's id as indicated in its install.rdf. "
            "An optional path can be specified too.",
        )

        self.add_argument(
            "--marionette",
            default=None,
            help="host:port to use when connecting to Marionette",
        )

        self.add_argument(
            "--marionette-socket-timeout", default=None, help=argparse.SUPPRESS
        )

        self.add_argument(
            "--marionette-startup-timeout", default=None, help=argparse.SUPPRESS
        )

        self.add_argument(
            "--setenv",
            action="append",
            type=str,
            default=[],
            dest="environment",
            metavar="NAME=VALUE",
            help="sets the given variable in the application's environment",
        )

        self.add_argument(
            "--filter",
            action="store",
            type=str,
            dest="filter",
            help="specifies a regular expression (as could be passed to the JS "
            "RegExp constructor) to test against URLs in the reftest manifest; "
            "only test items that have a matching test URL will be run.",
        )

        self.add_argument(
            "--shuffle",
            action="store_true",
            default=False,
            dest="shuffle",
            help="run reftests in random order",
        )

        self.add_argument(
            "--run-until-failure",
            action="store_true",
            default=False,
            dest="runUntilFailure",
            help="stop running on the first failure. Useful for RR recordings.",
        )

        self.add_argument(
            "--repeat",
            action="store",
            type=int,
            default=0,
            dest="repeat",
            help="number of times the select test(s) will be executed. Useful for "
            "finding intermittent failures.",
        )

        self.add_argument(
            "--focus-filter-mode",
            action="store",
            type=str,
            dest="focusFilterMode",
            default="all",
            help="filters tests to run by whether they require focus. "
            "Valid values are `all', `needs-focus', or `non-needs-focus'. "
            "Defaults to `all'.",
        )

        self.add_argument(
            "--disable-e10s",
            action="store_false",
            default=True,
            dest="e10s",
            help="disables content processes",
        )

        self.add_argument(
            "--disable-fission",
            action="store_true",
            default=False,
            dest="disableFission",
            help="Run tests with fission (site isolation) disabled.",
        )

        self.add_argument(
            "--setpref",
            action="append",
            type=str,
            default=[],
            dest="extraPrefs",
            metavar="PREF=VALUE",
            help="defines an extra user preference",
        )

        self.add_argument(
            "--reftest-extension-path",
            action="store",
            dest="reftestExtensionPath",
            help="Path to the reftest extension",
        )

        self.add_argument(
            "--special-powers-extension-path",
            action="store",
            dest="specialPowersExtensionPath",
            help="Path to the special powers extension",
        )

        self.add_argument(
            "--suite",
            choices=["reftest", "crashtest", "jstestbrowser"],
            default=None,
            help=argparse.SUPPRESS,
        )

        self.add_argument(
            "--cleanup-crashes",
            action="store_true",
            dest="cleanupCrashes",
            default=False,
            help="Delete pending crash reports before running tests.",
        )

        self.add_argument(
            "--max-retries",
            type=int,
            dest="maxRetries",
            default=4,
            help="The maximum number of attempts to try and recover from a "
            "crash before aborting the test run [default 4].",
        )

        self.add_argument(
            "tests",
            metavar="TEST_PATH",
            nargs="*",
            help="Path to test file, manifest file, or directory containing "
            "tests. For jstestbrowser, the relative path should be from the "
            "staged area "
            "($OBJDIR/dist/test-stage/jsreftest/tests)",
        )

        self.add_argument(
            "--sandbox-read-whitelist",
            action="append",
            dest="sandboxReadWhitelist",
            default=[],
            help="Path to add to the sandbox whitelist.",
        )

        self.add_argument(
            "--verify",
            action="store_true",
            default=False,
            help="Run tests in verification mode: Run many times in different "
            "ways, to see if there are intermittent failures.",
        )

        self.add_argument(
            "--verify-max-time",
            type=int,
            default=3600,
            help="Maximum time, in seconds, to run in --verify mode..",
        )

        self.add_argument(
            "--enable-webrender",
            action="store_true",
            dest="enable_webrender",
            default=False,
            help="Enable the WebRender compositor in Gecko.",
        )

        self.add_argument(
            "--headless",
            action="store_true",
            dest="headless",
            default=False,
            help="Run tests in headless mode.",
        )

        self.add_argument(
            "--topsrcdir",
            action="store",
            type=str,
            dest="topsrcdir",
            default=None,
            help="Path to source directory",
        )

        mozlog.commandline.add_logging_group(self)

    def get_ip(self):
        import moznetwork

        return moznetwork.get_ip()

    def set_default_suite(self, options):
        manifests = {
            "reftest.list": "reftest",
            "crashtests.list": "crashtest",
            "jstests.list": "jstestbrowser",
        }

        for test_path in options.tests:
            file_name = os.path.basename(test_path)
            if file_name in manifests:
                options.suite = manifests[file_name]
                return

        for test_path in options.tests:
            for manifest_file, suite in manifests.items():
                if os.path.exists(os.path.join(test_path, manifest_file)):
                    options.suite = suite
                    return

        self.error(
            "Failed to determine test suite; supply --suite to set this explicitly"
        )

    def validate(self, options, reftest):
        if not options.tests:
            # Can't just set this in the argument parser because mach will set a default
            self.error(
                "Must supply at least one path to a manifest file, "
                "test directory, or test file to run."
            )

        if options.suite is None:
            self.set_default_suite(options)

        if options.totalChunks is not None and options.thisChunk is None:
            self.error("thisChunk must be specified when totalChunks is specified")

        if options.totalChunks:
            if not 1 <= options.thisChunk <= options.totalChunks:
                self.error("thisChunk must be between 1 and totalChunks")

        if not options.disableFission and not options.e10s:
            self.error("Fission is not supported without e10s.")

        if options.logFile:
            options.logFile = reftest.getFullPath(options.logFile)

        if options.xrePath is not None:
            if not os.access(options.xrePath, os.F_OK):
                self.error("--xre-path '%s' not found" % options.xrePath)
            if not os.path.isdir(options.xrePath):
                self.error("--xre-path '%s' is not a directory" % options.xrePath)
            options.xrePath = reftest.getFullPath(options.xrePath)

        if options.reftestExtensionPath is None:
            if self.build_obj is not None:
                reftestExtensionPath = os.path.join(
                    self.build_obj.distdir, "xpi-stage", "reftest"
                )
            else:
                reftestExtensionPath = os.path.join(here, "reftest")
            options.reftestExtensionPath = os.path.normpath(reftestExtensionPath)

        if options.specialPowersExtensionPath is None:
            if self.build_obj is not None:
                specialPowersExtensionPath = os.path.join(
                    self.build_obj.distdir, "xpi-stage", "specialpowers"
                )
            else:
                specialPowersExtensionPath = os.path.join(here, "specialpowers")
            options.specialPowersExtensionPath = os.path.normpath(
                specialPowersExtensionPath
            )

        options.leakThresholds = {
            "default": options.defaultLeakThreshold,
            "tab": options.defaultLeakThreshold,
        }

        if options.topsrcdir is None:
            if self.build_obj:
                options.topsrcdir = self.build_obj.topsrcdir
            else:
                options.topsrcdir = os.getcwd()


class DesktopArgumentsParser(ReftestArgumentsParser):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        self.add_argument(
            "--run-tests-in-parallel",
            action="store_true",
            default=False,
            dest="runTestsInParallel",
            help="run tests in parallel if possible",
        )

    def validate(self, options, reftest):
        super().validate(options, reftest)

        if options.runTestsInParallel:
            if options.logFile is not None:
                self.error("cannot specify logfile with parallel tests")
            if options.totalChunks is not None or options.thisChunk is not None:
                self.error(
                    "cannot specify thisChunk or totalChunks with parallel tests"
                )
            if options.focusFilterMode != "all":
                self.error("cannot specify focusFilterMode with parallel tests")
            if options.debugger is not None:
                self.error("cannot specify a debugger with parallel tests")

        if options.debugger:
            # valgrind and some debuggers may cause Gecko to start slowly. Make sure
            # marionette waits long enough to connect.
            options.marionette_startup_timeout = 900
            options.marionette_socket_timeout = 540

        if not options.tests:
            self.error("No test files specified.")

        if options.app is None:
            if (
                self.build_obj
                and self.build_obj.substs["MOZ_BUILD_APP"] != "mobile/android"
            ):
                from mozbuild.base import BinaryNotFoundException

                try:
                    bin_dir = self.build_obj.get_binary_path()
                except BinaryNotFoundException as e:
                    print(f"{e}\n\n{e.help()}\n", file=sys.stderr)
                    sys.exit(1)
            else:
                bin_dir = None

            if bin_dir:
                options.app = bin_dir

        if options.symbolsPath and len(urlparse(options.symbolsPath).scheme) < 2:
            options.symbolsPath = reftest.getFullPath(options.symbolsPath)

        options.utilityPath = reftest.getFullPath(options.utilityPath)


class RemoteArgumentsParser(ReftestArgumentsParser):
    def __init__(self, **kwargs):
        super().__init__()

        # app, xrePath and utilityPath variables are set in main function
        self.set_defaults(
            logFile="reftest.log", app="", xrePath="", utilityPath="", localLogName=None
        )

        self.add_argument(
            "--adbpath",
            action="store",
            type=str,
            dest="adb_path",
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
            "Use 'adb devices' to see connected devices.",
        )

        self.add_argument(
            "--remote-webserver",
            action="store",
            type=str,
            dest="remoteWebServer",
            help="IP address of the remote web server.",
        )

        self.add_argument(
            "--http-port",
            action="store",
            type=str,
            dest="httpPort",
            help="http port of the remote web server.",
        )

        self.add_argument(
            "--ssl-port",
            action="store",
            type=str,
            dest="sslPort",
            help="ssl port of the remote web server.",
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
            "--httpd-path",
            action="store",
            type=str,
            dest="httpdPath",
            help="Path to the httpd.js file.",
        )

        self.add_argument(
            "--no-install",
            action="store_true",
            default=False,
            help="Skip the installation of the APK.",
        )

        self.add_argument(
            "--enable-isolated-process",
            action="store_true",
            dest="isolated_process",
            default=False,
            help="Enable the content service isolated process.",
        )

    def validate_remote(self, options):
        DEFAULT_HTTP_PORT = 8888
        DEFAULT_SSL_PORT = 4443

        if options.remoteWebServer is None:
            options.remoteWebServer = self.get_ip()

        if options.remoteWebServer == "127.0.0.1":
            self.error(
                "ERROR: Either you specified the loopback for the remote webserver or ",
                "your local IP cannot be detected.  "
                "Please provide the local ip in --remote-webserver",
            )

        if not options.httpPort:
            options.httpPort = DEFAULT_HTTP_PORT

        if not options.sslPort:
            options.sslPort = DEFAULT_SSL_PORT

        if options.xrePath is None:
            self.error(
                "ERROR: You must specify the path to the controller xre directory"
            )
        else:
            # Ensure xrepath is a full path
            options.xrePath = os.path.abspath(options.xrePath)

        # httpd-path is specified by standard makefile targets and may be specified
        # on the command line to select a particular version of httpd.js. If not
        # specified, try to select the one from hostutils.zip, as required in
        # bug 882932.
        if not options.httpdPath:
            options.httpdPath = os.path.join(options.utilityPath, "components")

        return options
