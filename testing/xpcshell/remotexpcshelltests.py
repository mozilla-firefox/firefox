#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import os
import posixpath
import shutil
import sys
import tempfile
import threading
import time
import uuid
from argparse import Namespace
from zipfile import ZipFile

import mozcrash
import mozdevice
import mozfile
import mozinfo
import runxpcshelltests as xpcshell
from mozdevice import ADBDevice, ADBDeviceFactory, ADBTimeoutError
from mozinfo.platforminfo import android_api_to_os_version
from mozlog import commandline
from xpcshellcommandline import parser_remote

here = os.path.dirname(os.path.abspath(__file__))


class RemoteProcessMonitor:
    processStatus = []
    _launchLock = threading.Lock()

    def __init__(self, package, device, log, remoteLogFile):
        self.package = package
        self.device = device
        self.log = log
        self.remoteLogFile = remoteLogFile
        self.selectedProcess = -1

    @classmethod
    def pickUnusedProcess(cls):
        for i in range(len(cls.processStatus)):
            if not cls.processStatus[i]:
                cls.processStatus[i] = True
                return i
        # No more free processes :(
        return -1

    @classmethod
    def freeProcess(cls, processId):
        cls.processStatus[processId] = False

    def kill(self):
        self.device.pkill(self.process_name, sig=9, attempts=1)

    def launch_service(self, extra_args, env, selectedProcess, test_name=None):
        with self._launchLock:
            if not self.device.process_exist(self.package):
                # Make sure the main app is running, this should help making the
                # tests get foreground priority scheduling.
                self.device.launch_activity(
                    self.package,
                    intent="org.mozilla.geckoview.test_runner.XPCSHELL_TEST_MAIN",
                    activity_name="TestRunnerActivity",
                )
                # Newer Androids require that background services originate from
                # active apps, so wait here until the test runner is the top
                # activity.
                retries = 20
                top = self.device.get_top_activity(timeout=60)
                while top != self.package and retries > 0:
                    self.log.info(
                        "%s | Checking that %s is the top activity."
                        % (test_name, self.package)
                    )
                    top = self.device.get_top_activity(timeout=60)
                    time.sleep(1)
                    retries -= 1

        self.process_name = self.package + (":xpcshell%d" % selectedProcess)

        retries = 20
        while retries > 0 and self.device.process_exist(self.process_name):
            self.log.info(
                "%s | %s | Killing left-over process %s"
                % (test_name, self.pid, self.process_name)
            )
            self.kill()
            time.sleep(1)
            retries -= 1

        if self.device.process_exist(self.process_name):
            raise Exception(
                "%s | %s | Could not kill left-over process" % (test_name, self.pid)
            )

        self.device.launch_service(
            self.package,
            activity_name=("XpcshellTestRunnerService$i%d" % selectedProcess),
            moz_env=env,
            grant_runtime_permissions=False,
            extra_args=extra_args,
            out_file=self.remoteLogFile,
        )
        return self.pid

    def wait(self, timeout, interval=0.1, test_name=None):
        timer = 0
        status = True

        # wait for log creation on startup
        retries = 0
        while retries < 20 / interval and not self.device.is_file(self.remoteLogFile):
            retries += 1
            time.sleep(interval)
        if not self.device.is_file(self.remoteLogFile):
            self.log.warning(
                "%s | Failed wait for remote log: %s missing?"
                % (test_name, self.remoteLogFile)
            )

        while self.device.process_exist(self.process_name):
            time.sleep(interval)
            timer += interval
            interval *= 1.5
            # We're using exponential back-off. To avoid unnecessarily waiting
            # for too long, cap the maximum sleep interval to 15 seconds.
            interval = min(15, interval)
            if timeout and timer > timeout:
                status = False
                self.log.info(
                    "remotexpcshelltests.py | %s | %s | Timing out"
                    % (test_name, str(self.pid))
                )
                self.kill()
                break
        return status

    @property
    def pid(self):
        """
        Determine the pid of the remote process (or the first process with
        the same name).
        """
        procs = self.device.get_process_list()
        # limit the comparison to the first 75 characters due to a
        # limitation in processname length in android.
        pids = [proc[0] for proc in procs if proc[1] == self.process_name[:75]]
        if pids is None or len(pids) < 1:
            return 0
        return pids[0]


class RemoteXPCShellTestThread(xpcshell.XPCShellTestThread):
    def __init__(self, *args, **kwargs):
        xpcshell.XPCShellTestThread.__init__(self, *args, **kwargs)

        self.shellReturnCode = None
        # embed the mobile params from the harness into the TestThread
        mobileArgs = kwargs.get("mobileArgs")
        for key in mobileArgs:
            setattr(self, key, mobileArgs[key])
        self.remoteLogFile = posixpath.join(
            mobileArgs["remoteLogFolder"], "xpcshell-%s.log" % str(uuid.uuid4())
        )

    def initDir(self, path, mask="777", timeout=None):
        """Initialize a directory by removing it if it exists, creating it
        and changing the permissions."""
        self.device.rm(path, recursive=True, force=True, timeout=timeout)
        self.device.mkdir(path, parents=True, timeout=timeout)

    def updateTestPrefsFile(self):
        # The base method will either be no-op (and return the existing
        # remote path), or return a path to a new local file.
        testPrefsFile = xpcshell.XPCShellTestThread.updateTestPrefsFile(self)
        if testPrefsFile == self.rootPrefsFile:
            # The pref file is the shared one, which has been already pushed on the
            # device, and so there is nothing more to do here.
            return self.rootPrefsFile

        # Push the per-test prefs file in the remote temp dir.
        remoteTestPrefsFile = posixpath.join(self.remoteTmpDir, "user.js")
        self.device.push(testPrefsFile, remoteTestPrefsFile)
        self.device.chmod(remoteTestPrefsFile)
        os.remove(testPrefsFile)
        return remoteTestPrefsFile

    def buildCmdTestFile(self, name):
        remoteDir = self.remoteForLocal(os.path.dirname(name))
        if remoteDir == self.remoteHere:
            remoteName = os.path.basename(name)
        else:
            remoteName = posixpath.join(remoteDir, os.path.basename(name))
        return [
            "-e",
            'const _TEST_CWD = "%s";' % self.remoteHere,
            "-e",
            'const _TEST_FILE = ["%s"];' % remoteName.replace("\\", "/"),
        ]

    def remoteForLocal(self, local):
        for mapping in self.pathMapping:
            if os.path.abspath(mapping.local) == os.path.abspath(local):
                return mapping.remote
        return local

    def setupTempDir(self):
        self.remoteTmpDir = posixpath.join(self.remoteTmpDir, str(uuid.uuid4()))
        # make sure the temp dir exists
        self.initDir(self.remoteTmpDir)
        # env var is set in buildEnvironment
        self.env["XPCSHELL_TEST_TEMP_DIR"] = self.remoteTmpDir
        return self.remoteTmpDir

    def setupProfileDir(self):
        profileId = str(uuid.uuid4())
        self.profileDir = posixpath.join(self.profileDir, profileId)
        self.initDir(self.profileDir)
        if self.interactive or self.singleFile:
            self.log.info("profile dir is %s" % self.profileDir)
        self.env["XPCSHELL_TEST_PROFILE_DIR"] = self.profileDir
        self.env["TMPDIR"] = self.profileDir
        self.remoteMinidumpDir = posixpath.join(self.remoteMinidumpRootDir, profileId)
        self.initDir(self.remoteMinidumpDir)
        self.env["XPCSHELL_MINIDUMP_DIR"] = self.remoteMinidumpDir
        return self.profileDir

    def clean_temp_dirs(self, name):
        self.log.info("Cleaning up profile for %s folder: %s" % (name, self.profileDir))
        self.device.rm(self.profileDir, force=True, recursive=True)
        self.device.rm(self.remoteTmpDir, force=True, recursive=True)
        self.device.rm(self.remoteMinidumpDir, force=True, recursive=True)

    def setupMozinfoJS(self):
        local = tempfile.mktemp()
        mozinfo.output_to_file(local)
        mozInfoJSPath = posixpath.join(self.profileDir, "mozinfo.json")
        self.device.push(local, mozInfoJSPath)
        self.device.chmod(mozInfoJSPath)
        os.remove(local)
        return mozInfoJSPath

    def logCommand(self, name, completeCmd, testdir):
        self.log.info("%s | full command: %r" % (name, completeCmd))
        self.log.info("%s | current directory: %r" % (name, self.remoteHere))
        self.log.info("%s | environment: %s" % (name, self.env))

    def getHeadFiles(self, test):
        """Override parent method to find files on remote device.

        Obtains lists of head- files.  Returns a list of head files.
        """

        def sanitize_list(s, kind):
            for f in s.strip().split(" "):
                f = f.strip()
                if len(f) < 1:
                    continue

                path = posixpath.join(self.remoteHere, f)

                # skip check for file existence: the convenience of discovering
                # a missing file does not justify the time cost of the round trip
                # to the device
                yield path

        self.remoteHere = self.remoteForLocal(test["here"])

        headlist = test.get("head", "")
        return list(sanitize_list(headlist, "head"))

    def buildXpcsCmd(self):
        # change base class' paths to remote paths and use base class to build command
        self.xpcshell = posixpath.join(self.remoteBinDir, "xpcw")
        self.headJSPath = posixpath.join(self.remoteScriptsDir, "head.js")
        self.testingModulesDir = self.remoteModulesDir
        self.testharnessdir = self.remoteScriptsDir
        xpcsCmd = xpcshell.XPCShellTestThread.buildXpcsCmd(self)
        # remove "-g <dir> -a <dir>" and replace with remote alternatives
        del xpcsCmd[1:5]
        if self.options["localAPK"]:
            xpcsCmd.insert(1, "--greomni")
            xpcsCmd.insert(2, self.remoteAPK)
        xpcsCmd.insert(1, "-g")
        xpcsCmd.insert(2, self.remoteBinDir)

        if self.remoteDebugger:
            # for example, "/data/local/gdbserver" "localhost:12345"
            xpcsCmd = [self.remoteDebugger, self.remoteDebuggerArgs] + xpcsCmd
        return xpcsCmd

    def killTimeout(self, proc):
        self.kill(proc)

    def launchProcess(
        self, cmd, stdout, stderr, env, cwd, timeout=None, test_name=None
    ):
        rpm = RemoteProcessMonitor(
            "org.mozilla.geckoview.test_runner",
            self.device,
            self.log,
            self.remoteLogFile,
        )

        startTime = datetime.datetime.now()

        try:
            pid = rpm.launch_service(
                cmd[1:], self.env, self.selectedProcess, test_name=test_name
            )
        except Exception as e:
            self.log.info(
                "remotexpcshelltests.py | Failed to start process: %s" % str(e)
            )
            self.shellReturnCode = 1
            return ""

        self.log.info(
            "remotexpcshelltests.py | %s | %s | Launched Test App"
            % (test_name, str(pid))
        )

        if rpm.wait(timeout, test_name=test_name):
            self.shellReturnCode = 0
        else:
            self.shellReturnCode = 1
        self.log.info(
            "remotexpcshelltests.py | %s | %s | Application ran for: %s"
            % (test_name, str(pid), str(datetime.datetime.now() - startTime))
        )

        try:
            return self.device.get_file(self.remoteLogFile)
        except mozdevice.ADBTimeoutError:
            raise
        except Exception as e:
            self.log.info(
                "remotexpcshelltests.py | %s | %s | Could not read log file: %s"
                % (test_name, str(pid), str(e))
            )
            self.shellReturnCode = 1
            return ""

    def checkForCrashes(self, dump_directory, symbols_path, test_name=None):
        with mozfile.TemporaryDirectory() as dumpDir:
            self.device.pull(self.remoteMinidumpDir, dumpDir)
            crashed = mozcrash.log_crashes(
                self.log,
                dumpDir,
                symbols_path,
                test=test_name,
                quiet=self.crashAsPass or self.retry,
            )
        return crashed

    def communicate(self, proc):
        return proc, ""

    def poll(self, proc):
        if not self.device.process_exist("xpcshell"):
            return self.getReturnCode(proc)
        # Process is still running
        return None

    def kill(self, proc):
        return self.device.pkill("xpcshell")

    def getReturnCode(self, proc):
        if self.shellReturnCode is not None:
            return self.shellReturnCode
        else:
            return -1

    def removeDir(self, dirname):
        try:
            self.device.rm(dirname, recursive=True)
        except ADBTimeoutError:
            raise
        except Exception as e:
            self.log.warning(str(e))

    def createLogFile(self, test, stdout):
        filename = test.replace("\\", "/").split("/")[-1] + ".log"
        with open(filename, "wb") as f:
            f.write(stdout)


# A specialization of XPCShellTests that runs tests on an Android device.
class XPCShellRemote(xpcshell.XPCShellTests):
    def __init__(self, options, log):
        xpcshell.XPCShellTests.__init__(self, log)

        self.options = options
        verbose = False
        if options["log_tbpl_level"] == "debug" or options["log_mach_level"] == "debug":
            verbose = True
        self.device = ADBDeviceFactory(
            adb=options["adbPath"] or "adb",
            device=options["deviceSerial"],
            test_root=options["remoteTestRoot"],
            verbose=verbose,
        )
        self.remoteTestRoot = posixpath.join(self.device.test_root, "xpc")
        self.remoteLogFolder = posixpath.join(self.remoteTestRoot, "logs")
        # Use Android version (SDK level) to get os_version for mozinfo
        # so that manifest entries can be conditional on os_version.
        android_version = str(self.device.version)
        os_version = android_api_to_os_version(android_version)
        self.log.info(
            f"Android sdk version '{android_version}' corresponds to os_version '{os_version}'; use os_version to filter manifests"
        )
        mozinfo.info["os_version"] = os_version
        mozinfo.info["is_emulator"] = self.device._device_serial.startswith("emulator-")
        mozinfo.info["isolated_process"] = options["isolated_process"]

        self.localBin = options["localBin"]
        self.pathMapping = []
        # remoteBinDir contains xpcshell and its wrapper script, both of which must
        # be executable. Since +x permissions cannot usually be set on /mnt/sdcard,
        # and the test root may be on /mnt/sdcard, remoteBinDir is set to be on
        # /data/local, always.
        self.remoteBinDir = posixpath.join(self.device.test_root, "xpcb")
        # Terse directory names are used here ("c" for the components directory)
        # to minimize the length of the command line used to execute
        # xpcshell on the remote device. adb has a limit to the number
        # of characters used in a shell command, and the xpcshell command
        # line can be quite complex.
        self.remoteTmpDir = posixpath.join(self.remoteTestRoot, "tmp")
        self.remoteScriptsDir = self.remoteTestRoot
        self.remoteComponentsDir = posixpath.join(self.remoteTestRoot, "c")
        self.remoteModulesDir = posixpath.join(self.remoteTestRoot, "m")
        self.remoteMinidumpRootDir = posixpath.join(self.remoteTestRoot, "minidumps")
        self.profileDir = posixpath.join(self.remoteTestRoot, "p")
        self.remoteDebugger = options["debugger"]
        self.remoteDebuggerArgs = options["debuggerArgs"]
        self.testingModulesDir = options["testingModulesDir"]
        self.isolatedProcess = options["isolated_process"]

        self.initDir(self.remoteTmpDir)
        self.initDir(self.profileDir)

        # Make sure we get a fresh start
        self.device.stop_application("org.mozilla.geckoview.test_runner")

        for i in range(options["threadCount"]):
            RemoteProcessMonitor.processStatus += [False]

        self.env = {}

        if options["objdir"]:
            self.xpcDir = os.path.join(options["objdir"], "_tests/xpcshell")
        elif os.path.isdir(os.path.join(here, "tests")):
            self.xpcDir = os.path.join(here, "tests")
        else:
            print("Couldn't find local xpcshell test directory", file=sys.stderr)
            sys.exit(1)

        self.remoteAPK = None
        if options["localAPK"]:
            self.localAPKContents = ZipFile(options["localAPK"])
            self.remoteAPK = posixpath.join(
                self.remoteBinDir, os.path.basename(options["localAPK"])
            )
        else:
            self.localAPKContents = None
        if options["setup"]:
            self.setupTestDir()
            self.setupUtilities()
            self.setupModules()
        self.initDir(self.remoteMinidumpRootDir)
        self.initDir(self.remoteLogFolder)

        eprefs = options.get("extraPrefs") or []
        if options.get("disableFission"):
            eprefs.append("fission.autostart=false")
        else:
            # should be by default, just in case
            eprefs.append("fission.autostart=true")
        options["extraPrefs"] = eprefs

        # data that needs to be passed to the RemoteXPCShellTestThread
        self.mobileArgs = {
            "device": self.device,
            "remoteBinDir": self.remoteBinDir,
            "remoteScriptsDir": self.remoteScriptsDir,
            "remoteComponentsDir": self.remoteComponentsDir,
            "remoteModulesDir": self.remoteModulesDir,
            "options": self.options,
            "remoteDebugger": self.remoteDebugger,
            "remoteDebuggerArgs": self.remoteDebuggerArgs,
            "pathMapping": self.pathMapping,
            "profileDir": self.profileDir,
            "remoteLogFolder": self.remoteLogFolder,
            "remoteTmpDir": self.remoteTmpDir,
            "remoteMinidumpRootDir": self.remoteMinidumpRootDir,
        }
        if self.remoteAPK:
            self.mobileArgs["remoteAPK"] = self.remoteAPK

    def initDir(self, path, mask="777", timeout=None):
        """Initialize a directory by removing it if it exists, creating it
        and changing the permissions."""
        self.device.rm(path, recursive=True, force=True, timeout=timeout)
        self.device.mkdir(path, parents=True, timeout=timeout)

    def setLD_LIBRARY_PATH(self):
        self.env["LD_LIBRARY_PATH"] = self.remoteBinDir

    def pushWrapper(self):
        # Rather than executing xpcshell directly, this wrapper script is
        # used. By setting environment variables and the cwd in the script,
        # the length of the per-test command line is shortened. This is
        # often important when using ADB, as there is a limit to the length
        # of the ADB command line.
        localWrapper = tempfile.mktemp()
        with open(localWrapper, "w") as f:
            f.write("#!/system/bin/sh\n")
            for envkey, envval in self.env.items():
                f.write("export %s=%s\n" % (envkey, envval))
            f.writelines([
                "cd $1\n",
                "echo xpcw: cd $1\n",
                "shift\n",
                'echo xpcw: xpcshell "$@"\n',
                '%s/xpcshell "$@"\n' % self.remoteBinDir,
            ])
        remoteWrapper = posixpath.join(self.remoteBinDir, "xpcw")
        self.device.push(localWrapper, remoteWrapper)
        self.device.chmod(remoteWrapper)
        os.remove(localWrapper)

    def start_test(self, test):
        test.selectedProcess = RemoteProcessMonitor.pickUnusedProcess()
        if test.selectedProcess == -1:
            self.log.error(
                "TEST-UNEXPECTED-FAIL | remotexpcshelltests.py | no more free processes"
            )
        test.start()

    def test_ended(self, test):
        RemoteProcessMonitor.freeProcess(test.selectedProcess)

    def buildPrefsFile(self, extraPrefs):
        prefs = super().buildPrefsFile(extraPrefs)
        remotePrefsFile = posixpath.join(self.remoteTestRoot, "user.js")
        self.device.push(self.prefsFile, remotePrefsFile)
        self.device.chmod(remotePrefsFile)
        # os.remove(self.prefsFile) is not called despite having pushed the
        # file to the device, because the local file is relied upon by the
        # updateTestPrefsFile method
        self.prefsFile = remotePrefsFile
        return prefs

    def buildEnvironment(self):
        self.buildCoreEnvironment()
        self.setLD_LIBRARY_PATH()
        self.env["MOZ_LINKER_CACHE"] = self.remoteBinDir
        self.env["GRE_HOME"] = self.remoteBinDir
        self.env["XPCSHELL_TEST_PROFILE_DIR"] = self.profileDir
        self.env["HOME"] = self.profileDir
        self.env["XPCSHELL_TEST_TEMP_DIR"] = self.remoteTmpDir
        self.env["MOZ_ANDROID_DATA_DIR"] = self.remoteBinDir
        self.env["MOZ_IN_AUTOMATION"] = "1"

        if self.isolatedProcess:
            self.env["MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_PROCESS"] = "1"

        # Guard against intermittent failures to retrieve abi property;
        # without an abi, xpcshell cannot find greprefs.js and crashes.
        abilistprop = None
        abi = None
        retries = 0
        while not abi and retries < 3:
            abi = self.device.get_prop("ro.product.cpu.abi")
            retries += 1
        if not abi:
            raise Exception("failed to get ro.product.cpu.abi from device")
        self.log.info("ro.product.cpu.abi %s" % abi)
        if self.localAPKContents:
            abilist = [abi]
            retries = 0
            while not abilistprop and retries < 3:
                abilistprop = self.device.get_prop("ro.product.cpu.abilist")
                retries += 1
            self.log.info("ro.product.cpu.abilist %s" % abilistprop)
            abi_found = False
            names = [
                n for n in self.localAPKContents.namelist() if n.startswith("lib/")
            ]
            self.log.debug("apk names: %s" % names)
            if abilistprop and len(abilistprop) > 0:
                abilist.extend(abilistprop.split(","))
            for abicand in abilist:
                abi_found = (
                    len([n for n in names if n.startswith("lib/%s" % abicand)]) > 0
                )
                if abi_found:
                    abi = abicand
                    break
            if not abi_found:
                self.log.info("failed to get matching abi from apk.")
                if len(names) > 0:
                    self.log.info(
                        "device cpu abi not found in apk. Using abi from apk."
                    )
                    abi = names[0].split("/")[1]
        self.log.info("Using abi %s." % abi)
        self.env["MOZ_ANDROID_CPU_ABI"] = abi
        self.log.info("Using env %r" % (self.env,))

    def setupUtilities(self):
        self.initDir(self.remoteTmpDir)
        self.initDir(self.remoteBinDir)
        remotePrefDir = posixpath.join(self.remoteBinDir, "defaults", "pref")
        self.initDir(posixpath.join(remotePrefDir, "extra"))
        self.initDir(self.remoteComponentsDir)

        local = os.path.join(os.path.dirname(os.path.abspath(__file__)), "head.js")
        remoteFile = posixpath.join(self.remoteScriptsDir, "head.js")
        self.device.push(local, remoteFile)
        self.device.chmod(remoteFile)

        # Additional binaries are required for some tests. This list should be
        # similar to TEST_HARNESS_BINS in testing/mochitest/Makefile.in.
        binaries = [
            "ssltunnel",
            "certutil",
            "pk12util",
            "BadCertAndPinningServer",
            "DelegatedCredentialsServer",
            "EncryptedClientHelloServer",
            "FaultyServer",
            "OCSPStaplingServer",
            "GenerateOCSPResponse",
            "SanctionsTestServer",
            "ZeroRttAcceptServer",
        ]
        for fname in binaries:
            local = os.path.join(self.localBin, fname)
            if os.path.isfile(local):
                print("Pushing %s.." % fname, file=sys.stderr)
                remoteFile = posixpath.join(self.remoteBinDir, fname)
                self.device.push(local, remoteFile)
                self.device.chmod(remoteFile)
            else:
                print(
                    "*** Expected binary %s not found in %s!" % (fname, self.localBin),
                    file=sys.stderr,
                )

        local = os.path.join(self.localBin, "components/httpd.sys.mjs")
        remoteFile = posixpath.join(self.remoteComponentsDir, "httpd.sys.mjs")
        self.device.push(local, remoteFile)
        self.device.chmod(remoteFile)

        if self.options["localAPK"]:
            remoteFile = posixpath.join(
                self.remoteBinDir, os.path.basename(self.options["localAPK"])
            )
            self.device.push(self.options["localAPK"], remoteFile)
            self.device.chmod(remoteFile)

            self.pushLibs()
        else:
            raise Exception("unable to install gre: no APK")

    def pushLibs(self):
        pushed_libs_count = 0
        try:
            dir = tempfile.mkdtemp()
            for info in self.localAPKContents.infolist():
                if info.filename.endswith(".so"):
                    print("Pushing %s.." % info.filename, file=sys.stderr)
                    remoteFile = posixpath.join(
                        self.remoteBinDir, os.path.basename(info.filename)
                    )
                    self.localAPKContents.extract(info, dir)
                    localFile = os.path.join(dir, info.filename)
                    self.device.push(localFile, remoteFile)
                    pushed_libs_count += 1
                    self.device.chmod(remoteFile)
        finally:
            shutil.rmtree(dir)
        return pushed_libs_count

    def setupModules(self):
        if self.testingModulesDir:
            self.device.push(self.testingModulesDir, self.remoteModulesDir)
            self.device.chmod(self.remoteModulesDir)

    def setupTestDir(self):
        print("pushing %s" % self.xpcDir)
        # The tests directory can be quite large: 5000 files and growing!
        # Sometimes - like on a low-end aws instance running an emulator - the push
        # may exceed the default 5 minute timeout, so we increase it here to 10 minutes.
        self.device.rm(self.remoteScriptsDir, recursive=True, force=True, timeout=None)
        self.device.push(self.xpcDir, self.remoteScriptsDir, timeout=600)
        self.device.chmod(self.remoteScriptsDir, recursive=True)

    def buildNodeEnvironment(self):
        """
        Filter self.env to avoid passing device-specific variables like HOME or
        LD_LIBRARY_PATH to the host node process.
        """
        node_env = dict(os.environ)
        for key, value in self.env.items():
            if key.startswith("MOZ"):
                node_env[key] = value
        return node_env

    def trySetupNode(self):
        NODE_PORT_KEYS = ["MOZHTTP2_PORT", "MOZNODE_EXEC_PORT"]
        MAX_ATTEMPTS = 3
        portsPreset = any(key in self.env for key in NODE_PORT_KEYS)
        for attempt in range(1, MAX_ATTEMPTS + 1):
            super().trySetupNode()
            # make node host ports visible to device
            reversedPorts = []
            try:
                for key in NODE_PORT_KEYS:
                    if key in self.env:
                        port = f"tcp:{self.env[key]}"
                        self.device.create_socket_connection(
                            ADBDevice.SOCKET_DIRECTION_REVERSE, port, port
                        )
                        reversedPorts.append(port)
                        self.log.info(f"reversed {key} connection for port {port}")
                return
            except mozdevice.ADBProcessError as e:
                # The node ports are free on the host but can be in use on the
                # device. If we picked them, start node again on new ports.
                if (
                    portsPreset
                    or self.mozHttp2Server is None
                    or attempt == MAX_ATTEMPTS
                    or "Address already in use" not in str(e)
                ):
                    raise
                self.log.info(f"node port in use on the device, retrying: {e}")
            for port in reversedPorts:
                self.device.remove_socket_connections(
                    ADBDevice.SOCKET_DIRECTION_REVERSE, port
                )
            super().shutdownNode()
            # The base trySetupNode stored the ports node picked in self.env,
            # and node listens on the ports in self.env if they are set, so
            # remove them to have node pick new ones.
            for key in NODE_PORT_KEYS:
                self.env.pop(key, None)

    def shutdownNode(self):
        super().shutdownNode()

        if "MOZHTTP2_PORT" in self.env:
            port = "tcp:{}".format(self.env["MOZHTTP2_PORT"])
            self.device.remove_socket_connections(
                ADBDevice.SOCKET_DIRECTION_REVERSE, port
            )
            self.log.info("cleared MOZHTTP2_PORT connection for port " + port)
        if "MOZNODE_EXEC_PORT" in self.env:
            port = "tcp:{}".format(self.env["MOZNODE_EXEC_PORT"])
            self.device.remove_socket_connections(
                ADBDevice.SOCKET_DIRECTION_REVERSE, port
            )
            self.log.info("cleared MOZNODE_EXEC_PORT connection for port " + port)

    def buildTestList(self, test_tags=None, test_paths=None, verify=False):
        xpcshell.XPCShellTests.buildTestList(
            self, test_tags=test_tags, test_paths=test_paths, verify=verify
        )
        uniqueTestPaths = set([])
        for test in self.alltests:
            uniqueTestPaths.add(test["here"])
        for testdir in uniqueTestPaths:
            abbrevTestDir = os.path.relpath(testdir, self.xpcDir)
            remoteScriptDir = posixpath.join(self.remoteScriptsDir, abbrevTestDir)
            self.pathMapping.append(PathMapping(testdir, remoteScriptDir))
        if self.options["setup"]:
            self.pushWrapper()


def verifyRemoteOptions(parser, options):
    if isinstance(options, Namespace):
        options = vars(options)

    if options["localBin"] is None:
        if options["objdir"]:
            options["localBin"] = os.path.join(options["objdir"], "dist", "bin")
            if not os.path.isdir(options["localBin"]):
                parser.error("Couldn't find local binary dir, specify --local-bin-dir")
        elif os.path.isfile(os.path.join(here, "..", "bin", "xpcshell")):
            # assume tests are being run from a tests archive
            options["localBin"] = os.path.abspath(os.path.join(here, "..", "bin"))
        else:
            parser.error("Couldn't find local binary dir, specify --local-bin-dir")
    return options


class PathMapping:
    def __init__(self, localDir, remoteDir):
        self.local = localDir
        self.remote = remoteDir


def main():
    parser = parser_remote()
    options = parser.parse_args()

    options = verifyRemoteOptions(parser, options)
    log = commandline.setup_logging("Remote XPCShell", options, {"raw": sys.stdout})

    if options["interactive"] and not options["testPath"]:
        print(
            "Error: You must specify a test filename in interactive mode!",
            file=sys.stderr,
        )
        sys.exit(1)

    if options["xpcshell"] is None:
        options["xpcshell"] = "xpcshell"

    # The threadCount depends on the emulator rather than the host machine and
    # empirically 10 seems to yield the best performance.
    options["threadCount"] = min(options["threadCount"], 10)

    xpcsh = XPCShellRemote(options, log)

    if not xpcsh.runTests(
        options, testClass=RemoteXPCShellTestThread, mobileArgs=xpcsh.mobileArgs
    ):
        sys.exit(1)


if __name__ == "__main__":
    main()
