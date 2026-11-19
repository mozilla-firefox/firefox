#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import copy
import glob
import multiprocessing
import os
import pathlib
import plistlib
import re
import subprocess
import sys
from shutil import copyfile, rmtree

from mozsystemmonitor.resourcemonitor import SystemResourceMonitor

import mozharness
from mozharness.base.errors import PythonErrorList
from mozharness.base.log import CRITICAL, DEBUG, ERROR, INFO, OutputParser
from mozharness.base.python import perfherder_schema_path
from mozharness.base.vcs.vcsbase import MercurialScript
from mozharness.mozilla.automation import (
    EXIT_STATUS_DICT,
    TBPL_RETRY,
    TBPL_SUCCESS,
    TBPL_WORST_LEVEL_TUPLE,
)
from mozharness.mozilla.testing.android import AndroidMixin
from mozharness.mozilla.testing.codecoverage import (
    CodeCoverageMixin,
    code_coverage_config_options,
)
from mozharness.mozilla.testing.errors import HarnessErrorList, TinderBoxPrintRe
from mozharness.mozilla.testing.testbase import TestingMixin, testing_config_options

scripts_path = os.path.abspath(os.path.dirname(os.path.dirname(mozharness.__file__)))
external_tools_path = os.path.join(scripts_path, "external_tools")
here = os.path.abspath(os.path.dirname(__file__))

RaptorErrorList = (
    PythonErrorList
    + HarnessErrorList
    + [
        {"regex": re.compile(r"""run-as: Package '.*' is unknown"""), "level": DEBUG},
        {"substr": r"""raptorDebug""", "level": DEBUG},
        {
            "regex": re.compile(r"""(?i)^raptor[a-z-]*( - )?( )?error(:)?"""),
            "level": ERROR,
        },
        {
            "regex": re.compile(r"""(?i)^raptor[a-z-]*( - )?( )?critical(:)?"""),
            "level": CRITICAL,
        },
        {
            "regex": re.compile(r"""No machine_name called '.*' can be found"""),
            "level": CRITICAL,
        },
        {
            "substr": r"""No such file or directory: 'browser_output.txt'""",
            "level": CRITICAL,
            "explanation": "Most likely the browser failed to launch, or the test otherwise "
            "failed to start.",
        },
    ]
)

# When running raptor locally, we can attempt to make use of
# the users locally cached ffmpeg binary from from when the user
# ran `./mach browsertime --setup`
FFMPEG_LOCAL_CACHE = {
    "mac": "ffmpeg-7.1",
    "linux": "ffmpeg-master-latest-linux64-gpl-shared",
    "win": "ffmpeg-n7.1-latest-win64-gpl-shared-7.1",
}


class Raptor(TestingMixin, MercurialScript, CodeCoverageMixin, AndroidMixin):
    """
    Install and run Raptor tests
    """

    # Options to Browsertime.  Paths are expected to be absolute.
    browsertime_options = [
        [
            ["--browsertime-node"],
            {"dest": "browsertime_node", "default": None, "help": argparse.SUPPRESS},
        ],
        [
            ["--browsertime-browsertimejs"],
            {
                "dest": "browsertime_browsertimejs",
                "default": None,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-vismet-script"],
            {
                "dest": "browsertime_vismet_script",
                "default": None,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-chromedriver"],
            {
                "dest": "browsertime_chromedriver",
                "default": None,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-ffmpeg"],
            {"dest": "browsertime_ffmpeg", "default": None, "help": argparse.SUPPRESS},
        ],
        [
            ["--browsertime-geckodriver"],
            {
                "dest": "browsertime_geckodriver",
                "default": None,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-video"],
            {
                "dest": "browsertime_video",
                "action": "store_true",
                "default": False,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-visualmetrics"],
            {
                "dest": "browsertime_visualmetrics",
                "action": "store_true",
                "default": False,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-no-ffwindowrecorder"],
            {
                "dest": "browsertime_no_ffwindowrecorder",
                "action": "store_true",
                "default": False,
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime-arg"],
            {
                "action": "append",
                "metavar": "PREF=VALUE",
                "dest": "browsertime_user_args",
                "default": [],
                "help": argparse.SUPPRESS,
            },
        ],
        [
            ["--browsertime"],
            {
                "dest": "browsertime",
                "action": "store_true",
                "default": True,
                "help": argparse.SUPPRESS,
            },
        ],
    ]

    config_options = (
        [
            [
                ["--test"],
                {"action": "store", "dest": "test", "help": "Raptor test to run"},
            ],
            [
                ["--power-profile"],
                {
                    "action": "store",
                    "choices": ["performance"],
                    "dest": "power_profile",
                    "help": "Set the host power-profiles-daemon profile to performance "
                    "for the duration of the Raptor run.",
                },
            ],
            [
                ["--app"],
                {
                    "default": "firefox",
                    "choices": [
                        "firefox",
                        "chrome",
                        "chrome-m",
                        "fennec",
                        "geckoview",
                        "refbrow",
                        "fenix",
                        "safari",
                        "safari-tp",
                        "custom-car",
                        "cstm-car-m",
                    ],
                    "dest": "app",
                    "help": "Name of the application we are testing (default: firefox).",
                },
            ],
            [
                ["--activity"],
                {
                    "dest": "activity",
                    "help": "The Android activity used to launch the Android app. "
                    "e.g.: org.mozilla.fenix.browser.BrowserPerformanceTestActivity",
                },
            ],
            [
                ["--intent"],
                {
                    "dest": "intent",
                    "help": "Name of the Android intent action used to launch the Android app",
                },
            ],
            [
                ["--is-release-build"],
                {
                    "action": "store_true",
                    "dest": "is_release_build",
                    "help": "Whether the build is a release build which requires work arounds "
                    "using MOZ_DISABLE_NONLOCAL_CONNECTIONS to support installing unsigned "
                    "webextensions. Defaults to False.",
                },
            ],
            [
                ["--add-option"],
                {
                    "action": "extend",
                    "dest": "raptor_cmd_line_args",
                    "default": None,
                    "help": "Extra options to Raptor.",
                },
            ],
            [
                ["--device-name"],
                {
                    "dest": "device_name",
                    "default": None,
                    "help": "Device name of mobile device.",
                },
            ],
            [
                ["--geckoProfile"],
                {
                    "dest": "gecko_profile",
                    "action": "store_true",
                    "default": False,
                    "help": argparse.SUPPRESS,
                },
            ],
            [
                ["--geckoProfileInterval"],
                {
                    "dest": "gecko_profile_interval",
                    "type": "int",
                    "help": argparse.SUPPRESS,
                },
            ],
            [
                ["--geckoProfileEntries"],
                {
                    "dest": "gecko_profile_entries",
                    "type": "int",
                    "help": argparse.SUPPRESS,
                },
            ],
            [
                ["--geckoProfileFeatures"],
                {
                    "dest": "gecko_profile_features",
                    "type": "str",
                    "help": argparse.SUPPRESS,
                },
            ],
            [
                ["--gecko-profile"],
                {
                    "dest": "gecko_profile",
                    "action": "store_true",
                    "default": False,
                    "help": (
                        "Whether to profile the test run and save the profile results. "
                        "Copy paste the parameters used in this profiling run directly from about:profiling in Nightly."
                    ),
                },
            ],
            [
                ["--gecko-profile-interval"],
                {
                    "dest": "gecko_profile_interval",
                    "type": "float",
                    "help": "The interval between samples taken by the profiler (ms).",
                },
            ],
            [
                ["--gecko-profile-entries"],
                {
                    "dest": "gecko_profile_entries",
                    "type": "int",
                    "help": "How many samples to take with the profiler.",
                },
            ],
            [
                ["--gecko-profile-threads"],
                {
                    "dest": "gecko_profile_threads",
                    "type": "str",
                    "help": "Comma-separated list of threads to sample.",
                },
            ],
            [
                ["--gecko-profile-features"],
                {
                    "dest": "gecko_profile_features",
                    "type": "str",
                    "help": "Features to enable in the profiler.",
                },
            ],
            [
                ["--extra-profiler-run"],
                {
                    "dest": "extra_profiler_run",
                    "action": "store_true",
                    "default": False,
                    "help": "Run the tests again with profiler enabled after the main run.",
                },
            ],
            [
                ["--page-cycles"],
                {
                    "dest": "page_cycles",
                    "type": "int",
                    "help": (
                        "How many times to repeat loading the test page (for page load "
                        "tests); for benchmark tests this is how many times the benchmark test "
                        "will be run."
                    ),
                },
            ],
            [
                ["--page-timeout"],
                {
                    "dest": "page_timeout",
                    "type": "int",
                    "help": "How long to wait (ms) for one page_cycle to complete, before timing out.",  # NOQA: E501
                },
            ],
            [
                ["--browser-cycles"],
                {
                    "dest": "browser_cycles",
                    "type": "int",
                    "help": (
                        "The number of times a cold load test is repeated (for cold load tests "
                        "only, where the browser is shutdown and restarted between test "
                        "iterations)."
                    ),
                },
            ],
            [
                ["--post-startup-delay"],
                {
                    "dest": "post_startup_delay",
                    "type": "int",
                    "help": (
                        "How long to wait (ms) after browser start-up before "
                        "starting the tests."
                    ),
                },
            ],
            [
                ["--project"],
                {
                    "action": "store",
                    "dest": "project",
                    "default": "mozilla-central",
                    "type": "str",
                    "help": "Name of the project (try, mozilla-central, etc.)",
                },
            ],
            [
                ["--test-url-params"],
                {
                    "action": "store",
                    "dest": "test_url_params",
                    "help": "Parameters to add to the test_url query string.",
                },
            ],
            [
                ["--host"],
                {
                    "dest": "host",
                    "type": "str",
                    "default": "127.0.0.1",
                    "help": "Hostname from which to serve urls (default: 127.0.0.1). "
                    "The value HOST_IP will cause the value of host to be "
                    "to be loaded from the environment variable HOST_IP.",
                },
            ],
            [
                ["--disable-perf-tuning"],
                {
                    "action": "store_true",
                    "dest": "disable_perf_tuning",
                    "default": False,
                    "help": "Disable performance tuning on android.",
                },
            ],
            [
                ["--conditioned-profile"],
                {
                    "dest": "conditioned_profile",
                    "type": "str",
                    "default": None,
                    "help": (
                        "Name of conditioned profile to use. Prefix with `artifact:` "
                        "if we should obtain the profile from CI.",
                    ),
                },
            ],
            [
                ["--live-sites"],
                {
                    "dest": "live_sites",
                    "action": "store_true",
                    "default": False,
                    "help": "Run tests using live sites instead of recorded sites.",
                },
            ],
            [
                ["--test-bytecode-cache"],
                {
                    "dest": "test_bytecode_cache",
                    "action": "store_true",
                    "default": False,
                    "help": (
                        "If set, the pageload test will set the preference "
                        "`dom.script_loader.bytecode_cache.strategy=-1` and wait 20 seconds "
                        "after the first cold pageload to populate the bytecode cache before "
                        "running a warm pageload test. Only available if `--chimera` "
                        "is also provided."
                    ),
                },
            ],
            [
                ["--chimera"],
                {
                    "dest": "chimera",
                    "action": "store_true",
                    "default": False,
                    "help": "Run tests in chimera mode. Each browser cycle will run a cold and warm test.",  # NOQA: E501
                },
            ],
            [
                ["--debug-mode"],
                {
                    "dest": "debug_mode",
                    "action": "store_true",
                    "default": False,
                    "help": "Run Raptor in debug mode (open browser console, limited page-cycles, etc.)",  # NOQA: E501
                },
            ],
            [
                ["--disable-e10s"],
                {
                    "dest": "e10s",
                    "action": "store_false",
                    "default": True,
                    "help": "Run without multiple processes (e10s).",
                },
            ],
            [
                ["--disable-fission"],
                {
                    "action": "store_false",
                    "dest": "fission",
                    "default": True,
                    "help": "Disable Fission (site isolation) in Gecko.",
                },
            ],
            [
                ["--setpref"],
                {
                    "action": "append",
                    "metavar": "PREF=VALUE",
                    "dest": "extra_prefs",
                    "default": [],
                    "help": "Set a browser preference. May be used multiple times.",
                },
            ],
            [
                ["--setenv"],
                {
                    "action": "append",
                    "metavar": "NAME=VALUE",
                    "dest": "environment",
                    "default": [],
                    "help": "Set a variable in the test environment. May be used multiple times.",
                },
            ],
            [
                ["--skip-preflight"],
                {
                    "action": "store_true",
                    "dest": "skip_preflight",
                    "default": False,
                    "help": "skip preflight commands to prepare machine.",
                },
            ],
            [
                ["--cold"],
                {
                    "action": "store_true",
                    "dest": "cold",
                    "default": False,
                    "help": "Enable cold page-load for browsertime tp6",
                },
            ],
            [
                ["--verbose"],
                {
                    "action": "store_true",
                    "dest": "verbose",
                    "default": False,
                    "help": "Verbose output",
                },
            ],
            [
                ["--enable-marionette-trace"],
                {
                    "action": "store_true",
                    "dest": "enable_marionette_trace",
                    "default": False,
                    "help": "Enable marionette tracing",
                },
            ],
            [
                ["--clean"],
                {
                    "action": "store_true",
                    "dest": "clean",
                    "default": False,
                    "help": (
                        "Clean the python virtualenv (remove, and rebuild) for "
                        "Raptor before running tests."
                    ),
                },
            ],
            [
                ["--collect-perfstats"],
                {
                    "action": "store_true",
                    "dest": "collect_perfstats",
                    "default": False,
                    "help": (
                        "If set, the test will collect perfstats in addition to "
                        "the regular metrics it gathers."
                    ),
                },
            ],
            [
                ["--simpleperf"],
                {
                    "action": "store_true",
                    "dest": "simpleperf",
                    "default": False,
                    "help": ("Enable Simpleperf profiling (Android only)."),
                },
            ],
            [
                ["--etw-profile"],
                {
                    "action": "store_true",
                    "dest": "etw_profile",
                    "default": False,
                    "help": ("Enable ETW (Xperf) profiling (Windows only)."),
                },
            ],
            [
                ["--samply-profile"],
                {
                    "action": "store_true",
                    "dest": "samply_profile",
                    "default": False,
                    "help": ("Enable Samply profiling (macOS only)."),
                },
            ],
            [
                ["--perf-profile"],
                {
                    "action": "store_true",
                    "dest": "perf_profile",
                    "default": False,
                    "help": ("Enable perf profiling (Linux only)."),
                },
            ],
            [
                ["--extra-summary-methods"],
                {
                    "action": "append",
                    "metavar": "OPTION",
                    "dest": "extra_summary_methods",
                    "default": [],
                    "help": (
                        "Alternative methods for summarizing technical and visual"
                        "pageload metrics."
                        "Options: median."
                    ),
                },
            ],
            [
                ["--benchmark-repository"],
                {
                    "dest": "benchmark_repository",
                    "type": "str",
                    "default": None,
                    "help": (
                        "Repository that should be used for a particular benchmark test. "
                        "e.g. https://github.com/mozilla-mobile/firefox-android"
                    ),
                },
            ],
            [
                ["--benchmark-revision"],
                {
                    "dest": "benchmark_revision",
                    "type": "str",
                    "default": None,
                    "help": (
                        "Repository revision that should be used for a particular "
                        "benchmark test."
                    ),
                },
            ],
            [
                ["--benchmark-branch"],
                {
                    "dest": "benchmark_branch",
                    "type": "str",
                    "default": None,
                    "help": (
                        "Repository branch that should be used for a particular benchmark test."
                    ),
                },
            ],
            [
                ["--screenshot-on-failure"],
                {
                    "action": "store_true",
                    "dest": "screenshot_on_failure",
                    "default": False,
                    "help": "Take a screenshot when the test fails.",
                },
            ],
            [
                ["--power-test"],
                {
                    "action": "store_true",
                    "dest": "power_test",
                    "default": False,
                    "help": "Run power usage testing on mobile tests using a USB power meter.",
                },
            ],
            [
                ["--install-extension"],
                {
                    "action": "append",
                    "dest": "install_extensions",
                    "default": [],
                    "help": "Install a webextension into the test profile before "
                    "running. Accepts an AMO addon GUID/slug, a direct .xpi URL, or a "
                    "local .xpi path. May be specified multiple times.",
                },
            ],
        ]
        + testing_config_options
        + copy.deepcopy(code_coverage_config_options)
        + browsertime_options
    )

    def __init__(self, **kwargs):
        kwargs.setdefault("config_options", self.config_options)
        kwargs.setdefault(
            "all_actions",
            [
                "clobber",
                "download-and-extract",
                "populate-webroot",
                "create-virtualenv",
                "start-emulator",
                "verify-device",
                "install-chrome-android",
                "install-chromium-android",
                "install-chromium-distribution",
                "install-safari-technology-preview",
                "install",
                "run-tests",
            ],
        )
        kwargs.setdefault(
            "default_actions",
            [
                "clobber",
                "download-and-extract",
                "populate-webroot",
                "create-virtualenv",
                "install-chromium-distribution",
                "install",
                "run-tests",
            ],
        )
        kwargs.setdefault("config", {})
        super().__init__(**kwargs)

        if not self.device_serial and self.config.get("device_serial"):
            self.device_serial = self.config["device_serial"]

        # Convenience
        self.workdir = self.query_abs_dirs()["abs_work_dir"]

        self.run_local = self.config.get("run_local")

        # App (browser testing on) defaults to firefox
        self.app = "firefox"

        if self.run_local:
            # Get app from command-line args, passed in from mach, inside 'raptor_cmd_line_args'
            # Command-line args can be in two formats depending on how the user entered them
            # i.e. "--app=geckoview" or separate as "--app", "geckoview" so we have to
            # parse carefully.  It's simplest to use `argparse` to parse partially.
            self.app = "firefox"
            if "raptor_cmd_line_args" in self.config:
                sub_parser = argparse.ArgumentParser()
                # It's not necessary to limit the allowed values: each value
                # will be parsed and verifed by raptor/raptor.py.
                sub_parser.add_argument("--app", default=None, dest="app")
                sub_parser.add_argument("-i", "--intent", default=None, dest="intent")
                sub_parser.add_argument(
                    "-a", "--activity", default=None, dest="activity"
                )

                # We'd prefer to use `parse_known_intermixed_args`, but that's
                # new in Python 3.7.
                known, unknown = sub_parser.parse_known_args(
                    self.config["raptor_cmd_line_args"]
                )

                if known.app:
                    self.app = known.app
                if known.intent:
                    self.intent = known.intent
                if known.activity:
                    self.activity = known.activity
        else:
            # Raptor initiated in production via mozharness
            self.test = self.config["test"]
            self.app = self.config.get("app", "firefox")
            self.binary_path = self.config.get("binary_path", None)

            if self.app in ("refbrow", "fenix"):
                self.app_name = self.binary_path

        self.installer_url = self.config.get("installer_url")
        self.raptor_json_url = self.config.get("raptor_json_url")
        self.raptor_json = self.config.get("raptor_json")
        self.raptor_json_config = self.config.get("raptor_json_config")
        self.repo_path = self.config.get("repo_path")
        self.obj_path = self.config.get("obj_path")
        self.mozbuild_path = self.config.get("mozbuild_path")
        self.test = None
        self.gecko_profile = self.config.get(
            "gecko_profile"
        ) or "--geckoProfile" in self.config.get("raptor_cmd_line_args", [])
        self.gecko_profile_interval = self.config.get("gecko_profile_interval")
        self.gecko_profile_entries = self.config.get("gecko_profile_entries")
        self.gecko_profile_threads = self.config.get("gecko_profile_threads")
        self.gecko_profile_features = self.config.get("gecko_profile_features")
        self.extra_profiler_run = self.config.get("extra_profiler_run")
        self.test_packages_url = self.config.get("test_packages_url")
        self.test_url_params = self.config.get("test_url_params")
        self.host = self.config.get("host")
        if self.host == "HOST_IP":
            self.host = os.environ["HOST_IP"]
        self.live_sites = self.config.get("live_sites")
        self.chimera = self.config.get("chimera")
        self.disable_perf_tuning = self.config.get("disable_perf_tuning")
        self.conditioned_profile = self.config.get("conditioned_profile")
        self.extra_prefs = self.config.get("extra_prefs")
        self.environment = self.config.get("environment")
        self.is_release_build = self.config.get("is_release_build")
        self.debug_mode = self.config.get("debug_mode", False)
        self.chromium_dist_path = None
        self.firefox_android_browsers = ["fennec", "geckoview", "refbrow", "fenix"]
        self.android_browsers = self.firefox_android_browsers + [
            "chrome-m",
            "cstm-car-m",
        ]
        self.browsertime_visualmetrics = self.config.get("browsertime_visualmetrics")
        self.browsertime_node = self.config.get("browsertime_node")
        self.browsertime_user_args = self.config.get("browsertime_user_args")
        self.browsertime_video = False
        self.enable_marionette_trace = self.config.get("enable_marionette_trace")
        self.browser_cycles = self.config.get("browser_cycles")
        self.clean = self.config.get("clean")
        self.page_timeout = self.config.get("page_timeout", None)
        self.screenshot_on_failure = self.config.get("screenshot_on_failure")

        for (arg,), details in Raptor.browsertime_options:
            # Allow overriding defaults on the `./mach raptor-test ...` command-line.
            value = self.config.get(details["dest"])
            if value and arg not in self.config.get("raptor_cmd_line_args", []):
                setattr(self, details["dest"], value)

    # We accept some configuration options from the try commit message in the
    # format mozharness: <options>. Example try commit message: mozharness:
    # --geckoProfile try: <stuff>
    def query_gecko_profile_options(self):
        gecko_results = []
        # If gecko_profile is set, we add that to Raptor's options
        if self.gecko_profile:
            gecko_results.append("--gecko-profile")
            if self.gecko_profile_interval:
                gecko_results.extend([
                    "--gecko-profile-interval",
                    str(self.gecko_profile_interval),
                ])
            if self.gecko_profile_entries:
                gecko_results.extend([
                    "--gecko-profile-entries",
                    str(self.gecko_profile_entries),
                ])
            if self.gecko_profile_features:
                gecko_results.extend([
                    "--gecko-profile-features",
                    self.gecko_profile_features,
                ])
            if self.gecko_profile_threads:
                gecko_results.extend([
                    "--gecko-profile-threads",
                    self.gecko_profile_threads,
                ])
        elif self.extra_profiler_run:
            gecko_results.append("--extra-profiler-run")
        return gecko_results

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super().query_abs_dirs()
        abs_dirs["abs_blob_upload_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "blobber_upload_dir"
        )
        abs_dirs["abs_test_install_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "tests"
        )

        # When running on an emulator, AndroidMixin.start_emulator / adb_path
        # expect the SDK and AVD directories in abs_dirs; these are not part of
        # the default raptor (hardware) layout, so derive them from the fetches.
        if self.is_emulator:
            work_dir = os.environ.get("MOZ_FETCHES_DIR") or abs_dirs["abs_work_dir"]
            abs_dirs["abs_sdk_dir"] = os.path.join(
                work_dir, self.config.get("sdk_dir_name", "android-sdk-linux")
            )
            abs_dirs["abs_avds_dir"] = os.path.join(
                work_dir, self.config.get("avds_dir_name", "android-device")
            )

        self.abs_dirs = abs_dirs
        return self.abs_dirs

    def install_chrome_android(self):
        """Install Google Chrome for Android in production from tooltool"""
        if self.app != "chrome-m":
            self.info("Google Chrome for Android not required")
            return
        if self.config.get("run_local"):
            self.info(
                "Google Chrome for Android will not be installed "
                "from tooltool when running locally"
            )
            return

        self.info("Fetching and installing Google Chrome for Android")
        self.device.shell_output("cmd package install-existing com.android.chrome")
        self.info("Google Chrome for Android successfully installed")

    def install_chromium_android(self):
        """Install custom Chromium-as-Release for Android from toolchain fetch"""
        if self.app != "cstm-car-m":
            self.info("Chromium-as-Release for Android not required")
            return
        if self.config.get("run_local"):
            self.info(
                "Chromium-as-Release for Android will not be installed "
                "when running locally"
            )
            return

        self.info("Installing Custom Chromium-as-Release for Android")
        cstm_car_m_apk = pathlib.Path(
            os.environ["MOZ_FETCHES_DIR"], "chromium", "apks", "ChromePublic.apk"
        )
        self.device.install_app(str(cstm_car_m_apk))
        self.info("Custom Chromium-as-Release for Android successfully installed")

    def _log_safari_version(self):
        app_name = {
            "safari": "Safari",
            "safari-tp": "Safari Technology Preview",
        }[self.app]
        plist_path = pathlib.Path(
            "/Applications", app_name + ".app", "Contents", "version.plist"
        )
        try:
            with plist_path.open("rb") as plist_file:
                plist = plistlib.load(plist_file)
            short_version = plist.get("CFBundleShortVersionString")
            bundle_version = plist.get("CFBundleVersion")
        except Exception as exc:
            self.warning(f"Unable to read {app_name} version from {plist_path}: {exc}")
            return

        missing_keys = [
            key
            for key, value in (
                ("CFBundleShortVersionString", short_version),
                ("CFBundleVersion", bundle_version),
            )
            if not value
        ]
        if missing_keys:
            self.warning(
                f"Unable to read {app_name} version: missing {', '.join(missing_keys)}"
            )
            return

        self.info(
            f"{app_name} version: {short_version} (CFBundleShortVersionString), "
            f"{bundle_version} (CFBundleVersion)"
        )

    def install_safari_technology_preview(self):
        """Update Safari TP in CI, then log the Safari or Safari TP version"""

        if self.app not in ("safari", "safari-tp") or self.run_local:
            return

        if self.app == "safari-tp":
            import mozprocess

            self.info("Checking for Safari Technology Preview updates...")
            install_script = "/usr/local/bin/install_safari_softwareupdate_updates.py"
            cmd = [sys.executable, install_script]
            mozprocess.run_and_wait(cmd)

        self._log_safari_version()

    def install_chromium_distribution(self):
        """Install Google Chromium distribution in production"""
        linux, mac, win = "linux", "mac", "win"
        chrome, chromium_release = (
            "chrome",
            "custom-car",
        )

        available_chromium_dists = [
            chrome,
            chromium_release,
        ]
        binary_location = {
            chromium_release: {
                linux: ["chromium", "Default", "chrome"],
                win: ["chromium", "Default", "chrome.exe"],
                mac: ["chromium", "Chromium.app", "Contents", "MacOS", "chromium"],
            },
        }

        if self.app not in available_chromium_dists:
            self.info("Google Chrome or Chromium distributions are not required.")
            return

        if self.app == "chrome":
            self.info("Chrome should be preinstalled.")
            if win in self.platform_name():
                base_path = "C:\\%s\\Google\\Chrome\\Application\\chrome.exe"
                self.chromium_dist_path = base_path % "Progra~1"
                if not os.path.exists(self.chromium_dist_path):
                    self.chromium_dist_path = base_path % "Progra~2"
            elif linux in self.platform_name():
                self.chromium_dist_path = "/usr/bin/google-chrome"
            elif mac in self.platform_name():
                self.chromium_dist_path = (
                    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
                )
            else:
                self.error(
                    f"Chrome is not installed on the platform {self.platform_name()} yet."
                )

            if os.path.exists(self.chromium_dist_path):
                self.info(
                    f"Google Chrome found in expected location {self.chromium_dist_path}"
                )
            else:
                self.error(f"Cannot find Google Chrome at {self.chromium_dist_path}")

            return

        chromium_dist = self.app

        if self.config.get("run_local"):
            self.info(f"Expecting {chromium_dist} to be pre-installed locally")
            return

        self.info(f"Getting fetched {chromium_dist} build")
        self.chromium_dist_dest = os.path.normpath(
            os.path.abspath(os.environ["MOZ_FETCHES_DIR"])
        )

        if mac in self.platform_name():
            self.chromium_dist_path = os.path.join(
                self.chromium_dist_dest, *binary_location[chromium_dist][mac]
            )

        elif linux in self.platform_name():
            self.chromium_dist_path = os.path.join(
                self.chromium_dist_dest, *binary_location[chromium_dist][linux]
            )

        else:
            self.chromium_dist_path = os.path.join(
                self.chromium_dist_dest, *binary_location[chromium_dist][win]
            )

        self.info(f"{chromium_dist} dest is: {self.chromium_dist_dest}")
        self.info(f"{chromium_dist} path is: {self.chromium_dist_path}")

        # Now ensure Chromium binary exists
        if os.path.exists(self.chromium_dist_path):
            self.info(
                f"Successfully installed {chromium_dist} to: {self.chromium_dist_path}"
            )
        else:
            self.info(f"Abort: failed to install {chromium_dist}")

    def raptor_options(self, args=None, **kw):
        """Return options to Raptor"""
        options = []
        kw_options = {}

        # Get the APK location to be able to get the browser version
        # through mozversion
        if self.app in self.firefox_android_browsers and not self.run_local:
            kw_options["installerpath"] = self.installer_path

        # If testing on Firefox, the binary path already came from mozharness/pro;
        # otherwise the binary path is forwarded from command-line arg (raptor_cmd_line_args).
        kw_options["app"] = self.app
        if self.app == "firefox" or (
            self.app in self.firefox_android_browsers and not self.run_local
        ):
            binary_path = self.binary_path or self.config.get("binary_path")
            if not binary_path:
                self.fatal("Raptor requires a path to the binary.")
            kw_options["binary"] = binary_path
            if self.app in self.firefox_android_browsers:
                # In production ensure we have correct app name,
                # i.e. fennec_aurora or fennec_release etc.
                kw_options["binary"] = self.query_package_name()
                self.info(
                    f"Set binary to {kw_options['binary']} instead of {binary_path}"
                )
        elif self.app == "safari" and not self.run_local:
            binary_path = "/Applications/Safari.app/Contents/MacOS/Safari"
            kw_options["binary"] = binary_path
        elif self.app == "safari-tp" and not self.run_local:
            binary_path = "/Applications/Safari Technology Preview.app/Contents/MacOS/Safari Technology Preview"
            kw_options["binary"] = binary_path
        # Custom Chromium-as-Release for Android
        elif self.app == "cstm-car-m":
            kw_options["binary"] = "org.chromium.chrome"
        # Running on Chromium
        elif not self.run_local:
            # When running locally we already set the Chromium binary above, in init.
            # In production, we already installed Chromium, so set the binary path
            # to our install.
            kw_options["binary"] = self.chromium_dist_path or ""

        # Options overwritten from **kw
        if "test" in self.config:
            kw_options["test"] = self.config["test"]
        if "binary" in self.config:
            kw_options["binary"] = self.config["binary"]
        if self.symbols_path:
            kw_options["symbolsPath"] = self.symbols_path
        if self.config.get("obj_path", None) is not None:
            kw_options["obj-path"] = self.config["obj_path"]
        if self.config.get("mozbuild_path", None) is not None:
            kw_options["mozbuild-path"] = self.config["mozbuild_path"]
        if self.test_url_params:
            kw_options["test-url-params"] = self.test_url_params
        if self.config.get("device_name") is not None:
            kw_options["device-name"] = self.config["device_name"]
        if self.config.get("activity") is not None:
            kw_options["activity"] = self.config["activity"]
        if self.config.get("conditioned_profile") is not None:
            kw_options["conditioned-profile"] = self.config["conditioned_profile"]
        if self.config.get("benchmark_repository"):
            kw_options["benchmark_repository"] = self.config["benchmark_repository"]
        if self.config.get("benchmark_revision"):
            kw_options["benchmark_revision"] = self.config["benchmark_revision"]
        if self.config.get("benchmark_repository"):
            kw_options["benchmark_branch"] = self.config["benchmark_branch"]

        kw_options.update(kw)
        if self.host:
            kw_options["host"] = self.host
        # Configure profiling options
        options.extend(self.query_gecko_profile_options())
        # Extra arguments
        if args is not None:
            options += args
        if os.getenv("PERF_FLAGS"):
            for option in os.getenv("PERF_FLAGS").split():
                if "=" in option:
                    eq_index = option.find("=")
                    kw_option, value = option[:eq_index], option[eq_index + 1 :]
                    kw_options[kw_option] = value
                else:
                    options.extend(["--" + option])

        if self.config.get("run_local", False):
            options.extend(["--run-local"])
        if "raptor_cmd_line_args" in self.config:
            options += self.config["raptor_cmd_line_args"]
        if self.config.get("code_coverage", False):
            options.extend(["--code-coverage"])
        if self.config.get("is_release_build", False):
            options.extend(["--is-release-build"])
        if self.config.get("live_sites", False):
            options.extend(["--live-sites"])
        if self.config.get("chimera", False):
            options.extend(["--chimera"])
        if self.config.get("disable_perf_tuning", False):
            options.extend(["--disable-perf-tuning"])
        if self.config.get("cold", False):
            options.extend(["--cold"])
        if not self.config.get("fission", True):
            options.extend(["--disable-fission"])
        if self.config.get("verbose", False):
            options.extend(["--verbose"])
        if self.config.get("extra_prefs"):
            options.extend([f"--setpref={i}" for i in self.config.get("extra_prefs")])
        if self.config.get("environment"):
            options.extend([f"--setenv={i}" for i in self.config.get("environment")])
        for extension in self.config.get("install_extensions") or []:
            options.extend(["--install-extension", extension])
        if self.config.get("enable_marionette_trace", False):
            options.extend(["--enable-marionette-trace"])
        if self.config.get("browser_cycles"):
            options.extend([f"--browser-cycles={self.config.get('browser_cycles')}"])
        if self.config.get("test_bytecode_cache", False):
            options.extend(["--test-bytecode-cache"])
        # Also opt in on try via `mach try --env MOZ_RAPTOR_COLLECT_PERFSTATS=1`.
        if (
            self.config.get("collect_perfstats", False)
            or os.environ.get("MOZ_RAPTOR_COLLECT_PERFSTATS") == "1"
        ):
            options.extend(["--collect-perfstats"])
        if self.config.get("simpleperf", False):
            options.extend(["--simpleperf"])
        if self.config.get("etw_profile", False):
            options.extend(["--etw-profile"])
        if self.config.get("samply_profile", False):
            options.extend(["--samply-profile"])
        if self.config.get("perf_profile", False):
            options.extend(["--perf-profile"])
        if self.config.get("extra_summary_methods"):
            options.extend([
                f"--extra-summary-methods={method}"
                for method in self.config.get("extra_summary_methods")
            ])
        if self.config.get("page_timeout"):
            options.extend([f"--page-timeout={self.page_timeout}"])
        if self.config.get("post_startup_delay"):
            options.extend([
                f"--post-startup-delay={self.config['post_startup_delay']}"
            ])
        if (
            self.config.get("screenshot_on_failure", False)
            or os.environ.get("MOZ_AUTOMATION", None) is not None
        ):
            options.extend(["--screenshot-on-failure"])
        if self.config.get("power_test", False):
            options.extend(["--power-test"])
        for (arg,), details in Raptor.browsertime_options:
            # Allow overriding defaults on the `./mach raptor-test ...` command-line
            value = self.config.get(details["dest"])
            if value is None or value != getattr(self, details["dest"], None):
                # Check for modifications done to the instance variables
                value = getattr(self, details["dest"], None)
            if value and arg not in self.config.get("raptor_cmd_line_args", []):
                if isinstance(value, str):
                    options.extend([arg, os.path.expandvars(value)])
                elif isinstance(value, (tuple, list)):
                    for val in value:
                        options.extend([arg, val])
                else:
                    options.extend([arg])

        for key, value in kw_options.items():
            options.extend([f"--{key}", value])

        return options

    def populate_webroot(self):
        """Populate the production test machines' webroots"""
        self.raptor_path = os.path.join(
            self.query_abs_dirs()["abs_test_install_dir"], "raptor"
        )
        if self.config.get("run_local"):
            self.raptor_path = os.path.join(self.repo_path, "testing", "raptor")

    def clobber(self):
        # Recreate the upload directory for storing the logcat collected
        # during APK installation.
        super().clobber()
        upload_dir = self.query_abs_dirs()["abs_blob_upload_dir"]
        if not os.path.isdir(upload_dir):
            self.mkdir_p(upload_dir)

    def install_android_app(self, apk, replace=False):
        # Override AndroidMixin's install_android_app in order to capture
        # logcat during the installation. If the installation fails,
        # the logcat file will be left in the upload directory.
        self.logcat_start()
        try:
            super().install_android_app(apk, replace=replace)
        finally:
            self.logcat_stop()

    def download_and_extract(self, extract_dirs=None, suite_categories=None):
        # Use in-tree wptserve for Python 3.10 compatibility
        extract_dirs = [
            "bin/*",
            "tools/wptserve/*",
            "tools/wpt_third_party/h2/*",
            "tools/wpt_third_party/pywebsocket3/*",
        ]
        return super().download_and_extract(
            extract_dirs=extract_dirs, suite_categories=["common", "condprof", "raptor"]
        )

    def query_python_constraints(self):
        return [
            os.path.abspath(
                os.path.join(
                    self.raptor_path, "..", "config", "requirements-raptor.txt"
                )
            )
        ]

    def create_virtualenv(self, **kwargs):
        """VirtualenvMixin.create_virtualenv() assumes we're using
        self.config['virtualenv_modules']. Since we're installing
        raptor from its source, we have to wrap that method here."""
        # If virtualenv already exists, just add to path and don't re-install.
        # We need it in-path to import jsonschema later when validating output for perfherder.
        _virtualenv_path = self.config.get("virtualenv_path")

        if self.clean:
            rmtree(_virtualenv_path, ignore_errors=True)

        _python_interp = self.query_exe("python")
        if "win" in self.platform_name() and os.path.exists(_python_interp):
            multiprocessing.set_executable(_python_interp)

        if self.run_local and os.path.exists(_virtualenv_path):
            self.info("Virtualenv already exists, skipping creation")
            # ffmpeg exists outside of this virtual environment so
            # we re-add it to the platform environment on repeated
            # local runs of browsertime visual metric tests
            self.setup_local_ffmpeg()

            if "win" in self.platform_name():
                _path = os.path.join(_virtualenv_path, "Lib", "site-packages")
            else:
                _path = os.path.join(
                    _virtualenv_path,
                    "lib",
                    os.path.basename(_python_interp),
                    "site-packages",
                )

            sys.path.append(_path)
            return

        requirements_file = (
            "source_requirements.txt" if self.run_local else "requirements.txt"
        )
        raptor_requirements = os.path.join(self.raptor_path, requirements_file)
        common_requirements = os.path.join(self.raptor_path, "requirements-common.txt")
        self.register_virtualenv_module(requirements=[raptor_requirements])

        modules = ["pip>=1.5"]

        # Add modules required for visual metrics. Packages with non-Python
        # components are particularly fussy about python version.
        py3_minor = sys.version_info.minor
        if not (self.run_local or self.browsertime_visualmetrics):
            pass
        elif py3_minor <= 11:
            modules.extend([
                "numpy==1.23.5",
                "Pillow==9.2.0",
                "scipy==1.9.3",
                "pyssim==0.4",
                "opencv-python==4.6.0.66",
            ])
        else:  # python version >= 3.12
            modules.extend([
                "numpy==2.2.3",
                "Pillow==11.1.0",
                "scipy==1.15.2",
                "pyssim==0.7",
                "opencv-python==4.11.0.86",
            ])

        if self.run_local:
            self.setup_local_ffmpeg()

        # Require pip >= 1.5 so pip will prefer .whl files to install
        super().create_virtualenv(modules=modules, requirements=[common_requirements])

    def setup_local_ffmpeg(self):
        """Make use of the users local ffmpeg when running browsertime visual
        metrics tests.
        """

        if "ffmpeg" in os.environ["PATH"]:
            return

        platform = self.platform_name()
        btime_cache = os.path.join(self.config["mozbuild_path"], "browsertime")
        if "mac" in platform:
            path_to_ffmpeg = os.path.join(
                btime_cache,
                FFMPEG_LOCAL_CACHE["mac"],
            )
        elif "linux" in platform:
            path_to_ffmpeg = os.path.join(
                btime_cache,
                FFMPEG_LOCAL_CACHE["linux"],
            )
        elif "win" in platform:
            path_to_ffmpeg = os.path.join(
                btime_cache,
                FFMPEG_LOCAL_CACHE["win"],
                "bin",
            )

        if os.path.exists(path_to_ffmpeg):
            os.environ["PATH"] += os.pathsep + path_to_ffmpeg
            self.browsertime_ffmpeg = path_to_ffmpeg
            self.info(f"Added local ffmpeg found at: {path_to_ffmpeg} to environment.")
        else:
            raise Exception(
                f"No local ffmpeg binary found. Expected it to be here: {path_to_ffmpeg}"
            )

    def install(self):
        if self.app in self.android_browsers:
            # Clear app data/cache so leftover state (e.g. a profile written
            # under the wrong storage path on a previous iteration) doesn't
            # carry over into the next run. Done even when --no-install is
            # set since stale data is what we're trying to flush.
            if not self.device.confirm_clear_app_data(self.binary_path):
                raise Exception("Abort: Declined to clear app data, can't run tests.")
            try:
                self.device.shell_output(f"pm clear {self.binary_path}")
            except Exception as e:
                self.info(
                    f"pm clear {self.binary_path} failed "
                    f"(app may not be installed yet): {e}"
                )

        if not self.config.get("no_install", False):
            if self.app in self.firefox_android_browsers:
                self.device.uninstall_app(self.binary_path)
                installer_path = self.installer_path
                self.info(f"Installing APK from: {installer_path}")
                if self.app == "fenix":
                    self.info("Installing Fenix APK with baseline profile")
                    self.device.install_app_baseline_profile(installer_path)
                else:
                    self.install_android_app(str(installer_path))
            else:
                super().install()

    def _artifact_perf_data(self, src, dest):
        if not os.path.isdir(os.path.dirname(dest)):
            # create upload dir if it doesn't already exist
            self.info(f"Creating dir: {os.path.dirname(dest)}")
            os.makedirs(os.path.dirname(dest))
        self.info(f"Copying raptor results from {src} to {dest}")
        try:
            copyfile(src, dest)
        except Exception as e:
            self.critical(f"Error copying results {src} to upload dir {dest}")
            self.info(str(e))

    def run_tests(self, args=None, **kw):
        """Run raptor tests"""

        # Get Raptor options
        options = self.raptor_options(args=args, **kw)

        # Python version check
        python = self.query_python_path()
        self.run_command([python, "--version"])
        parser = RaptorOutputParser(
            config=self.config, log_obj=self.log_obj, error_list=RaptorErrorList
        )
        env = {}
        env["MOZ_UPLOAD_DIR"] = self.query_abs_dirs()["abs_blob_upload_dir"]
        if not self.run_local:
            env["MINIDUMP_STACKWALK"] = self.query_minidump_stackwalk()
        env["MINIDUMP_SAVE_PATH"] = self.query_abs_dirs()["abs_blob_upload_dir"]
        env["RUST_BACKTRACE"] = "full"
        if not os.path.isdir(env["MOZ_UPLOAD_DIR"]):
            self.mkdir_p(env["MOZ_UPLOAD_DIR"])
        env = self.query_env(partial_env=env, log_level=INFO)
        power_profile = self.config.get("power_profile")
        # adjust PYTHONPATH to be able to use raptor as a python package
        if "PYTHONPATH" in env:
            env["PYTHONPATH"] = self.raptor_path + os.pathsep + env["PYTHONPATH"]
        else:
            env["PYTHONPATH"] = self.raptor_path

        # mitmproxy needs path to mozharness when installing the cert, and tooltool
        env["SCRIPTSPATH"] = scripts_path
        env["EXTERNALTOOLSPATH"] = external_tools_path
        env["PERFHERDER_SCHEMA_PATH"] = perfherder_schema_path()

        # xpcshell may come from local build, or fetched from build artifacts in
        # the case of CI.
        env["XPCSHELL_PATH"] = os.path.join(
            self.query_abs_dirs()["abs_test_install_dir"], "bin", "xpcshell.exe"
        )
        if os.path.exists(env["XPCSHELL_PATH"]) and not self.run_local:
            dest = os.path.join(
                self.query_abs_dirs()["abs_work_dir"],
                "application",
                "firefox",
                "xpcshell.exe",
            )
            copyfile(env["XPCSHELL_PATH"], dest)
            env["XPCSHELL_PATH"] = dest
        if self.run_local and self.obj_path:
            env["XPCSHELL_PATH"] = os.path.join(
                self.obj_path, "dist", "bin", "xpcshell.exe"
            )

        if not self.run_local:
            env["MOZ_INTERNAL_UPLOAD_DIR"] = os.path.join(
                os.path.dirname(env["MOZ_UPLOAD_DIR"]), "perftest"
            )
            if not os.path.exists(env["MOZ_INTERNAL_UPLOAD_DIR"]):
                os.makedirs(env["MOZ_INTERNAL_UPLOAD_DIR"])

        # Needed to load unsigned Raptor WebExt on release builds
        if self.is_release_build:
            env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "1"

        if self.repo_path is not None:
            env["MOZ_DEVELOPER_REPO_DIR"] = self.repo_path
        if self.obj_path is not None:
            env["MOZ_DEVELOPER_OBJ_DIR"] = self.obj_path
        if self.mozbuild_path is not None:
            env["MOZ_MOZBUILD_DIR"] = self.mozbuild_path

        # Sets a timeout for how long Raptor should run without output
        output_timeout = self.config.get("raptor_output_timeout", 3600)
        # Run Raptor tests
        run_tests = os.path.join(self.raptor_path, "raptor", "raptor.py")

        # Dynamically set the log level based on the raptor config for consistency
        # throughout the test
        mozlog_opts = [f"--log-tbpl-level={self.config['log_level']}"]

        if not self.run_local and "suite" in self.config:
            fname_pattern = f"{self.config['test']}_%s.log"
            mozlog_opts.append(
                f"--log-errorsummary={os.path.join(env['MOZ_UPLOAD_DIR'], fname_pattern % 'errorsummary')}"
            )

        def launch_in_debug_mode(cmdline):
            cmdline = set(cmdline)
            debug_opts = {"--debug", "--debugger", "--debugger_args"}

            return bool(debug_opts.intersection(cmdline))

        if self.app in self.android_browsers:
            self.logcat_start()

        command = [python, run_tests] + options + mozlog_opts
        if power_profile:
            command = [
                "powerprofilesctl",
                "launch",
                "--profile",
                "performance",
                "--reason",
                "Raptor performance test",
                "--appid",
                "org.mozilla.raptor",
                "--",
                "/bin/sh",
                "-c",
                (
                    'profile="$(powerprofilesctl get)" || exit $?; '
                    'printf "Active power profile: %s\\n" "$profile"; '
                    'if [ "$profile" != performance ]; then '
                    'printf "Expected active power profile performance, got %s\\n" '
                    '"$profile" >&2; exit 1; fi; '
                    'exec "$@"'
                ),
                "raptor-power-profile",
            ] + command

        if launch_in_debug_mode(command):
            raptor_process = subprocess.Popen(command, cwd=self.workdir, env=env)
            raptor_process.wait()
        else:
            self.return_code = self.run_command(
                command,
                cwd=self.workdir,
                output_timeout=output_timeout,
                output_parser=parser,
                env=env,
            )

        if self.app in self.android_browsers:
            self.logcat_stop()

        if parser.minidump_output:
            self.info("Looking at the minidump files for debugging purposes...")
            for item in parser.minidump_output:
                self.run_command(["ls", "-l", item])

        elif not self.run_local:
            # Copy results to upload dir so they are included as an artifact
            self.info("Copying Raptor results to upload dir:")

            src = os.path.join(self.query_abs_dirs()["abs_work_dir"], "raptor.json")
            dest = os.path.join(env["MOZ_UPLOAD_DIR"], "perfherder-data.json")
            self.info(str(dest))
            self._artifact_perf_data(src, dest)

            # Make individual perfherder data JSON's for each supporting data type
            for file in glob.glob(
                os.path.join(self.query_abs_dirs()["abs_work_dir"], "*")
            ):
                path, filename = os.path.split(file)

                if not filename.startswith("raptor-"):
                    continue

                # filename is expected to contain a unique data name
                # i.e. raptor-os-baseline-power.json would result in
                # the data name os-baseline-power
                data_name = "-".join(filename.split("-")[1:])
                data_name = ".".join(data_name.split(".")[:-1])

                src = file
                dest = os.path.join(
                    env["MOZ_UPLOAD_DIR"], f"perfherder-data-{data_name}.json"
                )
                self._artifact_perf_data(src, dest)

            src = os.path.join(
                self.query_abs_dirs()["abs_work_dir"], "screenshots.html"
            )
            if os.path.exists(src):
                dest = os.path.join(env["MOZ_UPLOAD_DIR"], "screenshots.html")
                self.info(str(dest))
                self._artifact_perf_data(src, dest)

        # Allow log failures to over-ride successful runs of the test harness and
        # give log failures priority, so that, for instance, log failures resulting
        # in TBPL_RETRY cause a retry rather than simply reporting an error.
        if parser.tbpl_status != TBPL_SUCCESS:
            parser_status = EXIT_STATUS_DICT[parser.tbpl_status]
            self.info(
                f"return code {str(self.return_code)} changed to {str(parser_status)} due to log output"
            )
            self.return_code = parser_status


class RaptorOutputParser(OutputParser):
    minidump_regex = re.compile(
        r'''raptorError: "error executing: '(\S+) (\S+) (\S+)'"'''
    )
    RE_PERF_DATA = re.compile(r".*PERFHERDER_DATA:\s+(\{.*\})")

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.minidump_output = None
        self.found_perf_data = []
        self.tbpl_status = TBPL_SUCCESS
        self.worst_log_level = INFO
        self.harness_retry_re = TinderBoxPrintRe["harness_error"]["retry_regex"]

    def parse_single_line(self, line):
        m = self.minidump_regex.search(line)
        if m:
            self.minidump_output = (m.group(1), m.group(2), m.group(3))

        m = self.RE_PERF_DATA.match(line)
        if m:
            self.found_perf_data.append(m.group(1))

        if self.harness_retry_re.search(line):
            self.critical(f" {line}")
            self.worst_log_level = self.worst_level(CRITICAL, self.worst_log_level)
            self.tbpl_status = self.worst_level(
                TBPL_RETRY, self.tbpl_status, levels=TBPL_WORST_LEVEL_TUPLE
            )
            return  # skip base parse_single_line
        if line.startswith("raptor-browsertime Info: "):
            raptor_line = line[len("raptor-browsertime Info: ") :]
            if raptor_line.startswith("BEGIN: "):
                SystemResourceMonitor.begin_marker(
                    "test", raptor_line[len("BEGIN: ") :]
                )
                return
            elif raptor_line.startswith("END: "):
                SystemResourceMonitor.end_marker("test", raptor_line[len("END: ") :])
                return
            else:
                SystemResourceMonitor.record_event(raptor_line)
        super().parse_single_line(line)
