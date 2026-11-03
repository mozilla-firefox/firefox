# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
import argparse
import os
import shutil
import sys

from mozlog.commandline import add_logging_group

(FIREFOX, CHROME, SAFARI, SAFARI_TP, CHROMIUM_RELEASE) = DESKTOP_APPS = [
    "firefox",
    "chrome",
    "safari",
    "safari-tp",
    "custom-car",
]
(GECKOVIEW, REFBROW, FENIX) = FIREFOX_ANDROID_APPS = [
    "geckoview",
    "refbrow",
    "fenix",
]
(CHROME_ANDROID, CHROMIUM_RELEASE_ANDROID) = CHROME_ANDROID_APPS = [
    "chrome-m",
    "cstm-car-m",
]
ANDROID_APPS = FIREFOX_ANDROID_APPS + CHROME_ANDROID_APPS

FIREFOX_APPS = FIREFOX_ANDROID_APPS + [FIREFOX]

CHROMIUM_DISTROS = [CHROME, CHROMIUM_RELEASE]
SAFARI_APPS = [SAFARI, SAFARI_TP]
APPS = {
    FIREFOX: {"long_name": "Firefox Desktop"},
    CHROME: {"long_name": "Google Chrome Desktop"},
    SAFARI: {"long_name": "Safari Desktop"},
    SAFARI_TP: {"long_name": "Safari Technology Preview Desktop"},
    CHROMIUM_RELEASE: {"long_name": "Custom Chromium-as-Release desktop"},
    GECKOVIEW: {
        "long_name": "Firefox GeckoView on Android",
        "default_activity": "org.mozilla.geckoview_example.GeckoViewActivity",
        "default_intent": "android.intent.action.MAIN",
    },
    REFBROW: {
        "long_name": "Firefox Android Components Reference Browser",
        "default_activity": "org.mozilla.reference.browser.BrowserTestActivity",
        "default_intent": "android.intent.action.MAIN",
    },
    FENIX: {
        "long_name": "Firefox Android Fenix Browser",
        "default_activity": "org.mozilla.fenix.IntentReceiverActivity",
        "default_intent": "android.intent.action.VIEW",
    },
    CHROME_ANDROID: {
        "long_name": "Google Chrome on Android",
        "default_activity": "com.android.chrome/com.google.android.apps.chrome.Main",
        "default_intent": "android.intent.action.VIEW",
    },
    CHROMIUM_RELEASE_ANDROID: {
        "long_name": "Custom Chromium-as-Release on Android",
        "default_activity": "com.android.chrome/com.google.android.apps.chrome.Main",
        "default_intent": "android.intent.action.VIEW",
    },
}
INTEGRATED_APPS = list(APPS.keys())

GECKO_PROFILER_APPS = (FIREFOX, GECKOVIEW, REFBROW, FENIX)

TRACE_APPS = (CHROME, CHROMIUM_RELEASE)

SIMPLEPERF_APPS = (FENIX, GECKOVIEW)

SAMPLY_PROFILE_APPS = (FIREFOX,)

PERF_PROFILE_APPS = (FIREFOX,)

APP_BINARIES = {
    "fenix": "org.mozilla.fenix",
    "focus": "org.mozilla.focus",
    "geckoview": "org.mozilla.geckoview_example",
    "refbrow": "org.mozilla.reference.browser",
}


def default_desktop_binary(app):
    """Return the usual install location of a non-Firefox desktop browser on
    this platform, or None if it isn't installed there."""
    candidates = []
    if app == CHROME:
        if sys.platform == "darwin":
            candidates = [
                "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
            ]
        elif sys.platform == "win32":
            candidates = [
                os.path.join(
                    os.environ[var], "Google", "Chrome", "Application", "chrome.exe"
                )
                for var in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA")
                if os.environ.get(var)
            ]
        else:
            candidates = [
                shutil.which("google-chrome"),
                shutil.which("google-chrome-stable"),
                "/usr/bin/google-chrome",
            ]
    elif app == SAFARI and sys.platform == "darwin":
        candidates = ["/Applications/Safari.app/Contents/MacOS/Safari"]
    elif app == SAFARI_TP and sys.platform == "darwin":
        candidates = [
            "/Applications/Safari Technology Preview.app/Contents/MacOS/"
            "Safari Technology Preview"
        ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    return None


def print_all_activities():
    all_activities = []
    for next_app in APPS:
        if APPS[next_app].get("default_activity", None) is not None:
            _activity = f"{next_app}:{APPS[next_app]['default_activity']}"
            all_activities.append(_activity)
    return all_activities


def print_all_intents():
    all_intents = []
    for next_app in APPS:
        if APPS[next_app].get("default_intent", None) is not None:
            _intent = f"{next_app}:{APPS[next_app]['default_intent']}"
            all_intents.append(_intent)
    return all_intents


def create_parser(mach_interface=False):
    parser = argparse.ArgumentParser()
    add_arg = parser.add_argument

    add_arg(
        "-t",
        "--test",
        required=True,
        dest="test",
        help="Name of Raptor test to run (can be a top-level suite name i.e. "
        "'--test speedometer3','--test raptor-tp6-1', or for page-load "
        "tests a suite sub-test i.e. '--test raptor-tp6-google-firefox')",
    )
    add_arg(
        "--app",
        default="firefox",
        dest="app",
        help="Name of the application we are testing (default: firefox)",
        choices=list(APPS),
    )
    add_arg(
        "-b",
        "--binary",
        dest="binary",
        help="path to the browser executable that we are testing",
    )
    add_arg(
        "-a",
        "--activity",
        dest="activity",
        default=None,
        help="Name of Android activity used to launch the Android app."
        f"i.e.: {print_all_activities()}",
    )
    add_arg(
        "-i",
        "--intent",
        dest="intent",
        default=None,
        help="Name of Android intent action used to launch the Android app."
        f"i.e.: {print_all_intents()}",
    )
    add_arg(
        "--host",
        dest="host",
        help="Hostname from which to serve URLs; defaults to 127.0.0.1. "
        "The value HOST_IP will cause the value of host to be "
        "loaded from the environment variable HOST_IP.",
        default="127.0.0.1",
    )

    add_arg(
        "--live-sites",
        dest="live_sites",
        action="store_true",
        default=False,
        help="Run tests using live sites instead of recorded sites.",
    )
    add_arg(
        "--chimera",
        dest="chimera",
        action="store_true",
        default=False,
        help="Run tests in chimera mode. Each browser cycle will run a cold and warm test.",
    )
    add_arg(
        "--is-release-build",
        dest="is_release_build",
        default=False,
        action="store_true",
        help="Whether the build is a release build which requires workarounds "
        "using MOZ_DISABLE_NONLOCAL_CONNECTIONS to support installing unsigned "
        "webextensions. Defaults to False.",
    )
    add_arg(
        "--geckoProfile",
        action="store_true",
        dest="gecko_profile",
        help=argparse.SUPPRESS,
    )
    add_arg(
        "--geckoProfileInterval",
        dest="gecko_profile_interval",
        type=float,
        help=argparse.SUPPRESS,
    )
    add_arg(
        "--geckoProfileEntries",
        dest="gecko_profile_entries",
        type=int,
        help=argparse.SUPPRESS,
    )
    add_arg(
        "--gecko-profile",
        action="store_true",
        dest="gecko_profile",
        help="Profile the run and output the results in $MOZ_UPLOAD_DIR. "
        "After talos is finished, profiler.firefox.com will be launched in Firefox "
        "so you can analyze the local profiles. To disable auto-launching of "
        "profiler.firefox.com, set the DISABLE_PROFILE_LAUNCH=1 env var. "
        "Copy paste the parameters used in this profiling run directly from about:profiling in Nightly.",
    )
    add_arg(
        "--gecko-profile-entries",
        dest="gecko_profile_entries",
        type=int,
        help="How many samples to take with the profiler",
    )
    add_arg(
        "--gecko-profile-interval",
        dest="gecko_profile_interval",
        type=float,
        help="How frequently to take samples (milliseconds)",
    )
    add_arg(
        "--gecko-profile-thread",
        dest="gecko_profile_extra_threads",
        default=[],
        action="append",
        help="Name of the extra thread to be profiled",
    )
    add_arg(
        "--gecko-profile-threads",
        dest="gecko_profile_threads",
        type=str,
        help="Comma-separated list of all threads to sample",
    )
    add_arg(
        "--gecko-profile-features",
        dest="gecko_profile_features",
        type=str,
        help="What features to enable in the profiler",
    )
    add_arg(
        "--extra-profiler-run",
        dest="extra_profiler_run",
        action="store_true",
        default=False,
        help="Run the tests again with profiler enabled after the main run.",
    )
    add_arg(
        "--simpleperf",
        action="store_true",
        dest="simpleperf",
        help="Enable Simpleperf profiling (Android only).",
    )
    add_arg(
        "--etw-profile",
        action="store_true",
        dest="etw_profile",
        default=False,
        help="Enable system-wide ETW profiling via Xperf (Windows only). ",
    )
    add_arg(
        "--samply-profile",
        action="store_true",
        dest="samply_profile",
        default=False,
        help="Enable system-wide profiling via Samply (macOS only).",
    )
    add_arg(
        "--perf-profile",
        action="store_true",
        dest="perf_profile",
        default=False,
        help="Enable system-wide profiling via perf (Linux only).",
    )
    add_arg(
        "--symbolsPath",
        dest="symbols_path",
        help="Path to the symbols for the build we are testing",
    )
    add_arg(
        "--page-cycles",
        dest="page_cycles",
        type=int,
        help="How many times to repeat loading the test page (for page load tests); "
        "for benchmark tests, this is how many times the benchmark test will be run",
    )
    add_arg(
        "--page-timeout",
        dest="page_timeout",
        type=int,
        help="How long to wait (ms) for one page_cycle to complete, before timing out",
    )
    add_arg(
        "--post-startup-delay",
        dest="post_startup_delay",
        type=int,
        default=None,
        help="How long to wait (ms) after browser start-up before starting the tests",
    )
    (
        add_arg(
            "--browser-cycles",
            dest="browser_cycles",
            type=int,
            help="The number of times a cold load test is repeated (for cold load tests only, "
            "where the browser is shutdown and restarted between test iterations)",
        ),
    )
    (
        add_arg(
            "--project",
            dest="project",
            type=str,
            default="mozilla-central",
            help="Project name (try, mozilla-central, etc.)",
        ),
    )
    add_arg(
        "--test-url-params",
        dest="test_url_params",
        help="Parameters to add to the test_url query string",
    )
    add_arg(
        "--print-tests",
        "--list-tests",
        action=_PrintTests,
        help="Print all available Raptor tests",
    )
    add_arg(
        "--debug-mode",
        dest="debug_mode",
        action="store_true",
        help="Run Raptor in debug mode (open browser console, limited page-cycles, etc.)",
    )
    add_arg(
        "--disable-e10s",
        dest="e10s",
        action="store_false",
        default=True,
        help="Run without multiple processes (e10s).",
    )
    add_arg(
        "--device-name",
        dest="device_name",
        default=None,
        type=str,
        help="Device name of mobile device.",
    )
    add_arg(
        "--disable-fission",
        dest="fission",
        action="store_false",
        default=True,
        help="Disable Fission (site isolation) in Gecko.",
    )
    add_arg(
        "--setpref",
        dest="extra_prefs",
        action="append",
        default=[],
        metavar="PREF=VALUE",
        help="Set a browser preference. May be used multiple times.",
    )
    add_arg(
        "--setenv",
        dest="environment",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Set a variable in the test environment. May be used multiple times.",
    )
    if not mach_interface:
        add_arg(
            "--run-local",
            dest="run_local",
            default=False,
            action="store_true",
            help="Flag which indicates if Raptor is running locally or in production",
        )
        add_arg(
            "--obj-path",
            dest="obj_path",
            default=None,
            help="Browser-build obj_path (received when running in production)",
        )
        add_arg(
            "--mozbuild-path",
            dest="mozbuild_path",
            default=None,
            help="This contains the path to mozbuild.",
        )
    add_arg(
        "--no-install",
        dest="no_install",
        default=False,
        action="store_true",
        help="Flag which indicates if Raptor should not offer to install Android APK.",
    )
    add_arg(
        "--installerpath",
        dest="installerpath",
        default=None,
        type=str,
        help="Location where Android browser APK was extracted to before installation.",
    )
    add_arg(
        "--disable-perf-tuning",
        dest="disable_perf_tuning",
        default=False,
        action="store_true",
        help="Disable performance tuning on android.",
    )
    add_arg(
        "--conditioned-profile",
        dest="conditioned_profile",
        default=None,
        type=str,
        help="Name of conditioned profile to use. Prefix with `artifact:` "
        "if we should obtain the profile from CI.",
    )
    add_arg(
        "--install-extension",
        dest="install_extensions",
        action="append",
        default=[],
        metavar="ADDON_ID_OR_URL",
        help="Install a webextension into the test profile before running. Accepts "
        "an AMO addon GUID/slug, a direct .xpi URL, or a local .xpi path. May be "
        "specified multiple times.",
    )
    add_arg(
        "--test-bytecode-cache",
        dest="test_bytecode_cache",
        default=False,
        action="store_true",
        help="If set, the pageload test will set the preference "
        "`dom.script_loader.bytecode_cache.strategy=-1` and wait 20 seconds after "
        "the first cold pageload to populate the bytecode cache before running "
        "a warm pageload test. Only available if `--chimera` is also provided.",
    )

    # for browsertime jobs, cold page load is determined by a '--cold' cmd line argument
    add_arg(
        "--cold",
        dest="cold",
        action="store_true",
        help="Enable cold page-load for browsertime tp6",
    )
    # Arguments for invoking browsertime.

    add_arg(
        "--browsertime",
        dest="browsertime",
        default=True,
        action="store_true",
        help="Whether to use browsertime to execute pageload tests",
    )
    add_arg(
        "--browsertime-arg",
        dest="browsertime_user_args",
        action="append",
        default=[],
        metavar="OPTION=VALUE",
        help="Add extra browsertime arguments to your test run using "
        "this option e.g.: --browsertime-arg timeout.scripts=1000",
    )
    add_arg(
        "--browsertime-node", dest="browsertime_node", help="path to Node.js executable"
    )
    add_arg(
        "--browsertime-browsertimejs",
        dest="browsertime_browsertimejs",
        help="path to browsertime.js script",
    )
    add_arg(
        "--browsertime-vismet-script",
        dest="browsertime_vismet_script",
        help="path to visualmetrics.py script",
    )
    add_arg(
        "--browsertime-chromedriver",
        dest="browsertime_chromedriver",
        help="path to chromedriver executable",
    )
    add_arg(
        "--browsertime-video",
        dest="browsertime_video",
        help="records the viewport",
        default=False,
        action="store_true",
    )
    add_arg(
        "--browsertime-visualmetrics",
        dest="browsertime_visualmetrics",
        help="enables visual metrics",
        default=False,
        action="store_true",
    )
    add_arg(
        "--browsertime-no-ffwindowrecorder",
        dest="browsertime_no_ffwindowrecorder",
        help="disable the firefox window recorder",
        default=False,
        action="store_true",
    )
    add_arg(
        "--browsertime-ffmpeg",
        dest="browsertime_ffmpeg",
        help="path to ffmpeg executable (for `--video=true`)",
    )
    add_arg(
        "--browsertime-geckodriver",
        dest="browsertime_geckodriver",
        help="path to geckodriver executable",
    )
    add_arg(
        "--browsertime-existing-results",
        dest="browsertime_existing_results",
        default=None,
        help="load existing results instead of running tests",
    )
    add_arg(
        "--verbose",
        dest="verbose",
        action="store_true",
        default=False,
        help="Verbose output",
    )
    add_arg(
        "--enable-marionette-trace",
        dest="enable_marionette_trace",
        action="store_true",
        default=False,
        help="Enable marionette tracing",
    )
    add_arg(
        "--clean",
        dest="clean",
        action="store_true",
        default=False,
        help="Clean the python virtualenv (remove, and rebuild) for Raptor before running tests.",
    )
    add_arg(
        "--collect-perfstats",
        dest="collect_perfstats",
        action="store_true",
        default=False,
        help="If set, the test will collect perfstats in addition to "
        "the regular metrics it gathers.",
    )
    add_arg(
        "--extra-summary-methods",
        dest="extra_summary_methods",
        action="append",
        default=[],
        metavar="OPTION",
        help="Alternative methods for summarizing technical and visual pageload metrics. "
        "Options: median.",
    )
    add_arg(
        "--benchmark-repository",
        dest="benchmark_repository",
        default=None,
        type=str,
        help="Repository that should be used for a particular benchmark test. "
        "e.g. https://github.com/mozilla-mobile/firefox-android",
    )
    add_arg(
        "--benchmark-revision",
        dest="benchmark_revision",
        default=None,
        type=str,
        help="Repository revision that should be used for a particular benchmark test.",
    )
    add_arg(
        "--benchmark-branch",
        dest="benchmark_branch",
        default=None,
        type=str,
        help="Repository branch that should be used for a particular benchmark test.",
    )
    add_arg(
        "--screenshot-on-failure",
        action="store_true",
        dest="screenshot_on_failure",
        default=False,
        help="Take a screenshot when the test fails.",
    )
    add_arg(
        "--power-test",
        action="store_true",
        dest="power_test",
        default=False,
        help="Gather power usage measurements on this test (Android only).",
    )

    add_logging_group(parser)
    return parser


def verify_options(parser, args):
    ctx = vars(args)
    if args.binary is None and args.app != "chrome-m":
        args.binary = APP_BINARIES.get(args.app) or default_desktop_binary(args.app)
        if args.binary is None:
            parser.error(
                f"Couldn't find an installed {args.app}, pass --binary <path/to/binary>"
            )
        else:
            print(f"Using {args.binary} as default binary argument for {args.app} app")

    # Debug-mode is disabled in CI (check for attribute in case of mach_interface issues)
    if hasattr(args, "run_local") and (not args.run_local and args.debug_mode):
        parser.error("Cannot run debug mode in CI")

    # make sure that browsertime_video is set if visual metrics are requested
    if args.browsertime_visualmetrics and not args.browsertime_video:
        args.browsertime_video = True

    # if running chrome android tests, make sure it's on browsertime and
    # that the chromedriver path was provided
    if args.app == "chrome-m":
        if not args.browsertime:
            parser.error("--browsertime is required to run android chrome tests")
        if not args.browsertime_chromedriver:
            parser.error(
                "--browsertime-chromedriver path is required for android chrome tests"
            )

    if args.chimera:
        if not args.browsertime:
            parser.error("--browsertime is required to run tests in chimera mode")
        if isinstance(args.page_cycles, int) and args.page_cycles != 2:
            parser.error(
                "--page-cycles must either not be set, or must be equal to 2 in chimera mode"
            )
        # Force cold pageloads with 2 page cycles
        args.cold = True
        args.page_cycles = 2
        # Create bytecode cache at the first cold load, so that the next warm load uses it.
        # This is applicable for chimera mode only
        if args.test_bytecode_cache:
            args.extra_prefs.append("dom.script_loader.bytecode_cache.strategy=-1")
    elif args.test_bytecode_cache:
        parser.error("--test-bytecode-cache can only be used in --chimera mode")

    # if running on a desktop browser make sure the binary exists
    if args.app in DESKTOP_APPS:
        if not os.path.isfile(args.binary):
            parser.error("{binary} does not exist!".format(**ctx))

    # if geckoProfile specified but running on Chrom[e|ium], not supported
    if args.gecko_profile and args.app in CHROMIUM_DISTROS:
        parser.error("Gecko profiling is not supported on Chrome/Chromium!")

    if args.fission:
        print("Fission enabled through browser preferences")
        args.extra_prefs.append("fission.autostart=true")
    else:
        print("Fission disabled through browser preferences")
        args.extra_prefs.append("fission.autostart=false")

    # if running on geckoview/refbrow/fenix, we need an activity and intent
    if args.app in ["geckoview", "refbrow", "fenix"]:
        if not args.activity:
            # if we have a default activity specified in APPS above, use that
            if APPS[args.app].get("default_activity", None) is not None:
                args.activity = APPS[args.app]["default_activity"]
            else:
                # otherwise fail out
                parser.error("--activity command-line argument is required!")
        if not args.intent:
            # if we have a default intent specified in APPS above, use that
            if APPS[args.app].get("default_intent", None) is not None:
                args.intent = APPS[args.app]["default_intent"]
            else:
                # otherwise fail out
                parser.error("--intent command-line argument is required!")

    if args.benchmark_repository:
        if not args.benchmark_revision:
            parser.error(
                "When a benchmark repository is provided, a revision is also required."
            )

    if args.post_startup_delay:
        if args.post_startup_delay < 0:
            parser.error("--post-startup-delay must be a positive integer (in ms).")

    if args.simpleperf and args.app not in SIMPLEPERF_APPS:
        parser.error(f"--simpleperf is only available in: {', '.join(SIMPLEPERF_APPS)}")

    if args.simpleperf and args.gecko_profile:
        parser.error("--simpleperf cannot be used with --gecko-profile.")


def parse_args(argv=None):
    parser = create_parser()
    args = parser.parse_args(argv)
    if args.host == "HOST_IP":
        args.host = os.environ["HOST_IP"]
    verify_options(parser, args)
    return args


class _StopAction(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest=argparse.SUPPRESS,
        default=argparse.SUPPRESS,
        help=None,
    ):
        super().__init__(
            option_strings=option_strings,
            dest=dest,
            default=default,
            nargs=0,
            help=help,
        )


class _PrintTests(_StopAction):
    def __init__(self, integrated_apps=INTEGRATED_APPS, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.integrated_apps = integrated_apps

    def __call__(self, parser, namespace, values, option_string=None):
        from manifestparser import TestManifest

        here = os.path.abspath(os.path.dirname(__file__))
        raptor_toml = os.path.join(here, "raptor.toml")

        for _app in self.integrated_apps:
            test_manifest = TestManifest([raptor_toml], strict=False)
            info = {"app": _app}
            available_tests = test_manifest.active_tests(
                exists=False, disabled=False, filters=[self.filter_app], **info
            )
            if len(available_tests) == 0:
                # none for that app; skip to next
                continue

            # print in readable format
            if _app == "firefox":
                title = f"\nRaptor Tests Available for {APPS[_app]['long_name']}"
            else:
                title = (
                    f"\nRaptor Tests Available for {APPS[_app]['long_name']} "
                    f"(--app={_app})"
                )

            print(title)
            print("=" * (len(title) - 1))

            # build the list of tests for this app
            test_list = {}

            for next_test in available_tests:
                if next_test.get("name", None) is None:
                    # no test name; skip it
                    continue

                suite = ".".join(
                    os.path.basename(next_test["manifest"]).split(".")[:-1]
                )
                if suite not in test_list:
                    test_list[suite] = {"type": None, "subtests": []}

                # for page-load tests, we want to list every subtest, so we
                # can see which pages are available in which tp6-* sets
                if next_test.get("type", None) is not None:
                    test_list[suite]["type"] = next_test["type"]
                    if next_test["type"] == "pageload":
                        subtest = next_test["name"]
                        measure = next_test.get("measure")
                        if measure is not None:
                            subtest = "{} ({})".format(
                                subtest, measure.replace("\n", ", ")
                            )
                        test_list[suite]["subtests"].append(subtest)

            # print the list in a nice, readable format
            for key in sorted(test_list.keys()):
                print(f"\n{key}")
                print(f"  type: {test_list[key]['type']}")
                if len(test_list[key]["subtests"]) != 0:
                    print("  subtests:")
                    for _sub in sorted(test_list[key]["subtests"]):
                        print(f"    {_sub}")

        print("\nDone.")
        # exit Raptor
        parser.exit()

    def filter_app(self, tests, values, strict=True):
        for test in tests:
            if values["app"] in test["apps"]:
                yield test
