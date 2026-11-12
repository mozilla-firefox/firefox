#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import posixpath
import subprocess
import sys
import traceback
from zipfile import ZipFile

import mozcrash
import mozfile
import mozinfo
import mozlog
import runcppunittests as cppunittests
from mozdevice import ADBDeviceFactory, ADBProcessError, ADBTimeoutError

try:
    from mozbuild.base import MozbuildObject

    build_obj = MozbuildObject.from_environment()
except ImportError:
    build_obj = None


class RemoteCPPUnitTests(cppunittests.CPPUnitTests):
    def __init__(self, options, progs):
        cppunittests.CPPUnitTests.__init__(self)
        self.options = options
        self.device = ADBDeviceFactory(
            adb=options.adb_path or "adb",
            device=options.device_serial,
            test_root=options.remote_test_root,
        )
        self.remote_test_root = posixpath.join(self.device.test_root, "cppunittests")
        self.remote_bin_dir = posixpath.join(self.remote_test_root, "b")
        self.remote_tmp_dir = posixpath.join(self.remote_test_root, "tmp")
        self.remote_home_dir = posixpath.join(self.remote_test_root, "h")
        if options.setup:
            self.setup_bin(progs)

    def setup_bin(self, progs):
        self.device.rm(self.remote_test_root, force=True, recursive=True)
        self.device.mkdir(self.remote_home_dir, parents=True)
        self.device.mkdir(self.remote_tmp_dir)
        self.device.mkdir(self.remote_bin_dir)
        self.push_libs()
        self.push_progs(progs)
        self.device.chmod(self.remote_bin_dir, recursive=True)

    def push_libs(self):
        if self.options.local_apk:
            with mozfile.TemporaryDirectory() as tmpdir:
                apk_contents = ZipFile(self.options.local_apk)

                for info in apk_contents.infolist():
                    if info.filename.endswith(".so"):
                        print("Pushing %s.." % info.filename, file=sys.stderr)
                        remote_file = posixpath.join(
                            self.remote_bin_dir, os.path.basename(info.filename)
                        )
                        apk_contents.extract(info, tmpdir)
                        local_file = os.path.join(tmpdir, info.filename)
                        with open(local_file, "rb") as f:
                            # Decompress xz-compressed file.
                            if f.read(5)[1:] == "7zXZ":
                                cmd = ["xz", "-df", "--suffix", ".so", local_file]
                                subprocess.check_output(cmd)
                                # xz strips the ".so" file suffix.
                                os.rename(local_file[:-3], local_file)
                        self.device.push(local_file, remote_file)

        elif self.options.local_lib:
            for path in os.listdir(self.options.local_lib):
                if path.endswith(".so"):
                    print(f"Pushing {path}..", file=sys.stderr)
                    remote_file = posixpath.join(self.remote_bin_dir, path)
                    local_file = os.path.join(self.options.local_lib, path)
                    self.device.push(local_file, remote_file)
            # Additional libraries may be found in a sub-directory such as
            # "lib/armeabi-v7a"
            for subdir in ["assets", "lib"]:
                local_arm_lib = os.path.join(self.options.local_lib, subdir)
                if os.path.isdir(local_arm_lib):
                    for root, dirs, paths in os.walk(local_arm_lib):
                        for path in paths:
                            if path.endswith(".so"):
                                print(f"Pushing {path}..", file=sys.stderr)
                                remote_file = posixpath.join(self.remote_bin_dir, path)
                                local_file = os.path.join(root, path)
                                self.device.push(local_file, remote_file)

    def push_progs(self, progs):
        for local_file in progs:
            remote_file = posixpath.join(
                self.remote_bin_dir, os.path.basename(local_file)
            )
            self.device.push(local_file, remote_file)

    def build_environment(self):
        env = self.build_core_environment({})
        env["LD_LIBRARY_PATH"] = self.remote_bin_dir
        env["TMPDIR"] = self.remote_tmp_dir
        env["HOME"] = self.remote_home_dir
        env["MOZ_XRE_DIR"] = self.remote_bin_dir
        if self.options.add_env:
            for envdef in self.options.add_env:
                envdef_parts = envdef.split("=", 1)
                if len(envdef_parts) == 2:
                    env[envdef_parts[0]] = envdef_parts[1]
                elif len(envdef_parts) == 1:
                    env[envdef_parts[0]] = ""
                else:
                    self.log.warning("invalid --addEnv option skipped: %s" % envdef)

        return env

    def run_one_test(
        self,
        prog,
        env,
        symbols_path=None,
        utility_path=None,
        interactive=False,
        timeout_factor=1,
    ):
        """
        Run a single C++ unit test program remotely.

        Arguments:
        * prog: The path to the test program to run.
        * env: The environment to use for running the program.
        * symbols_path: A path to a directory containing Breakpad-formatted
                        symbol files for producing stack traces on crash.
        * timeout_factor: An optional test-specific timeout multiplier.

        Return True if the program exits with a zero status, False otherwise.
        """
        basename = os.path.basename(prog)
        remote_bin = posixpath.join(self.remote_bin_dir, basename)
        self.log.test_start(basename)
        test_timeout = cppunittests.CPPUnitTests.TEST_PROC_TIMEOUT * timeout_factor

        try:
            output = self.device.shell_output(
                remote_bin, env=env, cwd=self.remote_home_dir, timeout=test_timeout
            )
            returncode = 0
        except ADBTimeoutError:
            raise
        except ADBProcessError as e:
            output = e.adb_process.stdout
            returncode = e.adb_process.exitcode

        self.log.process_output(basename, "\n%s" % output, command=[remote_bin])
        with mozfile.TemporaryDirectory() as tempdir:
            self.device.pull(self.remote_home_dir, tempdir)
            if mozcrash.check_for_crashes(tempdir, symbols_path, test_name=basename):
                self.log.test_end(basename, status="CRASH", expected="PASS")
                return False
        result = returncode == 0
        if not result:
            self.log.test_end(
                basename,
                status="FAIL",
                expected="PASS",
                message=("test failed with return code %s" % returncode),
            )
        else:
            self.log.test_end(basename, status="PASS", expected="PASS")
        return result


class RemoteCPPUnittestOptions(cppunittests.CPPUnittestOptions):
    def __init__(self):
        cppunittests.CPPUnittestOptions.__init__(self)
        defaults = {}

        self.add_option(
            "--deviceSerial",
            action="store",
            type="string",
            dest="device_serial",
            help="adb serial number of remote device. This is required "
            "when more than one device is connected to the host. "
            "Use 'adb devices' to see connected devices.",
        )
        defaults["device_serial"] = None

        self.add_option(
            "--adbPath",
            action="store",
            type="string",
            dest="adb_path",
            help="Path to adb binary.",
        )
        defaults["adb_path"] = None

        self.add_option(
            "--noSetup",
            action="store_false",
            dest="setup",
            help="Do not copy any files to device (to be used only if "
            "device is already setup).",
        )
        defaults["setup"] = True

        self.add_option(
            "--localLib",
            action="store",
            type="string",
            dest="local_lib",
            help="Location of libraries to push -- preferably stripped.",
        )
        defaults["local_lib"] = None

        self.add_option(
            "--apk",
            action="store",
            type="string",
            dest="local_apk",
            help="Local path to Firefox for Android APK.",
        )
        defaults["local_apk"] = None

        self.add_option(
            "--localBinDir",
            action="store",
            type="string",
            dest="local_bin",
            help="Local path to bin directory.",
        )
        defaults["local_bin"] = build_obj.bindir if build_obj is not None else None

        self.add_option(
            "--remoteTestRoot",
            action="store",
            type="string",
            dest="remote_test_root",
            help="Remote directory to use as test root "
            "(eg. /data/local/tmp/test_root).",
        )

        # /data/local/tmp/test_root is used because it is usually not
        # possible to set +x permissions on binaries on /mnt/sdcard
        # and since scope storage on Android 10 causes permission
        # errors on the sdcard.
        defaults["remote_test_root"] = "/data/local/tmp/test_root"

        self.add_option(
            "--addEnv",
            action="append",
            type="string",
            dest="add_env",
            help="additional remote environment variable definitions "
            '(eg. --addEnv "somevar=something")',
        )
        defaults["add_env"] = None

        self.set_defaults(**defaults)


def run_test_harness(options, args):
    options.xre_path = os.path.abspath(options.xre_path)
    cppunittests.update_mozinfo()
    progs = cppunittests.extract_unittests_from_args(
        args, mozinfo.info, options.manifest_path
    )
    tester = RemoteCPPUnitTests(options, [item[0] for item in progs])
    result = tester.run_tests(
        progs,
        options.xre_path,
        options.symbols_path,
    )
    return result


def main():
    parser = RemoteCPPUnittestOptions()
    mozlog.commandline.add_logging_group(parser)
    options, args = parser.parse_args()
    if not args:
        print(
            """Usage: %s <test binary> [<test binary>...]""" % sys.argv[0],
            file=sys.stderr,
        )
        sys.exit(1)
    if options.local_lib is not None and not os.path.isdir(options.local_lib):
        print(
            """Error: --localLib directory %s not found""" % options.local_lib,
            file=sys.stderr,
        )
        sys.exit(1)
    if options.local_apk is not None and not os.path.isfile(options.local_apk):
        print("""Error: --apk file %s not found""" % options.local_apk, file=sys.stderr)
        sys.exit(1)
    if not options.xre_path:
        print("""Error: --xre-path is required""", file=sys.stderr)
        sys.exit(1)

    log = mozlog.commandline.setup_logging(
        "remotecppunittests", options, {"raw": sys.stdout}
    )
    try:
        result = run_test_harness(options, args)
    except Exception as e:
        log.error(str(e))
        traceback.print_exc()
        result = False
    sys.exit(0 if result else 1)


if __name__ == "__main__":
    main()
