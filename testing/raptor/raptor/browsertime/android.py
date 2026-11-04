#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

from cmdline import CHROME_ANDROID_APPS, FIREFOX_ANDROID_APPS
from logger.logger import RaptorLogger
from mozdevice import ADBDeviceFactory
from performance_tuning import tune_performance
from perftest import PerftestAndroid

from .base import Browsertime

LOG = RaptorLogger(component="raptor-browsertime-android")


class BrowsertimeAndroid(PerftestAndroid, Browsertime):
    """Android setup and configuration for browsertime

    When running raptor-browsertime tests on android, we create the profile (and set the proxy
    prefs in the profile that is using playback) but we don't need to copy it onto the device
    because geckodriver takes care of that.
    We tell browsertime to use our profile (we pass it in with the firefox.profileTemplate arg);
    browsertime creates a copy of that and passes that into geckodriver. Geckodriver then takes
    the profile and copies it onto the mobile device's test root for us; and then it even writes
    the geckoview app config.yaml file onto the device, which points the app to the profile on
    the device's test root.
    Therefore, raptor doesn't have to copy the profile onto the scard (and create the config.yaml)
    file ourselves. Also note when using playback, the nss certificate db is created as usual when
    mitmproxy is started (and saved in the profile) so it is already included in the profile that
    browsertime/geckodriver copies onto the device.

    Storage selection (geckodriver --android-storage):
      - "app"    : profile lives in /data/data/<pkg>/test_root and geckodriver uses run-as
                   to push it. Requires either a rooted device or a debuggable app.
      - "sdcard" : profile lives in $EXTERNAL_STORAGE/Android/data/<pkg>/files/test_root.
                   The app reads it without run-as; works on stock production phones with
                   non-debuggable APKs. Used as the fallback when the app is not debuggable
                   and the device is not rooted.
    """

    def __init__(self, app, binary, activity=None, intent=None, **kwargs):
        # Building a conditioned profile in super().__init__ initializes the
        # device before run_tests() sets the test names.
        self._test_names = None
        super().__init__(
            app,
            binary,
            **kwargs,
        )

        self.config.update({"activity": activity, "intent": intent})
        self.remote_profile = None
        self.android_storage_mode = None

    def _initialize_device(self):
        if self.device is None:
            self.device = ADBDeviceFactory(verbose=True)
            if self.config.get("disable_perf_tuning", False):
                return
            if not self.device.is_rooted:
                LOG.info("Device is not rooted, skipping perf tuning ")
                return
            tune_performance(
                self.device,
                log=LOG,
                test_names=self._test_names,
            )

    @property
    def android_external_storage(self):
        if self._remote_test_root is None:
            self._initialize_device()

            external_storage = self.device.shell_output("echo $EXTERNAL_STORAGE")
            per_app_dir = os.path.join(
                external_storage,
                "Android",
                "data",
                self.config["binary"],
                "files",
                "test_root",
            )
            download_dir = os.path.join(external_storage, "Download")

            # Rooted: prefer the per-app dir (probe + Bug 1910111 fallback).
            # Non-rooted: always /sdcard/Download — Android 11+ FUSE blocks
            # adb pull of app-written files inside the per-app dir even with
            # `appops MANAGE_EXTERNAL_STORAGE` granted to shell on some OEM
            # images. Condprof grants the app MANAGE_EXTERNAL_STORAGE so it
            # can write to /sdcard/Download with raw paths.
            if self.device.is_rooted:
                try:
                    self.device.mkdir(per_app_dir, parents=True)
                    self._remote_test_root = per_app_dir
                except Exception as e:
                    LOG.info(
                        f"Per-app external storage {per_app_dir} is not writable "
                        f"({e}); falling back to {download_dir}."
                    )
                    self._remote_test_root = download_dir
            else:
                LOG.info(
                    f"Device is not rooted; using {download_dir} as the remote "
                    "test root (per-app dir is unreliable for adb pull-back "
                    "under scoped storage)."
                )
                self._remote_test_root = download_dir

        return self._remote_test_root

    @property
    def browsertime_args(self):
        args_list = [
            "--viewPort",
            "1366x695",
            "--videoParams.convert",
            "false",
            "--videoParams.addTimer",
            "false",
            "--videoParams.androidVideoWaitTime",
            "20000",
            "--android.enabled",
            "true",
        ]

        if self.config["app"] in CHROME_ANDROID_APPS:
            args_list.extend([
                "--browser",
                "chrome",
            ])
            if self.config["app"] == "cstm-car-m":
                args_list.extend(["--chrome.android.package", "org.chromium.chrome"])
        else:
            activity = self.config["activity"]
            if self.config["app"] == "fenix":
                LOG.info(
                    "Changing initial activity to "
                    "`mozilla.telemetry.glean.debug.GleanDebugActivity`"
                )
                activity = "mozilla.telemetry.glean.debug.GleanDebugActivity"

            # Storage mode is set by setup_adb_device() based on root + debuggable
            # Default to "app" for back-compat with paths that skip setup.
            storage_mode = self.android_storage_mode or "app"
            args_list.extend([
                '--firefox.geckodriverArgs="--android-storage"',
                f'--firefox.geckodriverArgs="{storage_mode}"',
            ])

            args_list.extend([
                "--browser",
                "firefox",
                "--firefox.android.package",
                self.config["binary"],
                "--firefox.android.activity",
                activity,
            ])

        if self.config["app"] == "geckoview":
            # This is needed as geckoview is crashing on shutdown and is throwing
            # marionette errors similar to 1768889
            args_list.extend(["--ignoreShutdownFailures", "true"])

        if self.config["app"] == "fenix":
            # See bug 1768889
            args_list.extend(["--ignoreShutdownFailures", "true"])

            # If running on Fenix we must add the intent as we use a
            # special non-default one there
            if self.config.get("intent") is not None:
                args_list.extend(["--firefox.android.intentArgument=-a"])
                args_list.extend([
                    "--firefox.android.intentArgument",
                    self.config["intent"],
                ])

                # Change glean ping names in all cases on Fenix
                args_list.extend([
                    "--firefox.android.intentArgument=--es",
                    "--firefox.android.intentArgument=startNext",
                    "--firefox.android.intentArgument=" + self.config["activity"],
                    "--firefox.android.intentArgument=--esa",
                    "--firefox.android.intentArgument=sourceTags",
                    "--firefox.android.intentArgument=automation",
                    "--firefox.android.intentArgument=--ez",
                    "--firefox.android.intentArgument=performancetest",
                    "--firefox.android.intentArgument=true",
                ])

                args_list.extend(["--firefox.android.intentArgument=-d"])
                args_list.extend(["--firefox.android.intentArgument", "about:blank"])

        return args_list

    def setup_chrome_args(self, test):
        chrome_args = [
            "--use-mock-keychain",
            "--no-default-browser-check",
            "--no-first-run",
            "--no-experiments",
            "--disable-site-isolation-trials",
        ]

        # Disable finch experiments
        chrome_args += ["--enable-benchmarking"]

        if test.get("playback", False):
            chrome_args.extend(self.playback_chrome_args())

        if self.debug_mode:
            chrome_args.extend(["--auto-open-devtools-for-tabs"])

        args_list = []
        for arg in chrome_args:
            args_list.extend(["--chrome.args=" + str(arg.replace("'", '"'))])

        return args_list

    def build_browser_profile(self):
        super().build_browser_profile()

        if self.config["app"] in FIREFOX_ANDROID_APPS:
            # Merge in the Android profile.
            path = os.path.join(self.profile_data_dir, "raptor-android")
            LOG.info(f"Merging profile: {path}")
            self.profile.merge(path)

            # There's no great way to have "after" advice in Python, so we do this
            # in super and then again here since the profile merging re-introduces
            # the "#MozRunner" delimiters.
            self.remove_mozprofile_delimiters_from_profile()

    def setup_adb_device(self):
        self._initialize_device()

        rooted = self.device.is_rooted

        self.clear_app_data()
        self.set_debug_app_flag()

        # Decide the storage mode for geckodriver. run-as only works on debuggable
        # apps. Use it when available, otherwise route everything through external
        # storage so the test works on non-rooted phones with production APKs.
        package = self.config["binary"]
        debuggable = False
        if not rooted and self.config["app"] in FIREFOX_ANDROID_APPS:
            debuggable = self.device.is_package_debuggable(package)

        if rooted or debuggable:
            self.android_storage_mode = "app"
            self.device.run_as_package = package
        else:
            self.android_storage_mode = "sdcard"
            LOG.info(
                f"Device is not rooted and {package} is not debuggable; using "
                "--android-storage sdcard for the geckodriver profile."
            )

        self.geckodriver_profile = os.path.join(
            self.android_external_storage,
            f"{self.config['binary']}-geckodriver-profile",
        )

        # Best-effort cleanup, can fail if storage/shell permissions aren't setup properly
        if self.device.exists(self.geckodriver_profile):
            try:
                self.device.rm(self.geckodriver_profile, force=True, recursive=True)
            except Exception as e:
                LOG.info(
                    f"Could not remove stale geckodriver profile "
                    f"{self.geckodriver_profile}: {e}. "
                    "Continuing; geckodriver will overwrite it."
                )

    def run_test_setup(self, test):
        super().run_test_setup(test)

        self.set_reverse_ports(test)

        if self.config["app"] in FIREFOX_ANDROID_APPS:
            if self.playback and self.playback.playback_mode != "direct":
                self.turn_on_android_app_proxy()
            self.remove_mozprofile_delimiters_from_profile()

    def run_tests(self, tests):
        self.setup_adb_device()

        if self.config["app"] in CHROME_ANDROID_APPS:
            # Make sure that chrome is enabled on the device
            self.device.shell_output("pm enable com.android.chrome")

        return super().run_tests(tests)

    def run_test_teardown(self, test):
        LOG.info("removing reverse socket connections")
        self.device.remove_socket_connections("reverse")

        super().run_test_teardown(test)
