#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import json
import os
import pathlib
import re
import signal
import sys
import tempfile
from abc import ABCMeta, abstractmethod
from copy import deepcopy

import mozcrash
import mozprocess
from benchmark import Benchmark
from cmdline import CHROME_ANDROID_APPS, DESKTOP_APPS, FIREFOX_ANDROID_APPS
from logger.logger import RaptorLogger
from manifestparser.util import evaluate_list_from_string
from perftest import GECKO_PROFILER_APPS, TRACE_APPS, Perftest
from results import BrowsertimeResultsHandler
from utils import bool_from_str, view_gecko_profile_from_raptor

LOG = RaptorLogger(component="raptor-browsertime")

DEFAULT_CHROMEVERSION = "77"
BROWSERTIME_PAGELOAD_OUTPUT_TIMEOUT = 120  # 2 minutes
BROWSERTIME_BENCHMARK_OUTPUT_TIMEOUT = (
    None  # Disable output timeout for benchmark tests
)
BROWSERTIME_MAX_ATTEMPTS = 3


class Browsertime(Perftest, metaclass=ABCMeta):
    """Abstract base class for Browsertime"""

    @property
    @abstractmethod
    def browsertime_args(self):
        pass

    def __init__(self, app, binary, **kwargs):
        self.browsertime = True
        self.browsertime_failure = ""
        self.browsertime_fatal_setup_error = ""
        self.browsertime_user_args = []
        self._crash_directory = None

        for key in list(kwargs):
            if key.startswith("browsertime_"):
                value = kwargs.pop(key)
                setattr(self, key, value)

        def klass(**config):
            root_results_dir = os.path.join(
                os.environ.get("MOZ_UPLOAD_DIR", os.getcwd()), "browsertime-results"
            )
            return BrowsertimeResultsHandler(config, root_results_dir=root_results_dir)

        profile_class = "firefox"
        if app in CHROME_ANDROID_APPS:
            # use the chrome-m profile class for both chrome-m and CaR-m
            profile_class = "chrome-m"

        super().__init__(
            app,
            binary,
            profile_class=profile_class,
            results_handler_class=klass,
            **kwargs,
        )
        LOG.info(f"cwd: '{os.getcwd()}'")
        self.config["browsertime"] = True

        # Setup browsertime-specific settings for result parsing
        self.results_handler.browsertime_visualmetrics = self.browsertime_visualmetrics

        # For debugging.
        for k in (
            "browsertime_node",
            "browsertime_browsertimejs",
            "browsertime_ffmpeg",
            "browsertime_geckodriver",
            "browsertime_chromedriver",
            "browsertime_user_args",
        ):
            try:
                if not self.browsertime_video and k == "browsertime_ffmpeg":
                    continue
                LOG.info(f"{k}: {getattr(self, k)}")
                LOG.info(f"{k}: {os.stat(getattr(self, k))}")
            except Exception as e:
                LOG.info(f"{k}: {e}")

    @property
    def crash_directory(self):
        if not self._crash_directory:
            self._crash_directory = tempfile.mkdtemp()
            self._dirs_to_remove.append(self._crash_directory)
        return self._crash_directory

    def build_browser_profile(self):
        super().build_browser_profile()
        if self.profile is not None:
            self.remove_mozprofile_delimiters_from_profile()

    def remove_mozprofile_delimiters_from_profile(self):
        # Perftest.build_browser_profile uses mozprofile to create the profile and merge in prefs;
        # while merging, mozprofile adds in special delimiters; these delimiters (along with blank
        # lines) are not recognized by selenium-webdriver ultimately causing Firefox launch to
        # fail. So we must remove these delimiters from the browser profile before passing into
        # btime via firefox.profileTemplate.

        LOG.info("Removing mozprofile delimiters from browser profile")
        userjspath = os.path.join(self.profile.profile, "user.js")
        try:
            with open(userjspath) as userjsfile:
                lines = userjsfile.readlines()
            lines = [
                line
                for line in lines
                if not line.startswith("#MozRunner") and line.strip()
            ]
            with open(userjspath, "w") as userjsfile:
                userjsfile.writelines(lines)
        except Exception as e:
            LOG.critical(f"Exception {e} while removing mozprofile delimiters")

    def set_browser_test_prefs(self, raw_prefs):
        # add test specific preferences
        LOG.info("setting test-specific Firefox preferences")

        self.profile.set_preferences(raw_prefs)
        self.remove_mozprofile_delimiters_from_profile()

    def _convert_prefs_to_dict(self, raw_prefs):
        pref_dict = {}
        prefparts = raw_prefs.split("\n")
        for pref in prefparts:
            if "=" not in pref:
                continue
            parts = pref.strip("\r").split("=")
            try:
                if "rue" in parts[-1] or "alse" in parts[-1]:
                    parts[-1] = bool_from_str(parts[-1])
                else:
                    parts[-1] = int(parts[-1])
            except ValueError:
                pass
            pref_dict[parts[0]] = parts[-1]
            if len(parts) > 2:
                pref_dict[parts[0]] = "=".join(parts[1:])
        return pref_dict

    def run_test_setup(self, test):
        if test.get("preferences", ""):
            test["preferences"] = self._convert_prefs_to_dict(test["preferences"])

        super().run_test_setup(test)

        if test.get("type") == "benchmark" or test.get("benchmark_webserver", False):
            # benchmark-type tests require the benchmark test to be served out
            self.benchmark = Benchmark(self.config, test, debug_mode=self.debug_mode)
            test["test_url"] = test["test_url"].replace("<host>", self.benchmark.host)
            test["test_url"] = test["test_url"].replace("<port>", self.benchmark.port)

        if test.get("playback") is not None and self.playback is None:
            self.start_playback(test)

        # TODO: geckodriver/chromedriver from tasks.
        self.driver_paths = []
        if self.browsertime_geckodriver:
            self.driver_paths.extend([
                "--firefox.geckodriverPath",
                self.browsertime_geckodriver,
            ])
        if self.browsertime_chromedriver and self.config["app"] in (
            "chrome",
            "chrome-m",
            "custom-car",
            "cstm-car-m",
        ):
            if (
                not self.config.get("run_local", None)
                or "{}" in self.browsertime_chromedriver
            ):
                if self.browser_version:
                    bvers = str(self.browser_version)
                    chromedriver_version = bvers.split(".", 1)[0]
                else:
                    chromedriver_version = DEFAULT_CHROMEVERSION

                self.browsertime_chromedriver = self.browsertime_chromedriver.format(
                    chromedriver_version
                )

                if not os.path.exists(self.browsertime_chromedriver):
                    raise Exception(
                        "Cannot find the chromedriver for the chrome version "
                        f"being tested: {self.browsertime_chromedriver}"
                    )

            self.driver_paths.extend([
                "--chrome.chromedriverPath",
                self.browsertime_chromedriver,
            ])

        # YTP tests fail in mozilla-release due to the `MOZ_DISABLE_NONLOCAL_CONNECTIONS`
        # environment variable. This logic changes this variable for the browsertime test
        # subprocess call in `run_test`
        if "youtube-playback" in test["name"] and self.config["is_release_build"]:
            os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "0"

        LOG.info(f"test: {test}")

    def run_test_teardown(self, test):
        super().run_test_teardown(test)

        # If we were using a playback tool, stop it
        if self.playback is not None:
            self.playback.stop()
            self.playback = None

        # Stop the benchmark server if we're running a benchmark test
        if self.benchmark:
            self.benchmark.stop_http_server()
            self.benchmark = None

        if test.get("support_class", None):
            LOG.info("Test support class is cleaning up...")
            test.get("support_class").clean_up()

    def check_for_crashes(self):
        super().check_for_crashes()
        self.crashes += mozcrash.log_crashes(
            LOG, self.crash_directory, self.config["symbols_path"]
        )

    def clean_up(self):
        super().clean_up()

    def _expose_browser_profiler(self, extra_profiler_run, test):
        """Use this method to check if we will use an exposed gecko profiler via browsertime.
        The exposed browser profiler let's us control the start/stop during tests.
        At the moment we would only want this for the Firefox or Chrome* applications and for
        any test with the `expose_browser_profiler` field set true (e.g. benchmark tests).
        """
        return (
            (self.config["gecko_profile"] or extra_profiler_run)
            and test.get("expose_browser_profiler")
            and self.config["app"] in GECKO_PROFILER_APPS + TRACE_APPS
        )

    def _compose_cmd(self, test, timeout, extra_profiler_run=False):
        """Browsertime has the following overwrite priorities(in order of highest-lowest)
        (1) User - input commandline flag.
        (2) Browsertime args mentioned for a test
        (3) Test-manifest settings
        (4) Default settings
        """

        browsertime_path = os.path.join(
            os.path.dirname(__file__), "..", "..", "browsertime"
        )

        if test.get("type", "") == "scenario":
            browsertime_script = os.path.join(
                browsertime_path,
                "browsertime_scenario.js",
            )
        elif (
            test.get("type", "") == "benchmark"
            and test.get("test_script", None) is None
        ):
            browsertime_script = os.path.join(
                browsertime_path,
                "browsertime_benchmark.js",
            )
        elif (
            test.get("name", "") == "browsertime"
        ):  # Custom scripts are treated as pageload tests for now
            # Check for either a script or a url from the
            # --browsertime-arg options
            browsertime_script = None
            for option in self.browsertime_user_args:
                arg, val = option.split("=", 1)
                if arg in ("test_script", "url"):
                    browsertime_script = val
            if browsertime_script is None:
                raise Exception(
                    "You must provide a path to the test script or the url like so: "
                    "`--browsertime-arg test_script=PATH/TO/SCRIPT`, or "
                    "`--browsertime-arg url=https://www.sitespeed.io`"
                )

            # Make it simple to use our builtin test scripts
            if browsertime_script == "pageload":
                browsertime_script = os.path.join(
                    browsertime_path, "browsertime_pageload.js"
                )
            elif browsertime_script == "interactive":
                browsertime_script = os.path.join(
                    browsertime_path, "browsertime_interactive.js"
                )

        elif test.get("interactive", False):
            browsertime_script = os.path.join(
                browsertime_path,
                "browsertime_interactive.js",
            )
        else:
            browsertime_script = os.path.join(
                browsertime_path,
                test.get("test_script", "browsertime_pageload.js"),
            )

        page_cycle_delay = "1000"
        if self.config["live_sites"]:
            # Wait a bit longer when we run live site tests
            page_cycle_delay = "5000"

        page_cycles = (
            test.get("page_cycles", 1)
            if not extra_profiler_run
            else test.get("extra_profiler_run_page_cycles", 1)
        )
        browser_cycles = (
            test.get("browser_cycles", 1)
            if not extra_profiler_run
            else test.get("extra_profiler_run_browser_cycles", 1)
        )

        # All the configurations in the browsertime_options variable initialization
        # and the secondary_url are priority 3, since none overlap they are grouped together
        browsertime_options = [
            "--firefox.noDefaultPrefs",
            "--browsertime.page_cycle_delay",
            page_cycle_delay,
            # Raptor's `pageCycleDelay` delay (ms) between pageload cycles
            "--skipHar",
            "--pageLoadStrategy",
            "none",
            "--webdriverPageload",
            "true",
            "--firefox.disableBrowsertimeExtension",
            "true",
            "--pageCompleteCheckStartWait",
            "5000",
            # url load timeout (milliseconds)
            "--pageCompleteCheckPollTimeout",
            "1000",
            # delay before pageCompleteCheck (milliseconds)
            "--beforePageCompleteWaitTime",
            "2000",
            # running browser scripts timeout (milliseconds)
            "--timeouts.pageLoad",
            str(timeout),
            "--timeouts.script",
            str(timeout * int(page_cycles)),
            "--browsertime.page_cycles",
            str(page_cycles),
            # a delay was added by default to browsertime from 5s -> 8s for iphones, not needed
            "--pageCompleteWaitTime",
            str(test.get("page_complete_wait_time", "5000")),
            "--browsertime.url",
            test["test_url"],
            # Raptor's `post startup delay` is settle time after the browser has started
            "--browsertime.post_startup_delay",
            # If we are on the extra profiler run, limit the startup delay to 1 second.
            (
                str(min(self.post_startup_delay, 1000))
                if extra_profiler_run
                else str(self.post_startup_delay)
            ),
            "--iterations",
            str(browser_cycles),
            # running browsertime test in chimera mode
            "--browsertime.chimera",
            "true" if self.config["chimera"] else "false",
            "--browsertime.test_bytecode_cache",
            "true" if self.config["test_bytecode_cache"] else "false",
            "--firefox.perfStats",
            test.get("perfstats", "false"),
            "--browsertime.moz_fetch_dir",
            os.environ.get("MOZ_FETCHES_DIR", "None"),
            "--browsertime.expose_profiler",
            (
                "true"
                if self._expose_browser_profiler(extra_profiler_run, test)
                else "false"
            ),
        ]

        # MOZ_LOG resolution order: explicit env var wins; else toml's
        # firefox_moz_log (verbatim modules); else the "network" preset
        # if the toml just sets firefox_log_modules = "network". The
        # collected log is written by Firefox to
        # browsertime-results/<test>/moz_log.txt.<pid>.
        moz_log_settings = os.environ.get("MOZ_LOG")
        if not moz_log_settings:
            moz_log_settings = test.get("firefox_moz_log") or None
        if not moz_log_settings:
            preset = (test.get("firefox_log_modules") or "").strip().lower()
            if preset == "network":
                # Enough to diagnose DNS/connect/TLS/HTTP hangs without
                # drowning the run; timestamp+append keeps it readable
                # across multiple browser_cycles.
                moz_log_settings = (
                    "timestamp,append,sync,"
                    "nsHostResolver:5,"
                    "nsSocketTransport:4,"
                    "nsHttp:3,"
                    "TRRIDLECONNECTION:5,"
                    "cookie:3"
                )
        if moz_log_settings:
            LOG.info(f"Passing MOZ_LOG settings to browsertime: {moz_log_settings}")
            browsertime_options.extend(["--firefox.collectMozLog", "true"])
            browsertime_options.extend(["--firefox.setMozLog", moz_log_settings])

        if test.get("perfstats") == "true":
            # Take a non-standard approach for perfstats as we
            # want to enable them everywhere shortly (bug 1770152)
            self.results_handler.perfstats = True

        if test.get("support_class", None):
            # Add a flag to denote when a test uses a support class.
            # Used to prevent running cpuTime/wallClock measurements
            # on tests that don't support it yet
            browsertime_options.extend(["--browsertime.support_class", "true"])

        if self.config["app"] in ("fenix",):
            # Fenix can take a lot of time to startup
            browsertime_options.extend([
                "--browsertime.browserRestartTries",
                "10",
                "--timeouts.browserStart",
                "180000",
            ])

        if test.get("secondary_url"):
            browsertime_options.extend([
                "--browsertime.secondary_url",
                test.get("secondary_url"),
            ])

        # These options can have multiple entries in a browsertime command
        MULTI_OPTS = [
            "--firefox.android.intentArgument",
            "--firefox.args",
            "--firefox.geckodriverArgs",
            "--firefox.preference",
            "--chrome.traceCategory",
        ]

        # In this code block we check if any priority 2 argument is in conflict with a priority
        # 3 arg if so we overwrite the value with the priority 2 argument, and otherwise we
        # simply add the priority 2 arg
        if test.get("browsertime_args", None):
            split_args = test.get("browsertime_args").strip().split()
            for split_arg in split_args:
                pairing = split_arg.split("=")
                if len(pairing) not in (1, 2):
                    raise Exception(
                        "One of the browsertime_args from the test was not split properly. "
                        f"Expecting a --flag, or a --option=value pairing. Found: {split_arg}"
                    )
                if pairing[0] in browsertime_options and pairing[0] not in MULTI_OPTS:
                    # If it's not a flag, then overwrite the existing value
                    if len(pairing) > 1:
                        ind = browsertime_options.index(pairing[0])
                        browsertime_options[ind + 1] = pairing[1]
                else:
                    browsertime_options.extend(pairing)

        priority1_options = self.browsertime_args
        if self.config["app"] in (
            "chrome",
            "chrome-m",
            "custom-car",
            "cstm-car-m",
        ):
            priority1_options.extend(self.setup_chrome_args(test))

        if self.debug_mode:
            browsertime_options.extend(["-vv", "--debug", "true"])

        if not extra_profiler_run:
            # must happen before --firefox.profileTemplate and --resultDir
            self.results_handler.remove_result_dir_for_test(test)
            priority1_options.extend([
                "--resultDir",
                self.results_handler.result_dir_for_test(test),
            ])
        else:
            priority1_options.extend([
                "--resultDir",
                self.results_handler.result_dir_for_test_profiling(test),
            ])
        if self.profile is not None:
            priority1_options.extend([
                "--firefox.profileTemplate",
                str(self.profile.profile),
            ])

        # This argument can have duplicates of the value "--firefox.env" so we do not need
        # to check if it conflicts
        for var, val in self.config.get("environment", {}).items():
            browsertime_options.extend(["--firefox.env", f"{var}={val}"])

        browsertime_options.extend(["--firefox.env", "MOZ_REMOTE_SETTINGS_DEVTOOLS=1"])

        # Parse the test commands (if any) from the test manifest
        cmds = evaluate_list_from_string(test.get("test_cmds", "[]"))
        parsed_cmds = [":::".join([str(i) for i in item]) for item in cmds if item]
        browsertime_options.extend(["--browsertime.commands", ";;;".join(parsed_cmds)])

        if self.verbose:
            browsertime_options.append("-vvv")

        if self.browsertime_video:
            priority1_options.extend([
                "--video",
                "true",
                "--visualMetrics",
                "true" if self.browsertime_visualmetrics else "false",
                "--visualMetricsContentful",
                "true",
                "--visualMetricsPerceptual",
                "true",
                "--visualMetricsPortable",
                "true",
                "--videoParams.keepOriginalVideo",
                "true",
            ])

            if self.browsertime_no_ffwindowrecorder or self.config["app"] in (
                "chrome-m",
                "chrome",
                "custom-car",
                "cstm-car-m",
            ):
                priority1_options.extend([
                    "--firefox.windowRecorder",
                    "false",
                    "--xvfbParams.display",
                    "0",
                ])
                LOG.info(
                    "Using adb screenrecord for mobile, or ffmpeg on desktop for videos"
                )
            else:
                priority1_options.extend([
                    "--firefox.windowRecorder",
                    "true",
                ])
                LOG.info("Using Firefox Window Recorder for videos")
        else:
            priority1_options.extend(["--video", "false", "--visualMetrics", "false"])

        if self.config["app"] in GECKO_PROFILER_APPS and (
            self.config["gecko_profile"] or extra_profiler_run
        ):
            self.config["browsertime_result_dir"] = (
                self.results_handler.result_dir_for_test(test)
            )
            self._compose_gecko_profiler_cmds(test, priority1_options)

        elif self.config["app"] in TRACE_APPS and extra_profiler_run:
            self.config["browsertime_result_dir"] = (
                self.results_handler.result_dir_for_test(test)
            )
            self._compose_chrome_trace_cmds(
                test, priority1_options, browsertime_options
            )

        # Add any user-specified flags here, let them override anything
        # with no restrictions
        for user_arg in self.browsertime_user_args:
            arg, val = user_arg.split("=", 1)
            if val.startswith("-"):
                priority1_options.extend([f"--{arg}={val}"])
            else:
                priority1_options.extend([f"--{arg}", val])

        # In this code block we check if any priority 1 arguments are in conflict with a
        # priority 2/3/4 argument
        for index, argument in list(enumerate(priority1_options)):
            if argument in MULTI_OPTS:
                browsertime_options.extend([argument, priority1_options[index + 1]])
            elif argument.startswith("--"):
                if index == len(priority1_options) - 1:
                    if argument not in browsertime_options:
                        browsertime_options.append(argument)
                else:
                    arg = priority1_options[index + 1]
                    try:
                        ind = browsertime_options.index(argument)
                        if not arg.startswith("--"):
                            browsertime_options[ind + 1] = arg
                    except ValueError:
                        res = [argument]
                        if not arg.startswith("--"):
                            res.append(arg)
                        browsertime_options.extend(res)
            else:
                continue

        # Finalize the `browsertime_options` before starting pageload tests
        if test.get("type") == "pageload":
            self._finalize_pageload_test_setup(
                browsertime_options=browsertime_options, test=test
            )

        cmd = (
            [self.browsertime_node, self.browsertime_browsertimejs]
            + self.driver_paths
            + [browsertime_script]
            + browsertime_options
        )

        if test.get("support_class", None):
            LOG.info("Test support class is modifying the command...")
            test.get("support_class").modify_command(cmd, test)

        return cmd

    def _compose_gecko_profiler_cmds(self, test, priority1_options):
        """Modify the command line options for running the gecko profiler
        in the Firefox application"""

        LOG.info("Composing Gecko Profiler commands")
        self._init_gecko_profiling(test)
        priority1_options.append("--firefox.geckoProfiler")
        if self._expose_browser_profiler(self.config.get("extra_profiler_run"), test):
            priority1_options.extend([
                "--firefox.geckoProfilerRecordingType",
                "custom",
            ])
        for option, browsertime_option, default in (
            (
                "gecko_profile_features",
                "--firefox.geckoProfilerParams.features",
                "js,stackwalk,screenshots,memory,java",
            ),
            (
                "gecko_profile_threads",
                "--firefox.geckoProfilerParams.threads",
                "GeckoMain,Compositor,Renderer",
            ),
            (
                "gecko_profile_interval",
                "--firefox.geckoProfilerParams.interval",
                None,
            ),
            (
                "gecko_profile_entries",
                "--firefox.geckoProfilerParams.bufferSize",
                str(128 * 1024 * 1024),  # 1GiB
            ),
        ):
            # 0 is a valid value. The setting may be present but set to None.
            value = self.config.get(option)
            if value is None:
                value = test.get(option)
            if value is None:
                value = default
            if option == "gecko_profile_threads":
                extra = self.config.get("gecko_profile_extra_threads", [])
                value = ",".join(value.split(",") + extra)
            if value is not None:
                priority1_options.extend([browsertime_option, str(value)])

    def _compose_chrome_trace_cmds(self, test, priority1_options, browsertime_options):
        """Modify the command line options for running a Trace on chrom* applications
        as defined by the TRACE_APPS variable"""

        LOG.info("Composing Chrome Trace commands")
        self._init_chrome_trace(test)

        priority1_options.extend(["--chrome.trace"])
        if self._expose_browser_profiler(self.config.get("extra_profiler_run"), test):
            priority1_options.extend(["--chrome.timelineRecordingType", "custom"])

        # current categories to capture, we can modify this as needed in the future
        # reference:
        # https://source.chromium.org/chromium/chromium/src/+/main:third_party/devtools-frontend/src/front_end/panels/timeline/TimelineController.ts;l=80-94;drc=6c0298a8ce155553c84b312a701298141bfc1330
        # https://github.com/sitespeedio/browsertime/blob/28bd484d31f51412b6e5e132f81749f65949b47c/lib/chrome/settings/traceCategories.js#L4
        trace_categories = [
            "disabled-by-default-devtools.timeline",
            "disabled-by-default-devtools.timeline.frame",
            "disabled-by-default-devtools.timeline.stack",
            "disabled-by-default-v8.compile",
            "disabled-by-default-v8.cpu_profiler.hires",
            "disabled-by-default-lighthouse",
            "disabled-by-default-v8.cpu_profiler",
        ]

        if "--chrome.traceCategory" not in browsertime_options:
            # if this option already exists presumably the user has
            # already specified the desired options So we can skip to
            # enabling screenshots before returning
            for cat in trace_categories:
                # add each tracing event we want to capture with browsertime
                priority1_options.extend(["--chrome.traceCategory", cat])
        priority1_options.extend(["--chrome.enableTraceScreenshots"])

    def _finalize_pageload_test_setup(self, browsertime_options, test):
        """This function finalizes remaining configurations for browsertime pageload tests.

        For pageload tests, ensure that the test name is available to the `browsertime_pageload.js`
        script. In addition, make the `live_sites` and `login` boolean available as these will be
        required in determining the flow of the login-logic. Finally, we disable the verbose mode
        as a safety precaution when doing a live login site.
        """
        browsertime_options.extend(["--browsertime.testName", str(test.get("name"))])
        browsertime_options.extend([
            "--browsertime.liveSite",
            str(self.config["live_sites"]),
        ])

        login_required = self.is_live_login_site(test.get("name"))
        browsertime_options.extend(["--browsertime.loginRequired", str(login_required)])

        # Turn off verbose if login logic is required and we are running on CI
        if (
            login_required
            and self.config.get("verbose", False)
            and os.environ.get("MOZ_AUTOMATION")
        ):
            LOG.info("Turning off verbose mode for login-logic.")
            LOG.info(
                "Please contact the perftest team if you need verbose mode enabled."
            )
            self.config["verbose"] = False
            for verbose_level in ("-v", "-vv", "-vvv", "-vvvv"):
                try:
                    browsertime_options.remove(verbose_level)
                except ValueError:
                    pass

    @staticmethod
    def is_live_login_site(test_name):
        """This function checks the login field of a live-site in the pageload_sites.json

        After reading in the json file, perform a brute force search for the matching test name
        and return the login field boolean
        """

        # That pathing is different on CI vs locally for the pageload_sites.json file
        if os.environ.get("MOZ_AUTOMATION"):
            PAGELOAD_SITES = os.path.join(
                os.getcwd(), "tests/raptor/browsertime/pageload_sites.json"
            )
        else:
            base_dir = os.path.dirname(os.path.dirname(os.getcwd()))
            pageload_subpath = "raptor/browsertime/pageload_sites.json"
            PAGELOAD_SITES = os.path.join(base_dir, pageload_subpath)

        with open(PAGELOAD_SITES) as f:
            pageload_data = json.load(f)

        desktop_sites = pageload_data["desktop"]
        for site in desktop_sites:
            if site["name"] == test_name:
                return site["login"]

        return False

    def _compute_process_timeout(self, test, timeout, cmd):
        if self.debug_mode:
            return sys.maxsize

        # bt_timeout will be the overall browsertime cmd/session timeout (seconds)
        # browsertime deals with page cycles internally, so we need to give it a timeout
        # value that includes all page cycles
        # pylint --py3k W1619
        bt_timeout = int(timeout / 1000) * int(test.get("page_cycles", 1))

        # the post-startup-delay is a delay after the browser has started, to let it settle
        # it's handled within browsertime itself by loading a 'preUrl' (about:blank) and having a
        # delay after that ('preURLDelay') as the post-startup-delay, so we must add that in sec
        # pylint --py3k W1619
        bt_timeout += int(self.post_startup_delay / 1000)

        # add some time for browser startup, time for the browsertime measurement code
        # to be injected/invoked, and for exceptions to bubble up; be generous
        bt_timeout += 20

        # When we produce videos, sometimes FFMPEG can take time to process
        # large folders of JPEG files into an MP4 file
        if self.browsertime_video:
            bt_timeout += 30

        # Visual metrics processing takes another extra 30 seconds
        if self.browsertime_visualmetrics:
            bt_timeout += 30

        # browsertime also handles restarting the browser/running all of the browser cycles;
        # so we need to multiply our bt_timeout by the number of browser cycles
        iterations = int(test.get("browser_cycles", 1))
        for i, entry in enumerate(cmd):
            if entry == "--iterations":
                try:
                    iterations = int(cmd[i + 1])
                    break
                except ValueError:
                    raise Exception(
                        f"Received a non-int value for the iterations: {cmd[i + 1]}"
                    )
        bt_timeout = bt_timeout * iterations

        if any([
            self.config.get("gecko_profile"),
            self.config.get("etw_profile") and self.etw_profiler,
            self.config.get("samply_profile") and self.samply_profiler,
            self.config.get("perf_profile") and self.perf_profiler,
            self.config.get("simpleperf"),
        ]):
            bt_timeout += 5 * 60

        return bt_timeout

    @staticmethod
    def _kill_browsertime_process(msg):
        """This method determines if a browsertime process should be killed.

        Examine the error message from the line handler to determine what to do by returning
        a boolean.

        In the future, we can extend this method to consider other scenarios.
        """

        # If we encounter an `xpath` & `double click` related error
        # message, it is due to a failure in the 2FA checks during the
        # login logic since not all websites have 2FA
        if "xpath" in msg and "double click" in msg:
            LOG.info("Ignoring 2FA error")
            return False

        return True

    def _fatal_setup_error(self, line):
        """Return instructions for a browsertime error that retrying cannot
        fix, or None."""
        if (
            self.config["app"] in ("safari", "safari-tp")
            and "Allow remote automation" in line
        ):
            return (
                "Safari does not allow remote automation, so it can't be controlled "
                "via WebDriver. Enable it in one of two ways, then run the test "
                "again:\n"
                "  1. Run: sudo safaridriver --enable\n"
                '  2. In Safari, open Settings > Advanced and check "Show features '
                'for web developers", then open Settings > Developer and check '
                '"Allow remote automation".'
            )
        return None

    def kill(self, proc):
        if "win" in self.config["platform"]:
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()

    def get_failure_screenshot(self):
        if not self.config.get("screenshot_on_failure"):
            return

        # Bug 1884178
        # Temporarily disable on Windows + Chrom* applications.
        if self.config["app"] in TRACE_APPS and "win" in self.config["platform"]:
            return
        if self.config["app"] in DESKTOP_APPS:
            from mozscreenshot import dump_screen

            obj_dir = os.environ.get("MOZ_DEVELOPER_OBJ_DIR", None)
            if obj_dir is None:
                build_dir = pathlib.Path(os.environ.get("MOZ_UPLOAD_DIR")).parent
                utility_path = pathlib.Path(build_dir, "tests", "bin")
            else:
                utility_path = os.path.join(obj_dir, "dist", "bin")
            dump_screen(utility_path, LOG)

        elif self.config["app"] in FIREFOX_ANDROID_APPS + CHROME_ANDROID_APPS:
            from mozdevice import ADBDeviceFactory
            from mozscreenshot import dump_device_screen

            device = ADBDeviceFactory(verbose=True)
            dump_device_screen(device, LOG)

    def run_extra_profiler_run(
        self, test, timeout, proc_timeout, output_timeout, line_handler, env
    ):
        self.timed_out = False
        self.output_timed_out = False

        def timeout_handler(proc):
            self.timed_out = True
            self.kill(proc)

        def output_timeout_handler(proc):
            self.output_timed_out = True
            self.kill(proc)

        try:
            LOG.info(
                "Running browsertime with the profiler enabled after the main run."
            )
            profiler_test = deepcopy(test)
            cmd = self._compose_cmd(profiler_test, timeout, True)
            LOG.info(f"browsertime profiling cmd: {' '.join([str(c) for c in cmd])}")
            mozprocess.run_and_wait(
                cmd,
                output_line_handler=line_handler,
                env=env,
                timeout=proc_timeout,
                timeout_handler=timeout_handler,
                output_timeout=output_timeout,
                output_timeout_handler=output_timeout_handler,
                text=False,
            )

            # Do not raise exception for the browsertime failure or timeout for this case.
            # Second profiler browsertime run is fallible.
            if self.output_timed_out:
                LOG.info(
                    "Browsertime process for extra profiler run timed out after "
                    f"waiting {output_timeout} seconds for output"
                )
            if self.timed_out:
                LOG.info(
                    "Browsertime process for extra profiler run timed out after "
                    f"{proc_timeout} seconds"
                )

            if self.browsertime_failure:
                LOG.info(
                    f"Browsertime for extra profiler run failure: {self.browsertime_failure}"
                )

        except Exception as e:
            LOG.info("Failed during the extra profiler run: " + str(e))

    def run_test(self, test, timeout):
        self.timed_out = False
        self.output_timed_out = False

        def timeout_handler(proc):
            self.timed_out = True
            self.kill(proc)

        def output_timeout_handler(proc):
            self.output_timed_out = True
            self.kill(proc)

        self.run_test_setup(test)

        # xperf profiling is currently only supported in CI as it requires
        # scheduled tasks that are currently only configured on the
        # CI machines.
        if self.config.get("etw_profile"):
            if not self.config.get("run_local"):
                self._init_etw_profiling(test)
            else:
                raise Exception("ETW profiling is not supported in local runs")

        if self.config.get("samply_profile"):
            self._init_samply_profiling(test)

        samply_started = False
        if self.config.get("samply_profile") and self.samply_profiler:
            try:
                samply_started = self.samply_profiler.start()
            except Exception as e:
                LOG.warning(f"Failed to start Samply profiling: {e}")
                self.samply_profiler = None

        if self.config.get("simpleperf"):
            self._init_simpleperf_profiling(test)

        if self.config.get("perf_profile"):
            self._init_perf_profiling(test)

        # timeout is a single page-load timeout value (ms) from the test INI
        # this will be used for btime --timeouts.pageLoad
        cmd = self._compose_cmd(test, timeout)

        output_timeout = BROWSERTIME_PAGELOAD_OUTPUT_TIMEOUT
        if test.get("type", "") == "scenario":
            # Change the timeout for scenarios since they
            # don't output much for a long period of time
            output_timeout = timeout
        elif test.get("type", "") == "benchmark":
            output_timeout = BROWSERTIME_BENCHMARK_OUTPUT_TIMEOUT

        if self.debug_mode:
            output_timeout = 2147483647

        LOG.info(f"timeout (ms): {timeout}")
        LOG.info(f"browsertime cwd: {os.getcwd()}")
        LOG.info(f"browsertime cmd: {' '.join([str(c) for c in cmd])}")
        if self.browsertime_video:
            LOG.info(f"browsertime_ffmpeg: {self.browsertime_ffmpeg}")

        # browsertime requires ffmpeg on the PATH for `--video=true`.
        # It's easier to configure the PATH here than at the TC level.
        env = dict(os.environ)
        env["PYTHON"] = sys.executable
        env["MINIDUMP_SAVE_PATH"] = str(self.crash_directory)
        env["MOZ_REMOTE_SETTINGS_DEVTOOLS"] = "1"
        env["MOZ_REMOTE_SETTINGS_DISABLED"] = "1"
        # Temporary workaround (bug 2083658): browsertime's bundled geckodriver
        # doesn't set this. Remove once browsertime is updated (bug 2083786).
        env["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS"] = "1"
        if test.get("support_class", None):
            test.get("support_class").modify_env(env, test)
        if self.browsertime_video and self.browsertime_ffmpeg:
            ffmpeg_dir = os.path.dirname(os.path.abspath(self.browsertime_ffmpeg))
            old_path = env.setdefault("PATH", "")
            new_path = os.pathsep.join([ffmpeg_dir, old_path])
            env["PATH"] = new_path

        # Used to enable usage of browsertime packages within user scripts
        if self.config["run_local"]:
            env["BROWSERTIME_ROOT"] = str(
                pathlib.Path(
                    os.environ["MOZ_DEVELOPER_REPO_DIR"], "tools", "browsertime"
                )
            )
        else:
            env["BROWSERTIME_ROOT"] = str(
                pathlib.Path(os.environ["MOZ_FETCHES_DIR"], "browsertime")
            )

        LOG.info(f"PATH: {env['PATH']}")

        # Tee browsertime's raw stdout/stderr to a persistent log next to
        # mitmproxy.log so post-mortem debugging of CI failures doesn't
        # rely on the harness stdout stream being preserved. Opened in
        # append mode with a per-test header so multi-test runs aren't
        # destructive.
        browsertime_log_path = os.path.join(
            os.environ.get("MOZ_UPLOAD_DIR", os.getcwd()), "browsertime.log"
        )
        try:
            browsertime_log_fp = open(browsertime_log_path, "ab", buffering=0)
        except OSError as e:
            LOG.warning(
                f"Could not open browsertime stderr log {browsertime_log_path}: {e}"
            )
            browsertime_log_fp = None
        else:
            LOG.info(f"browsertime log file: {browsertime_log_path}")
            header = (
                f"\n===== {test.get('name', '?')} | {datetime.datetime.utcnow().isoformat()} =====\n"
            ).encode()
            try:
                browsertime_log_fp.write(header)
            except Exception:
                pass

        # Start ETW profiling if enabled before browsertime starts
        etw_started = False
        if self.config.get("etw_profile") and self.etw_profiler:
            try:
                etw_started = self.etw_profiler.start()
            except Exception as e:
                LOG.warning(f"Failed to start ETW profiling: {e}")
                self.etw_profiler = None  # Disable profiler to skip stop() later

        perf_started = False
        if self.config.get("perf_profile") and self.perf_profiler:
            try:
                perf_started = self.perf_profiler.start()
            except Exception as e:
                LOG.warning(f"Failed to start perf profiling: {e}")
                self.perf_profiler = None

        browsertime_test_failed = False
        try:
            line_matcher = re.compile(r".*(\[.*\])\s+([a-zA-Z]+):\s+(.*)")

            def _create_line_handler(extra_profiler_run=False):
                def _line_handler(proc, line):
                    """This function acts as a bridge between browsertime
                    and raptor. It reforms the lines to get rid of information
                    that is not needed, and outputs them appropriately based
                    on the level that is found. (Debug and info all go to info).

                    For errors, we set an attribute (self.browsertime_failure) to
                    it, then raise a generic exception. When we return, we check
                    if self.browsertime_failure, and raise an Exception if necessary
                    to stop Raptor execution (preventing the results processing).
                    """

                    # Tee the raw line to disk first, before any rewriting
                    # or LOG-level filtering, so the artifact captures
                    # exactly what browsertime emitted.
                    if browsertime_log_fp is not None:
                        try:
                            browsertime_log_fp.write(line)
                            if not line.endswith(b"\n"):
                                browsertime_log_fp.write(b"\n")
                        except Exception:
                            pass

                    # NOTE: this hack is to workaround encoding issues on windows
                    # a newer version of browsertime adds a `σ` character to output
                    line = line.replace(b"\xcf\x83", b"")

                    line = line.decode("utf-8")
                    fatal_setup_error = self._fatal_setup_error(line)
                    if fatal_setup_error:
                        LOG.info(line)
                        if not self.browsertime_fatal_setup_error:
                            self.browsertime_fatal_setup_error = fatal_setup_error
                            proc.kill()
                        return
                    match = line_matcher.match(line)
                    if not match:
                        LOG.info(line)
                        return

                    date, level, msg = match.groups()
                    level = level.lower()
                    if "error" in level and not self.debug_mode:
                        if self._kill_browsertime_process(msg):
                            self.browsertime_failure = msg
                            if extra_profiler_run:
                                # Do not trigger the log parser for extra profiler run.
                                LOG.info(
                                    "Browsertime failed to run on extra profiler run"
                                )
                            else:
                                LOG.error("Browsertime failed to run")
                            proc.kill()
                    elif "warning" in level:
                        if extra_profiler_run:
                            # Do not trigger the log parser for extra profiler run.
                            LOG.info(msg)
                        else:
                            LOG.warning(msg)
                    elif "metrics" in level:
                        vals = msg.split(":")[-1].strip()
                        self.page_count = vals.split(",")
                    else:
                        LOG.info(msg)

                return _line_handler

            proc_timeout = self._compute_process_timeout(test, timeout, cmd)
            output_timeout = BROWSERTIME_PAGELOAD_OUTPUT_TIMEOUT
            if test.get("type", "") == "benchmark":
                output_timeout = BROWSERTIME_BENCHMARK_OUTPUT_TIMEOUT
            elif test.get("output_timeout", None) is not None:
                output_timeout = int(test.get("output_timeout"))
                proc_timeout = max(proc_timeout, output_timeout)

            # Double the timeouts on live sites and when running with Fenix
            if self.config["live_sites"] or self.config["app"] in ("fenix",):
                # Since output_timeout is None for benchmark tests we should
                # not perform any operations on it.
                if output_timeout is not None:
                    output_timeout *= 2
                proc_timeout *= 2

            LOG.info(
                f"Calling browsertime with proc_timeout={proc_timeout}, "
                f"and output_timeout={output_timeout}"
            )

            if self.config["power_test"]:
                if not self.config["run_local"]:
                    env["USB_POWER_METER_SERIAL_NUMBER"] = os.environ.get(
                        "USB_POWER_METER_SERIAL_NUMBER", ""
                    )

                if test.get("type") == "benchmark":
                    cmd.extend(["--browsertime.power_test", "true"])
                else:
                    cmd.extend(["--android.usbPowerTesting", "true"])

            is_browsertime_process_completed = False
            browsertime_process_attempt_count = 1

            while (
                not is_browsertime_process_completed
                and browsertime_process_attempt_count <= BROWSERTIME_MAX_ATTEMPTS
            ):
                LOG.info(
                    f"Running browsertime process, attempt {browsertime_process_attempt_count}/{BROWSERTIME_MAX_ATTEMPTS}"
                )
                self.browsertime_failure = ""
                browsertime_mozprocess = mozprocess.run_and_wait(
                    cmd,
                    output_line_handler=_create_line_handler(),
                    env=env,
                    timeout=proc_timeout,
                    timeout_handler=timeout_handler,
                    output_timeout=output_timeout,
                    output_timeout_handler=output_timeout_handler,
                    text=False,
                )

                if self.output_timed_out:
                    self.get_failure_screenshot()
                    raise Exception(
                        f"Browsertime process timed out after waiting {output_timeout} seconds "
                        "for output"
                    )
                if self.timed_out:
                    self.get_failure_screenshot()
                    raise Exception(
                        f"Browsertime process timed out after {proc_timeout} seconds"
                    )

                if self.browsertime_failure:
                    self.get_failure_screenshot()

                if self.browsertime_fatal_setup_error:
                    raise Exception(self.browsertime_fatal_setup_error)

                if browsertime_mozprocess.returncode != 0:
                    browsertime_process_attempt_count += 1

                    if browsertime_process_attempt_count <= BROWSERTIME_MAX_ATTEMPTS:
                        self.browsertime_failure = f"Browsertime process exited with code {browsertime_mozprocess.returncode}"
                        LOG.warning(
                            f"Browsertime process exited with code "
                            f"{browsertime_mozprocess.returncode}, retrying..."
                        )
                        continue
                    else:
                        self.get_failure_screenshot()
                        raise Exception(self.browsertime_failure)

                else:
                    is_browsertime_process_completed = True
                    LOG.info("Browsertime process completed successfully")
                    break

            # We've run the main browsertime process, now we need to run the
            # browsertime one more time if the profiler wasn't enabled already
            # in the previous run and user wants this extra run.
            if (
                self.config.get("extra_profiler_run")
                and not self.config["gecko_profile"]
            ):
                self.run_extra_profiler_run(
                    test,
                    timeout,
                    proc_timeout,
                    output_timeout,
                    _create_line_handler(extra_profiler_run=True),
                    env,
                )

        except Exception as e:
            browsertime_test_failed = True
            LOG.critical(str(e))
            raise

        finally:
            if browsertime_log_fp is not None:
                try:
                    browsertime_log_fp.close()
                except Exception:
                    pass

            profile_to_view = None
            is_local = self.config.get("run_local")

            if etw_started and self.etw_profiler:
                try:
                    self.etw_profiler.stop()
                    # Additionally upload kernel and user ETL files
                    # for debugging if test fails
                    self.etw_profiler.upload_etl(debug=browsertime_test_failed)
                    self.etw_profiler.symbolicate()
                    self.etw_profiler.post_process_profiles()
                    self.etw_profiler.clean()
                    etw_started = False
                    LOG.info("ETW profiling has completed successfully")
                except Exception as e:
                    LOG.error(f"Failed to finalize ETW profiling: {e}")

            if samply_started and self.samply_profiler:
                try:
                    self.samply_profiler.stop()
                    processed_profiles = self.samply_profiler.post_process_profiles()
                    if is_local:
                        profile_to_view = self.samply_profiler.archive(
                            processed_profiles
                        )
                    self.samply_profiler.clean()
                    LOG.info("Samply profiling has completed successfully")
                except Exception as e:
                    LOG.error(f"Failed to finalize Samply profiling: {e}")

            if perf_started and self.perf_profiler:
                try:
                    if not self.perf_profiler.stop():
                        raise RuntimeError("perf recording did not complete")
                    self.perf_profiler.symbolicate()
                    processed_profiles = self.perf_profiler.post_process_profiles()
                    if is_local:
                        profile_to_view = self.perf_profiler.archive(processed_profiles)
                    LOG.info("Perf profiling has completed successfully")
                except Exception as e:
                    LOG.error(f"Failed to stop perf profiling: {e}")
                finally:
                    self.perf_profiler.clean()
                    perf_started = False

            if is_local and profile_to_view and profile_to_view.exists():
                # Enable profile viewing (view_gecko_profile_from_raptor())
                os.environ["RAPTOR_LATEST_PROFILE"] = str(profile_to_view)

            # Serve profile before propagating the exception
            if (
                browsertime_test_failed
                and profile_to_view
                and profile_to_view.exists()
                and is_local
                and os.environ.get("DISABLE_PROFILE_LAUNCH", "0") != "1"
            ):
                try:
                    LOG.info("Viewing failure profile")
                    os.environ["RAPTOR_LATEST_PROFILE"] = str(profile_to_view)
                    view_gecko_profile_from_raptor()
                except Exception as e:
                    LOG.error(f"Failed to view {profile_to_view}: {e}")
