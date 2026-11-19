# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
import posixpath
import shutil
import signal
import subprocess
import time
from collections import namedtuple
from enum import Enum

from mozdevice import ADBDeviceFactory, ADBHost

try:
    import telnetlib
except ImportError:  # telnetlib was removed in Python 3.13
    from . import telnetlib

from .host_utils import ensure_host_utils, get_host_platform

MOZBUILD_PATH = os.environ.get(
    "MOZBUILD_STATE_PATH", os.path.expanduser(os.path.join("~", ".mozbuild"))
)

EMULATOR_HOME_DIR = os.path.join(MOZBUILD_PATH, "android-device")

EMULATOR_AUTH_FILE = os.path.join(
    os.path.expanduser("~"), ".emulator_console_auth_token"
)

SHORT_TIMEOUT = 10

verbose_logging = False

LLDB_SERVER_INSTALL_COMMANDS_SCRIPT = """
umask 0002

mkdir -p {lldb_bin_dir}

cp /data/local/tmp/lldb-server {lldb_bin_dir}
chmod +x {lldb_bin_dir}/lldb-server

chmod 0775 {lldb_dir}
""".lstrip()

LLDB_SERVER_START_COMMANDS_SCRIPT = """
umask 0002

export LLDB_DEBUGSERVER_LOG_FILE={lldb_log_file}
export LLDB_SERVER_LOG_CHANNELS="{lldb_log_channels}"
export LLDB_DEBUGSERVER_DOMAINSOCKET_DIR={socket_dir}

rm -rf {lldb_tmp_dir}
mkdir {lldb_tmp_dir}
export TMPDIR={lldb_tmp_dir}

rm -rf {lldb_log_dir}
mkdir {lldb_log_dir}

touch {lldb_log_file}
touch {platform_log_file}

cd {lldb_tmp_dir}
{lldb_bin_dir}/lldb-server platform --server --listen {listener_scheme}://{socket_file} \\
    --log-file "{platform_log_file}" --log-channels "{lldb_log_channels}" \\
    < /dev/null > {platform_stdout_log_file} 2>&1 &
""".lstrip()


class InstallIntent(Enum):
    YES = 1
    NO = 2


class UninstallIntent(Enum):
    YES = 1
    NO = 2


class AvdInfo:
    """
    Simple class to contain an AVD description.
    """

    def __init__(self, description, name, extra_args, x86):
        self.description = description
        self.name = name
        self.extra_args = extra_args
        self.x86 = x86


"""
   A dictionary to map an AVD type to a description of that type of AVD.

   There is one entry for each type of AVD used in Mozilla automated tests
   and the parameters for each reflect those used in mozharness.
"""
AVD_DICT = {
    "arm": AvdInfo(
        "Android arm",
        "mozemulator-armeabi-v7a",
        [
            "-skip-adb-auth",
            "-verbose",
            "-show-kernel",
            "-ranchu",
            "-selinux",
            "permissive",
            "-memory",
            "3072",
            "-cores",
            "4",
            "-skin",
            "800x1280",
            "-gpu",
            "on",
            "-no-snapstorage",
            "-no-snapshot",
            "-no-metrics",
            "-prop",
            "ro.test_harness=true",
        ],
        False,
    ),
    "arm64": AvdInfo(
        "Android arm64",
        "mozemulator-arm64",
        [
            "-skip-adb-auth",
            "-verbose",
            "-show-kernel",
            "-ranchu",
            "-selinux",
            "permissive",
            "-memory",
            "3072",
            "-cores",
            "4",
            "-skin",
            "800x1280",
            "-gpu",
            "on",
            "-no-snapstorage",
            "-no-snapshot",
            "-no-metrics",
            "-prop",
            "ro.test_harness=true",
        ],
        False,
    ),
    "x86_64": AvdInfo(
        "Android x86_64",
        "mozemulator-android34-x86_64",
        [
            "-skip-adb-auth",
            "-verbose",
            "-show-kernel",
            "-ranchu",
            "-selinux",
            "permissive",
            "-memory",
            "4096",
            "-cores",
            "8",
            "-prop",
            "ro.test_harness=true",
            "-no-snapstorage",
            "-no-snapshot",
            "-no-metrics",
            "-skin",
            "1080x1920",
        ],
        True,
    ),
}


def _get_device(substs, device_serial=None):
    adb_path = _find_sdk_exe(substs, "adb", False)
    if not adb_path:
        adb_path = "adb"
    device = ADBDeviceFactory(
        adb=adb_path, verbose=verbose_logging, device=device_serial
    )
    return device


def metadata_for_app(app, aab=False):
    """Given an app name like "fenix", "focus", or "org.mozilla.geckoview_example",
    return Android metadata including launch `activity_name`, `package_name`, 'subcommand'.
    to reduce special-casing throughout the code base"""
    metadata = namedtuple("metadata", ["activity_name", "package_name", "subcommand"])
    package_name = None
    activity_name = None
    install_apk = None
    install_aab = None

    # GeckoView Test Runner (for xpcshell, junit, wpt, mochitest, etc)
    if not app or app in {"test_runner", "org.mozilla.geckoview.test_runner"}:
        package_name = "org.mozilla.geckoview.test_runner"
        activity_name = "org.mozilla.geckoview.test_runner.TestRunnerActivity"
        install_apk = "install-geckoview-test_runner"
        install_aab = "install-geckoview-test_runner-aab"

    # GeckoView AndroidTest (install only)
    elif app in {"geckoview.test", "org.mozilla.geckoview.test"}:
        install_apk = "install-geckoview-test"

    # GeckoView Example
    elif app in {"geckoview_example", "org.mozilla.geckoview_example"}:
        package_name = "org.mozilla.geckoview_example"
        activity_name = "org.mozilla.geckoview_example.GeckoViewActivity"
        install_apk = "install-geckoview_example"
        install_aab = "install-geckoview_example-aab"

    # Fenix Debug/Nightly/Beta/Release
    # NOTE: We target the `IntentReceiverActivity` since that is what is used when
    #       opening URLs which is often what callers want to do.
    elif app in {"fenix", "fenix.debug", "org.mozilla.fenix.debug"}:
        package_name = "org.mozilla.fenix.debug"
        activity_name = "org.mozilla.fenix.IntentReceiverActivity"
        install_apk = "install-fenix"
    elif app in {"fenix.nightly", "org.mozilla.fenix"}:
        package_name = "org.mozilla.fenix"
        activity_name = "org.mozilla.fenix.IntentReceiverActivity"
        install_apk = "install-fenix-nightly"
    elif app in {"fenix.beta", "org.mozilla.firefox_beta"}:
        package_name = "org.mozilla.firefox_beta"
        activity_name = "org.mozilla.fenix.IntentReceiverActivity"
        install_apk = "install-fenix-beta"
    elif app in {"fenix.release", "org.mozilla.firefox"}:
        package_name = "org.mozilla.firefox"
        activity_name = "org.mozilla.fenix.IntentReceiverActivity"
        install_apk = "install-fenix-release"

    # Focus for Android
    elif app in {"focus", "focus-android", "focus.debug", "org.mozilla.focus.debug"}:
        package_name = "org.mozilla.focus.debug"
        activity_name = "org.mozilla.focus.activity.IntentReceiverActivity"
        install_apk = "install-focus"

    if aab:
        # Error out if we don't know how to use AABs to avoid confusion
        if not install_aab:
            raise Exception("I don't know how to use AABs for this app")
        return metadata(activity_name, package_name, install_aab)
    else:
        return metadata(activity_name, package_name, install_apk)


def get_android_device(build_obj, device_serial=None, verbose=False):
    # If device_serial not specified, check environment variables.
    if device_serial is None:
        device_serial = os.environ.get("ANDROID_SERIAL")
    if device_serial is None:
        device_serial = os.environ.get("DEVICE_SERIAL")

    # If no serial specified, check for a connected and ready device
    if device_serial is None:
        adb_path = _find_sdk_exe(build_obj.substs, "adb", False)
        if adb_path is None:
            _log_info("Couldn't find ADB. Quitting")
            return
        adbhost = ADBHost(adb=adb_path, verbose=verbose, timeout=SHORT_TIMEOUT)
        devices = adbhost.devices(timeout=SHORT_TIMEOUT)
        ready_devices = [d["device_serial"] for d in devices if d["state"] == "device"]
        if ready_devices:
            if len(ready_devices) > 1:
                _log_info(
                    "Multiple Android devices available. Please set ANDROID_SERIAL to pick one."
                )
                return
            device_serial = ready_devices[0]

    # If no device available, ask about starting an emulator
    if device_serial is None:
        emulator = AndroidEmulator("*", substs=build_obj.substs, verbose=verbose)
        if emulator.is_available():
            response = input(
                "No Android devices connected. Start an emulator? (Y/n) "
            ).strip()
            if response.lower().startswith("y") or response == "":
                if not emulator.check_avd():
                    _log_info("Android AVD not found, please run |mach bootstrap|")
                    return
                _log_info(
                    "Starting emulator running %s..." % emulator.get_avd_description()
                )
                emulator.start()
                emulator.wait_for_start()
                device_serial = "emulator-5554"

    # Try to open a connection to chosen device
    device = None
    if device_serial:
        device = _get_device(substs=build_obj.substs, device_serial=device_serial)

    # For compatability, we record active device in DEVICE_SERIAL environment
    if device:
        os.environ["DEVICE_SERIAL"] = device_serial

    return [device, device_serial]


def verify_android_device(
    build_obj,
    install=InstallIntent.NO,
    uninstall=UninstallIntent.YES,
    xre=False,
    debugger=False,
    network=False,
    verbose=False,
    app=None,
    device_serial=None,
    aab=False,
):
    """
    Determine if any Android device is connected via adb.
    If no device is found, prompt to start an emulator.
    If a device is found or an emulator started and 'install' is
    specified, also check whether Firefox is installed on the
    device; if not, prompt to install Firefox.
    If 'xre' is specified, also check with MOZ_HOST_BIN is set
    to a valid xre/host-utils directory; if not, prompt to set
    one up.
    If 'debugger' is specified, also check that lldb-server is installed;
    if it is not found, set it up.
    If 'network' is specified, also check that the device has basic
    network connectivity.
    Returns True if the emulator was started or another device was
    already connected.
    """
    if "MOZ_DISABLE_ADB_INSTALL" in os.environ or install == InstallIntent.NO:
        install = InstallIntent.NO
        _log_info(
            "Found MOZ_DISABLE_ADB_INSTALL in environment and/or the"
            " --no-install flag, skipping android app installation"
        )
    else:
        _log_info(
            "*********************************************************************\n"
            "Neither the MOZ_DISABLE_ADB_INSTALL environment variable nor the\n"
            "--no-install flag was found."
        )

        if uninstall == UninstallIntent.YES:
            _log_info(
                "The code will now uninstall the current app then re-install the\n"
                "android app from a different source. If you want to avoid unintalling\n"
                "the current app, set the --no-uninstall flag."
            )

        _log_info(
            "If you don't want this set your local env so that\n"
            "MOZ_DISABLE_ADB_INSTALL=True or pass the --no-install flag\n"
            "*********************************************************************"
        )

    [device, device_serial] = get_android_device(build_obj, device_serial, verbose)

    metadata = metadata_for_app(app, aab)

    if device and install != InstallIntent.NO:
        # Determine if test app is installed on the device; if not,
        # prompt to install. This feature allows a test command to
        # launch an emulator, install the test app, and proceed with testing
        # in one operation. It is also a basic safeguard against other
        # cases where testing is requested but test app installation has
        # been forgotten.
        # If a test app is installed, there is no way to determine whether
        # the current build is installed, and certainly no way to
        # determine if the installed build is the desired build.
        # Installing every time (without prompting) is problematic because:
        #  - it prevents testing against other builds (downloaded apk)
        #  - installation may take a couple of minutes.
        response = ""
        installed = device.is_app_installed(metadata.package_name)

        if not installed:
            _log_info(
                "It looks like %s is not installed on this device."
                % metadata.package_name
            )

        if metadata.subcommand and installed and uninstall == UninstallIntent.YES:
            device.uninstall_app(metadata.package_name)

        if metadata.activity_name == "org.mozilla.gecko.BrowserApp":
            if installed and uninstall == UninstallIntent.YES:
                device.uninstall_app(metadata.package_name)
            _log_info("Installing Firefox...")
            build_obj._run_make(directory=".", target="install", ensure_exit_code=False)
        elif metadata.subcommand:
            _log_info("Installing %s..." % metadata.package_name)
            build_obj._mach_context.commands.dispatch(
                "android",
                build_obj._mach_context,
                subcommand=metadata.subcommand,
                args=[],
            )
        elif not installed:
            response = input(
                "It looks like %s is not installed on this device,\n"
                "but I don't know how to install it.\n"
                "Install it now, then hit Enter " % metadata.package_name
            )

        device.run_as_package = metadata.package_name

    if device and xre:
        ensure_host_utils(build_obj, verbose=verbose)

    if device and network:
        # Optionally check the network: If on a device that does not look like
        # an emulator, verify that the device IP address can be obtained
        # and check that this host can ping the device.
        if "emulator" not in device_serial:
            device.run_as_package = metadata.package_name
            try:
                addr = device.get_ip_address()
                if not addr:
                    _log_warning("unable to get Android device's IP address!")
                    _log_warning(
                        "tests may fail without network connectivity to the device!"
                    )
                else:
                    _log_info("Android device's IP address: %s" % addr)
                    response = subprocess.check_output(["ping", "-c", "1", addr])
                    _log_debug(response)
            except Exception as e:
                _log_warning(
                    "unable to verify network connection to device: %s" % str(e)
                )
                _log_warning(
                    "tests may fail without network connectivity to the device!"
                )
        else:
            _log_debug("network check skipped on emulator")

    if debugger:
        _setup_or_run_lldb_server(
            metadata.package_name, build_obj.substs, device_serial, setup=True
        )

    return device is not None


def run_lldb_server(app, substs, device_serial):
    return _setup_or_run_lldb_server(app, substs, device_serial, setup=False)


def _setup_or_run_lldb_server(app, substs, device_serial, setup=True):
    device = _get_device(substs, device_serial)
    device.run_as_package = app

    # Don't use enable_run_as here, as this will not give you what you
    # want if we have root access on the device.
    pkg_dir = device.shell_output("run-as %s pwd" % app, attempts=3)
    if not pkg_dir or pkg_dir == "/":
        pkg_dir = "/data/data/%s" % app
        _log_warning(
            "Unable to resolve data directory for package %s, falling back to hardcoded path"
            % app
        )

    pkg_lldb_dir = posixpath.join(pkg_dir, "lldb")
    pkg_lldb_bin_dir = posixpath.join(pkg_lldb_dir, "bin")
    pkg_lldb_server = posixpath.join(pkg_lldb_bin_dir, "lldb-server")

    if setup:
        # Check whether lldb-server is already there
        if device.shell_bool("test -x %s" % pkg_lldb_server, enable_run_as=True):
            _log_info(
                "Found lldb-server binary, terminating any running server processes..."
            )
            # lldb-server is already present. Kill any running server.
            device.shell("pkill -f lldb-server", enable_run_as=True)
        else:
            _log_info("lldb-server not found, installing...")

            # We need to do an install
            try:
                server_path_local = substs["ANDROID_LLDB_SERVER"]
            except KeyError:
                _log_info(
                    "ANDROID_LLDB_SERVER is not configured correctly; "
                    "please re-configure your build."
                )
                return

            device.push(server_path_local, "/data/local/tmp")

            install_cmds = LLDB_SERVER_INSTALL_COMMANDS_SCRIPT.format(
                lldb_bin_dir=pkg_lldb_bin_dir, lldb_dir=pkg_lldb_dir
            )

            install_cmds = [l for l in install_cmds.splitlines() if l]

            _log_debug(
                "Running the following installation commands:\n%r" % (install_cmds,)
            )

            device.batch_execute(install_cmds, enable_run_as=True)
        return

    pkg_lldb_sock_file = posixpath.join(pkg_dir, "platform-%d.sock" % int(time.time()))

    pkg_lldb_log_dir = posixpath.join(pkg_lldb_dir, "log")
    pkg_lldb_tmp_dir = posixpath.join(pkg_lldb_dir, "tmp")

    pkg_lldb_log_file = posixpath.join(pkg_lldb_log_dir, "lldb-server.log")
    pkg_platform_log_file = posixpath.join(pkg_lldb_log_dir, "platform.log")
    pkg_platform_stdout_log_file = posixpath.join(
        pkg_lldb_log_dir, "platform-stdout.log"
    )

    listener_scheme = "unix-abstract"
    log_channels = "lldb process:gdb-remote packets"

    start_cmds = LLDB_SERVER_START_COMMANDS_SCRIPT.format(
        lldb_bin_dir=pkg_lldb_bin_dir,
        lldb_log_file=pkg_lldb_log_file,
        lldb_log_channels=log_channels,
        socket_dir=pkg_dir,
        lldb_tmp_dir=pkg_lldb_tmp_dir,
        lldb_log_dir=pkg_lldb_log_dir,
        platform_log_file=pkg_platform_log_file,
        listener_scheme=listener_scheme,
        platform_stdout_log_file=pkg_platform_stdout_log_file,
        socket_file=pkg_lldb_sock_file,
    )

    start_cmds = [l for l in start_cmds.splitlines() if l]

    _log_debug("Running the following start commands:\n%r" % (start_cmds,))

    device.batch_execute(start_cmds, enable_run_as=True)

    return pkg_lldb_sock_file


def get_adb_path(build_obj):
    return _find_sdk_exe(build_obj.substs, "adb", False)


def prepend_adb_dir_to_path(build_obj):
    """Prepend the directory of the adb that mach would use to PATH, so that
    harness code running a bare ``adb`` finds the one from mach bootstrap.

    Legacy harnesses only: new code should use ``get_adb_path``. Also
    shadows host tools like ``sqlite3``.
    """
    try:
        substs = build_obj.substs
    except Exception:
        substs = None
    adb_path = _find_sdk_exe(substs, "adb", False)
    if adb_path:
        os.environ["PATH"] = os.pathsep.join([
            os.path.dirname(adb_path),
            os.environ.get("PATH", ""),
        ])


def grant_runtime_permissions(build_obj, app, device_serial=None):
    """
    Grant required runtime permissions to the specified app
    (eg. org.mozilla.geckoview.test_runner).
    """
    device = _get_device(build_obj.substs, device_serial)
    device.run_as_package = app
    device.grant_runtime_permissions(app)


class AndroidEmulator:
    """
    Support running the Android emulator with an AVD from Mozilla
    test automation.

    Example usage:
        emulator = AndroidEmulator()
        if not emulator.is_running() and emulator.is_available():
            if not emulator.check_avd():
                print("Android Emulator AVD not found, please run |mach bootstrap|")
            emulator.start()
            emulator.wait_for_start()
            emulator.wait()
    """

    def __init__(self, avd_type=None, verbose=False, substs=None, device_serial=None):
        global verbose_logging
        self.emulator_log = None
        self.emulator_path = "emulator"
        verbose_logging = verbose
        self.substs = substs
        self.avd_type = self._get_avd_type(avd_type)
        self.avd_info = AVD_DICT[self.avd_type]
        self.gpu = True
        self.restarted = False
        self.device_serial = device_serial
        self.avd_path = os.path.join(
            EMULATOR_HOME_DIR, "avd", "%s.avd" % self.avd_info.name
        )
        _log_debug("Running on %s" % platform.platform())
        _log_debug("Emulator created with type %s" % self.avd_type)

    def __del__(self):
        if self.emulator_log:
            self.emulator_log.close()

    def is_running(self):
        """
        Returns True if the Android emulator is running.
        """
        adb_path = _find_sdk_exe(self.substs, "adb", False)
        if not adb_path:
            adb_path = "adb"
        adbhost = ADBHost(adb=adb_path, verbose=verbose_logging, timeout=SHORT_TIMEOUT)
        devs = adbhost.devices(timeout=SHORT_TIMEOUT)
        devs = [(d["device_serial"], d["state"]) for d in devs]
        _log_debug(devs)
        if ("emulator-5554", "device") in devs:
            return True
        return False

    def is_available(self):
        """
        Returns True if an emulator executable is found.
        """
        found = False
        emulator_path = _find_sdk_exe(self.substs, "emulator", True)
        if emulator_path:
            self.emulator_path = emulator_path
            found = True
        return found

    def check_avd(self):
        """
        Determine if the AVD is already installed locally.

        Returns True if the AVD is installed.
        """
        if os.path.exists(self.avd_path):
            _log_debug("AVD found at %s" % self.avd_path)
            return True
        _log_warning("Could not find AVD at %s" % self.avd_path)
        return False

    def start(self, gpu_arg=None):
        """
        Launch the emulator.
        """
        if self.avd_info.x86 and "linux" in get_host_platform():
            _verify_kvm(self.substs)
        if os.path.exists(EMULATOR_AUTH_FILE):
            os.remove(EMULATOR_AUTH_FILE)
            _log_debug("deleted %s" % EMULATOR_AUTH_FILE)
        self._update_avd_paths()
        # create an empty auth file to disable emulator authentication
        auth_file = open(EMULATOR_AUTH_FILE, "w")
        auth_file.close()

        env = os.environ
        env["ANDROID_EMULATOR_HOME"] = EMULATOR_HOME_DIR
        env["ANDROID_AVD_HOME"] = os.path.join(EMULATOR_HOME_DIR, "avd")
        command = [self.emulator_path, "-avd", self.avd_info.name]
        override = os.environ.get("MOZ_EMULATOR_COMMAND_ARGS")
        if override:
            command += override.split()
            _log_debug("Found MOZ_EMULATOR_COMMAND_ARGS in env: %s" % override)
        else:
            if gpu_arg:
                command += ["-gpu", gpu_arg]
                # Clear self.gpu to avoid our restart-without-gpu feature: if a specific
                # gpu setting is requested, try to use that, and nothing else.
                self.gpu = False
            elif self.gpu:
                command += ["-gpu", "on"]
            if self.avd_info.extra_args:
                # -enable-kvm option is not valid on OSX and Windows
                if (
                    get_host_platform() in ("macosx64", "win32")
                    and "-enable-kvm" in self.avd_info.extra_args
                ):
                    self.avd_info.extra_args.remove("-enable-kvm")
                command += self.avd_info.extra_args
            if os.environ.get("MOZ_HEADLESS"):
                command += ["-no-window"]
        log_path = os.path.join(EMULATOR_HOME_DIR, "emulator.log")
        self.emulator_log = open(log_path, "w+")
        _log_debug("Starting the emulator with this command: %s" % " ".join(command))
        _log_debug("Emulator output will be written to '%s'" % log_path)
        self.proc = subprocess.Popen(
            command,
            env=env,
            stdin=subprocess.PIPE,
            stdout=self.emulator_log,
            stderr=self.emulator_log,
        )
        _log_debug("Emulator started with pid %d" % int(self.proc.pid))

    def wait_for_start(self):
        """
        Verify that the emulator is running, the emulator device is visible
        to adb, and Android has booted.
        """
        if not self.proc:
            _log_warning("Emulator not started!")
            return False
        if self.check_completed():
            return False
        _log_debug("Waiting for device status...")
        adb_path = _find_sdk_exe(self.substs, "adb", False)
        if not adb_path:
            adb_path = "adb"
        adbhost = ADBHost(adb=adb_path, verbose=verbose_logging, timeout=SHORT_TIMEOUT)
        devs = adbhost.devices(timeout=SHORT_TIMEOUT)
        devs = [(d["device_serial"], d["state"]) for d in devs]
        while ("emulator-5554", "device") not in devs:
            time.sleep(10)
            if self.check_completed():
                return False
            devs = adbhost.devices(timeout=SHORT_TIMEOUT)
            devs = [(d["device_serial"], d["state"]) for d in devs]
        _log_debug("Device status verified.")

        _log_debug("Checking that Android has booted...")
        device = _get_device(self.substs, self.device_serial)
        complete = False
        while not complete:
            output = ""
            try:
                output = device.get_prop("sys.boot_completed", timeout=5)
            except Exception:
                # adb not yet responding...keep trying
                pass
            if output.strip() == "1":
                complete = True
            else:
                time.sleep(10)
                if self.check_completed():
                    return False
        _log_debug("Android boot status verified.")

        if not self._verify_emulator():
            return False
        if self.avd_info.x86:
            _log_info(
                "Running the x86_64 emulator; be sure to install a compatible APK!"
            )
            _log_info(
                "AArch64 APKs are supported but will perform poorly compared to x86_64."
            )
        else:
            _log_info("Running the arm emulator; be sure to install an arm APK!")
        return True

    def check_completed(self):
        if self.proc.poll() is not None:
            if self.gpu:
                try:
                    for line in self.emulator_log.readlines():
                        if (
                            "Invalid value for -gpu" in line
                            or "Invalid GPU mode" in line
                        ):
                            self.gpu = False
                            break
                except Exception as e:
                    _log_warning(str(e))

            if not self.gpu and not self.restarted:
                _log_warning(
                    "Emulator failed to start. Your emulator may be out of date."
                )
                _log_warning("Trying to restart the emulator without -gpu argument.")
                self.restarted = True
                self.start()
                return False
            _log_warning("Emulator has already completed!")
            log_path = os.path.join(EMULATOR_HOME_DIR, "emulator.log")
            _log_warning(
                "See log at %s and/or use --verbose for more information." % log_path
            )
            return True
        return False

    def wait(self):
        """
        Wait for the emulator to close. If interrupted, close the emulator.
        """
        try:
            self.proc.wait()
        except Exception:
            if self.proc.poll() is None:
                self.cleanup()
        return self.proc.poll()

    def cleanup(self):
        """
        Close the emulator.
        """
        self.proc.kill(signal.SIGTERM)

    def get_avd_description(self):
        """
        Return the human-friendly description of this AVD.
        """
        return self.avd_info.description

    def _update_avd_paths(self):
        ini_path = os.path.join(EMULATOR_HOME_DIR, "avd", "%s.ini" % self.avd_info.name)
        with open(ini_path) as f:
            lines = f.readlines()
        with open(ini_path, "w") as f:
            for line in lines:
                if line.startswith("path="):
                    f.write("path=%s\n" % self.avd_path)
                elif line.startswith("path.rel="):
                    f.write("path.rel=avd/%s.avd\n" % self.avd_info.name)
                else:
                    f.write(line)

    def _telnet_read_until(self, telnet, expected, timeout):
        if isinstance(expected, str):
            expected = expected.encode("ascii")
        return telnet.read_until(expected, timeout)

    def _telnet_write(self, telnet, command):
        if isinstance(command, str):
            command = command.encode("ascii")
        telnet.write(command)

    def _telnet_cmd(self, telnet, command):
        _log_debug(">>> %s" % command)
        self._telnet_write(telnet, "%s\n" % command)
        result = self._telnet_read_until(telnet, "OK", 10)
        _log_debug("<<< %s" % result)
        return result

    def _verify_emulator(self):
        telnet_ok = False
        tn = None
        while not telnet_ok:
            try:
                tn = telnetlib.Telnet("localhost", 5554, 10)
                if tn is not None:
                    self._telnet_read_until(tn, "OK", 10)
                    self._telnet_cmd(tn, "avd status")
                    self._telnet_cmd(tn, "redir list")
                    self._telnet_cmd(tn, "network status")
                    self._telnet_write(tn, "quit\n")
                    tn.read_all()
                    telnet_ok = True
                else:
                    _log_warning("Unable to connect to port 5554")
            except Exception:
                _log_warning("Trying again after unexpected exception")
            finally:
                if tn is not None:
                    tn.close()
            if not telnet_ok:
                time.sleep(10)
                if self.proc.poll() is not None:
                    _log_warning("Emulator has already completed!")
                    return False
        return telnet_ok

    def _get_avd_type(self, requested):
        if requested in AVD_DICT.keys():
            return requested
        if self.substs:
            target_cpu = self.substs["TARGET_CPU"]
            host_cpu = self.substs["HOST_CPU_ARCH"]
            if host_cpu == "aarch64" and target_cpu == "aarch64":
                return "arm64"
            elif target_cpu.startswith("arm"):
                return "arm"
        return "x86_64"


def _find_sdk_exe(substs, exe, tools):
    if tools:
        subdirs = ["emulator", "tools"]
    else:
        subdirs = ["platform-tools"]

    found = False
    if not found and substs:
        # It's best to use the tool specified by the build, rather
        # than something we find on the PATH or crawl for.
        try:
            exe_path = substs[exe.upper()]
            if os.path.exists(exe_path):
                found = True
            else:
                _log_debug("Unable to find executable at %s" % exe_path)
        except KeyError:
            _log_debug("%s not set" % exe.upper())

    # Append '.exe' to the name on Windows if it's not present,
    # so that the executable can be found.
    if os.name == "nt" and not exe.lower().endswith(".exe"):
        exe += ".exe"

    if not found:
        # Can exe be found in the Android SDK?
        try:
            android_sdk_root = os.environ["ANDROID_SDK_ROOT"]
            for subdir in subdirs:
                exe_path = os.path.join(android_sdk_root, subdir, exe)
                if os.path.exists(exe_path):
                    found = True
                    break
                else:
                    _log_debug("Unable to find executable at %s" % exe_path)
        except KeyError:
            _log_debug("ANDROID_SDK_ROOT not set")

    if not found:
        # Can exe be found in the default bootstrap location?
        for subdir in subdirs:
            for sdkdir in [
                "android-sdk-linux",
                "android-sdk-macosx",
                "android-sdk-windows",
            ]:
                exe_path = os.path.join(MOZBUILD_PATH, sdkdir, subdir, exe)
                if os.path.exists(exe_path):
                    found = True
                    break
                else:
                    _log_debug("Unable to find executable at %s" % exe_path)

            if found:
                break

    if not found:
        # Is exe on PATH?
        exe_path = shutil.which(exe)
        if exe_path:
            found = True
        else:
            _log_debug("Unable to find executable on PATH")

    if found:
        _log_debug("%s found at %s" % (exe, exe_path))
        try:
            creation_time = os.path.getctime(exe_path)
            _log_debug("  ...with creation time %s" % time.ctime(creation_time))
        except Exception:
            _log_warning("Could not get creation time for %s" % exe_path)

        prop_path = os.path.join(os.path.dirname(exe_path), "source.properties")
        if os.path.exists(prop_path):
            with open(prop_path) as f:
                for line in f.readlines():
                    if line.startswith("Pkg.Revision"):
                        line = line.strip()
                        _log_debug(
                            "  ...with SDK version in %s: %s" % (prop_path, line)
                        )
                        break
    else:
        exe_path = None
    return exe_path


def _log_debug(text):
    if verbose_logging:
        print("DEBUG: %s" % text)


def _log_warning(text):
    print("WARNING: %s" % text)


def _log_info(text):
    print("%s" % text)


def _verify_kvm(substs):
    # 'emulator -accel-check' should produce output like:
    # accel:
    # 0
    # KVM (version 12) is installed and usable
    # accel
    emulator_path = _find_sdk_exe(substs, "emulator", True)
    if not emulator_path:
        emulator_path = "emulator"
    command = [emulator_path, "-accel-check"]
    try:
        out = subprocess.check_output(command)
        out = out.decode()
        if "is installed and usable" in "".join(out):
            return
    except Exception as e:
        _log_warning(str(e))
    _log_warning("Unable to verify kvm acceleration!")
    _log_warning("The x86/x86_64 emulator may fail to start without kvm.")
