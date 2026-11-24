# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
import argparse
import os

from mozlog.commandline import add_logging_group


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


class _ListTests(_StopAction):
    def __call__(self, parser, namespace, values, option_string=None):
        from talos import test

        print("Available tests:")
        print("================\n")
        test_class_names = [
            (test_class.name(), test_class.description())
            for test_class in test.test_dict().values()
        ]
        test_class_names.sort()
        for name, description in test_class_names:
            print(name)
            print("-" * len(name))
            print(description)
            print()  # Appends a single blank line to the end
        parser.exit()


class _ListSuite(_StopAction):
    def __call__(self, parser, namespace, values, option_string=None):
        from talos.config import suites_conf

        print("Available suites:")
        conf = suites_conf()
        max_suite_name = max([len(s) for s in conf])
        for name in conf:
            tests = ":".join(conf[name]["tests"])
            print(f" {name:<{max_suite_name}} ({tests})")
        print()
        parser.exit()


def create_parser(mach_interface=False):
    parser = argparse.ArgumentParser()
    add_arg = parser.add_argument

    if not mach_interface:
        add_arg(
            "-e",
            "--executablePath",
            required=True,
            dest="browser_path",
            help="path to executable we are testing",
        )
    add_arg("-t", "--title", default="qm-pxp01", help="Title of the test run")
    add_arg(
        "--browserWait",
        dest="browser_wait",
        default=5,
        type=int,
        help="Amount of time allowed for the browser to cleanly close",
    )
    add_arg(
        "-a",
        "--activeTests",
        help="List of tests to run, separated by ':' (ex. damp:tart)",
    )
    add_arg("--suite", help="Suite to use (instead of --activeTests)")
    add_arg("--subtests", help="Name of the subtest(s) to run (works only on DAMP)")
    add_arg(
        "--mainthread",
        action="store_true",
        help="Collect mainthread IO data from the browser by setting"
        " an environment variable",
    )
    add_arg(
        "--mozAfterPaint",
        action="store_true",
        dest="tpmozafterpaint",
        help="wait for MozAfterPaint event before recording the time",
    )
    add_arg(
        "--firstPaint",
        action="store_true",
        dest="firstpaint",
        help="Also report the first paint value in supported tests",
    )
    add_arg(
        "--useHero",
        action="store_true",
        dest="tphero",
        help="use Hero elementtiming attribute to record the time",
    )
    add_arg(
        "--userReady",
        action="store_true",
        dest="userready",
        help="Also report the user ready value in supported tests",
    )
    add_arg(
        "--spsProfile",
        action="store_true",
        dest="gecko_profile",
        help="(Deprecated - Use --gecko-profile instead.) Profile the "
        "run and output the results in $MOZ_UPLOAD_DIR.",
    )
    add_arg(
        "--spsProfileInterval",
        dest="gecko_profile_interval",
        type=float,
        help="(Deprecated - Use --gecko-profile-interval instead.) How "
        "frequently to take samples (ms)",
    )
    add_arg(
        "--spsProfileEntries",
        dest="gecko_profile_entries",
        type=int,
        help="(Deprecated - Use --gecko-profile-entries instead.) How "
        "many samples to take with the profiler",
    )
    add_arg(
        "--gecko-profile",
        action="store_true",
        dest="gecko_profile",
        help="Profile the run and output the results in $MOZ_UPLOAD_DIR. "
        "After talos is finished, profiler.firefox.com will be launched in "
        "Firefox so you can analyze the local profiles. To disable "
        "auto-launching of profiler.firefox.com set the "
        "DISABLE_PROFILE_LAUNCH=1 env var. "
        "Copy paste the parameters used in this profiling run directly from about:profiling in Nightly.",
    )
    add_arg(
        "--gecko-profile-interval",
        dest="gecko_profile_interval",
        type=float,
        help="How frequently to take samples (ms)",
    )
    add_arg(
        "--gecko-profile-entries",
        dest="gecko_profile_entries",
        type=int,
        help="How many samples to take with the profiler",
    )
    add_arg(
        "--gecko-profile-features",
        dest="gecko_profile_features",
        type=str,
        help="Comma-separated list of features to enable in the profiler",
    )
    add_arg(
        "--gecko-profile-threads",
        dest="gecko_profile_threads",
        type=str,
        help="Comma-separated list of threads to sample",
    )
    add_arg(
        "--gecko-profile-extra-threads",
        dest="gecko_profile_extra_threads",
        type=str,
        help="Comma-separated list of extra threads to add to the default list of threads to profile.",
    )
    add_arg(
        "--extension",
        dest="extensions",
        action="append",
        default=["${talos}/talos-powers"],
        help="Extension to install while running",
    )
    add_arg("--fast", action="store_true", help="Run tp tests as tp_fast")
    add_arg(
        "--symbolsPath",
        dest="symbols_path",
        help="Path to the symbols for the build we are testing",
    )
    add_arg("--xperf_path", help="Path to windows performance tool xperf.exe")
    add_arg(
        "--test_timeout",
        type=int,
        default=1200,
        help="Time to wait for the browser to output to the log file",
    )
    add_arg(
        "--errorFile",
        dest="error_filename",
        default=os.path.abspath("browser_failures.txt"),
        help="Filename to store the errors found during the test."
        " Currently used for xperf only.",
    )
    add_arg(
        "--setpref",
        action="append",
        default=[],
        dest="extraPrefs",
        metavar="PREF=VALUE",
        help="Set a browser preference. May be used multiple times.",
    )
    add_arg(
        "--firstNonBlankPaint",
        action="store_true",
        dest="fnbpaint",
        help="Wait for firstNonBlankPaint event before recording the time",
    )
    add_arg(
        "--pdfPaint",
        action="store_true",
        dest="pdfpaint",
        help="Wait for the first page of a PDF to be rendered",
    )
    add_arg(
        "--pdfPaintChunk",
        type=int,
        default=None,
        dest="pdfpaint_chunk",
        help=(
            "Chunk of the pdfpaint test to run (each chunk runs at most 100 pdfs). "
            "Defaults to None to run all the pdfs at the same time."
        ),
    )
    add_arg(
        "--pdfPaintName",
        type=str,
        default=None,
        dest="pdfpaint_name",
        help=(
            "Name of a pdfpaint test to run (e.g. xfa_imm5257e.pdf). Chunking will be "
            "ignored/disabled if this option is used."
        ),
    )
    add_arg("--webServer", dest="webserver", help="DEPRECATED")
    if not mach_interface:
        add_arg(
            "--develop",
            action="store_true",
            default=False,
            help="useful for running tests on a developer machine."
            " Doesn't upload to the graph servers.",
        )
    add_arg("--cycles", type=int, help="number of browser cycles to run")
    add_arg("--tpmanifest", help="manifest file to test")
    add_arg("--tpcycles", type=int, help="number of pageloader cycles to run")
    add_arg(
        "--tptimeout",
        type=int,
        help="number of milliseconds to wait for a load event after"
        " calling loadURI before timing out",
    )
    add_arg(
        "--tppagecycles",
        type=int,
        help="number of pageloader cycles to run for each page in the manifest",
    )
    add_arg(
        "--no-download",
        action="store_true",
        dest="no_download",
        help="Do not download the talos test pagesets",
    )
    add_arg(
        "--sourcestamp",
        help="Specify the hg revision or sourcestamp for the changeset"
        " we are testing.  This will use the value found in"
        " application.ini if it is not specified.",
    )
    add_arg(
        "--repository",
        help="Specify the url for the repository we are testing. "
        "This will use the value found in application.ini if"
        " it is not specified.",
    )
    add_arg(
        "--framework",
        help="Will post to the specified framework for Perfherder. "
        'Default "talos".  Used primarily for experiments on '
        "new platforms",
    )
    add_arg("--print-tests", action=_ListTests, help="print available tests")
    add_arg("--print-suites", action=_ListSuite, help="list available suites")
    add_arg(
        "--no-upload-results",
        action="store_true",
        dest="no_upload_results",
        help="If given, it disables uploading of talos results.",
    )
    add_arg(
        "--profile",
        type=str,
        default=None,
        help="Downloads a profile from TaskCluster and uses it",
    )
    debug_options = parser.add_argument_group("Command Arguments for debugging")
    debug_options.add_argument(
        "--debug",
        action="store_true",
        help="Enable the debugger. Not specifying a --debugger option will"
        "result in the default debugger being used.",
    )
    debug_options.add_argument(
        "--debugger", default=None, help="Name of debugger to use."
    )
    debug_options.add_argument(
        "--debugger-args",
        default=None,
        metavar="params",
        help="Command-line arguments to pass to the debugger itself; split"
        "as the Bourne shell would.",
    )
    add_arg(
        "--code-coverage",
        action="store_true",
        dest="code_coverage",
        help="Remove any existing ccov gcda output files after browser"
        " initialization but before starting the tests. NOTE:"
        " Currently only supported in production.",
    )
    add_arg(
        "--disable-fission",
        action="store_false",
        dest="fission",
        default=True,
        help="Disable Fission (site isolation) in Gecko.",
    )
    add_arg(
        "--project",
        type=str,
        default="mozilla-central",
        help="The project branch we're running tests on. Used for "
        "disabling/skipping tests.",
    )
    add_arg(
        "--screenshot-on-failure",
        action="store_true",
        dest="screenshot_on_failure",
        default=False,
        help="Take a screenshot when the test fails.",
    )

    add_logging_group(parser)
    return parser


def parse_args(argv=None):
    parser = create_parser()
    return parser.parse_args(argv)
