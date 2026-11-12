#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import datetime
import glob
import json
import os
import pathlib
import posixpath
import re
import shutil
import sys
import tempfile
import time
import traceback

import mozcrash
import mozdevice
import mozinfo
import six
from mozlog import commandline

LOGGER_NAME = "gtest"
log = None
PERFHERDER_MATCHER = re.compile(r"PERFHERDER_DATA:\s*(\{.*\})\s*$")


class RemoteGTests:
    """
    A test harness to run gtest on Android.
    """

    def __init__(self):
        self.device = None
        self.perfherder_data = []

    def build_environment(self, shuffle, test_filter):
        """
        Create and return a dictionary of all the appropriate env variables
        and values.
        """
        env = {}
        env["XPCOM_DEBUG_BREAK"] = "stack-and-abort"
        env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
        env["MOZ_CRASHREPORTER"] = "1"
        env["MOZ_RUN_GTEST"] = "1"
        env["MOZ_GTEST_LOG_PATH"] = self.remote_log
        env["MOZ_GTEST_CWD"] = self.remote_profile
        env["MOZ_GTEST_MINIDUMPS_PATH"] = self.remote_minidumps
        env["MOZ_IN_AUTOMATION"] = "1"
        env["MOZ_ANDROID_LIBDIR_OVERRIDE"] = posixpath.join(
            self.remote_libdir, "libxul.so"
        )
        if shuffle:
            env["GTEST_SHUFFLE"] = "True"
        if test_filter:
            env["GTEST_FILTER"] = test_filter

        # webrender needs gfx.webrender.all=true, gtest doesn't use prefs
        env["MOZ_WEBRENDER"] = "1"

        return env

    def run_gtest(
        self,
        test_dir,
        shuffle,
        test_filter,
        package,
        adb_path,
        device_serial,
        remote_test_root,
        libxul_path,
        symbols_path,
    ):
        """
        Launch the test app, run gtest, collect test results and wait for completion.
        Return False if a crash or other failure is detected, else True.
        """
        # mach gtest will call run_gtest() directly
        setup_logging()
        update_mozinfo()
        self.device = mozdevice.ADBDeviceFactory(
            adb=adb_path,
            device=device_serial,
            test_root=remote_test_root,
            logger_name=LOGGER_NAME,
            verbose=False,
            run_as_package=package,
        )
        root = self.device.test_root
        self.remote_profile = posixpath.join(root, "gtest-profile")
        self.remote_minidumps = posixpath.join(root, "gtest-minidumps")
        self.remote_log = posixpath.join(root, "gtest.log")
        self.remote_libdir = posixpath.join(root, "gtest")

        self.package = package
        self.cleanup()
        self.device.mkdir(self.remote_profile)
        self.device.mkdir(self.remote_minidumps)
        self.device.mkdir(self.remote_libdir)

        log.info("Running Android gtest")
        if not self.device.is_app_installed(self.package):
            raise Exception("%s is not installed on this device" % self.package)

        # Push the gtest libxul.so to the device. The harness assumes an architecture-
        # appropriate library is specified and pushes it to the arch-agnostic remote
        # directory.
        # TODO -- consider packaging the gtest libxul.so in an apk
        self.device.push(libxul_path, self.remote_libdir)

        for buildid in ["correct", "broken", "missing"]:
            libxul_buildid_name = f"libxul_{buildid}_buildid.so"
            libxul_buildid_path = os.path.join(
                os.path.dirname(libxul_path), libxul_buildid_name
            )
            if os.path.isfile(libxul_buildid_path):
                self.device.push(libxul_buildid_path, self.remote_libdir)

        # Push support files to device. Avoid gtest_bin so that libxul.so
        # is not included.
        for f in glob.glob(os.path.join(test_dir, "**"), recursive=True):
            if not "gtest_bin" in os.path.abspath(f):
                self.device.push(f, self.remote_profile)

        if test_filter is not None:
            test_filter = six.ensure_text(test_filter)
        env = self.build_environment(shuffle, test_filter)
        args = [
            "-unittest",
            "--gtest_death_test_style=threadsafe",
            "-profile %s" % self.remote_profile,
        ]
        if "geckoview" in self.package:
            activity = "TestRunnerActivity"
            self.device.launch_activity(
                self.package,
                activity_name=activity,
                moz_env=env,
                extra_args=args,
                wait=False,
            )
        else:
            self.device.launch_fennec(self.package, moz_env=env, extra_args=args)
        waiter = AppWaiter(
            self.device, self.remote_log, on_perfherder=self.perfherder_data.append
        )
        timed_out = waiter.wait(self.package)
        self.shutdown(use_kill=True if timed_out else False)
        if self.perfherder_data and "MOZ_AUTOMATION" in os.environ:
            upload_dir = pathlib.Path(os.getenv("MOZ_UPLOAD_DIR"))
            upload_dir.mkdir(parents=True, exist_ok=True)
            merged_perfherder_data = self.merge_perfherder_data(self.perfherder_data)
            for framework_name, data in merged_perfherder_data.items():
                file_name = (
                    "perfherder-data-gtest.json"
                    if len(merged_perfherder_data) == 1
                    else f"perfherder-data-gtest-{framework_name}.json"
                )
                out_path = upload_dir / file_name
                with out_path.open("w", encoding="utf-8") as f:
                    json.dump(data, f)
        if self.check_for_crashes(symbols_path):
            return False
        return True

    def shutdown(self, use_kill):
        """
        Stop the remote application.
        If use_kill is specified, a multi-stage kill procedure is used,
        attempting to trigger ANR and minidump reports before ending
        the process.
        """
        if not use_kill:
            self.device.stop_application(self.package)
        else:
            # Trigger an ANR report with "kill -3" (SIGQUIT)
            try:
                self.device.pkill(self.package, sig=3, attempts=1)
            except mozdevice.ADBTimeoutError:
                raise
            except:  # NOQA: E722
                pass
            time.sleep(3)
            # Trigger a breakpad dump with "kill -6" (SIGABRT)
            try:
                self.device.pkill(self.package, sig=6, attempts=1)
            except mozdevice.ADBTimeoutError:
                raise
            except:  # NOQA: E722
                pass
            # Wait for process to end
            retries = 0
            while retries < 3:
                if self.device.process_exist(self.package):
                    log.info("%s still alive after SIGABRT: waiting..." % self.package)
                    time.sleep(5)
                else:
                    break
                retries += 1
            if self.device.process_exist(self.package):
                try:
                    self.device.pkill(self.package, sig=9, attempts=1)
                except mozdevice.ADBTimeoutError:
                    raise
                except:  # NOQA: E722
                    log.warning("%s still alive after SIGKILL!" % self.package)
            if self.device.process_exist(self.package):
                self.device.stop_application(self.package)
        # Test harnesses use the MOZ_CRASHREPORTER environment variables to suppress
        # the interactive crash reporter, but that may not always be effective;
        # check for and cleanup errant crashreporters.
        crashreporter = "%s.CrashReporter" % self.package
        if self.device.process_exist(crashreporter):
            log.warning("%s unexpectedly found running. Killing..." % crashreporter)
            try:
                self.device.pkill(crashreporter)
            except mozdevice.ADBTimeoutError:
                raise
            except:  # NOQA: E722
                pass
        if self.device.process_exist(crashreporter):
            log.error("%s still running!!" % crashreporter)

    def check_for_crashes(self, symbols_path):
        """
        Pull minidumps from the remote device and generate crash reports.
        Returns True if a crash was detected, or suspected.
        """
        try:
            dump_dir = tempfile.mkdtemp()
            remote_dir = self.remote_minidumps
            if not self.device.is_dir(remote_dir):
                return False
            self.device.pull(remote_dir, dump_dir)
            crashed = mozcrash.check_for_crashes(
                dump_dir, symbols_path, test_name="gtest"
            )
        except Exception as e:
            log.error("unable to check for crashes: %s" % str(e))
            crashed = True
        finally:
            try:
                shutil.rmtree(dump_dir)
            except Exception:
                log.warning("unable to remove directory: %s" % dump_dir)
        return crashed

    def cleanup(self):
        if self.device:
            self.device.stop_application(self.package)
            self.device.rm(self.remote_log, force=True)
            self.device.rm(self.remote_profile, recursive=True, force=True)
            self.device.rm(self.remote_minidumps, recursive=True, force=True)
            self.device.rm(self.remote_libdir, recursive=True, force=True)

    def merge_perfherder_data(self, perfherder_data):
        grouped = {}

        for data in perfherder_data:
            framework_name = data.get("framework", {}).get("name")
            suites_by_name = grouped.setdefault(framework_name, {})
            for suite in data.get("suites", []):
                suite_name = suite.get("name")
                suite_data = suites_by_name.setdefault(
                    suite_name, {"name": suite_name, "subtests": []}
                )
                suite_data["subtests"].extend(suite.get("subtests", []))

        results = {}
        for framework_name, suites in grouped.items():
            results[framework_name] = {
                "framework": {"name": framework_name},
                "suites": list(suites.values()),
            }

        return results


class AppWaiter:
    def __init__(
        self,
        device,
        remote_log,
        test_proc_timeout=1200,
        test_proc_no_output_timeout=300,
        test_proc_start_timeout=60,
        output_poll_interval=10,
        on_perfherder=None,
    ):
        self.device = device
        self.remote_log = remote_log
        self.start_time = datetime.datetime.now()
        self.timeout_delta = datetime.timedelta(seconds=test_proc_timeout)
        self.output_timeout_delta = datetime.timedelta(
            seconds=test_proc_no_output_timeout
        )
        self.start_timeout_delta = datetime.timedelta(seconds=test_proc_start_timeout)
        self.output_poll_interval = output_poll_interval
        self.last_output_time = datetime.datetime.now()
        self.remote_log_len = 0
        self.on_perfherder = on_perfherder or (lambda _data: None)

    def start_timed_out(self):
        if datetime.datetime.now() - self.start_time > self.start_timeout_delta:
            return True
        return False

    def timed_out(self):
        if datetime.datetime.now() - self.start_time > self.timeout_delta:
            return True
        return False

    def output_timed_out(self):
        if datetime.datetime.now() - self.last_output_time > self.output_timeout_delta:
            return True
        return False

    def get_top(self):
        top = self.device.get_top_activity(timeout=60)
        if top is None:
            log.info("Failed to get top activity, retrying, once...")
            top = self.device.get_top_activity(timeout=60)
        return top

    def wait_for_start(self, package):
        top = None
        while top != package and not self.start_timed_out():
            if self.update_log():
                # if log content is available, assume the app started; otherwise,
                # a short run (few tests) might complete without ever being detected
                # in the foreground
                return package
            time.sleep(1)
            top = self.get_top()
        return top

    def wait(self, package):
        """
        Wait until:
         - the app loses foreground, or
         - no new output is observed for the output timeout, or
         - the timeout is exceeded.
        While waiting, update the log every periodically: pull the gtest log from
        device and log any new content.
        """
        top = self.wait_for_start(package)
        if top != package:
            log.error("gtest | %s failed to start" % package)
            return
        while not self.timed_out():
            if not self.update_log():
                top = self.get_top()
                if top != package or self.output_timed_out():
                    time.sleep(self.output_poll_interval)
                    break
            time.sleep(self.output_poll_interval)
        self.update_log()
        if self.timed_out():
            log.error("gtest | timed out after %d seconds" % self.timeout_delta.seconds)
        elif self.output_timed_out():
            log.error(
                "gtest | timed out after %d seconds without output"
                % self.output_timeout_delta.seconds
            )
        else:
            log.info("gtest | wait for %s complete; top activity=%s" % (package, top))
        return True if top == package else False

    def update_log(self):
        """
        Pull the test log from the remote device and display new content.
        """
        if not self.device.is_file(self.remote_log):
            log.info("gtest | update_log %s is not a file." % self.remote_log)
            return False
        try:
            new_content = self.device.get_file(
                self.remote_log, offset=self.remote_log_len
            )
        except mozdevice.ADBTimeoutError:
            raise
        except Exception as e:
            log.info("gtest | update_log : exception reading log: %s" % str(e))
            return False
        if not new_content:
            log.info("gtest | update_log : no new content")
            return False
        new_content = six.ensure_text(new_content)
        last_full_line_pos = new_content.rfind("\n")
        if last_full_line_pos <= 0:
            # wait for a full line
            return False
        # trim partial line
        new_content = new_content[:last_full_line_pos]
        self.remote_log_len += len(new_content)
        for line in new_content.lstrip("\n").split("\n"):
            print(line)
            match = PERFHERDER_MATCHER.search(line)
            if match:
                data = json.loads(match.group(1))
                self.on_perfherder(data)
        self.last_output_time = datetime.datetime.now()
        return True


class remoteGtestOptions(argparse.ArgumentParser):
    def __init__(self):
        super().__init__(usage="usage: %prog [options] test_filter")
        commandline.add_logging_group(self)
        self.add_argument(
            "--package",
            dest="package",
            default="org.mozilla.geckoview.test_runner",
            help="Package name of test app.",
        )
        self.add_argument(
            "--adbpath",
            action="store",
            type=str,
            dest="adb_path",
            default="adb",
            help="Path to adb binary.",
        )
        self.add_argument(
            "--deviceSerial",
            action="store",
            type=str,
            dest="device_serial",
            help="adb serial number of remote device. This is required "
            "when more than one device is connected to the host. "
            "Use 'adb devices' to see connected devices. ",
        )
        self.add_argument(
            "--remoteTestRoot",
            action="store",
            type=str,
            dest="remote_test_root",
            help="Remote directory to use as test root "
            "(eg. /data/local/tmp/test_root).",
        )
        self.add_argument(
            "--libxul",
            action="store",
            type=str,
            dest="libxul_path",
            default=None,
            help="Path to gtest libxul.so.",
        )
        self.add_argument(
            "--symbols-path",
            dest="symbols_path",
            action="append",
            default=None,
            help="absolute path to directory containing breakpad "
            "symbols, or the URL of a zip file containing symbols; "
            "can be given multiple times",
        )
        self.add_argument(
            "--shuffle",
            action="store_true",
            default=False,
            help="Randomize the execution order of tests.",
        )
        self.add_argument(
            "--tests-path",
            default=None,
            help="Path to gtest directory containing test support files.",
        )
        self.add_argument("args", nargs=argparse.REMAINDER)


def update_mozinfo():
    """
    Walk up directories to find mozinfo.json and update the info.
    """
    path = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
    dirs = set()
    while path != os.path.expanduser("~"):
        if path in dirs:
            break
        dirs.add(path)
        path = os.path.split(path)[0]
    mozinfo.find_and_update_from_json(*dirs)


def setup_logging(options=None):
    global log
    if log is None:
        log = commandline.setup_logging("gtest", options, {"raw": sys.stdout})


def main():
    parser = remoteGtestOptions()
    options = parser.parse_args()
    setup_logging(options)
    args = options.args
    if not options.libxul_path:
        parser.error("--libxul is required")
        sys.exit(1)
    if len(args) > 1:
        parser.error("only one test_filter is allowed")
        sys.exit(1)
    test_filter = args[0] if args else None
    tester = RemoteGTests()
    result = False
    try:
        device_exception = False
        result = tester.run_gtest(
            options.tests_path,
            options.shuffle,
            test_filter,
            options.package,
            options.adb_path,
            options.device_serial,
            options.remote_test_root,
            options.libxul_path,
            options.symbols_path,
        )
    except KeyboardInterrupt:
        log.info("gtest | Received keyboard interrupt")
    except Exception as e:
        log.error(str(e))
        traceback.print_exc()
        if isinstance(e, mozdevice.ADBTimeoutError):
            device_exception = True
    finally:
        if not device_exception:
            tester.cleanup()
    sys.exit(0 if result else 1)


if __name__ == "__main__":
    main()
