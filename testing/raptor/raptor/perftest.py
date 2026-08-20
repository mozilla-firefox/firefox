#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
from abc import ABCMeta, abstractmethod

import mozinfo
import mozproxy.utils as mpu
import mozversion
from mozprofile import create_profile
from mozproxy import get_playback

# need this so raptor imports work both from /raptor and via mach
here = os.path.abspath(os.path.dirname(__file__))
paths = [here]

for path in paths:
    if not os.path.exists(path):
        raise OSError(f"{path} does not exist. ")
    sys.path.insert(0, path)

from addon_utils import is_local_path, is_url, resolve_amo_addon
from chrome_trace import ChromeTrace
from cmdline import (
    CHROME_ANDROID_APPS,
    FIREFOX_ANDROID_APPS,
    FIREFOX_APPS,
    GECKO_PROFILER_APPS,
    TRACE_APPS,
)
from condprof.client import ProfileNotFoundError, get_profile
from condprof.util import download_file, get_current_platform
from etw_profile import ETWProfile
from gecko_profile import GeckoProfile
from logger.logger import RaptorLogger
from perf_profile import PerfProfile
from results import RaptorResultsHandler
from samply_profile import SamplyProfile
from simpleperf import SimpleperfProfile

LOG = RaptorLogger(component="raptor-perftest")

# - mozproxy.utils LOG displayed INFO messages even when LOG.error() was used in mitm.py
mpu.LOG = RaptorLogger(component="raptor-mitmproxy")

try:
    from mozbuild.base import MozbuildObject

    build = MozbuildObject.from_environment(cwd=here)
except ImportError:
    build = None

POST_DELAY_CONDPROF = 1000
POST_DELAY_MOBILE = 20000
POST_DELAY_DEBUG = 3000
POST_DELAY_DEFAULT = 30000


class Perftest(metaclass=ABCMeta):
    """Abstract base class for perftests that execute via a subharness,
    either Raptor or browsertime."""

    def __init__(
        self,
        app,
        binary,
        run_local=False,
        no_install=False,
        obj_path=None,
        profile_class=None,
        installerpath=None,
        gecko_profile=False,
        gecko_profile_interval=None,
        gecko_profile_entries=None,
        gecko_profile_extra_threads=None,
        gecko_profile_threads=None,
        gecko_profile_features=None,
        extra_profiler_run=False,
        etw_profile=False,
        samply_profile=False,
        simpleperf=False,
        perf_profile=False,
        symbols_path=None,
        host=None,
        cold=False,
        live_sites=False,
        is_release_build=False,
        debug_mode=False,
        post_startup_delay=None,
        interrupt_handler=None,
        e10s=True,
        results_handler_class=RaptorResultsHandler,
        device_name=None,
        disable_perf_tuning=False,
        conditioned_profile=None,
        test_bytecode_cache=False,
        chimera=False,
        extra_prefs={},
        environment={},
        project="mozilla-central",
        verbose=False,
        python=None,
        fission=True,
        extra_summary_methods=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        clean=False,
        screenshot_on_failure=False,
        power_test=False,
        install_extensions=None,
        **kwargs,
    ):
        self._remote_test_root = None
        self._dirs_to_remove = []
        self.verbose = verbose
        self.page_count = []

        # Override the magic --host HOST_IP with the value of the environment variable.
        if host == "HOST_IP":
            host = os.environ["HOST_IP"]

        self.config = {
            "app": app,
            "binary": binary,
            "platform": mozinfo.os,
            "processor": mozinfo.processor,
            "run_local": run_local,
            "obj_path": obj_path,
            "gecko_profile": gecko_profile,
            "gecko_profile_interval": gecko_profile_interval,
            "gecko_profile_entries": gecko_profile_entries,
            "gecko_profile_extra_threads": gecko_profile_extra_threads,
            "gecko_profile_threads": gecko_profile_threads,
            "gecko_profile_features": gecko_profile_features,
            "extra_profiler_run": extra_profiler_run,
            "etw_profile": etw_profile,
            "samply_profile": samply_profile,
            "simpleperf": simpleperf,
            "perf_profile": perf_profile,
            "symbols_path": symbols_path,
            "host": host,
            "cold": cold,
            "live_sites": live_sites,
            "is_release_build": is_release_build,
            "e10s": e10s,
            "device_name": device_name,
            "fission": fission,
            "disable_perf_tuning": disable_perf_tuning,
            "conditioned_profile": conditioned_profile,
            "test_bytecode_cache": test_bytecode_cache,
            "chimera": chimera,
            "extra_prefs": extra_prefs,
            "environment": environment,
            "project": project,
            "verbose": verbose,
            "extra_summary_methods": extra_summary_methods,
            "benchmark_repository": benchmark_repository,
            "benchmark_revision": benchmark_revision,
            "benchmark_branch": benchmark_branch,
            "clean": clean,
            "screenshot_on_failure": screenshot_on_failure,
            "power_test": power_test,
            "install_extensions": install_extensions or [],
        }

        self.firefox_android_apps = FIREFOX_ANDROID_APPS

        # We are deactivating the conditioned profiles for:
        # - win10-aarch64 : no support for geckodriver see 1582757
        # - reference browser: no conditioned profiles created see 1606767
        if (
            self.config["platform"] == "win" and self.config["processor"] == "aarch64"
        ) or self.config["binary"] == "org.mozilla.reference.browser.raptor":
            self.config["conditioned_profile"] = None

        if self.config["conditioned_profile"]:
            LOG.info("Using a conditioned profile.")
        else:
            LOG.info("Using an empty profile.")

        # To differentiate between chrome/firefox failures, we
        # set an app variable in the logger which prefixes messages
        # with the app name
        if self.config["app"] in (
            "chrome",
            "chrome-m",
            "custom-car",
            "cstm-car-m",
        ):
            LOG.set_app(self.config["app"])

        self.browser_name = None
        self.browser_version = None

        self.raptor_venv = os.path.join(os.getcwd(), "raptor-venv")
        self.installerpath = installerpath
        self.playback = None
        self.benchmark = None
        self.gecko_profiler = None
        self.chrome_trace = None
        self.etw_profiler = None
        self.samply_profiler = None
        self.simpleperf_profiler = None
        self.perf_profiler = None
        self.device = None
        self.runtime_error = None
        self.profile_class = profile_class or app
        # Use the `chromium` profile class for custom-car
        if app in ["custom-car"]:
            self.profile_class = "chromium"
        self.conditioned_profile_dir = None
        self.interrupt_handler = interrupt_handler
        self.results_handler = results_handler_class(**self.config)

        self.browser_name, self.browser_version = self.get_browser_meta()

        browser_name, browser_version = self.get_browser_meta()
        self.results_handler.add_browser_meta(self.config["app"], browser_version)

        # debug mode is currently only supported when running locally
        self.run_local = self.config["run_local"]
        self.debug_mode = debug_mode if self.run_local else False

        if post_startup_delay is None:
            # For the post startup delay, we want to max it to 1s when using the
            # conditioned profiles.
            if self.config.get("conditioned_profile"):
                self.post_startup_delay = POST_DELAY_CONDPROF
            elif (
                self.debug_mode
            ):  # if running debug-mode reduce the pause after browser startup
                self.post_startup_delay = POST_DELAY_DEBUG
            else:
                self.post_startup_delay = POST_DELAY_DEFAULT

            if (
                app in CHROME_ANDROID_APPS + FIREFOX_ANDROID_APPS
                and not self.config.get("conditioned_profile")
            ):
                LOG.info("Mobile non-conditioned profile")
                self.post_startup_delay = POST_DELAY_MOBILE
        else:
            # User supplied a custom post_startup_delay value
            self.post_startup_delay = post_startup_delay

        LOG.info(f"Post startup delay set to {self.post_startup_delay} ms")
        LOG.info(f"main raptor init, config is: {self.config}")

        # TODO: Move this outside of the perftest initialization, it contains
        # platform-specific code
        self.build_browser_profile()

        # Crashes counter
        self.crashes = 0

    def _get_temp_dir(self):
        tempdir = tempfile.mkdtemp()
        self._dirs_to_remove.append(tempdir)
        return tempdir

    @property
    def is_localhost(self):
        return self.config.get("host") in ("localhost", "127.0.0.1")

    @property
    def android_external_storage(self):
        return "/sdcard/test_root/"

    @property
    def conditioned_profile_copy(self):
        """Returns a copy of the original conditioned profile that was created."""
        condprof_copy = os.path.join(self._get_temp_dir(), "profile")
        shutil.copytree(
            self.conditioned_profile_dir,
            condprof_copy,
            ignore=shutil.ignore_patterns("lock"),
        )
        LOG.info(f"Created a conditioned-profile copy: {condprof_copy}")
        return condprof_copy

    def build_conditioned_profile(self):
        # Late import so python-test doesn't import it
        import asyncio

        # The following import patchs an issue with invalid
        # content-type, see bug 1655869
        from condprof import patch  # noqa
        from condprof.runner import Runner

        if not getattr(self, "browsertime"):
            raise Exception(
                "Building conditioned profiles within a test is only supported "
                "when using Browsertime."
            )

        geckodriver = getattr(self, "browsertime_geckodriver", None)
        if not geckodriver:
            geckodriver = (
                sys.platform.startswith("win") and "geckodriver.exe" or "geckodriver"
            )

        scenario = self.config.get("conditioned_profile")
        runner = Runner(
            profile=None,
            firefox=self.config.get("binary"),
            geckodriver=geckodriver,
            archive=None,
            device_name=self.config.get("device_name"),
            strict=True,
            visible=True,
            force_new=True,
            skip_logs=True,
            remote_test_root=self.android_external_storage,
        )

        if self.config.get("is_release_build", False):
            # Enable non-local connections for building the conditioned profile
            self.enable_non_local_connections()

        if scenario == "settled-youtube":
            runner.prepare(scenario, "youtube")

            loop = asyncio.get_event_loop()
            loop.run_until_complete(runner.one_run(scenario, "youtube"))
        elif scenario == "settled-webext":
            runner.prepare("settled", "webext")

            loop = asyncio.get_event_loop()
            loop.run_until_complete(runner.one_run("settled", "webext"))
        else:
            runner.prepare(scenario, "default")

            loop = asyncio.get_event_loop()
            loop.run_until_complete(runner.one_run(scenario, "default"))

        if self.config.get("is_release_build", False):
            self.disable_non_local_connections()

        self.conditioned_profile_dir = runner.env.profile
        return self.conditioned_profile_copy

    def get_conditioned_profile(self, binary=None):
        """Downloads a platform-specific conditioned profile, using the
        condprofile client API; returns a self.conditioned_profile_dir"""
        if self.conditioned_profile_dir:
            # We already have a directory, so provide a copy that
            # will get deleted after it's done with
            return self.conditioned_profile_copy

        # Build the conditioned profile before the test
        if not self.config.get("conditioned_profile").startswith("artifact:"):
            return self.build_conditioned_profile()

        # create a temp file to help ensure uniqueness
        temp_download_dir = self._get_temp_dir()
        LOG.info(
            f"Making temp_download_dir from inside get_conditioned_profile {temp_download_dir}"
        )
        # call condprof's client API to yield our platform-specific
        # conditioned-profile binary
        if isinstance(self, PerftestAndroid):
            android_app = self.config["binary"].split("org.mozilla.")[-1]
            device_name = self.config.get("device_name")
            if device_name is None:
                device_name = "g5"
            platform = f"{device_name}-{android_app}"
        else:
            platform = get_current_platform()

        LOG.info(f"Platform used: {platform}")

        # when running under mozharness, the --project value
        # is set to match the project (try, mozilla-central, etc.)
        # By default it's mozilla-central, even on local runs.
        # We use it to prioritize conditioned profiles indexed
        # into the same project when it runs on the CI
        repo = self.config["project"]

        # we fall back to mozilla-central in all cases. If it
        # was already mozilla-central, we fall back to try
        alternate_repo = "mozilla-central" if repo != "mozilla-central" else "try"
        LOG.info(f"Getting profile from project {repo}")

        profile_scenario = self.config.get("conditioned_profile").replace(
            "artifact:", ""
        )
        try:
            cond_prof_target_dir = get_profile(
                temp_download_dir, platform, profile_scenario, repo=repo
            )
        except ProfileNotFoundError:
            cond_prof_target_dir = get_profile(
                temp_download_dir, platform, profile_scenario, repo=alternate_repo
            )
        except Exception:
            # any other error is a showstopper
            LOG.critical("Could not get the conditioned profile")
            traceback.print_exc()
            raise

        # now get the full directory path to our fetched conditioned profile
        self.conditioned_profile_dir = os.path.join(
            temp_download_dir, cond_prof_target_dir
        )
        if not os.path.exists(cond_prof_target_dir):
            LOG.critical(
                f"Can't find target_dir {cond_prof_target_dir}, from get_profile()"
                f"temp_download_dir {temp_download_dir}, platform {platform}, scenario {profile_scenario}"
            )
            raise OSError

        LOG.info(
            f"Original self.conditioned_profile_dir is now set: {self.conditioned_profile_dir}"
        )
        return self.conditioned_profile_copy

    def build_browser_profile(self):
        if self.config["app"] in FIREFOX_APPS:
            if self.config.get("conditioned_profile") is None:
                self.profile = create_profile(self.profile_class)
            else:
                # use mozprofile to create a profile for us, from our conditioned profile's path
                self.profile = create_profile(
                    self.profile_class, profile=self.get_conditioned_profile()
                )
        else:
            self.profile = None
            return
        # Merge extra profile data from testing/profiles
        with open(os.path.join(self.profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["raptor"]

        for profile in base_profiles:
            path = os.path.join(self.profile_data_dir, profile)
            LOG.info(f"Merging profile: {path}")
            self.profile.merge(path)

        LOG.info(f"Browser preferences: {self.config['extra_prefs']}")
        self.profile.set_preferences(self.config["extra_prefs"])

        self.install_extra_extensions()

        # share the profile dir with the config and the control server
        self.config["local_profile_dir"] = self.profile.profile
        LOG.info(f"Local browser profile: {self.profile.profile}")

    def install_extra_extensions(self):
        """Install webextensions requested via --install-extension into the profile.

        Each entry may be an AMO addon GUID/slug, a direct .xpi URL, or a local
        .xpi path. A single entry may also be a comma-separated list of these, which
        is how `mach try perf` forwards them through the PERF_FLAGS environment.
        """
        install_extensions = [
            entry
            for value in self.config.get("install_extensions") or []
            for entry in value.split(",")
            if entry
        ]
        if not install_extensions:
            return

        # Sideloaded extensions are disabled by default; these prefs make them
        # install and enable on startup (mirrors the condprof webext customization).
        self.profile.set_preferences({
            "extensions.autoDisableScopes": 0,
            "extensions.enabledScopes": 1,
            "extensions.startupScanScopes": 1,
        })

        xpis = []
        for entry in install_extensions:
            if is_local_path(entry):
                xpis.append(entry)
            else:
                url = entry if is_url(entry) else resolve_amo_addon(entry)
                LOG.info(f"Downloading webextension {entry} from {url}")
                xpis.append(download_file(url, mozfetches_subdir="firefox-addons"))

        self.profile.addons.install(xpis)
        for xpi in xpis:
            details = self.profile.addons.addon_details(xpi)
            LOG.info(
                f"Installed webextension {details['id']} "
                f"({details['version']}) into the test profile"
            )

    @property
    def profile_data_dir(self):
        if "MOZ_DEVELOPER_REPO_DIR" in os.environ:
            return os.path.join(
                os.environ["MOZ_DEVELOPER_REPO_DIR"], "testing", "profiles"
            )
        if build:
            return os.path.join(build.topsrcdir, "testing", "profiles")
        return os.path.join(here, "profile_data")

    @property
    def artifact_dir(self):
        artifact_dir = os.getcwd()
        if self.config.get("run_local", False):
            if "MOZ_DEVELOPER_REPO_DIR" in os.environ:
                artifact_dir = os.path.join(
                    os.environ["MOZ_DEVELOPER_REPO_DIR"],
                    "testing",
                    "mozharness",
                    "build",
                )
            else:
                artifact_dir = here
        elif os.getenv("MOZ_UPLOAD_DIR"):
            artifact_dir = os.getenv("MOZ_UPLOAD_DIR")
        return artifact_dir

    @abstractmethod
    def run_test_setup(self, test):
        LOG.info(f"starting test: {test['name']}")

        # if 'alert_on' was provided in the test INI, add to our config for results/output
        self.config["subtest_alert_on"] = test.get("alert_on")

        if test.get("playback") is not None and self.playback is None:
            self.start_playback(test)

        if test.get("preferences") is not None and self.config["app"] in FIREFOX_APPS:
            self.set_browser_test_prefs(test["preferences"])

    @abstractmethod
    def setup_chrome_args(self):
        pass

    @abstractmethod
    def get_browser_meta(self):
        pass

    def run_tests(self, tests, test_names):
        tests_to_run = tests
        if self.results_handler.existing_results:
            tests_to_run = []
        try:
            for test in tests_to_run:
                try:
                    self.run_test(test, timeout=int(test.get("page_timeout")))
                except RuntimeError as e:
                    # Check for crashes before showing the timeout error.
                    self.check_for_crashes()
                    if self.crashes == 0:
                        LOG.critical(e)
                    os.sys.exit(1)
                finally:
                    self.run_test_teardown(test)
            return self.process_results(tests, test_names)
        finally:
            self.clean_up()

    @abstractmethod
    def run_test(self, test, timeout):
        raise NotImplementedError()

    @abstractmethod
    def run_test_teardown(self, test):
        self.check_for_crashes()

    def process_results(self, tests, test_names):
        # when running locally output results in build/raptor.json; when running
        # in production output to a local.json to be turned into tc job artifact
        raptor_json_path = os.path.join(self.artifact_dir, "raptor.json")
        if not self.config.get("run_local", False):
            raptor_json_path = os.path.join(os.getcwd(), "local.json")

        self.config["raptor_json_path"] = raptor_json_path
        self.config["artifact_dir"] = self.artifact_dir
        self.config["page_count"] = self.page_count
        res = self.results_handler.summarize_and_output(self.config, tests, test_names)

        # Gecko profiling symbolication
        # We enable the gecko profiler either when the profiler is enabled with
        # gecko_profile flag form the command line or when an extra profiler-enabled
        # run is added with extra_profiler_run flag.
        if self.config["gecko_profile"] or self.config.get("extra_profiler_run"):
            if self.config["app"] in GECKO_PROFILER_APPS:
                self.gecko_profiler.symbolicate()
                # clean up the temp gecko profiling folders
                LOG.info("cleaning up after gecko profiling")
                self.gecko_profiler.clean()
            elif self.config["app"] in TRACE_APPS:
                self.chrome_trace.output_trace()
                LOG.info("cleaning up after gathering chrome trace")
                self.chrome_trace.clean()

        # Simpleperf profiling symbolication. This is currently only enabled for CI runs
        # as the dependencies for this symbolication (Samply) are
        # not available locally yet. See Bug 2028955.
        if self.config.get("simpleperf") and self.simpleperf_profiler:
            self.simpleperf_profiler.symbolicate()
            self.simpleperf_profiler.clean()

        return res

    @abstractmethod
    def set_browser_test_prefs(self):
        pass

    @abstractmethod
    def check_for_crashes(self):
        pass

    def clean_up_mitmproxy(self):
        if not self.run_local or self.config["clean"]:
            mitmproxy_dir = pathlib.Path("~/.mitmproxy").expanduser().resolve()
            if mitmproxy_dir.exists():
                shutil.rmtree(mitmproxy_dir, ignore_errors=True)

    def clean_up(self):
        # Cleanup all of our temporary directories
        for dir_to_rm in self._dirs_to_remove:
            if not os.path.exists(dir_to_rm):
                continue
            LOG.info(f"Removing temporary directory: {dir_to_rm}")
            shutil.rmtree(dir_to_rm, ignore_errors=True)
        self._dirs_to_remove = []

        # Go through the artifact directory and ensure we
        # don't have too many JPG/PNG files from a task failure
        if (
            not self.run_local
            and self.results_handler
            and self.results_handler.result_dir()
        ):
            artifact_dir = pathlib.Path(self.artifact_dir)
            for filetype in ("*.png", "*.jpg"):
                # Limit the number of images uploaded to the last (newest) 5
                for file in sorted(artifact_dir.rglob(filetype))[:-5]:
                    try:
                        file.unlink()
                    except FileNotFoundError:
                        pass

        self.clean_up_mitmproxy()

    def get_page_timeout_list(self):
        return self.results_handler.page_timeout_list

    def delete_proxy_settings_from_profile(self):
        # Must delete the proxy settings from the profile if running
        # the test with a host different from localhost.
        userjspath = os.path.join(self.profile.profile, "user.js")
        with open(userjspath) as userjsfile:
            prefs = userjsfile.readlines()
        prefs = [pref for pref in prefs if "network.proxy" not in pref]
        with open(userjspath, "w") as userjsfile:
            userjsfile.writelines(prefs)

    def start_playback(self, test):
        # creating the playback tool
        manifests_subdir = test.get("playback_manifests_dir", "playback")
        playback_dir = os.path.join(here, "tooltool-manifests", manifests_subdir)

        playback_manifest = test.get("playback_pageset_manifest")
        playback_manifests = playback_manifest.split(",")
        playback_files = [
            os.path.join(playback_dir, manifest) for manifest in playback_manifests
        ]

        self.config.update({
            "playback_tool": test.get("playback"),
            "playback_version": test.get("playback_version", "8.1.1"),
            "playback_files": playback_files,
            "verbose": self.config.get("verbose", False)
            or bool(test.get("verbose", False)),
            "test_name": test.get("name", ""),
        })

        LOG.info(f"test uses playback tool: {self.config['playback_tool']} ")

        self.clean_up_mitmproxy()
        self.playback = get_playback(self.config)

        # let's start it!
        self.playback.start()

    def _init_gecko_profiling(self, test):
        LOG.info("initializing gecko profiler")
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("Profiling ignored because MOZ_UPLOAD_DIR was not set")
        else:
            self.gecko_profiler = GeckoProfile(upload_dir, self.config, test)

    def _init_etw_profiling(self, test):
        LOG.info("initializing ETW profiler")
        # Only enable on Windows
        if mozinfo.os != "win":
            LOG.warning("ETW profiling is only supported on Windows")
            return

        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("ETW Profiling ignored because MOZ_UPLOAD_DIR was not set")
        else:
            self.etw_profiler = ETWProfile(upload_dir, self.config, test)

    def _init_samply_profiling(self, test):
        LOG.info("initializing Samply profiler")
        if mozinfo.os != "mac":
            LOG.warning("Samply profiling is only supported on macOS")
            return
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("Samply profiling ignored because MOZ_UPLOAD_DIR was not set")
        else:
            self.samply_profiler = SamplyProfile(upload_dir, self.config, test)

    def _init_chrome_trace(self, test):
        LOG.info("initializing Chrome Trace handler")
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("Chrome Trace ignored because MOZ_UPLOAD_DIR was not set")
        else:
            self.chrome_trace = ChromeTrace(upload_dir, self.config, test)

    def _init_simpleperf_profiling(self, test):
        LOG.info("Initializing Simpleperf profiler")
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("Profiling ignored because MOZ_UPLOAD_DIR was not set")
            self.simpleperf_profiler = None
        else:
            self.simpleperf_profiler = SimpleperfProfile(upload_dir, self.config, test)

    def _init_perf_profiling(self, test):
        LOG.info("Initializing perf profiler")
        if mozinfo.os != "linux":
            LOG.warning("Perf profiling is only supported on Linux")
            return
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if not upload_dir:
            LOG.critical("Perf profiling ignored because MOZ_UPLOAD_DIR was not set")
        else:
            self.perf_profiler = PerfProfile(upload_dir, self.config, test)

    def disable_non_local_connections(self):
        # For Firefox we need to set MOZ_DISABLE_NONLOCAL_CONNECTIONS=1 env var before startup
        # when testing release builds from mozilla-beta/release. This is because of restrictions
        # on release builds that require webextensions to be signed unless this env var is set
        LOG.info("setting MOZ_DISABLE_NONLOCAL_CONNECTIONS=1")
        os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "1"

    def enable_non_local_connections(self):
        # pageload tests need to be able to access non-local connections via mitmproxy
        LOG.info("setting MOZ_DISABLE_NONLOCAL_CONNECTIONS=0")
        os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "0"


class PerftestAndroid(Perftest):
    """Mixin class for Android-specific Perftest subclasses."""

    def setup_chrome_args(self, test):
        """Sets up chrome/chromium cmd-line arguments.

        Needs to be "implemented" here to deal with Python 2
        unittest failures.
        """
        raise NotImplementedError

    def get_browser_meta(self):
        """Returns the browser name and version in a tuple (name, version).

        Uses mozversion as the primary method to get this meta data and for
        android this is the only method which exists to get this data. With android,
        we use the installerpath attribute to determine this and this only works
        with Firefox browsers.
        """
        browser_name = None
        browser_version = None

        if self.config["app"] in self.firefox_android_apps:
            try:
                meta = mozversion.get_version(binary=self.installerpath)
                browser_name = meta.get("application_name")
                browser_version = meta.get("application_version")
            except Exception as e:
                LOG.warning(
                    "Failed to get android browser meta data through mozversion: "
                    f"{e.__class__.__name__}-{e}"
                )
        elif self.config["app"] in CHROME_ANDROID_APPS or browser_version is None:
            # We absolutely need to determine the chrome
            # version here so that we can select the correct
            # chromedriver for browsertime
            from mozdevice import ADBDeviceFactory

            device = ADBDeviceFactory(verbose=True)

            # Chrome uses a specific binary that we don't set as a command line option
            binary = (
                "com.android.chrome"
                if self.config["app"] == "chrome-m"
                else "org.chromium.chrome"
            )
            if self.config["app"] not in CHROME_ANDROID_APPS:
                binary = self.config["binary"]

            pkg_info = device.shell_output(f"dumpsys package {binary}")
            version_matcher = re.compile(r".*versionName=([\d.]+)")
            for line in pkg_info.split("\n"):
                match = version_matcher.match(line)
                if match:
                    browser_version = match.group(1)
                    browser_name = self.config["app"]
                    # First one found is the non-system
                    # or latest version.
                    break

            if not browser_version:
                raise Exception(f"Could not determine version for apk {binary}")

        if not browser_name:
            LOG.warning("Could not find a browser name")
        else:
            LOG.info(f"Browser name: {browser_name}")

        if not browser_version:
            LOG.warning("Could not find a browser version")
        else:
            LOG.info(f"Browser version: {browser_version}")

        return (browser_name, browser_version)

    def set_reverse_port(self, port):
        tcp_port = f"tcp:{port}"
        self.device.create_socket_connection("reverse", tcp_port, tcp_port)

    def set_reverse_ports(self):
        if self.is_localhost:
            if self.playback:
                LOG.info("making the raptor playback server port available to device")
                self.set_reverse_port(self.playback.port)

            if self.benchmark:
                LOG.info("making the raptor benchmarks server port available to device")
                self.set_reverse_port(int(self.benchmark.port))
        else:
            LOG.info("Reverse port forwarding is used only on local devices")

    def build_browser_profile(self):
        super().build_browser_profile()

        if self.config["app"] in FIREFOX_ANDROID_APPS:
            # Merge in the Android profile.
            path = os.path.join(self.profile_data_dir, "raptor-android")
            LOG.info(f"Merging profile: {path}")
            self.profile.merge(path)

    def clear_app_data(self):
        if not self.device.confirm_clear_app_data(self.config["binary"]):
            raise Exception("Abort: Declined to clear app data, can't run tests.")
        LOG.info(f"clearing {self.config['binary']} app data")
        self.device.shell(f"pm clear {self.config['binary']}")

    def set_debug_app_flag(self):
        # required so release apks will read the android config.yml file
        LOG.info(f"setting debug-app flag for {self.config['binary']}")
        self.device.shell(f"am set-debug-app --persistent {self.config['binary']}")

    def copy_profile_to_device(self):
        """Copy the profile to the device, and update permissions of all files."""
        if not self.device.is_app_installed(self.config["binary"]):
            raise Exception(f"{self.config['binary']} is not installed")

        try:
            LOG.info(f"copying profile to device: {self.remote_profile}")
            self.device.rm(self.remote_profile, force=True, recursive=True)
            self.device.push(self.profile.profile, self.remote_profile)
            self.device.chmod(self.remote_profile, recursive=True)

        except Exception:
            LOG.error("Unable to copy profile to device.")
            raise

    def turn_on_android_app_proxy(self):
        # for geckoview/android pageload playback we can't use a policy to turn on the
        # proxy; we need to set prefs instead; note that the 'host' may be different
        # than '127.0.0.1' so we must set the prefs accordingly
        proxy_prefs = {}
        proxy_prefs["network.proxy.type"] = 1
        proxy_prefs["network.proxy.http"] = self.playback.host
        proxy_prefs["network.proxy.http_port"] = self.playback.port
        proxy_prefs["network.proxy.ssl"] = self.playback.host
        proxy_prefs["network.proxy.ssl_port"] = self.playback.port
        proxy_prefs["network.proxy.no_proxies_on"] = self.config["host"]

        LOG.info(
            f"setting profile prefs to turn on the android app proxy: {proxy_prefs}"
        )
        self.profile.set_preferences(proxy_prefs)


class PerftestDesktop(Perftest):
    """Mixin class for Desktop-specific Perftest subclasses"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def setup_chrome_args(self, test):
        """Sets up chrome/chromium cmd-line arguments.

        Needs to be "implemented" here to deal with Python 2
        unittest failures.
        """
        raise NotImplementedError

    def desktop_chrome_args(self, test):
        """Returns cmd line options required to run pageload tests on Desktop Chrome
        and Chromium as Release (CaR). Also add the cmd line options to turn on the
        proxy and ignore security certificate errors if using host localhost, 127.0.0.1.
        """
        chrome_args = ["--use-mock-keychain", "--no-default-browser-check"]

        if test.get("playback", False):
            pb_args = [
                f"--proxy-server={self.playback.host}:{self.playback.port}",
                "--proxy-bypass-list=localhost;127.0.0.1",
                "--ignore-certificate-errors",
            ]

            if not self.is_localhost:
                pb_args[0] = pb_args[0].replace("127.0.0.1", self.config["host"])

            chrome_args.extend(pb_args)

        if self.debug_mode:
            chrome_args.extend(["--auto-open-devtools-for-tabs"])

        chrome_args.extend(
            arg.strip()
            for arg in test.get("chrome_args", "").splitlines()
            if arg.strip()
        )

        return chrome_args

    def get_browser_meta(self):
        """Returns the browser name and version in a tuple (name, version).

        On desktop, we use mozversion but a fallback method also exists
        for non-firefox browsers, where mozversion is known to fail. The
        methods are OS-specific, with windows being the outlier.
        """
        browser_name = None
        browser_version = None

        try:
            meta = mozversion.get_version(binary=self.config["binary"])
            browser_name = meta.get("application_name")
            browser_version = meta.get("application_version")
        except Exception as e:
            LOG.warning(
                "Failed to get browser meta data through mozversion: "
                f"{e.__class__.__name__}-{e}"
            )
            LOG.info("Attempting to get version through fallback method...")

            # Fall-back method to get browser version on desktop
            try:
                if "mac" in self.config["platform"]:
                    import plistlib

                    for plist_file in ("version.plist", "Info.plist"):
                        try:
                            binary_path = pathlib.Path(self.config["binary"])
                            plist_path = binary_path.parent.parent.joinpath(plist_file)
                            with plist_path.open("rb") as plist_file_content:
                                plist = plistlib.load(plist_file_content)
                        except FileNotFoundError:
                            pass
                    browser_name = self.config["app"]
                    browser_version = plist.get("CFBundleShortVersionString")
                elif "linux" in self.config["platform"]:
                    command = [self.config["binary"], "--version"]
                    proc = subprocess.run(
                        command, check=False, timeout=10, capture_output=True, text=True
                    )

                    bmeta = proc.stdout.split("\n")
                    meta_re = re.compile(r"([A-z\s]+)\s+([\w.]*)")
                    if len(bmeta) != 0:
                        match = meta_re.match(bmeta[0])
                        if match:
                            browser_name = self.config["app"]
                            browser_version = match.group(2)
                    else:
                        LOG.info("Couldn't get browser version and name")
                else:
                    # Define the PowerShell command. We use this method on Windows since WMIC will
                    # soon be deprecated.
                    binary_path = self.config.get("binary")
                    command = rf'(Get-ItemProperty -Path "{binary_path}").VersionInfo.FileVersion'
                    LOG.info(
                        "Attempting to get browser application version with powershell..."
                    )
                    bmeta = subprocess.check_output(
                        ["powershell", "-Command", command],
                        text=True,
                    )
                    if not bmeta:
                        LOG.warning("Unable to acquire browser version")
                    else:
                        browser_version = bmeta.strip()
                        browser_name = self.config["app"]
                        LOG.info(
                            f"Successfully acquired browser version: {browser_version}"
                        )
            except Exception as e:
                LOG.warning(
                    "Failed to get browser meta data through fallback method: "
                    f"{e.__class__.__name__}-{e}"
                )

        if not browser_name:
            LOG.warning("Could not find a browser name")
        else:
            LOG.info(f"Browser name: {browser_name}")

        if not browser_version:
            LOG.warning("Could not find a browser version")
        else:
            LOG.info(f"Browser version: {browser_version}")

        return (browser_name, browser_version)
