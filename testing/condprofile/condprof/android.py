# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Drives an android device."""

import os
import posixpath
import tempfile
import contextlib
import time
import logging

import attr
from arsenic.services import Geckodriver, free_port, subprocess_based_service
from mozdevice import ADBDeviceFactory, ADBError

from condprof.util import (
    write_yml_file,
    logger,
    DEFAULT_PREFS,
    BaseEnv,
)


# XXX most of this code should migrate into mozdevice - see Bug 1574849
class AndroidDevice:
    def __init__(
        self,
        app_name,
        marionette_port=2828,
        verbose=False,
        remote_test_root="/sdcard/test_root/",
    ):
        self.app_name = app_name

        # XXX make that an option
        if "fenix" in app_name:
            self.activity = "org.mozilla.fenix.IntentReceiverActivity"
        else:
            self.activity = "org.mozilla.geckoview_example.GeckoViewActivity"
        self.verbose = verbose
        self.device = None
        self.marionette_port = marionette_port
        self.profile = None
        self.remote_profile = None
        self.log_file = None
        self.remote_test_root = remote_test_root
        self._adb_fh = None

    def _set_adb_logger(self, log_file):
        self.log_file = log_file
        if self.log_file is None:
            return
        logger.info(f"Setting ADB log file to {self.log_file}")
        adb_logger = logging.getLogger("adb")
        adb_logger.setLevel(logging.DEBUG)
        self._adb_fh = logging.FileHandler(self.log_file)
        self._adb_fh.setLevel(logging.DEBUG)
        adb_logger.addHandler(self._adb_fh)

    def _unset_adb_logger(self):
        if self._adb_fh is None:
            return
        logging.getLogger("adb").removeHandler(self._adb_fh)
        self._adb_fh = None

    def clear_logcat(self, timeout=None, buffers=[]):
        if not self.device:
            return
        self.device.clear_logcat(timeout, buffers)

    def get_logcat(self):
        if not self.device:
            return None
        # we don't want to have ADBCommand dump the command
        # in the debug stream so we reduce its verbosity here
        # temporarely
        old_verbose = self.device._verbose
        self.device._verbose = False
        try:
            return self.device.get_logcat()
        finally:
            self.device._verbose = old_verbose

    def prepare(self, profile, logfile):
        self._set_adb_logger(logfile)
        try:
            # See android_emulator_pgo.py run_tests for more
            # details on why test_root must be /sdcard/test_root
            # for android pgo due to Android 4.3.
            self.device = ADBDeviceFactory(
                verbose=self.verbose, logger_name="adb", test_root=self.remote_test_root
            )
        except Exception:
            logger.error("Cannot initialize device")
            raise
        device = self.device
        self.profile = profile

        # checking that the app is installed
        if not device.is_app_installed(self.app_name):
            raise Exception(f"{self.app_name} is not installed")

        if not device.confirm_clear_app_data(self.app_name):
            raise Exception(
                "Abort: Declined to clear app data, can't build conditioned profile."
            )

        # pm clear wipes /data/data/<pkg>/ and /sdcard/Android/data/<pkg>/,
        # avoiding shell-can't-rm-app-owned-files issues on Android 11+. Resets
        # runtime perms, so grants below must come after this.
        logger.info(f"Clearing app data for {self.app_name}")
        try:
            device.shell_output(f"pm clear {self.app_name}")
        except ADBError as e:
            logger.info(f"pm clear {self.app_name} failed: {e}. Continuing.")

        logger.info(f"Setting {self.app_name} as the debug app on the phone")
        device.shell(
            f"am set-debug-app --persistent {self.app_name}",
            stdout_callback=logger.info,
        )

        # mirror geckodriver's sdcard-storage grants so the app can read/write
        # the pushed profile (see testing/geckodriver/src/android.rs prepare())
        for perm in ("READ_EXTERNAL_STORAGE", "WRITE_EXTERNAL_STORAGE"):
            try:
                device.shell(
                    f"pm grant {self.app_name} android.permission.{perm}",
                    stdout_callback=logger.info,
                )
            except ADBError as e:
                logger.info(
                    f"Could not grant {perm} to {self.app_name} (likely already "
                    f"granted or not applicable on this Android version): {e}"
                )

        # on Android 11+ the app needs MANAGE_EXTERNAL_STORAGE to open
        # /sdcard/Download/profile/... with raw paths
        try:
            device.shell_output(
                f"appops set {self.app_name} MANAGE_EXTERNAL_STORAGE allow"
            )
            logger.info(
                f"Granted MANAGE_EXTERNAL_STORAGE to {self.app_name} via appops"
            )
        except ADBError as e:
            logger.info(
                f"Could not grant MANAGE_EXTERNAL_STORAGE to {self.app_name} "
                f"(likely Android < 11 where the op does not exist): {e}"
            )

        # grant shell MANAGE_EXTERNAL_STORAGE so adb pull
        # works in the per-app dir on OEM images that honor it. Numeric uid
        # first since `--uid shell` is rejected by some appops builds.
        if not device.is_rooted:
            granted = False
            for cmd in (
                "appops set --uid 2000 MANAGE_EXTERNAL_STORAGE allow",
                "appops set --uid shell MANAGE_EXTERNAL_STORAGE allow",
            ):
                try:
                    device.shell_output(cmd)
                    logger.info(f"Granted MANAGE_EXTERNAL_STORAGE to shell via: {cmd}")
                    granted = True
                    break
                except ADBError as e:
                    logger.info(f"appops grant attempt failed ({cmd}): {e}")
            if not granted:
                logger.warning(
                    "Could not grant MANAGE_EXTERNAL_STORAGE to shell; "
                    "pulling the profile back from the per-app external "
                    "storage dir may fail on Android 11+."
                )

        # creating the profile on the device
        logger.info("Creating the profile on the device")

        remote_profile = posixpath.join(self.remote_test_root, "profile")
        logger.info(f"The profile on the phone will be at {remote_profile}")

        # best-effort cleanup, this can fail when storage/shell permissions are
        # not setup correctly, push below overwrites existing files, but can leave
        # non-overwritten files in the same state as before
        try:
            device.rm(remote_profile, force=True, recursive=True)
        except ADBError as e:
            logger.info(
                f"Could not remove stale remote profile {remote_profile}: {e}. "
                "Continuing."
            )

        logger.info(f"Pushing {self.profile} on the phone")
        device.push(profile, remote_profile)

        # best-effort, FAT/scoped-storage paths reject chmod, push sets perms though
        try:
            device.chmod(remote_profile, recursive=True)
        except ADBError as e:
            logger.info(
                f"Could not chmod {remote_profile} "
                f"(sdcard / scoped-storage path): {e}. Continuing."
            )

        self.profile = profile
        self.remote_profile = remote_profile

        # creating the yml file
        yml_data = {
            "args": ["-marionette", "-profile", self.remote_profile],
            "prefs": DEFAULT_PREFS,
            "env": {"LOG_VERBOSE": 1, "R_LOG_LEVEL": 6, "MOZ_LOG": ""},
        }

        yml_name = f"{self.app_name}-geckoview-config.yaml"
        yml_on_host = posixpath.join(tempfile.mkdtemp(), yml_name)
        write_yml_file(yml_on_host, yml_data)
        tmp_on_device = posixpath.join("/data", "local", "tmp")
        if not device.exists(tmp_on_device):
            raise IOError(f"{tmp_on_device} does not exists on the device")
        yml_on_device = posixpath.join(tmp_on_device, yml_name)
        try:
            device.rm(yml_on_device, force=True, recursive=True)
            device.push(yml_on_host, yml_on_device)
            device.chmod(yml_on_device, recursive=True)
        except Exception:
            logger.info("could not create the yaml file on device. Permission issue?")
            raise

        # command line 'extra' args not used with geckoview apps; instead we use
        # an on-device config.yml file
        intent = "android.intent.action.VIEW"
        device.stop_application(self.app_name)
        device.launch_application(
            self.app_name, self.activity, intent, extras=None, url="about:blank"
        )
        if not device.process_exist(self.app_name):
            raise Exception(f"Could not start {self.app_name}")

        logger.info(f"Creating socket forwarding on port {self.marionette_port}")
        device.forward(
            local=f"tcp:{self.marionette_port}",
            remote=f"tcp:{self.marionette_port}",
        )

        # we don't have a clean way for now to check that GV or Fenix
        # is ready to handle our tests. So here we just wait 30s
        logger.info("Sleeping for 30s")
        time.sleep(30)

    def stop_browser(self):
        logger.info(f"Stopping {self.app_name}")
        try:
            self.device.stop_application(self.app_name)
        except ADBError:
            logger.info("Could not stop the application using force-stop")

        time.sleep(5)
        if self.device.process_exist(self.app_name):
            logger.info(f"{self.app_name} still running, trying SIGKILL")
            num_tries = 0
            while self.device.process_exist(self.app_name) and num_tries < 5:
                try:
                    self.device.pkill(self.app_name)
                except ADBError:
                    pass
                num_tries += 1
                time.sleep(1)
        logger.info(f"{self.app_name} stopped")

    def collect_profile(self):
        logger.info(f"Collecting profile from {self.remote_profile}")
        self.device.pull(self.remote_profile, self.profile)

    def close(self):
        self._unset_adb_logger()
        if self.device is None:
            return
        try:
            self.device.remove_forwards(f"tcp:{self.marionette_port}")
        except ADBError:
            logger.warning("Could not remove forward port")


# XXX redundant, remove
@contextlib.contextmanager
def device(
    app_name, marionette_port=2828, verbose=True, remote_test_root="/sdcard/test_root/"
):
    device_ = AndroidDevice(
        app_name, marionette_port, verbose, remote_test_root=remote_test_root
    )
    try:
        yield device_
    finally:
        device_.close()


@attr.s
class AndroidGeckodriver(Geckodriver):
    async def start(self):
        port = free_port()
        await self._check_version()
        logger.info(f"Running Webdriver on port {port}")
        logger.info("Running Marionette on port 2828")
        pargs = [
            self.binary,
            "--log",
            "trace",
            "--port",
            str(port),
            "--marionette-port",
            "2828",
        ]
        logger.info("Connecting on Android device")
        pargs.append("--connect-existing")
        return await subprocess_based_service(
            pargs, f"http://localhost:{port}", self.log_file
        )


class AndroidEnv(BaseEnv):
    @contextlib.contextmanager
    def get_device(self, *args, **kw):
        with device(self.firefox, *args, **kw) as d:
            self.device = d
            yield self.device

    def get_target_platform(self):
        app = self.firefox.split("org.mozilla.")[-1]
        if self.device_name is None:
            return app
        return f"{self.device_name}-{app}"

    def dump_logs(self):
        logger.info("Dumping Android logs")
        try:
            logcat = self.device.get_logcat()
            if logcat:
                # local path, not using posixpath
                logfile = os.path.join(self.archive, "logcat.log")
                logger.info(f"Writing logcat at {logfile}")
                with open(logfile, "wb") as f:
                    for line in logcat:
                        f.write(line.encode("utf8", errors="replace") + b"\n")
            else:
                logger.info("logcat came back empty")
        except Exception:
            logger.error("Could not extract the logcat", exc_info=True)

    @contextlib.contextmanager
    def get_browser(self):
        yield

    def get_browser_args(self, headless, prefs=None):
        # XXX merge with DesktopEnv.get_browser_args
        options = ["--allow-downgrade"]
        if headless:
            options.append("-headless")
        if prefs is None:
            prefs = {}
        return {"moz:firefoxOptions": {"args": options, "prefs": prefs}}

    def prepare(self, logfile):
        self.device.prepare(self.profile, logfile)

    def get_browser_version(self):
        return self.target_platform + "-XXXneedtograbversion"

    def get_geckodriver(self, log_file):
        return AndroidGeckodriver(binary=self.geckodriver, log_file=log_file)

    def check_session(self, session):
        async def fake_close(*args):
            pass

        session.close = fake_close

    def collect_profile(self):
        self.device.collect_profile()

    def stop_browser(self):
        self.device.stop_browser()
