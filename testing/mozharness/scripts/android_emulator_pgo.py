#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import glob
import json
import os
import posixpath
import subprocess
import sys
import time

# load modules from parent dir
sys.path.insert(1, os.path.dirname(sys.path[0]))

from mozharness.base.script import BaseScript, PreScriptAction
from mozharness.mozilla.automation import EXIT_STATUS_DICT, TBPL_RETRY
from mozharness.mozilla.mozbase import MozbaseMixin
from mozharness.mozilla.testing.android import AndroidMixin
from mozharness.mozilla.testing.testbase import TestingMixin, testing_config_options

# Upper bound on how long to wait for build/pgo/index.html to go through the
# training corpus. It must exceed the sum of the timeouts of the items it runs on
# Android, about 8 minutes, or 13 minutes on slow emulators, and matches the time
# limit of the screen recording.
TRAINING_TIMEOUT = 1200


class AndroidProfileRun(TestingMixin, BaseScript, MozbaseMixin, AndroidMixin):
    """
    Mozharness script to generate an android PGO profile using the emulator
    """

    python_constraints = "requirements-android-pgo.txt"

    config_options = copy.deepcopy(testing_config_options)

    def __init__(self, require_config_file=False):
        super().__init__(
            config_options=self.config_options,
            all_actions=[
                "download",
                "create-virtualenv",
                "start-emulator",
                "verify-device",
                "install",
                "run-tests",
            ],
            require_config_file=require_config_file,
            config={
                "virtualenv_modules": [],
                "virtualenv_requirements": [],
                "require_test_zip": True,
                "mozbase_requirements": "mozbase_source_requirements.txt",
            },
        )

        # these are necessary since self.config is read only
        c = self.config
        self.installer_path = c.get("installer_path")
        self.device_serial = "emulator-5554"

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super().query_abs_dirs()
        dirs = {}

        dirs["abs_test_install_dir"] = os.path.join(abs_dirs["abs_src_dir"], "testing")
        # On macOS, use checkout dir; on Linux, use /builds/worker
        if sys.platform == "darwin":
            base_dir = abs_dirs["abs_src_dir"]
        else:
            base_dir = "/builds/worker"
        dirs["abs_artifacts_dir"] = os.path.join(base_dir, "artifacts")
        dirs["abs_blob_upload_dir"] = os.path.join(
            dirs["abs_artifacts_dir"], "blobber_upload_dir"
        )
        dirs["abs_workspace_dir"] = os.path.join(base_dir, "workspace")
        work_dir = os.environ.get("MOZ_FETCHES_DIR") or abs_dirs["abs_work_dir"]
        dirs["abs_xre_dir"] = os.path.join(work_dir, "hostutils")
        sdk_dirname = (
            "android-sdk-macosx" if sys.platform == "darwin" else "android-sdk-linux"
        )
        dirs["abs_sdk_dir"] = os.path.join(work_dir, sdk_dirname)
        dirs["abs_avds_dir"] = os.path.join(work_dir, "android-device")
        dirs["abs_bundletool_path"] = os.path.join(work_dir, "bundletool.jar")

        for key in dirs.keys():
            if key not in abs_dirs:
                abs_dirs[key] = dirs[key]
        self.abs_dirs = abs_dirs
        return self.abs_dirs

    ##########################################
    # Actions for AndroidProfileRun        #
    ##########################################

    def preflight_install(self):
        # in the base class, this checks for mozinstall, but we don't use it
        pass

    @PreScriptAction("create-virtualenv")
    def pre_create_virtualenv(self, action):
        dirs = self.query_abs_dirs()
        self.register_virtualenv_module(
            "marionette",
            os.path.join(dirs["abs_test_install_dir"], "marionette", "client"),
        )

    def download(self):
        """
        Download host utilities
        """
        dirs = self.query_abs_dirs()
        self.xre_path = dirs["abs_xre_dir"]

    def install(self):
        """
        Install APKs on the device.
        """
        assert self.installer_path is not None, (
            "Either add installer_path to the config or use --installer-path."
        )
        self.install_android_app(self.installer_path)
        self.info(f"Finished installing apps for {self.device_serial}")

    def _wait_for_process(self, adbdevice, process_name, running, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                output = adbdevice.shell_output(f"pgrep {process_name}").strip()
                found = bool(output)
            except Exception:
                found = False
            if found == running:
                return True
            time.sleep(0.5)
        return False

    def _stop_and_pull_recording(self, adbdevice, recording_path, video_filename):
        try:
            self.info("Stopping screen recording...")
            try:
                adbdevice.shell_output("pkill -SIGINT screenrecord", timeout=30)
            except Exception:
                # pkill exits with 1 if no process was found; ignore and still pull
                pass
            if not self._wait_for_process(
                adbdevice, "screenrecord", running=False, timeout=15
            ):
                self.warning("Timed out waiting for screenrecord to stop")
            dirs = self.query_abs_dirs()
            blob_dir = dirs["abs_blob_upload_dir"]
            os.makedirs(blob_dir, exist_ok=True)
            video_dest = os.path.join(blob_dir, video_filename)
            adbdevice.pull(recording_path, video_dest)
            self.info(f"Screen recording saved to {video_dest}")
            adbdevice.shell_output(f"rm {recording_path}")
        except Exception as e:
            self.warning(f"Failed to stop/pull screen recording: {e}")

    def _log_item(self, item):
        """Log an item of the training corpus, reported by build/pgo/index.html
        when it is done."""
        self.info(
            f"PGO training item {item['url']}: {item['outcome']} after "
            f"{item['ms'] / 1000:.1f}s"
        )

    def _wait_for_training(self, driver):
        from marionette_driver import Wait, errors

        start = time.monotonic()
        try:
            Wait(
                driver,
                timeout=TRAINING_TIMEOUT,
                interval=5,
                ignored_exceptions=(
                    errors.JavascriptException,
                    errors.NoSuchWindowException,
                ),
            ).until(
                lambda m: m.execute_script(
                    "return !!window.pgoTrainingDone;", sandbox=None
                )
            )
        except errors.TimeoutException:
            self.warning(f"Training did not finish within {TRAINING_TIMEOUT}s")
            return

        self.info(f"Training finished after {int(time.monotonic() - start)}s")

    def run_tests(self):
        """
        Generate the PGO profile data
        """
        from marionette_driver.marionette import Marionette
        from mozdevice import ADBDeviceFactory, ADBTimeoutError
        from mozprofile import Preferences

        app = self.query_package_name()

        IP = "10.0.2.2"
        PORT = 8888

        PATH_MAPPINGS = {
            "/webkit/PerformanceTests": "third_party/webkit/PerformanceTests",
        }

        dirs = self.query_abs_dirs()
        topsrcdir = dirs["abs_src_dir"]
        adb = self.query_exe("adb")

        sys.path.insert(0, os.path.join(topsrcdir, "build", "pgo"))
        from pgo_metrics import report_perfherder, stage
        from pgoserver import PGOServer

        path_mappings = {
            k: os.path.join(topsrcdir, v) for k, v in PATH_MAPPINGS.items()
        }

        httpd = PGOServer(
            os.path.join(topsrcdir, "build", "pgo"),
            PORT,
            path_mappings,
            {"/pgo-item": self._log_item},
        )
        httpd.start()

        profile_data_dir = os.path.join(topsrcdir, "testing", "profiles")
        with open(os.path.join(profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["profileserver"]

        prefpaths = [
            os.path.join(profile_data_dir, profile, "user.js")
            for profile in base_profiles
        ]

        prefs = {}
        for path in prefpaths:
            prefs.update(Preferences.read_prefs(path))

        interpolation = {
            "server": f"{httpd.httpd.server_address[0]}:{httpd.httpd.server_address[1]}",
            "OOP": "false",
        }
        for k, v in prefs.items():
            if isinstance(v, str):
                v = v.format(**interpolation)
            prefs[k] = Preferences.cast(v)

        adbdevice = ADBDeviceFactory(adb=adb, device="emulator-5554")

        outputdir = posixpath.join(adbdevice.test_root, "pgo_profile")
        jarlog = posixpath.join(outputdir, "en-US.log")
        profdata = posixpath.join(outputdir, "default_%p_random_%m.profraw")

        env = {}
        env["XPCOM_DEBUG_BREAK"] = "warn"
        env["MOZ_IN_AUTOMATION"] = "1"
        env["MOZ_JAR_LOG_FILE"] = jarlog
        env["LLVM_PROFILE_FILE"] = profdata

        if self.query_minidump_stackwalk():
            os.environ["MINIDUMP_STACKWALK"] = self.minidump_stackwalk_path
        os.environ["MINIDUMP_SAVE_PATH"] = self.query_abs_dirs()["abs_blob_upload_dir"]
        if not self.symbols_path:
            self.symbols_path = os.environ.get("MOZ_FETCHES_DIR")

        adbdevice.mkdir(outputdir, parents=True)

        # Start screen recording of PGO training
        recording_path = "/sdcard/pgo-training.mp4"
        recording_started = False
        try:
            self.info("Starting screen recording of PGO training session...")
            sdk = int(
                adbdevice.shell_output(
                    "getprop ro.build.version.sdk", timeout=5
                ).strip()
            )
            # --time-limit > 180 is only supported on Android 14 (API 34) and above
            time_limit_arg = f"--time-limit {TRAINING_TIMEOUT} " if sdk >= 34 else ""
            adbdevice.shell_output(
                f"sh -c 'screenrecord --verbose {time_limit_arg}{recording_path} > /dev/null 2>&1 &'",
                timeout=10,
            )
            if self._wait_for_process(
                adbdevice, "screenrecord", running=True, timeout=10
            ):
                recording_started = True
                self.info("Screen recording started")
            else:
                self.warning("screenrecord did not start within timeout")
        except Exception as e:
            self.warning(f"Failed to start screen recording: {e}")

        try:
            # Run Fennec a first time to initialize its profile
            with stage("profile initialization"):
                driver = Marionette(
                    app="fennec",
                    package_name=app,
                    adb_path=adb,
                    bin="geckoview-androidTest.apk",
                    prefs=prefs,
                    connect_to_running_emulator=True,
                    startup_timeout=1000,
                    env=env,
                    symbols_path=self.symbols_path,
                )
                driver.start_session()

            # Now generate the profile and wait for it to complete
            # TODO: Add support for the pgo-extended-corpus to get JetStream3 running here.
            with stage("training run"):
                query = "android=true"
                if self.config.get("pgo_slow_emulator"):
                    query += "&slowEmulator=true"
                driver.navigate(f"http://{IP}:{PORT}/index.html?{query}")
                self._wait_for_training(driver)

                driver.quit(in_app=True)

            if recording_started:
                self._stop_and_pull_recording(
                    adbdevice, recording_path, "pgo-training.mp4"
                )

            training_errors = httpd.training_errors()
            for error in training_errors:
                self.error(f"PGO training failed: {error}")
            if training_errors:
                self.fatal("PGO training failed")

            # Pull all the profraw files and en-US.log
            dirs = self.query_abs_dirs()
            workspace_dir = dirs["abs_workspace_dir"]
            os.makedirs(workspace_dir, exist_ok=True)
            with stage("profile pull"):
                adbdevice.pull(outputdir, workspace_dir)
        except ADBTimeoutError:
            if recording_started:
                self._stop_and_pull_recording(
                    adbdevice, recording_path, "pgo-training-partial.mp4"
                )
            self.fatal(
                "INFRA-ERROR: Failed with an ADBTimeoutError",
                EXIT_STATUS_DICT[TBPL_RETRY],
            )

        dirs = self.query_abs_dirs()
        workspace_dir = dirs["abs_workspace_dir"]
        profraw_files = glob.glob(os.path.join(workspace_dir, "*.profraw"))
        if not profraw_files:
            self.fatal(f"Could not find any profraw files in {workspace_dir}")
        elif len(profraw_files) == 1:
            self.fatal(
                "Only found 1 profraw file. Did child processes terminate early?"
            )
        merged_profdata = os.path.join(workspace_dir, "merged.profdata")
        llvm_profdata = os.path.join(
            os.environ["MOZ_FETCHES_DIR"], "clang/bin/llvm-profdata"
        )
        merge_cmd = [
            llvm_profdata,
            "merge",
            "-o",
            merged_profdata,
        ] + profraw_files
        with stage("llvm-profdata merge"):
            rc = subprocess.call(merge_cmd)
        if rc != 0:
            self.fatal(
                "INFRA-ERROR: Failed to merge profile data. Corrupt profile?",
                EXIT_STATUS_DICT[TBPL_RETRY],
            )

        # tarfile doesn't support xz in this version of Python
        artifacts_dir = dirs["abs_artifacts_dir"]
        os.makedirs(artifacts_dir, exist_ok=True)
        tar_cmd = [
            "tar",
            "-acvf",
            os.path.join(artifacts_dir, "profdata.tar.xz"),
            "-C",
            workspace_dir,
            "merged.profdata",
            "en-US.log",
        ]
        subprocess.check_call(tar_cmd)

        report_perfherder(llvm_profdata, profraw_files, merged_profdata, artifacts_dir)

        httpd.stop()


if __name__ == "__main__":
    test = AndroidProfileRun()
    test.run_and_exit()
