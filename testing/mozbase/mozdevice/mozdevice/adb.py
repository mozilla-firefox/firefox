# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import os
import posixpath
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback
from shutil import copy2 as shutil_copy2
from shutil import copytree
from threading import Thread

from . import version_codes

_TEST_ROOT = None

# Caches the user's response to the clear-app-data confirmation prompt so they
# are asked at most once per process. None until the user has been prompted,
# then True (confirmed) or False (declined).
_CLEAR_APP_DATA_CONFIRMED = None


class ADBProcess:
    """ADBProcess encapsulates the data related to executing the adb process."""

    def __init__(self, args, use_stdout_pipe=False, timeout=None):
        #: command argument list.
        self.args = args
        Popen_args = {}

        #: Temporary file handle to be used for stdout.
        if use_stdout_pipe:
            self.stdout_file = subprocess.PIPE
            # Reading utf-8 from the stdout pipe
            Popen_args["encoding"] = "utf-8"
        else:
            self.stdout_file = tempfile.NamedTemporaryFile(mode="w+b")
        Popen_args["stdout"] = self.stdout_file

        #: boolean indicating if the command timed out.
        self.timedout = None

        #: exitcode of the process.
        self.exitcode = None

        #: subprocess Process object used to execute the command.
        Popen_args["stderr"] = subprocess.STDOUT
        self.proc = subprocess.Popen(args, **Popen_args)

        # If a timeout is set, then create a thread responsible for killing the
        # process, as well as updating the exitcode and timedout status.
        def timeout_thread(adb_process, timeout):
            start_time = time.time()
            polling_interval = 0.001
            adb_process.exitcode = adb_process.proc.poll()
            while (time.time() - start_time) <= float(
                timeout
            ) and adb_process.exitcode is None:
                time.sleep(polling_interval)
                adb_process.exitcode = adb_process.proc.poll()

            if adb_process.exitcode is None:
                adb_process.proc.kill()
                adb_process.timedout = True
                adb_process.exitcode = adb_process.proc.poll()

        if timeout:
            Thread(target=timeout_thread, args=(self, timeout), daemon=True).start()

    @property
    def stdout(self):
        """Return the contents of stdout."""
        assert not self.stdout_file == subprocess.PIPE
        if not self.stdout_file or self.stdout_file.closed:
            content = ""
        else:
            self.stdout_file.seek(0, os.SEEK_SET)
            content = self.stdout_file.read().rstrip().decode()
        return content

    def __str__(self):
        # Remove -s <serialno> from the error message to allow bug suggestions
        # to be independent of the individual failing device.
        arg_string = " ".join(self.args)
        arg_string = re.sub(r" -s [\w-]+", "", arg_string)
        return f"args: {arg_string}, exitcode: {self.exitcode}, stdout: {self.stdout}"

    def __iter__(self):
        assert self.stdout_file == subprocess.PIPE
        return self

    def __next__(self):
        assert self.stdout_file == subprocess.PIPE
        try:
            return next(self.proc.stdout)
        except StopIteration:
            # Wait until the process ends.
            while self.exitcode is None or self.timedout:
                time.sleep(0.001)
            raise StopIteration


# ADBError and ADBTimeoutError are treated differently in order that
# ADBTimeoutErrors can be handled distinctly from ADBErrors.


class ADBError(Exception):
    """ADBError is raised in situations where a command executed on a
    device either exited with a non-zero exitcode or when an
    unexpected error condition has occurred. Generally, ADBErrors can
    be handled and the device can continue to be used.
    """

    pass


class ADBProcessError(ADBError):
    """ADBProcessError is raised when an associated ADBProcess is
    available and relevant.
    """

    def __init__(self, adb_process):
        ADBError.__init__(self, str(adb_process))
        self.adb_process = adb_process


class ADBListDevicesError(ADBError):
    """ADBListDevicesError is raised when errors are found listing the
    devices, typically not any permissions.

    The devices information is stocked with the *devices* member.
    """

    def __init__(self, msg, devices):
        ADBError.__init__(self, msg)
        self.devices = devices


class ADBTimeoutError(Exception):
    """ADBTimeoutError is raised when either a host command or shell
    command takes longer than the specified timeout to execute. The
    timeout value is set in the ADBCommand constructor and is 300 seconds by
    default. This error is typically fatal since the host is having
    problems communicating with the device. You may be able to recover
    by rebooting, but this is not guaranteed.

    Recovery options are:

    * Killing and restarting the adb server via
      ::

          adb kill-server; adb start-server

    * Rebooting the device manually.
    * Rebooting the host.
    """

    pass


class ADBDeviceFactoryError(Exception):
    """ADBDeviceFactoryError is raised when the ADBDeviceFactory is in
    an inconsistent state.
    """

    pass


class ADBCommand:
    """ADBCommand provides a basic interface to adb commands
    which is used to provide the 'command' methods for the
    classes ADBHost and ADBDevice.

    ADBCommand should only be used as the base class for other
    classes and should not be instantiated directly. To enforce this
    restriction calling ADBCommand's constructor will raise a
    NonImplementedError exception.

    :param str adb: path to adb executable. Defaults to 'adb'.
    :param str adb_host: host of the adb server.
    :param int adb_port: port of the adb server.
    :param str logger_name: logging logger name. Defaults to 'adb'.
    :param int timeout: The default maximum time in
        seconds for any spawned adb process to complete before
        throwing an ADBTimeoutError.  This timeout is per adb call. The
        total time spent may exceed this value. If it is not
        specified, the value defaults to 300.
    :param bool verbose: provide verbose output
    :param bool use_root: Use root if available on device
    :raises: :exc:`ADBError`
             :exc:`ADBTimeoutError`

    ::

       from mozdevice import ADBCommand

       try:
           adbcommand = ADBCommand()
       except NotImplementedError:
           print "ADBCommand can not be instantiated."
    """

    def __init__(
        self,
        adb="adb",
        adb_host=None,
        adb_port=None,
        logger_name="adb",
        timeout=300,
        verbose=False,
        use_root=True,
    ):
        if self.__class__ == ADBCommand:
            raise NotImplementedError

        self._logger = self._get_logger(logger_name, verbose)
        self._verbose = verbose
        self._use_root = use_root
        self._adb_path = adb
        self._adb_host = adb_host
        self._adb_port = adb_port
        self._timeout = timeout
        self._polling_interval = 0.001
        self._adb_version = ""

        self._logger.debug(f"{self.__class__.__name__}: {self.__dict__}")

        # catch early a missing or non executable adb command
        # and get the adb version while we are at it.
        try:
            output = subprocess.Popen(
                [adb, "version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
            ).communicate()
            re_version = re.compile(r"Android Debug Bridge version (.*)")
            if isinstance(output[0], bytes):
                self._adb_version = re_version.match(
                    output[0].decode("utf-8", "replace")
                ).group(1)
            else:
                self._adb_version = re_version.match(output[0]).group(1)

            if self._adb_version < "1.0.36":
                raise ADBError(
                    "adb version %s less than minimum 1.0.36" % self._adb_version
                )

        except Exception as exc:
            raise ADBError(f"{exc}: {adb} is not executable.")

    def _get_logger(self, logger_name, verbose):
        logger = None
        level = "DEBUG" if verbose else "INFO"
        try:
            import mozlog

            logger = mozlog.get_default_logger(logger_name)
            if not logger:
                if sys.__stdout__.isatty():
                    defaults = {"mach": sys.stdout}
                else:
                    defaults = {"tbpl": sys.stdout}
                logger = mozlog.commandline.setup_logging(
                    logger_name, {}, defaults, formatter_defaults={"level": level}
                )
        except ImportError:
            pass

        if logger is None:
            import logging

            logger = logging.getLogger(logger_name)
            logger.setLevel(level)
        return logger

    # Host Command methods

    def command(self, cmds, device_serial=None, timeout=None):
        """Executes an adb command on the host.

        :param list cmds: The command and its arguments to be
            executed.
        :param str device_serial: The device's
            serial number if the adb command is to be executed against
            a specific device. If it is not specified, ANDROID_SERIAL
            from the environment will be used if it is set.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBCommand constructor is used.
        :return: :class:`ADBProcess`

        command() provides a low level interface for executing
        commands on the host via adb.

        command() executes on the host in such a fashion that stdout
        of the adb process is a file handle on the host and
        the exit code is available as the exit code of the adb
        process.

        The caller provides a list containing commands, as well as a
        timeout period in seconds.

        A subprocess is spawned to execute adb with stdout and stderr
        directed to a temporary file. If the process takes longer than
        the specified timeout, the process is terminated.

        It is the caller's responsibilty to clean up by closing
        the stdout temporary file.
        """
        args = [self._adb_path]
        device_serial = device_serial or os.environ.get("ANDROID_SERIAL")
        if self._adb_host:
            args.extend(["-H", self._adb_host])
        if self._adb_port:
            args.extend(["-P", str(self._adb_port)])
        if device_serial:
            args.extend(["-s", device_serial, "wait-for-device"])
        args.extend(cmds)

        adb_process = ADBProcess(args)

        if timeout is None:
            timeout = self._timeout

        start_time = time.time()
        adb_process.exitcode = adb_process.proc.poll()
        while (time.time() - start_time) <= float(
            timeout
        ) and adb_process.exitcode is None:
            time.sleep(self._polling_interval)
            adb_process.exitcode = adb_process.proc.poll()
        if adb_process.exitcode is None:
            adb_process.proc.kill()
            adb_process.timedout = True
            adb_process.exitcode = adb_process.proc.poll()

        adb_process.stdout_file.seek(0, os.SEEK_SET)

        return adb_process

    def command_output(self, cmds, device_serial=None, timeout=None):
        """Executes an adb command on the host returning stdout.

        :param list cmds: The command and its arguments to be
            executed.
        :param str device_serial: The device's
            serial number if the adb command is to be executed against
            a specific device. If it is not specified, ANDROID_SERIAL
            from the environment will be used if it is set.
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBCommand constructor is used.
        :return: str - content of stdout.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        adb_process = None
        try:
            # Need to force the use of the ADBCommand class's command
            # since ADBDevice will redefine command and call its
            # own version otherwise.
            adb_process = ADBCommand.command(
                self, cmds, device_serial=device_serial, timeout=timeout
            )
            if adb_process.timedout:
                raise ADBTimeoutError("%s" % adb_process)
            if adb_process.exitcode:
                raise ADBProcessError(adb_process)
            output = adb_process.stdout
            if self._verbose:
                self._logger.debug(
                    "command_output: %s, "
                    "timeout: %s, "
                    "timedout: %s, "
                    "exitcode: %s, output: %s"
                    % (
                        " ".join(adb_process.args),
                        timeout,
                        adb_process.timedout,
                        adb_process.exitcode,
                        output,
                    )
                )

            return output
        finally:
            if adb_process and isinstance(adb_process.stdout_file, io.IOBase):
                adb_process.stdout_file.close()


class ADBHost(ADBCommand):
    """ADBHost provides a basic interface to adb host commands
    which do not target a specific device.

    :param str adb: path to adb executable. Defaults to 'adb'.
    :param str adb_host: host of the adb server.
    :param int adb_port: port of the adb server.
    :param logger_name: logging logger name. Defaults to 'adb'.
    :param int timeout: The default maximum time in
        seconds for any spawned adb process to complete before
        throwing an ADBTimeoutError.  This timeout is per adb call. The
        total time spent may exceed this value. If it is not
        specified, the value defaults to 300.
    :param bool verbose: provide verbose output
    :raises: :exc:`ADBError`
             :exc:`ADBTimeoutError`

    ::

       from mozdevice import ADBHost

       adbhost = ADBHost()
       adbhost.start_server()
    """

    def __init__(
        self,
        adb="adb",
        adb_host=None,
        adb_port=None,
        logger_name="adb",
        timeout=300,
        verbose=False,
    ):
        ADBCommand.__init__(
            self,
            adb=adb,
            adb_host=adb_host,
            adb_port=adb_port,
            logger_name=logger_name,
            timeout=timeout,
            verbose=verbose,
            use_root=True,
        )

    def command(self, cmds, timeout=None):
        """Executes an adb command on the host.

        :param list cmds: The command and its arguments to be
            executed.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBHost constructor is used.
        :return: :class:`ADBProcess`

        command() provides a low level interface for executing
        commands on the host via adb.

        command() executes on the host in such a fashion that stdout
        of the adb process is a file handle on the host and
        the exit code is available as the exit code of the adb
        process.

        The caller provides a list containing commands, as well as a
        timeout period in seconds.

        A subprocess is spawned to execute adb with stdout and stderr
        directed to a temporary file. If the process takes longer than
        the specified timeout, the process is terminated.

        It is the caller's responsibilty to clean up by closing
        the stdout temporary file.
        """
        return ADBCommand.command(self, cmds, timeout=timeout)

    def command_output(self, cmds, timeout=None):
        """Executes an adb command on the host returning stdout.

        :param list cmds: The command and its arguments to be
            executed.
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBHost constructor is used.
        :return: str - content of stdout.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        return ADBCommand.command_output(self, cmds, timeout=timeout)

    def start_server(self, timeout=None):
        """Starts the adb server.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBHost constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`

        Attempting to use start_server with any adb_host value other than None
        will fail with an ADBError exception.

        You will need to start the server on the remote host via the command:

        .. code-block:: shell

            adb -a fork-server server

        If you wish the remote adb server to restart automatically, you can
        enclose the command in a loop as in:

        .. code-block:: shell

            while true; do
              adb -a fork-server server
            done
        """
        self.command_output(["start-server"], timeout=timeout)

    def kill_server(self, timeout=None):
        """Kills the adb server.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBHost constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self.command_output(["kill-server"], timeout=timeout)

    def devices(self, timeout=None):
        """Executes adb devices -l and returns a list of objects describing attached devices.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBHost constructor is used.
        :return: an object contain
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBListDevicesError`
                 :exc:`ADBError`

        The output of adb devices -l

        ::

            $ adb devices -l
            List of devices attached
            b313b945               device usb:1-7 product:d2vzw model:SCH_I535 device:d2vzw

        is parsed and placed into an object as in

        ::

            [
                {
                    "device_serial": "b313b945",
                    "state": "device",
                    "product": "d2vzw",
                    "usb": "1-7",
                    "device": "d2vzw",
                    "model": "SCH_I535",
                }
            ]
        """
        # b313b945               device usb:1-7 product:d2vzw model:SCH_I535 device:d2vzw
        # from Android system/core/adb/transport.c statename()
        re_device_info = re.compile(
            r"([^\s]+)\s+(offline|bootloader|device|host|recovery|sideload|"
            "no permissions|unauthorized|unknown)"
        )
        devices = []
        lines = self.command_output(["devices", "-l"], timeout=timeout).splitlines()
        for line in lines:
            if line == "List of devices attached ":
                continue
            match = re_device_info.match(line)
            if match:
                device = {"device_serial": match.group(1), "state": match.group(2)}
                remainder = line[match.end(2) :].strip()
                if remainder:
                    try:
                        device.update(
                            dict([j.split(":") for j in remainder.split(" ")])
                        )
                    except ValueError:
                        self._logger.warning(
                            "devices: Unable to parse remainder for device %s" % line
                        )
                devices.append(device)
        for device in devices:
            if device["state"] == "no permissions":
                raise ADBListDevicesError(
                    "No permissions to detect devices. You should restart the"
                    " adb server as root:\n"
                    "\n# adb kill-server\n# adb start-server\n"
                    "\nor maybe configure your udev rules.",
                    devices,
                )
        return devices


ADBDEVICES = {}


def ADBDeviceFactory(
    device=None,
    adb="adb",
    adb_host=None,
    adb_port=None,
    test_root=None,
    logger_name="adb",
    timeout=300,
    verbose=False,
    device_ready_retry_wait=20,
    device_ready_retry_attempts=3,
    use_root=True,
    share_test_root=True,
    run_as_package=None,
):
    """ADBDeviceFactory provides a factory for :class:`ADBDevice`
    instances that enforces the requirement that only one
    :class:`ADBDevice` be created for each attached device. It uses
    the identical arguments as the :class:`ADBDevice`
    constructor. This is also used to ensure that the device's
    test_root is initialized to an empty directory before tests are
    run on the device.

    :return: :class:`ADBDevice`
    :raises: :exc:`ADBDeviceFactoryError`
             :exc:`ADBError`
             :exc:`ADBTimeoutError`

    """
    device = device or os.environ.get("ANDROID_SERIAL")
    if device is not None and device in ADBDEVICES:
        # We have already created an ADBDevice for this device, just re-use it.
        adbdevice = ADBDEVICES[device]
    elif device is None and ADBDEVICES:
        # We did not specify the device serial number and we have
        # already created an ADBDevice which means we must only have
        # one device connected and we can re-use the existing ADBDevice.
        devices = list(ADBDEVICES.keys())
        assert len(devices) == 1, (
            "Only one device may be connected if the device serial number is not specified."
        )
        adbdevice = ADBDEVICES[devices[0]]
    elif (
        device is not None
        and device not in ADBDEVICES
        or device is None
        and not ADBDEVICES
    ):
        # The device has not had an ADBDevice created yet.
        adbdevice = ADBDevice(
            device=device,
            adb=adb,
            adb_host=adb_host,
            adb_port=adb_port,
            test_root=test_root,
            logger_name=logger_name,
            timeout=timeout,
            verbose=verbose,
            device_ready_retry_wait=device_ready_retry_wait,
            device_ready_retry_attempts=device_ready_retry_attempts,
            use_root=use_root,
            share_test_root=share_test_root,
            run_as_package=run_as_package,
        )
        ADBDEVICES[adbdevice._device_serial] = adbdevice
    else:
        raise ADBDeviceFactoryError(
            "Inconsistent ADBDeviceFactory: device: %s, ADBDEVICES: %s"
            % (device, ADBDEVICES)
        )
    # Clean the test root before testing begins.
    if test_root:
        adbdevice.rm(
            posixpath.join(adbdevice.test_root, "*"),
            recursive=True,
            force=True,
            timeout=timeout,
        )
    # Sync verbose and update the logger configuration in case it has
    # changed since the initial initialization
    if verbose != adbdevice._verbose:
        adbdevice._verbose = verbose
        adbdevice._logger = adbdevice._get_logger(adbdevice._logger.name, verbose)
    return adbdevice


class ADBDevice(ADBCommand):
    """ADBDevice provides methods which can be used to interact with the
    associated Android-based device.

    :param str device: When a string is passed in device, it
        is interpreted as the device serial number. This form is not
        compatible with devices containing a ":" in the serial; in
        this case ValueError will be raised.  When a dictionary is
        passed it must have one or both of the keys "device_serial"
        and "usb". This is compatible with the dictionaries in the
        list returned by ADBHost.devices(). If the value of
        device_serial is a valid serial not containing a ":" it will
        be used to identify the device, otherwise the value of the usb
        key, prefixed with "usb:" is used.  If None is passed and
        there is exactly one device attached to the host, that device
        is used. If None is passed and ANDROID_SERIAL is set in the environment,
        that device is used. If there is more than one device attached and
        device is None and ANDROID_SERIAL is not set in the environment, ValueError
        is raised. If no device is attached the constructor will block
        until a device is attached or the timeout is reached.
    :param str adb_host: host of the adb server to connect to.
    :param int adb_port: port of the adb server to connect to.
    :param str test_root: value containing the test root to be
        used on the device. This value will be shared among all
        instances of ADBDevice if share_test_root is True.
    :param str logger_name: logging logger name. Defaults to 'adb'
    :param int timeout: The default maximum time in
        seconds for any spawned adb process to complete before
        throwing an ADBTimeoutError.  This timeout is per adb call. The
        total time spent may exceed this value. If it is not
        specified, the value defaults to 300.
    :param bool verbose: provide verbose output
    :param int device_ready_retry_wait: number of seconds to wait
        between attempts to check if the device is ready after a
        reboot.
    :param integer device_ready_retry_attempts: number of attempts when
        checking if a device is ready.
    :param bool use_root: Use root if it is available on device
    :param bool share_test_root: True if instance should share the
        same test_root value with other ADBInstances. Defaults to True.
    :param str run_as_package: Name of package to be used in run-as in liew of
        using su.
    :raises: :exc:`ADBError`
             :exc:`ADBTimeoutError`
             :exc:`ValueError`

    ::

       from mozdevice import ADBDevice

       adbdevice = ADBDevice()
       print(adbdevice.list_files("/mnt/sdcard"))
       if adbdevice.process_exist("org.mozilla.geckoview.test_runner"):
           print("org.mozilla.geckoview.test_runner is running")
    """

    SOCKET_DIRECTION_REVERSE = "reverse"

    SOCKET_DIRECTION_FORWARD = "forward"

    # BUILTINS is used to determine which commands can not be executed
    # via su or run-as. This set of possible builtin commands was
    # obtained from `man builtin` on Linux.
    BUILTINS = set([
        "alias",
        "bg",
        "bind",
        "break",
        "builtin",
        "caller",
        "cd",
        "command",
        "compgen",
        "complete",
        "compopt",
        "continue",
        "declare",
        "dirs",
        "disown",
        "echo",
        "enable",
        "eval",
        "exec",
        "exit",
        "export",
        "false",
        "fc",
        "fg",
        "getopts",
        "hash",
        "help",
        "history",
        "jobs",
        "kill",
        "let",
        "local",
        "logout",
        "mapfile",
        "popd",
        "printf",
        "pushd",
        "pwd",
        "read",
        "readonly",
        "return",
        "set",
        "shift",
        "shopt",
        "source",
        "suspend",
        "test",
        "times",
        "trap",
        "true",
        "type",
        "typeset",
        "ulimit",
        "umask",
        "unalias",
        "unset",
        "wait",
    ])

    def __init__(
        self,
        device=None,
        adb="adb",
        adb_host=None,
        adb_port=None,
        test_root=None,
        logger_name="adb",
        timeout=300,
        verbose=False,
        device_ready_retry_wait=20,
        device_ready_retry_attempts=3,
        use_root=True,
        share_test_root=True,
        run_as_package=None,
    ):
        global _TEST_ROOT

        ADBCommand.__init__(
            self,
            adb=adb,
            adb_host=adb_host,
            adb_port=adb_port,
            logger_name=logger_name,
            timeout=timeout,
            verbose=verbose,
            use_root=use_root,
        )
        self._logger.info("Using adb %s" % self._adb_version)
        self._device_serial = self._get_device_serial(device)
        self._initial_test_root = test_root
        self._share_test_root = share_test_root
        if share_test_root and not _TEST_ROOT:
            _TEST_ROOT = test_root
        self._test_root = None
        self._run_as_package = None
        # Cache packages debuggable state.
        self._debuggable_packages = {}
        self._device_ready_retry_wait = device_ready_retry_wait
        self._device_ready_retry_attempts = device_ready_retry_attempts
        self._have_root_shell = False
        self._have_su = False
        self._have_android_su = False
        self._selinux = None
        self._re_internal_storage = None

        self._wait_for_boot_completed(timeout=timeout)

        # Record the start time of the ADBDevice initialization so we can
        # determine if we should abort with an ADBTimeoutError if it is
        # taking too long.
        start_time = time.time()

        # Attempt to get the Android version as early as possible in order
        # to work around differences in determining adb command exit codes
        # in Android before and after Android 7.
        self.version = 0
        while self.version < 1 and (time.time() - start_time) <= float(timeout):
            try:
                version = self.get_prop("ro.build.version.sdk", timeout=timeout)
                self.version = int(version)
            except ValueError:
                self._logger.info("unexpected ro.build.version.sdk: '%s'" % version)
                time.sleep(2)
        if self.version < 1:
            # note slightly different meaning to the ADBTimeoutError here (and above):
            # failed to get valid (numeric) version string in all attempts in allowed time
            raise ADBTimeoutError(
                "ADBDevice: unable to determine ro.build.version.sdk."
            )

        self._mkdir_p = None
        # Force the use of /system/bin/ls or /system/xbin/ls in case
        # there is /sbin/ls which embeds ansi escape codes to colorize
        # the output.  Detect if we are using busybox ls. We want each
        # entry on a single line and we don't want . or ..
        ls_dir = "/system"

        # Using self.is_file is problematic on emulators either
        # using ls or test to check for their existence.
        # Executing the command to detect its existence works around
        # any issues with ls or test.
        boot_completed = False
        while not boot_completed and (time.time() - start_time) <= float(timeout):
            try:
                self.shell_output(
                    "/system/bin/ls /system/bin/ls", timeout=timeout, attempts=3
                )
                boot_completed = True
                self._ls = "/system/bin/ls"
            except ADBError as e1:
                self._logger.debug(f"detect /system/bin/ls {e1}")
                try:
                    self.shell_output(
                        "/system/xbin/ls /system/xbin/ls", timeout=timeout, attempts=3
                    )
                    boot_completed = True
                    self._ls = "/system/xbin/ls"
                except ADBError as e2:
                    self._logger.debug(f"detect /system/xbin/ls : {e2}")
            if not boot_completed:
                time.sleep(2)
        if not boot_completed:
            raise ADBError("ADBDevice.__init__: ls could not be found")

        # A race condition can occur especially with emulators where
        # the device appears to be available but it has not completed
        # mounting the sdcard. We can work around this by checking if
        # the sdcard is missing when we attempt to ls it and retrying
        # if it is not yet available.
        boot_completed = False
        while not boot_completed and (time.time() - start_time) <= float(timeout):
            try:
                self.shell_output(
                    f"{self._ls} -1A {ls_dir}", timeout=timeout, attempts=3
                )
                boot_completed = True
                self._ls += " -1A"
            except ADBError as e:
                self._logger.debug(f"detect ls -1A: {e}")
                if "No such file or directory" not in str(e):
                    boot_completed = True
                    self._ls += " -a"
            if not boot_completed:
                time.sleep(2)
        if not boot_completed:
            raise ADBTimeoutError("ADBDevice: /sdcard not found.")

        self._logger.info("%s supported" % self._ls)

        # builtin commands which do not exist as separate programs can
        # not be executed using su or run-as.  Remove builtin commands
        # from self.BUILTINS which also exist as separate programs so
        # that we will be able to execute them using su or run-as if
        # necessary.
        remove_builtins = set()
        for builtin in self.BUILTINS:
            try:
                # It is important not to retry ls (with attempts=) here
                # since many of these calls are expected to fail:
                # additional retries can introduce a significant delay.
                self.ls("/system/*bin/%s" % builtin, timeout=timeout)
                self._logger.debug("Removing %s from BUILTINS" % builtin)
                remove_builtins.add(builtin)
            except ADBError:
                pass
        self.BUILTINS.difference_update(remove_builtins)

        # Do we have cp?
        boot_completed = False
        while not boot_completed and (time.time() - start_time) <= float(timeout):
            try:
                self.shell_output("cp --help", timeout=timeout)
                boot_completed = True
                self._have_cp = True
            except ADBError as e:
                if "not found" in str(e):
                    self._have_cp = False
                    boot_completed = True
                elif "known option" in str(e):
                    self._have_cp = True
                    boot_completed = True
                elif "invalid option" in str(e):
                    self._have_cp = True
                    boot_completed = True
            if not boot_completed:
                time.sleep(2)
        if not boot_completed:
            raise ADBTimeoutError("ADBDevice: cp not found.")
        self._logger.info("Native cp support: %s" % self._have_cp)

        # Do we have chmod -R?
        try:
            self._chmod_R = False
            re_recurse = re.compile(r"[-]R")
            chmod_output = self.shell_output("chmod --help", timeout=timeout)
            match = re_recurse.search(chmod_output)
            if match:
                self._chmod_R = True
        except ADBError as e:
            self._logger.debug(f"Check chmod -R: {e}")
            match = re_recurse.search(str(e))
            if match:
                self._chmod_R = True
        self._logger.info(f"Native chmod -R support: {self._chmod_R}")

        # Do we have chown -R?
        try:
            self._chown_R = False
            chown_output = self.shell_output("chown --help", timeout=timeout)
            match = re_recurse.search(chown_output)
            if match:
                self._chown_R = True
        except ADBError as e:
            self._logger.debug(f"Check chown -R: {e}")
        self._logger.info(f"Native chown -R support: {self._chown_R}")

        try:
            cleared = self.shell_bool('logcat -P ""', timeout=timeout)
        except ADBError:
            cleared = False
        if not cleared:
            self._logger.info("Unable to turn off logcat chatty")

        # Do we have pidof?
        if self.version < version_codes.N:
            # unexpected pidof behavior observed on Android 6 in bug 1514363
            self._have_pidof = False
        else:
            boot_completed = False
            while not boot_completed and (time.time() - start_time) <= float(timeout):
                try:
                    self.shell_output("pidof --help", timeout=timeout)
                    boot_completed = True
                    self._have_pidof = True
                except ADBError as e:
                    if "not found" in str(e):
                        self._have_pidof = False
                        boot_completed = True
                    elif "known option" in str(e):
                        self._have_pidof = True
                        boot_completed = True
                if not boot_completed:
                    time.sleep(2)
            if not boot_completed:
                raise ADBTimeoutError("ADBDevice: pidof not found.")
        # Bug 1529960 observed pidof intermittently returning no results for a
        # running process on the 7.0 x86_64 emulator.

        characteristics = self.get_prop("ro.build.characteristics", timeout=timeout)

        abi = self.get_prop("ro.product.cpu.abi", timeout=timeout)
        self._have_flaky_pidof = (
            self.version == version_codes.N
            and abi == "x86_64"
            and "emulator" in characteristics
        )
        self._logger.info(
            "Native {} pidof support: {}".format(
                "flaky" if self._have_flaky_pidof else "normal", self._have_pidof
            )
        )

        if self._use_root:
            # Detect if root is available, but do not fail if it is not.
            # Catch exceptions due to the potential for segfaults
            # calling su when using an improperly rooted device.

            self._check_adb_root(timeout=timeout)

            if not self._have_root_shell:
                # To work around bug 1525401 where su -c id will return an
                # exitcode of 1 if selinux permissive is not already in effect,
                # we need su to turn off selinux prior to checking for su.
                # We can use shell() directly to prevent the non-zero exitcode
                # from raising an ADBError.
                # Note: We are assuming su -c is supported and do not attempt to
                # use su 0.
                adb_process = self.shell("su -c setenforce 0")
                self._logger.info(
                    "su -c setenforce 0 exitcode %s, stdout: %s"
                    % (adb_process.proc.poll(), adb_process.proc.stdout)
                )

                uid = "uid=0"
                # Do we have a 'Superuser' sh like su?
                try:
                    if self.shell_output("su -c id", timeout=timeout).find(uid) != -1:
                        self._have_su = True
                        self._logger.info("su -c supported")
                except ADBError as e:
                    self._logger.debug(f"Check for su -c failed: {e}")

                # Check if Android's su 0 command works.
                # If we already have detected su -c support, we can skip this check.
                try:
                    if (
                        not self._have_su
                        and self.shell_output("su 0 id", timeout=timeout).find(uid)
                        != -1
                    ):
                        self._have_android_su = True
                        self._logger.info("su 0 supported")
                except ADBError as e:
                    self._logger.debug(f"Check for su 0 failed: {e}")

        # Guarantee that /data/local/tmp exists and is accessible to all.
        # It is a fatal error if /data/local/tmp does not exist and can not be created.
        if not self.exists("/data/local/tmp", timeout=timeout):
            # parents=True is required on emulator, where exist() may be flaky
            self.mkdir("/data/local/tmp", parents=True, timeout=timeout)

        # Beginning in Android 8.1 /data/anr/traces.txt no longer contains
        # a single file traces.txt but instead will contain individual files
        # for each stack.
        # See https://github.com/aosp-mirror/platform_build/commit/
        # fbba7fe06312241c7eb8c592ec2ac630e4316d55
        stack_trace_dir = self.shell_output(
            "getprop dalvik.vm.stack-trace-dir", timeout=timeout, attempts=3
        )
        if not stack_trace_dir:
            stack_trace_file = self.shell_output(
                "getprop dalvik.vm.stack-trace-file", timeout=timeout, attempts=3
            )
            if stack_trace_file:
                stack_trace_dir = posixpath.dirname(stack_trace_file)
            else:
                stack_trace_dir = "/data/anr"
        self.stack_trace_dir = stack_trace_dir
        self.enforcing = "Permissive"
        self.run_as_package = run_as_package

        self._logger.debug("ADBDevice: %s" % self.__dict__)
        self.shell("settings put system accelerometer_rotation 0")
        self.shell("settings put system user_rotation 0")

    @property
    def is_rooted(self):
        return self._have_root_shell or self._have_su or self._have_android_su

    def _wait_for_boot_completed(self, timeout=None):
        """Internal method to wait for boot to complete.

        Wait for sys.boot_completed=1 and raise ADBError if boot does
        not complete within retry attempts.

        :param int timeout: The default maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value defaults to 300.
        :raises: :exc:`ADBError`
        """
        for attempt in range(self._device_ready_retry_attempts):
            sys_boot_completed = self.shell_output(
                "getprop sys.boot_completed", timeout=timeout, attempts=3
            )
            if sys_boot_completed == "1":
                break
            time.sleep(self._device_ready_retry_wait)
        if sys_boot_completed != "1":
            raise ADBError("Failed to complete boot in time")

    def _get_device_serial(self, device):
        device = device or os.environ.get("ANDROID_SERIAL")
        if device is None:
            devices = ADBHost(
                adb=self._adb_path, adb_host=self._adb_host, adb_port=self._adb_port
            ).devices()
            ready_devices = [d for d in devices if d["state"] == "device"]
            if len(ready_devices) > 1:
                raise ValueError(
                    "ADBDevice called with multiple devices "
                    "available and no device specified"
                )
            if len(ready_devices) == 0:
                raise ADBError("No ready devices found.")
            device = ready_devices[0]

        # Allow : in device serial if it matches a tcpip device serial.
        re_device_serial_tcpip = re.compile(r"[^:]+:[0-9]+$")

        def is_valid_serial(serial):
            return (
                serial.startswith("usb:")
                or re_device_serial_tcpip.match(serial) is not None
                or ":" not in serial
            )

        if isinstance(device, str):
            # Treat this as a device serial
            if not is_valid_serial(device):
                raise ValueError(
                    "Device serials containing ':' characters are "
                    "invalid. Pass the output from "
                    "ADBHost.devices() for the device instead"
                )
            return device

        serial = device.get("device_serial")
        if serial is not None and is_valid_serial(serial):
            return serial
        usb = device.get("usb")
        if usb is not None:
            return "usb:%s" % usb

        raise ValueError("Unable to get device serial")

    def _check_root_user(self, timeout=None):
        uid = "uid=0"
        # Is shell already running as root?
        try:
            if self.shell_output("id", timeout=timeout).find(uid) != -1:
                self._logger.info("adbd running as root")
                return True
        except ADBError:
            self._logger.debug("Check for root user failed")
        return False

    def _check_adb_root(self, timeout=None):
        self._have_root_shell = self._check_root_user(timeout=timeout)

        # Exclude these devices from checking for a root shell due to
        # potential hangs.
        exclude_set = set()
        exclude_set.add("E5823")  # Sony Xperia Z5 Compact (E5823)
        # Do we need to run adb root to get a root shell?
        if not self._have_root_shell:
            if self.get_prop("ro.product.model") in exclude_set:
                self._logger.warning(
                    "your device was excluded from attempting adb root."
                )
            else:
                try:
                    self.command_output(["root"], timeout=timeout)
                    self._have_root_shell = self._check_root_user(timeout=timeout)
                    if self._have_root_shell:
                        self._logger.info("adbd restarted as root")
                    else:
                        self._logger.info("adbd not restarted as root")
                except ADBError:
                    self._logger.debug("Check for root adbd failed")

    def pidof(self, app_name, timeout=None):
        """
        Return a list of pids for all extant processes running within the
        specified application package.

        :param str app_name: The name of the application package to examine
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per
            adb call. The total time spent may exceed this
            value. If it is not specified, the value set
            in the ADBDevice constructor is used.
        :return: List of integers containing the pid(s) of the various processes.
        :raises: :exc:`ADBTimeoutError`
        """
        if self._have_pidof:
            try:
                pid_output = self.shell_output("pidof %s" % app_name, timeout=timeout)
                re_pids = re.compile(r"[0-9]+")
                pids = re_pids.findall(pid_output)
                if self._have_flaky_pidof and not pids:
                    time.sleep(0.1)
                    pid_output = self.shell_output(
                        "pidof %s" % app_name, timeout=timeout
                    )
                    pids = re_pids.findall(pid_output)
            except ADBError:
                pids = []
        else:
            procs = self.get_process_list(timeout=timeout)
            # limit the comparion to the first 75 characters due to a
            # limitation in processname length in android.
            pids = [proc[0] for proc in procs if proc[1] == app_name[:75]]

        return [int(pid) for pid in pids]

    def _sync(self, timeout=None):
        """Sync the file system using shell_output in order that exceptions
        are raised to the caller."""
        self.shell_output("sync", timeout=timeout, attempts=3)

    @staticmethod
    def _should_quote(arg):
        """Utility function if command argument should be quoted."""
        if not arg:
            return False
        if arg[0] == "'" and arg[-1] == "'" or arg[0] == '"' and arg[-1] == '"':
            # Already quoted
            return False
        re_quotable_chars = re.compile(r"[ ()\"&'\];]")
        return re_quotable_chars.search(arg)

    @staticmethod
    def _quote(arg):
        """Utility function to return quoted version of command argument."""
        if hasattr(shlex, "quote"):
            quote = shlex.quote
        else:

            def quote(arg):
                arg = arg or ""
                re_unsafe = re.compile(r"[^\w@%+=:,./-]")
                if re_unsafe.search(arg):
                    arg = "'" + arg.replace("'", "'\"'\"'") + "'"
                return arg

        return quote(arg)

    @staticmethod
    def _escape_command_line(cmds):
        """Utility function which takes a list of command arguments and returns
        escaped and quoted version of the command as a string.
        """
        assert isinstance(cmds, list)
        # This is identical to shlex.join in Python 3.8. We can
        # replace it should we ever get Python 3.8 as a minimum.
        quoted_cmd = " ".join([ADBDevice._quote(arg) for arg in cmds])

        return quoted_cmd

    @staticmethod
    def _get_exitcode(file_obj):
        """Get the exitcode from the last line of the file_obj for shell
        commands executed on Android prior to Android 7.
        """
        re_returncode = re.compile(r"adb_returncode=([0-9]+)")
        file_obj.seek(0, os.SEEK_END)

        line = ""
        length = file_obj.tell()
        offset = 1
        while length - offset >= 0:
            file_obj.seek(-offset, os.SEEK_END)
            char = file_obj.read(1).decode()
            if not char:
                break
            if char not in {"\r", "\n"}:
                line = char + line
            elif line:
                # we have collected everything up to the beginning of the line
                break
            offset += 1
        match = re_returncode.match(line)
        if match:
            exitcode = int(match.group(1))
            # Set the position in the file to the position of the
            # adb_returncode and truncate it from the output.
            file_obj.seek(-1, os.SEEK_CUR)
            file_obj.truncate()
        else:
            exitcode = None
            # We may have a situation where the adb_returncode= is not
            # at the end of the output. This happens at least in the
            # failure jit-tests on arm. To work around this
            # possibility, we can search the entire output for the
            # appropriate match.
            file_obj.seek(0, os.SEEK_SET)
            for line in file_obj:
                line_str = line.decode()
                match = re_returncode.search(line_str)
                if match:
                    exitcode = int(match.group(1))
                    break
            # Reset the position in the file to the end.
            file_obj.seek(0, os.SEEK_END)

        return exitcode

    def is_path_internal_storage(self, path, timeout=None):
        """
        Return True if the path matches an internal storage path
        as defined by either '/sdcard', '/mnt/sdcard', or any of the
        .*_STORAGE environment variables on the device otherwise False.

        :param str path: The path to test.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBDevice constructor is used.
        :return: boolean
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if not self._re_internal_storage:
            storage_dirs = set(["/mnt/sdcard", "/sdcard"])
            re_STORAGE = re.compile("([^=]+STORAGE)=(.*)")
            lines = self.shell_output("set", timeout=timeout).split()
            for line in lines:
                m = re_STORAGE.match(line.strip())
                if m and m.group(2):
                    storage_dirs.add(m.group(2))
            self._re_internal_storage = re.compile("/|".join(list(storage_dirs)) + "/")
        return self._re_internal_storage.match(path) is not None

    def is_package_debuggable(self, package):
        if not package:
            return False

        if not self.is_app_installed(package):
            self._logger.warning(
                "Can not check if package %s is debuggable as it is not installed."
                % package
            )
            return False

        if package in self._debuggable_packages:
            return self._debuggable_packages[package]

        try:
            self.shell_output("run-as %s ls /system" % package)
            self._debuggable_packages[package] = True
        except ADBError as e:
            self._debuggable_packages[package] = False
            self._logger.warning(f"Package {package} is not debuggable: {str(e)}")
        return self._debuggable_packages[package]

    @property
    def package_dir(self):
        if not self._run_as_package:
            return None
        # If we have a debuggable app and can use its directory to
        # locate the test_root, this returns the location of the app's
        # directory. If it is not located in the default location this
        # will not be correct.
        return "/data/data/%s" % self._run_as_package

    @property
    def run_as_package(self):
        """Returns the name of the package which will be used in run-as to change
        the effective user executing a command."""
        return self._run_as_package

    @run_as_package.setter
    def run_as_package(self, value):
        if self._have_root_shell or self._have_su or self._have_android_su:
            # When we have root available, use that instead of run-as.
            return

        if self._run_as_package == value:
            # Do nothing if the value doesn't change.
            return

        if not value:
            if self._test_root:
                # Make sure the old test_root is clean without using
                # the test_root property getter.
                self.rm(
                    posixpath.join(self._test_root, "*"), recursive=True, force=True
                )
            self._logger.info(
                "Setting run_as_package to None. Resetting test root from %s to %s"
                % (self._test_root, self._initial_test_root)
            )
            self._run_as_package = None
            # We must set _run_as_package to None before assigning to
            # self.test_root in order to prevent attempts to use
            # run-as.
            self.test_root = self._initial_test_root
            if self._test_root:
                # Make sure the new test_root is clean.
                self.rm(
                    posixpath.join(self._test_root, "*"), recursive=True, force=True
                )
            return

        if not self.is_package_debuggable(value):
            self._logger.warning(
                "Can not set run_as_package to %s since it is not debuggable." % value
            )
            # Since we are attempting to set run_as_package assume
            # that we are not rooted and do not include
            # /data/local/tmp as an option when checking for possible
            # test_root paths using external storage.
            paths = [
                "/storage/emulated/0/Android/data/%s/test_root" % value,
                "/sdcard/test_root",
                "/mnt/sdcard/test_root",
            ]
            self._try_test_root_candidates(paths)
            return

        # Require these devices to have Verify bytecode turned off due to failures with run-as.
        include_set = set()
        include_set.add("SM-G973F")  # Samsung S10g SM-G973F

        if (
            self.get_prop("ro.product.model") in include_set
            and self.shell_output("settings get global art_verifier_verify_debuggable")
            == "1"
        ):
            self._logger.warning(
                """Your device has Verify bytecode of debuggable apps set which
            causes problems attempting to use run-as to delegate command execution to debuggable
            apps. You must turn this setting off in Developer options on your device.
            """
            )
            raise ADBError(
                "Verify bytecode of debuggable apps must be turned off to use run-as"
            )

        self._logger.info("Setting run_as_package to %s" % value)

        self._run_as_package = value
        old_test_root = self._test_root
        new_test_root = posixpath.join(self.package_dir, "test_root")
        if old_test_root != new_test_root:
            try:
                # Make sure the old test_root is clean.
                if old_test_root:
                    self.rm(
                        posixpath.join(old_test_root, "*"), recursive=True, force=True
                    )
                self.test_root = posixpath.join(self.package_dir, "test_root")
                # Make sure the new test_root is clean.
                self.rm(posixpath.join(self.test_root, "*"), recursive=True, force=True)
            except ADBError as e:
                # There was a problem using run-as to initialize
                # the new test_root in the app's directory.
                # Restore the old test root and raise an ADBError.
                self._run_as_package = None
                self.test_root = old_test_root
                self._logger.warning(
                    "Exception %s setting test_root to %s. "
                    "Resetting test_root to %s."
                    % (str(e), new_test_root, old_test_root)
                )
                raise ADBError(
                    "Unable to initialize test root while setting run_as_package %s"
                    % value
                )

    def enable_run_as_for_path(self, path):
        return self._run_as_package is not None and path.startswith(self.package_dir)

    @property
    def _host_gcov_prefix(self):
        return os.getenv("GCOV_PREFIX")

    @property
    def _target_gcov_prefix_strip(self):
        return os.getenv("GCOV_PREFIX_STRIP")

    @property
    def _host_jscov_prefix(self):
        return os.getenv("JS_CODE_COVERAGE_OUTPUT_DIR")

    @property
    def _target_gcov_prefix(self):
        if self._host_gcov_prefix is None or self._test_root is None:
            return None
        return posixpath.join(self._test_root, os.path.basename(self._host_gcov_prefix))

    @property
    def _target_jscov_prefix(self):
        if self._host_jscov_prefix is None or self._test_root is None:
            return None
        return posixpath.join(
            self._test_root, os.path.basename(self._host_jscov_prefix)
        )

    @property
    def target_ccov_env(self):
        env = {}
        if self._target_gcov_prefix is not None:
            env["GCOV_PREFIX"] = self._target_gcov_prefix
        if self._target_gcov_prefix_strip is not None:
            env["GCOV_PREFIX_STRIP"] = self._target_gcov_prefix_strip
        if self._target_jscov_prefix is not None:
            env["JS_CODE_COVERAGE_OUTPUT_DIR"] = self._target_jscov_prefix
        return env

    @property
    def test_root(self):
        """
        The test_root property returns the directory on the device where
        temporary test files are stored.

        The first time test_root it is called it determines and caches a value
        for the test root on the device. It determines the appropriate test
        root by attempting to create a 'proof' directory on each of a list of
        directories and returning the first successful directory as the
        test_root value. The cached value for the test_root will be shared
        by subsequent instances of ADBDevice if self._share_test_root is True.

        The default list of directories checked by test_root are:

        If the device is rooted:
            - /data/local/tmp/test_root

        If run_as_package is not available and the device is not rooted:

            - /data/local/tmp/test_root
            - /sdcard/test_root
            - /storage/sdcard/test_root
            - /mnt/sdcard/test_root

        You may override the default list by providing a test_root argument to
        the :class:`ADBDevice` constructor which will then be used when
        attempting to create the 'proof' directory.

        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if self._test_root is not None:
            self._logger.debug("Using cached test_root %s" % self._test_root)
            return self._test_root

        if self.run_as_package is not None:
            raise ADBError(
                "run_as_package is %s however test_root is None" % self.run_as_package
            )

        if self._share_test_root and _TEST_ROOT:
            self._logger.debug(
                "Attempting to use shared test_root %s" % self._test_root
            )
            paths = [_TEST_ROOT]
        elif self._initial_test_root is not None:
            self._logger.debug(
                "Attempting to use initial test_root %s" % self._test_root
            )
            paths = [self._initial_test_root]
        else:
            # Android 10's scoped storage means we can no longer
            # reliably host profiles and tests on the sdcard though it
            # depends on the device. See
            # https://developer.android.com/training/data-storage#scoped-storage
            # Also see RunProgram in
            # python/mozbuild/mozbuild/mach_commands.py where they
            # choose /data/local/tmp as the default location for the
            # profile because GeckoView only takes its configuration
            # file from /data/local/tmp.  Since we have not specified
            # a run_as_package yet, assume we may be attempting to use
            # a shell program which creates files owned by the shell
            # user and which would work using /data/local/tmp/ even if
            # the device is not rooted. Fall back to external storage
            # if /data/local/tmp is not available.
            paths = ["/data/local/tmp/test_root"]
            if not self.is_rooted:
                # Note that /sdcard may be accessible while
                # /mnt/sdcard is not.
                paths.extend([
                    "/sdcard/test_root",
                    "/storage/sdcard/test_root",
                    "/mnt/sdcard/test_root",
                ])

        return self._try_test_root_candidates(paths)

    @test_root.setter
    def test_root(self, value):
        # Cache the requested test root so that
        # other invocations of ADBDevice will pick
        # up the same value.
        global _TEST_ROOT
        if self._test_root == value:
            return
        self._logger.debug(f"Setting test_root from {self._test_root} to {value}")
        old_test_root = self._test_root
        self._test_root = value
        if self._share_test_root:
            _TEST_ROOT = value
        if not value:
            return
        if not self._try_test_root(value):
            self._test_root = old_test_root
            raise ADBError("Unable to set test_root to %s" % value)
        readme = posixpath.join(value, "README")
        if not self.is_file(readme):
            tmpf = tempfile.NamedTemporaryFile(mode="w", delete=False)
            tmpf.write(
                "This directory is used by mozdevice to contain all content "
                "related to running tests on this device.\n"
            )
            tmpf.close()
            try:
                self.push(tmpf.name, readme)
            finally:
                if tmpf:
                    os.unlink(tmpf.name)

    def _try_test_root_candidates(self, paths):
        max_attempts = 3
        for test_root in paths:
            for attempt in range(1, max_attempts + 1):
                self._logger.debug(
                    "Setting test root to %s attempt %d of %d"
                    % (test_root, attempt, max_attempts)
                )

                if self._try_test_root(test_root):
                    if not self._test_root:
                        # Cache the detected test_root so that we can
                        # restore the value without having re-run
                        # _try_test_root.
                        self._initial_test_root = test_root
                    self._test_root = test_root
                    self._logger.info("Setting test_root to %s" % self._test_root)
                    return self._test_root

                self._logger.debug(
                    "_setup_test_root: "
                    "Attempt %d of %d failed to set test_root to %s"
                    % (attempt, max_attempts, test_root)
                )

                if attempt != max_attempts:
                    time.sleep(20)

        raise ADBError(
            "Unable to set up test root using paths: [%s]" % ", ".join(paths)
        )

    def _try_test_root(self, test_root):
        try:
            if not self.is_dir(test_root):
                self.mkdir(test_root, parents=True)
            proof_dir = posixpath.join(test_root, "proof")
            if self.is_dir(proof_dir):
                self.rm(proof_dir, recursive=True)
            self.mkdir(proof_dir)
            self.rm(proof_dir, recursive=True)
        except ADBError as e:
            self._logger.warning(f"{test_root} is not writable: {str(e)}")
            return False

        return True

    # Host Command methods

    def command(self, cmds, timeout=None):
        """Executes an adb command on the host against the device.

        :param list cmds: The command and its arguments to be
            executed.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBDevice constructor is used.
        :return: :class:`ADBProcess`

        command() provides a low level interface for executing
        commands for a specific device on the host via adb.

        command() executes on the host in such a fashion that stdout
        of the adb process are file handles on the host and
        the exit code is available as the exit code of the adb
        process.

        For executing shell commands on the device, use
        ADBDevice.shell().  The caller provides a list containing
        commands, as well as a timeout period in seconds.

        A subprocess is spawned to execute adb for the device with
        stdout and stderr directed to a temporary file. If the process
        takes longer than the specified timeout, the process is
        terminated.

        It is the caller's responsibilty to clean up by closing
        the stdout temporary file.
        """

        return ADBCommand.command(
            self, cmds, device_serial=self._device_serial, timeout=timeout
        )

    def command_output(self, cmds, timeout=None):
        """Executes an adb command on the host against the device returning
        stdout.

        :param list cmds: The command and its arguments to be executed.
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: str - content of stdout.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        return ADBCommand.command_output(
            self, cmds, device_serial=self._device_serial, timeout=timeout
        )

    # Networking methods

    def _validate_port(self, port, is_local=True):
        """Validate a port forwarding specifier. Raises ValueError on failure.

        :param str port: The port specifier to validate
        :param bool is_local: Flag indicating whether the port represents a local port.
        """
        prefixes = ["tcp", "localabstract", "localreserved", "localfilesystem", "dev"]

        if not is_local:
            prefixes += ["jdwp"]

        parts = port.split(":", 1)
        if len(parts) != 2 or parts[0] not in prefixes:
            raise ValueError("Invalid port specifier %s" % port)

    def _validate_direction(self, direction):
        """Validate direction of the socket connection. Raises ValueError on failure.

        :param str direction: The socket direction specifier to validate
        :raises: :exc:`ValueError`
        """
        if direction not in [
            self.SOCKET_DIRECTION_FORWARD,
            self.SOCKET_DIRECTION_REVERSE,
        ]:
            raise ValueError(f"Invalid direction specifier {direction}")

    def create_socket_connection(
        self, direction, local, remote, allow_rebind=True, timeout=None
    ):
        """Sets up a socket connection in the specified direction.

        :param str direction: Direction of the socket connection
        :param str local: Local port
        :param str remote: Remote port
        :param bool allow_rebind: Do not fail if port is already bound
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: When forwarding from "tcp:0", an int containing the port number
                 of the local port assigned by adb, otherwise None.
        :raises: :exc:`ValueError`
                 :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        # validate socket direction, and local and remote port formatting.
        self._validate_direction(direction)
        for port, is_local in [(local, True), (remote, False)]:
            self._validate_port(port, is_local=is_local)

        cmd = [direction, local, remote]

        if not allow_rebind:
            cmd.insert(1, "--no-rebind")

        # execute commands to establish socket connection.
        cmd_output = self.command_output(cmd, timeout=timeout)

        # If we want to forward using local port "tcp:0", then we're letting
        # adb assign the port for us, so we need to return that assignment.
        if (
            direction == self.SOCKET_DIRECTION_FORWARD
            and local == "tcp:0"
            and cmd_output
        ):
            return int(cmd_output)

        return None

    def list_socket_connections(self, direction, timeout=None):
        """Return a list of tuples specifying active socket connectionss.

        Return values are of the form (device, local, remote).

        :param str direction: 'forward' to list forward socket connections
                          'reverse' to list reverse socket connections
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ValueError`
                 :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self._validate_direction(direction)

        cmd = [direction, "--list"]
        output = self.command_output(cmd, timeout=timeout)
        return [tuple(line.split(" ")) for line in output.splitlines() if line.strip()]

    def remove_socket_connections(self, direction, local=None, timeout=None):
        """Remove existing socket connections for a given direction.

        :param str direction: 'forward' to remove forward socket connection
                          'reverse' to remove reverse socket connection
        :param str local: local port specifier as for ADBDevice.forward. If local
            is not specified removes all forwards.
        :param int timeout: The maximum time in seconds
            for any spawned adb process to complete before throwing
            an ADBTimeoutError. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ValueError`
                 :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self._validate_direction(direction)

        cmd = [direction]

        if local is None:
            cmd.extend(["--remove-all"])
        else:
            self._validate_port(local, is_local=True)
            cmd.extend(["--remove", local])

        self.command_output(cmd, timeout=timeout)

    # Legacy port forward methods

    def forward(self, local, remote, allow_rebind=True, timeout=None):
        """Forward a local port to a specific port on the device.

        :return: When forwarding from "tcp:0", an int containing the port number
                 of the local port assigned by adb, otherwise None.

        See `ADBDevice.create_socket_connection`.
        """
        return self.create_socket_connection(
            self.SOCKET_DIRECTION_FORWARD, local, remote, allow_rebind, timeout
        )

    def list_forwards(self, timeout=None):
        """Return a list of tuples specifying active forwards.

        See `ADBDevice.list_socket_connection`.
        """
        return self.list_socket_connections(self.SOCKET_DIRECTION_FORWARD, timeout)

    def remove_forwards(self, local=None, timeout=None):
        """Remove existing port forwards.

        See `ADBDevice.remove_socket_connection`.
        """
        self.remove_socket_connections(self.SOCKET_DIRECTION_FORWARD, local, timeout)

    # Legacy port reverse methods

    def reverse(self, local, remote, allow_rebind=True, timeout=None):
        """Sets up a reverse socket connection from device to host.

        See `ADBDevice.create_socket_connection`.
        """
        self.create_socket_connection(
            self.SOCKET_DIRECTION_REVERSE, local, remote, allow_rebind, timeout
        )

    def list_reverses(self, timeout=None):
        """Returns a list of tuples showing active reverse socket connections.

        See `ADBDevice.list_socket_connection`.
        """
        return self.list_socket_connections(self.SOCKET_DIRECTION_REVERSE, timeout)

    def remove_reverses(self, local=None, timeout=None):
        """Remove existing reverse socket connections.

        See `ADBDevice.remove_socket_connection`.
        """
        self.remove_socket_connections(self.SOCKET_DIRECTION_REVERSE, local, timeout)

    # Device Shell methods

    def shell(
        self,
        cmd,
        env=None,
        cwd=None,
        timeout=None,
        stdout_callback=None,
        yield_stdout=None,
        enable_run_as=False,
    ):
        """Executes a shell command on the device.

        :param str cmd: The command to be executed.
        :param dict env: Contains the environment variables and
            their values.
        :param str cwd: The directory from which to execute.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per adb call. The
            total time spent may exceed this value. If it is not
            specified, the value set in the ADBDevice constructor is used.
        :param function stdout_callback: Function called for each line of output.
        :param bool yield_stdout: Flag used to make the returned process
            iteratable. The return process can be used in a loop to get the output
            and the loop would exit as the process ends.
        :param bool enable_run_as: Flag used to temporarily enable use
            of run-as to execute the command.
        :return: :class:`ADBProcess`

        shell() provides a low level interface for executing commands
        on the device via adb shell.

        shell() executes on the host in such as fashion that stdout
        contains the stdout and stderr of the host abd process
        combined with the stdout and stderr of the shell command
        on the device. The exit code of shell() is the exit code of
        the adb command if it was non-zero or the extracted exit code
        from the output of the shell command executed on the
        device.

        The caller provides a flag indicating if the command is to be
        executed as root, a string for any requested working
        directory, a hash defining the environment, a string
        containing shell commands, as well as a timeout period in
        seconds.

        The command line to be executed is created to set the current
        directory, set the required environment variables, optionally
        execute the command using su and to output the return code of
        the command to stdout. The command list is created as a
        command sequence separated by && which will terminate the
        command sequence on the first command which returns a non-zero
        exit code.

        A subprocess is spawned to execute adb shell for the device
        with stdout and stderr directed to a temporary file. If the
        process takes longer than the specified timeout, the process
        is terminated. The return code is extracted from the stdout
        and is then removed from the file.

        It is the caller's responsibilty to clean up by closing
        the stdout temporary files.

        If the yield_stdout flag is set, then the returned ADBProcess
        can be iterated over to get the output as it is produced by
        adb command. The iterator ends when the process timed out or
        if it exited. This flag is incompatible with stdout_callback.

        """

        def _timed_read_line_handler(signum, frame):
            raise OSError("ReadLineTimeout")

        def _timed_read_line(filehandle, timeout=None):
            """
            Attempt to readline from filehandle. If readline does not return
            within timeout seconds, raise IOError('ReadLineTimeout').
            On Windows, required signal facilities are usually not available;
            as a result, the timeout is not respected and some reads may
            block on Windows.
            """
            if not hasattr(signal, "SIGALRM"):
                return filehandle.readline()
            if timeout is None:
                timeout = 5
            line = ""
            default_alarm_handler = signal.getsignal(signal.SIGALRM)
            signal.signal(signal.SIGALRM, _timed_read_line_handler)
            signal.alarm(int(timeout))
            try:
                line = filehandle.readline()
            finally:
                signal.alarm(0)
                signal.signal(signal.SIGALRM, default_alarm_handler)
            return line

        first_word = cmd.split(" ")[0]
        if first_word in self.BUILTINS:
            # Do not attempt to use su or run-as with builtin commands
            pass
        elif self._have_root_shell:
            pass
        elif self._have_android_su:
            cmd = "su 0 %s" % cmd
        elif self._have_su:
            cmd = "su -c %s" % ADBDevice._quote(cmd)
        elif self._run_as_package and enable_run_as:
            cmd = f"run-as {self._run_as_package} {cmd}"
        else:
            pass

        if env is None:
            env = {}
        env.update(self.target_ccov_env)

        # prepend cwd and env to command if necessary
        if cwd:
            cmd = f"cd {cwd} && {cmd}"
        if env:
            envstr = "&& ".join([f"export {x[0]}={x[1]}" for x in env.items()])
            cmd = envstr + "&& " + cmd
        # Before Android 7, an exitcode 0 for the process on the host
        # did not mean that the exitcode of the Android process was
        # also 0. We therefore used the echo adb_returncode=$? hack to
        # obtain it there. However Android 7 and later intermittently
        # do not emit the adb_returncode in stdout using this hack. In
        # Android 7 and later the exitcode of the host process does
        # match the exitcode of the Android process and we can use it
        # directly.
        if (
            self._device_serial.startswith("emulator")
            or not hasattr(self, "version")
            or self.version < version_codes.N
        ):
            cmd += "; echo adb_returncode=$?"

        args = [self._adb_path]
        if self._adb_host:
            args.extend(["-H", self._adb_host])
        if self._adb_port:
            args.extend(["-P", str(self._adb_port)])
        if self._device_serial:
            args.extend(["-s", self._device_serial])
        args.extend(["wait-for-device", "shell", cmd])

        if timeout is None:
            timeout = self._timeout

        if yield_stdout:
            # When using yield_stdout, rely on the timeout implemented in
            # ADBProcess instead of relying on our own here.
            assert not stdout_callback
            return ADBProcess(args, use_stdout_pipe=yield_stdout, timeout=timeout)
        else:
            adb_process = ADBProcess(args)

        start_time = time.time()
        exitcode = adb_process.proc.poll()
        if not stdout_callback:
            while ((time.time() - start_time) <= float(timeout)) and exitcode is None:
                time.sleep(self._polling_interval)
                exitcode = adb_process.proc.poll()
        else:
            # https://docs.python.org/3/library/tempfile.html#tempfile.NamedTemporaryFile
            #
            # NamedTemporaryFile with delete=True, Windows cannot open this file
            # file normally. We have to add the temporary flag.
            def opener(path, flags):
                if sys.platform == "win32":
                    flags |= os.O_TEMPORARY
                return os.open(path, flags, mode=0o666)

            stdout2 = open(adb_process.stdout_file.name, "rb", opener=opener)
            partial = b""
            while ((time.time() - start_time) <= float(timeout)) and exitcode is None:
                try:
                    line = _timed_read_line(stdout2)
                    if line and len(line) > 0:
                        if line.endswith(b"\n") or line.endswith(b"\r"):
                            line = partial + line
                            partial = b""
                            line = line.rstrip()
                            if self._verbose:
                                self._logger.info(line.decode())
                            stdout_callback(line)
                        else:
                            # no more output available now, but more to come?
                            partial = partial + line
                    else:
                        # no new output, so sleep and poll
                        time.sleep(self._polling_interval)
                except OSError:
                    pass
                exitcode = adb_process.proc.poll()
        if exitcode is None:
            adb_process.proc.kill()
            adb_process.timedout = True
            adb_process.exitcode = adb_process.proc.poll()
        elif exitcode == 0:
            if (
                not self._device_serial.startswith("emulator")
                and hasattr(self, "version")
                and self.version >= version_codes.N
            ):
                adb_process.exitcode = 0
            else:
                adb_process.exitcode = self._get_exitcode(adb_process.stdout_file)
        else:
            adb_process.exitcode = exitcode

        if stdout_callback:
            line = stdout2.readline()
            while line:
                if line.endswith(b"\n") or line.endswith(b"\r"):
                    line = partial + line
                    partial = b""
                    stdout_callback(line.rstrip())
                else:
                    # no more output available now, but more to come?
                    partial = partial + line
                line = stdout2.readline()
            if partial:
                stdout_callback(partial)
            stdout2.close()

        adb_process.stdout_file.seek(0, os.SEEK_SET)

        return adb_process

    def shell_bool(self, cmd, env=None, cwd=None, timeout=None, enable_run_as=False):
        """Executes a shell command on the device returning True on success
        and False on failure.

        :param str cmd: The command to be executed.
        :param dict env: Contains the environment variables and
            their values.
        :param str cwd: The directory from which to execute.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param bool enable_run_as: Flag used to temporarily enable use
            of run-as to execute the command.
        :return: bool
        :raises: :exc:`ADBTimeoutError`
        """
        adb_process = None
        try:
            adb_process = self.shell(
                cmd, env=env, cwd=cwd, timeout=timeout, enable_run_as=enable_run_as
            )
            if adb_process.timedout:
                raise ADBTimeoutError("%s" % adb_process)
            return adb_process.exitcode == 0
        finally:
            if adb_process:
                if self._verbose:
                    output = adb_process.stdout
                    self._logger.debug(
                        "shell_bool: %s, "
                        "timeout: %s, "
                        "timedout: %s, "
                        "exitcode: %s, "
                        "output: %s"
                        % (
                            " ".join(adb_process.args),
                            timeout,
                            adb_process.timedout,
                            adb_process.exitcode,
                            output,
                        )
                    )

                adb_process.stdout_file.close()

    def shell_output(
        self, cmd, env=None, cwd=None, timeout=None, enable_run_as=False, attempts=1
    ):
        """Executes an adb shell on the device returning stdout.

        :param str cmd: The command to be executed.
        :param dict env: Contains the environment variables and their values.
        :param str cwd: The directory from which to execute.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per
            adb call. The total time spent may exceed this
            value. If it is not specified, the value set
            in the ADBDevice constructor is used.
        :param bool enable_run_as: Flag used to temporarily enable use
            of run-as to execute the command.
        :return: str - content of stdout.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        for attempt in range(attempts):
            try:
                output = self._shell_output(
                    cmd, env=env, cwd=cwd, timeout=timeout, enable_run_as=enable_run_as
                )
                return output
            except ADBError:
                if attempt >= attempts - 1:
                    raise
                # pause for an arbitrary length of time, to allow for recovery from
                # intermittent failures that might persist for a short time
                time.sleep(2)

    def _shell_output(self, cmd, env=None, cwd=None, timeout=None, enable_run_as=False):
        adb_process = None
        try:
            adb_process = self.shell(
                cmd, env=env, cwd=cwd, timeout=timeout, enable_run_as=enable_run_as
            )
            if adb_process.timedout:
                raise ADBTimeoutError("%s" % adb_process)
            if adb_process.exitcode:
                raise ADBProcessError(adb_process)
            output = adb_process.stdout
            if self._verbose:
                self._logger.debug(
                    "shell_output: %s, "
                    "timeout: %s, "
                    "timedout: %s, "
                    "exitcode: %s, "
                    "output: %s"
                    % (
                        " ".join(adb_process.args),
                        timeout,
                        adb_process.timedout,
                        adb_process.exitcode,
                        output,
                    )
                )

            return output
        finally:
            if adb_process and isinstance(adb_process.stdout_file, io.IOBase):
                adb_process.stdout_file.close()

    # Informational methods

    def _get_logcat_buffer_args(self, buffers):
        valid_buffers = set(["radio", "main", "events"])
        invalid_buffers = set(buffers).difference(valid_buffers)
        if invalid_buffers:
            raise ADBError(
                "Invalid logcat buffers %s not in %s "
                % (list(invalid_buffers), list(valid_buffers))
            )
        args = []
        for b in buffers:
            args.extend(["-b", b])
        return args

    def clear_logcat(self, timeout=None, buffers=[]):
        """Clears logcat via adb logcat -c.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.  This timeout is per
            adb call. The total time spent may exceed this
            value. If it is not specified, the value set
            in the ADBDevice constructor is used.
        :param list buffers: Log buffers to clear. Valid buffers are
            "radio", "events", and "main". Defaults to "main".
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        buffers = self._get_logcat_buffer_args(buffers)
        cmds = ["logcat", "-c"] + buffers
        try:
            self.command_output(cmds, timeout=timeout)
            self.shell_output("log logcat cleared", timeout=timeout)
        except ADBTimeoutError:
            raise
        except ADBProcessError as e:
            if "failed to clear" not in str(e):
                raise
            self._logger.warning(
                f"retryable logcat clear error?: {str(e)}. Retrying..."
            )
            try:
                self.command_output(cmds, timeout=timeout)
                self.shell_output("log logcat cleared", timeout=timeout)
            except ADBProcessError as e2:
                if "failed to clear" not in str(e):
                    raise
                self._logger.warning(f"Ignoring failure to clear logcat: {str(e2)}.")

    def get_logcat(
        self,
        filter_specs=[
            "dalvikvm:I",
            "ConnectivityService:S",
            "WifiMonitor:S",
            "WifiStateTracker:S",
            "wpa_supplicant:S",
            "NetworkStateTracker:S",
            "EmulatedCamera_Camera:S",
            "EmulatedCamera_Device:S",
            "EmulatedCamera_FakeCamera:S",
            "EmulatedCamera_FakeDevice:S",
            "EmulatedCamera_CallbackNotifier:S",
            "GnssLocationProvider:S",
            "Hyphenator:S",
            "BatteryStats:S",
        ],
        format="time",
        filter_out_regexps=[],
        timeout=None,
        buffers=[],
    ):
        """Returns the contents of the logcat file as a list of strings.

        :param list filter_specs: Optional logcat messages to
            be included.
        :param str format: Optional logcat format.
        :param list filter_out_regexps: Optional logcat messages to be
            excluded.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param list buffers: Log buffers to retrieve. Valid buffers are
            "radio", "events", and "main". Defaults to "main".
        :return: list of lines logcat output.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        buffers = self._get_logcat_buffer_args(buffers)
        cmds = ["logcat", "-v", format, "-d"] + buffers + filter_specs
        lines = self.command_output(cmds, timeout=timeout).splitlines()

        for regex in filter_out_regexps:
            lines = [line for line in lines if not re.search(regex, line)]

        return lines

    def get_prop(self, prop, timeout=None):
        """Gets value of a property from the device via adb shell getprop.

        :param str prop: The propery name.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: str value of property.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        output = self.shell_output("getprop %s" % prop, timeout=timeout, attempts=3)
        return output

    def get_state(self, timeout=None):
        """Returns the device's state via adb get-state.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: str value of adb get-state.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        output = self.command_output(["get-state"], timeout=timeout).strip()
        return output

    def get_ip_address(self, interfaces=None, timeout=None):
        """Returns the device's ip address, or None if it doesn't have one

        :param list interfaces: Interfaces to allow, or None to allow any
            non-loopback interface.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: str ip address of the device or None if it could not
            be found.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if not self.is_rooted:
            self._logger.warning("Device not rooted. Can not obtain ip address.")
            return None
        self._logger.debug("get_ip_address: interfaces: %s" % interfaces)
        if not interfaces:
            interfaces = ["wlan0", "eth0"]
            wifi_interface = self.get_prop("wifi.interface", timeout=timeout)
            self._logger.debug("get_ip_address: wifi_interface: %s" % wifi_interface)
            if wifi_interface and wifi_interface not in interfaces:
                interfaces = interfaces.append(wifi_interface)

        # ifconfig interface
        # can return two different formats:
        # eth0: ip 192.168.1.139 mask 255.255.255.0 flags [up broadcast running multicast]
        # or
        # wlan0     Link encap:Ethernet  HWaddr 00:9A:CD:B8:39:65
        # inet addr:192.168.1.38  Bcast:192.168.1.255  Mask:255.255.255.0
        # inet6 addr: fe80::29a:cdff:feb8:3965/64 Scope: Link
        # UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
        # RX packets:180 errors:0 dropped:0 overruns:0 frame:0
        # TX packets:218 errors:0 dropped:0 overruns:0 carrier:0
        # collisions:0 txqueuelen:1000
        # RX bytes:84577 TX bytes:31202

        re1_ip = re.compile(r"(\w+): ip ([0-9.]+) mask.*")
        # re1_ip will match output of the first format
        # with group 1 returning the interface and group 2 returing the ip address.

        # re2_interface will match the interface line in the second format
        # while re2_ip will match the inet addr line of the second format.
        re2_interface = re.compile(r"(\w+)\s+Link")
        re2_ip = re.compile(r"\s+inet addr:([0-9.]+)")

        matched_interface = None
        matched_ip = None
        re_bad_addr = re.compile(r"127.0.0.1|0.0.0.0")

        self._logger.debug("get_ip_address: ifconfig")
        for interface in interfaces:
            try:
                output = self.shell_output(
                    "ifconfig %s" % interface, timeout=timeout, attempts=3
                )
            except ADBError as e:
                self._logger.warning(f"get_ip_address ifconfig {interface}: {str(e)}")
                output = ""

            for line in output.splitlines():
                if not matched_interface:
                    match = re1_ip.match(line)
                    if match:
                        matched_interface, matched_ip = match.groups()
                    else:
                        match = re2_interface.match(line)
                        if match:
                            matched_interface = match.group(1)
                else:
                    match = re2_ip.match(line)
                    if match:
                        matched_ip = match.group(1)

                if matched_ip:
                    if not re_bad_addr.match(matched_ip):
                        self._logger.debug(
                            "get_ip_address: found: %s %s"
                            % (matched_interface, matched_ip)
                        )
                        return matched_ip
                    matched_interface = None
                    matched_ip = None

        self._logger.debug("get_ip_address: netcfg")
        # Fall back on netcfg if ifconfig does not work.
        # $ adb shell netcfg
        # lo       UP   127.0.0.1/8       0x00000049 00:00:00:00:00:00
        # dummy0   DOWN   0.0.0.0/0       0x00000082 8e:cd:67:48:b7:c2
        # rmnet0   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet1   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet2   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet3   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet4   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet5   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet6   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # rmnet7   DOWN   0.0.0.0/0       0x00000000 00:00:00:00:00:00
        # sit0     DOWN   0.0.0.0/0       0x00000080 00:00:00:00:00:00
        # vip0     DOWN   0.0.0.0/0       0x00001012 00:01:00:00:00:01
        # wlan0    UP   192.168.1.157/24  0x00001043 38:aa:3c:1c:f6:94

        re3_netcfg = re.compile(
            r"(\w+)\s+UP\s+([1-9]\d{0,2}\.\d{1,3}\.\d{1,3}\.\d{1,3})"
        )
        try:
            output = self.shell_output("netcfg", timeout=timeout, attempts=3)
        except ADBError as e:
            self._logger.warning("get_ip_address netcfg: %s" % str(e))
            output = ""
        for line in output.splitlines():
            match = re3_netcfg.search(line)
            if match:
                matched_interface, matched_ip = match.groups()
                if matched_interface == "lo" or re_bad_addr.match(matched_ip):
                    matched_interface = None
                    matched_ip = None
                elif matched_ip and matched_interface in interfaces:
                    self._logger.debug(
                        f"get_ip_address: found: {matched_interface} {matched_ip}"
                    )
                    return matched_ip
        self._logger.debug("get_ip_address: not found")
        return matched_ip

    # File management methods

    def remount(self, timeout=None):
        """Remount /system/ in read/write mode

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """

        rv = self.command_output(["remount"], timeout=timeout)
        if "remount succeeded" not in rv:
            raise ADBError("Unable to remount device")

    def batch_execute(self, commands, timeout=None, enable_run_as=False):
        """Writes commands to a temporary file then executes on the device.

        :param list commands_list: List of commands to be run by the shell.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param bool enable_run_as: Flag used to temporarily enable use
            of run-as to execute the command.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        try:
            tmpf = tempfile.NamedTemporaryFile(mode="w", delete=False)
            tmpf.write("\n".join(commands))
            tmpf.close()
            script = posixpath.join("/data/local/tmp", os.path.basename(tmpf.name))
            self.push(tmpf.name, script)
            self.shell_output(
                f"sh {script}", enable_run_as=enable_run_as, timeout=timeout
            )
        finally:
            if tmpf:
                os.unlink(tmpf.name)
            if script:
                self.rm(script, timeout=timeout)

    def chmod(self, path, recursive=False, mask="777", timeout=None):
        """Recursively changes the permissions of a directory on the
        device.

        :param str path: The directory name on the device.
        :param bool recursive: Flag specifying if the command should be
            executed recursively.
        :param str mask: The octal permissions.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        # Note that on some tests such as webappstartup, an error
        # occurs during recursive calls to chmod where a "No such file
        # or directory" error will occur for the
        # /data/data/org.mozilla.fennec/files/mozilla/*.webapp0/lock
        # which is a symbolic link to a socket: lock ->
        # 127.0.0.1:+<port>.  On Linux, chmod -R ignores symbolic
        # links but it appear Android's version does not. We ignore
        # this type of error, but pass on any other errors that are
        # detected.
        path = posixpath.normpath(path.strip())
        enable_run_as = self.enable_run_as_for_path(path)
        self._logger.debug(f"chmod: path={path}, recursive={recursive}, mask={mask}")
        if self.is_path_internal_storage(path, timeout=timeout):
            # External storage on Android is case-insensitive and permissionless
            # therefore even with the proper privileges it is not possible
            # to change modes.
            self._logger.debug("Ignoring attempt to chmod external storage")
            return

        # build up the command to be run based on capabilities.
        command = ["chmod"]

        if recursive and self._chmod_R:
            command.append("-R")

        command.append(mask)

        if recursive and not self._chmod_R:
            paths = self.ls(path, recursive=True, timeout=timeout, attempts=3)
            base = " ".join(command)
            commands = [" ".join([base, entry]) for entry in paths]
            self.batch_execute(commands, timeout=timeout, enable_run_as=enable_run_as)
        else:
            command.append(path)
            try:
                self.shell_output(
                    cmd=" ".join(command),
                    timeout=timeout,
                    enable_run_as=enable_run_as,
                    attempts=3,
                )
            except ADBProcessError as e:
                if "No such file or directory" not in str(e):
                    # It appears that chmod -R with symbolic links will exit with
                    # exit code 1 but the files apart from the symbolic links
                    # were transfered.
                    raise

    def chown(self, path, owner, group=None, recursive=False, timeout=None):
        """Run the chown command on the provided path.

        :param str path: path name on the device.
        :param str owner: new owner of the path.
        :param str group: optional parameter specifying the new group the path
                      should belong to.
        :param bool recursive: optional value specifying whether the command should
                    operate on files and directories recursively.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before throwing
            an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        path = posixpath.normpath(path.strip())
        enable_run_as = self.enable_run_as_for_path(path)
        if self.is_path_internal_storage(path, timeout=timeout):
            self._logger.warning("Ignoring attempt to chown external storage")
            return

        # build up the command to be run based on capabilities.
        command = ["chown"]

        if recursive and self._chown_R:
            command.append("-R")

        if group:
            # officially supported notation is : but . has been checked with
            # sdk 17 and it works.
            command.append(f"{owner}.{group}")
        else:
            command.append(owner)

        if recursive and not self._chown_R:
            # recursive desired, but chown -R is not supported natively.
            # like with chmod, get the list of subpaths, put them into a script
            # then run it with adb with one call.
            paths = self.ls(path, recursive=True, timeout=timeout, attempts=3)
            base = " ".join(command)
            commands = [" ".join([base, entry]) for entry in paths]

            self.batch_execute(commands, timeout=timeout, enable_run_as=enable_run_as)
        else:
            # recursive or not, and chown -R is supported natively.
            # command can simply be run as provided by the user.
            command.append(path)
            self.shell_output(
                cmd=" ".join(command),
                timeout=timeout,
                enable_run_as=enable_run_as,
                attempts=3,
            )

    def _test_path(self, argument, path, timeout=None):
        """Performs path and file type checking.

        :param str argument: Command line argument to the test command.
        :param str path: The path or filename on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param bool root: Flag specifying if the command should be
            executed as root.
        :return: boolean - True if path or filename fulfills the
            condition of the test.
        :raises: :exc:`ADBTimeoutError`
        """
        enable_run_as = self.enable_run_as_for_path(path)
        if not enable_run_as and not self._device_serial.startswith("emulator"):
            return self.shell_bool(
                f"test -{argument} {path}",
                timeout=timeout,
                enable_run_as=False,
            )
        # Bug 1572563 - work around intermittent test path failures on emulators.
        # The shell built-in test is not supported via run-as.
        if argument == "f":
            return self.exists(path, timeout=timeout) and not self.is_dir(
                path, timeout=timeout
            )
        if argument == "d":
            return self.shell_bool(
                f"ls -a {path}/", timeout=timeout, enable_run_as=enable_run_as
            )
        if argument == "e":
            return self.shell_bool(
                f"ls -a {path}", timeout=timeout, enable_run_as=enable_run_as
            )
        raise ADBError("_test_path: Unknown argument %s" % argument)

    def exists(self, path, timeout=None):
        """Returns True if the path exists on the device.

        :param str path: The path name on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param bool root: Flag specifying if the command should be
            executed as root.
        :return: boolean - True if path exists.
        :raises: :exc:`ADBTimeoutError`
        """
        path = posixpath.normpath(path)
        return self._test_path("e", path, timeout=timeout)

    def is_dir(self, path, timeout=None):
        """Returns True if path is an existing directory on the device.

        :param str path: The directory on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: boolean - True if path exists on the device and is a
            directory.
        :raises: :exc:`ADBTimeoutError`
        """
        path = posixpath.normpath(path)
        return self._test_path("d", path, timeout=timeout)

    def is_file(self, path, timeout=None):
        """Returns True if path is an existing file on the device.

        :param str path: The file name on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: boolean - True if path exists on the device and is a
            file.
        :raises: :exc:`ADBTimeoutError`
        """
        path = posixpath.normpath(path)
        return self._test_path("f", path, timeout=timeout)

    def list_files(self, path, timeout=None):
        """Return a list of files/directories contained in a directory
        on the device.

        :param str path: The directory name on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: list of files/directories contained in the directory.
        :raises: :exc:`ADBTimeoutError`
        """
        path = posixpath.normpath(path.strip())
        enable_run_as = self.enable_run_as_for_path(path)
        data = []
        if self.is_dir(path, timeout=timeout):
            try:
                data = self.shell_output(
                    f"{self._ls} {path}",
                    timeout=timeout,
                    enable_run_as=enable_run_as,
                    attempts=3,
                ).splitlines()
                self._logger.debug("list_files: data: %s" % data)
            except ADBError:
                self._logger.error(
                    "Ignoring exception in ADBDevice.list_files\n%s"
                    % traceback.format_exc()
                )
        data[:] = [item for item in data if item]
        self._logger.debug("list_files: %s" % data)
        return data

    def ls(self, path, recursive=False, timeout=None, attempts=1):
        """Return a list of matching files/directories on the device.

        The ls method emulates the behavior of the ls shell command.
        It differs from the list_files method by supporting wild cards
        and returning matches even if the path is not a directory and
        by allowing a recursive listing.

        ls /sdcard always returns /sdcard and not the contents of the
        sdcard path. The ls method makes the behavior consistent with
        others paths by adjusting /sdcard to /sdcard/. Note this is
        also the case of other sdcard related paths such as
        /storage/emulated/legacy but no adjustment is made in those
        cases.

        The ls method works around a Nexus 4 bug which prevents
        recursive listing of directories on the sdcard unless the path
        ends with "/*" by adjusting sdcard paths ending in "/" to end
        with "/*". This adjustment is only made on official Nexus 4
        builds with property ro.product.model "Nexus 4". Note that
        this will fail to return any "hidden" files or directories
        which begin with ".".

        :param str path: The directory name on the device.
        :param bool recursive: Flag specifying if a recursive listing
            is to be returned. If recursive is False, the returned
            matches will be relative to the path. If recursive is True,
            the returned matches will be absolute paths.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param int attempts: The maximum number of shell retries.
        :return: list of files/directories contained in the directory.
        :raises: :exc:`ADBTimeoutError`
        """
        path = posixpath.normpath(path.strip())
        enable_run_as = self.enable_run_as_for_path(path)
        parent = ""
        entries = {}

        if path == "/sdcard":
            path += "/"

        # Android 2.3 and later all appear to support ls -R however
        # Nexus 4 does not perform a recursive search on the sdcard
        # unless the path is a directory with * wild card.
        if not recursive:
            recursive_flag = ""
        else:
            recursive_flag = "-R"
            if path.startswith("/sdcard") and path.endswith("/"):
                model = self.get_prop("ro.product.model", timeout=timeout)
                if model == "Nexus 4":
                    path += "*"
        lines = self.shell_output(
            f"{self._ls} {recursive_flag} {path}",
            timeout=timeout,
            enable_run_as=enable_run_as,
            attempts=attempts,
        ).splitlines()
        for line in lines:
            stripped_line = line.strip()
            if not stripped_line:
                parent = ""
                continue
            if stripped_line.endswith(":"):  # This is a directory
                parent = stripped_line.replace(":", "/")
                entry = parent
                # Remove earlier entry which is marked as a file.
                if parent[:-1] in entries:
                    del entries[parent[:-1]]
            elif parent:
                entry = f"{parent}{stripped_line}"
            else:
                entry = stripped_line
            entries[entry] = 1
        entry_list = list(entries.keys())
        entry_list.sort()
        return entry_list

    def mkdir(self, path, parents=False, timeout=None):
        """Create a directory on the device.

        :param str path: The directory name on the device
            to be created.
        :param bool parents: Flag indicating if the parent directories are
            also to be created. Think mkdir -p path.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """

        def verify_mkdir(path):
            # Verify that the directory was actually created. On some devices
            # (x86_64 emulator, v 29.0.11) the directory is sometimes not
            # immediately visible, so retries are allowed.
            retry = 0
            while retry < 10:
                if self.is_dir(path, timeout=timeout):
                    return True
                time.sleep(1)
                retry += 1
            return False

        self._sync(timeout=timeout)

        path = posixpath.normpath(path)
        enable_run_as = self.enable_run_as_for_path(path)
        if parents:
            if self._mkdir_p is None or self._mkdir_p:
                # Use shell_bool to catch the possible
                # non-zero exitcode if -p is not supported.
                if self.shell_bool(
                    "mkdir -p %s" % path, timeout=timeout, enable_run_as=enable_run_as
                ) or verify_mkdir(path):
                    self.chmod(path, recursive=True, timeout=timeout)
                    self._mkdir_p = True
                    self._sync(timeout=timeout)
                    return
            # mkdir -p is not supported. create the parent
            # directories individually.
            if not self.is_dir(posixpath.dirname(path)):
                parts = path.split("/")
                name = "/"
                for part in parts[:-1]:
                    if part != "":
                        name = posixpath.join(name, part)
                        if not self.is_dir(name):
                            # Use shell_output to allow any non-zero
                            # exitcode to raise an ADBError.
                            self.shell_output(
                                "mkdir %s" % name,
                                timeout=timeout,
                                enable_run_as=enable_run_as,
                            )
                            self.chmod(name, recursive=True, timeout=timeout)
                            self._sync(timeout=timeout)

        # If parents is True and the directory does exist, we don't
        # need to do anything. Otherwise we call mkdir. If the
        # directory already exists or if it is a file instead of a
        # directory, mkdir will fail and we will raise an ADBError.
        if not parents or not self.is_dir(path):
            self.shell_output(
                "mkdir %s" % path, timeout=timeout, enable_run_as=enable_run_as
            )
            self._sync(timeout=timeout)
            self.chmod(path, recursive=True, timeout=timeout)
        if not verify_mkdir(path):
            raise ADBError("mkdir %s Failed" % path)

    def push(self, local, remote, timeout=None):
        """Pushes a file or directory to the device.

        :param str local: The name of the local file or
            directory name.
        :param str remote: The name of the remote file or
            directory name.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self._sync(timeout=timeout)

        # remove trailing /
        local = os.path.normpath(local)
        remote = posixpath.normpath(remote)
        copy_required = False
        sdcard_remote = None
        if os.path.isfile(local) and self.is_dir(remote):
            # force push to use the correct filename in the remote directory
            remote = posixpath.join(remote, os.path.basename(local))
        elif os.path.isdir(local):
            copy_required = True
            temp_parent = tempfile.mkdtemp()
            remote_name = os.path.basename(remote)
            new_local = os.path.join(temp_parent, remote_name)

            # The build system puts the tests in objdir, as symlinks pointing
            # to the actual file in the source tree. When the symlink target is
            # removed (e.g. by updating the checkout), the symlink itself is
            # not removed by `./mach build`. By default, shutil.copytree tries
            # to read symlinks and raises an error in this case (bug 1950855).
            # Ignore dangling symlinks to avoid this issue.
            #
            # shutil.copytree's ignore_dangling_symlinks=True should be used,
            # but https://bugzilla.mozilla.org/show_bug.cgi?id=1950855#c1 shows
            # that it is buggy. As an alternative, customize copy_function:
            def copy_ignore_broken_link(src, dst):
                try:
                    return shutil_copy2(src, dst)
                except OSError as e:
                    if e.errno == 2 and os.path.islink(src):
                        self._logger.debug("Ignoring broken symlink: %s" % src)
                    else:
                        raise e

            copytree(local, new_local, copy_function=copy_ignore_broken_link)

            local = new_local
            # See do_sync_push in
            # https://android.googlesource.com/platform/system/core/+/master/adb/file_sync_client.cpp
            # Work around change in behavior in adb 1.0.36 where if
            # the remote destination directory exists, adb push will
            # copy the source directory *into* the destination
            # directory otherwise it will copy the source directory
            # *onto* the destination directory.
            if self.is_dir(remote):
                remote = "/".join(remote.rstrip("/").split("/")[:-1])
        try:
            if not self._run_as_package:
                self.command_output(["push", local, remote], timeout=timeout)
                self.chmod(remote, recursive=True, timeout=timeout)
            else:
                # When using run-as to work around the lack of root on a
                # device, we can not push directly to the app's
                # internal storage since the shell user under which
                # the push runs does not have permission to write to
                # the app's directory. Instead, we use a two stage
                # operation where we first push to a temporary
                # intermediate location under /data/local/tmp which
                # should be writable by the shell user, then using
                # run-as, copy the data into the app's internal
                # storage.
                try:
                    with tempfile.NamedTemporaryFile(delete=True) as tmpf:
                        intermediate = posixpath.join(
                            "/data/local/tmp", os.path.basename(tmpf.name)
                        )
                    self.command_output(["push", local, intermediate], timeout=timeout)
                    self.chmod(intermediate, recursive=True, timeout=timeout)
                    parent_dir = posixpath.dirname(remote)
                    if not self.is_dir(parent_dir, timeout=timeout):
                        self.mkdir(parent_dir, parents=True, timeout=timeout)
                    self.cp(intermediate, remote, recursive=True, timeout=timeout)
                finally:
                    self.rm(intermediate, recursive=True, force=True, timeout=timeout)
        except ADBProcessError as e:
            if "remote secure_mkdirs failed" not in str(e):
                raise
            self._logger.warning(
                f"remote secure_mkdirs failed push('{local}', '{remote}') {str(e)}"
            )
            # Work around change in Android where push creates
            # directories which can not be written by "other" by first
            # pushing the source to the sdcard which has no
            # permissions issues, then moving it from the sdcard to
            # the final destination.
            self._logger.info("Falling back to using intermediate /sdcard in push.")
            self.mkdir(posixpath.dirname(remote), parents=True, timeout=timeout)
            with tempfile.NamedTemporaryFile(delete=True) as tmpf:
                sdcard_remote = posixpath.join("/sdcard", os.path.basename(tmpf.name))
            self.command_output(["push", local, sdcard_remote], timeout=timeout)
            self.cp(sdcard_remote, remote, recursive=True, timeout=timeout)
        except BaseException:
            raise
        finally:
            self._sync(timeout=timeout)
            if copy_required:
                shutil.rmtree(temp_parent)
            if sdcard_remote:
                self.rm(sdcard_remote, recursive=True, force=True, timeout=timeout)

    def pull(self, remote, local, timeout=None):
        """Pulls a file or directory from the device.

        :param str remote: The path of the remote file or
            directory.
        :param str local: The path of the local file or
            directory name.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self._sync(timeout=timeout)

        # remove trailing /
        local = os.path.normpath(local)
        remote = posixpath.normpath(remote)
        copy_required = False
        original_local = local
        if os.path.isdir(local) and self.is_dir(remote):
            # See do_sync_pull in
            # https://android.googlesource.com/platform/system/core/+/master/adb/file_sync_client.cpp
            # Work around change in behavior in adb 1.0.36 where if
            # the local destination directory exists, adb pull will
            # copy the source directory *into* the destination
            # directory otherwise it will copy the source directory
            # *onto* the destination directory.
            #
            # If the destination directory does exist, pull to its
            # parent directory. If the source and destination leaf
            # directory names are different, pull the source directory
            # into a temporary directory and then copy the temporary
            # directory onto the destination.
            local_name = os.path.basename(local)
            remote_name = os.path.basename(remote)
            if local_name != remote_name:
                copy_required = True
                temp_parent = tempfile.mkdtemp()
                local = os.path.join(temp_parent, remote_name)
            else:
                local = "/".join(local.rstrip("/").split("/")[:-1])
        try:
            if not self._run_as_package:
                # We must first make the remote directory readable.
                self.chmod(remote, recursive=True, timeout=timeout)
                self.command_output(["pull", remote, local], timeout=timeout)
            else:
                # When using run-as to work around the lack of root on
                # a device, we can not pull directly from the apps
                # internal storage since the shell user under which
                # the pull runs does not have permission to read from
                # the app's directory. Instead, we use a two stage
                # operation where we first use run-as to copy the data
                # from the app's internal storage to a temporary
                # intermediate location under /data/local/tmp which
                # should be writable by the shell user, then using
                # pull, to copy the data off of the device.
                try:
                    with tempfile.NamedTemporaryFile(delete=True) as tmpf:
                        intermediate = posixpath.join(
                            "/data/local/tmp", os.path.basename(tmpf.name)
                        )
                    # When using run-as <app>, we must first use the
                    # shell to create the intermediate and chmod it
                    # before the app will be able to access it.
                    if self.is_dir(remote, timeout=timeout):
                        self.mkdir(
                            posixpath.join(intermediate, remote_name),
                            parents=True,
                            timeout=timeout,
                        )
                    else:
                        self.shell_output("echo > %s" % intermediate, timeout=timeout)
                        self.chmod(intermediate, timeout=timeout)
                    self.cp(remote, intermediate, recursive=True, timeout=timeout)
                    self.command_output(["pull", intermediate, local], timeout=timeout)
                except ADBError as e:
                    self._logger.error(f"pull {intermediate} {local}: {str(e)}")
                finally:
                    self.rm(intermediate, recursive=True, force=True, timeout=timeout)
        finally:
            if copy_required:
                copytree(local, original_local, dirs_exist_ok=True)
                shutil.rmtree(temp_parent)

    def get_file(self, remote, offset=None, length=None, timeout=None):
        """Pull file from device and return the file's content

        :param str remote: The path of the remote file.
        :param offset: If specified, return only content beyond this offset.
        :param length: If specified, limit content length accordingly.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self._sync(timeout=timeout)

        with tempfile.NamedTemporaryFile() as tf:
            self.pull(remote, tf.name, timeout=timeout)
            with open(tf.name, mode="rb") as tf2:
                # ADB pull does not support offset and length, but we can
                # instead read only the requested portion of the local file
                if offset is not None and length is not None:
                    tf2.seek(offset)
                    return tf2.read(length)
                if offset is not None:
                    tf2.seek(offset)
                    return tf2.read()
                return tf2.read()

    def rm(self, path, recursive=False, force=False, timeout=None):
        """Delete files or directories on the device.

        :param str path: The path of the remote file or directory.
        :param bool recursive: Flag specifying if the command is
            to be applied recursively to the target. Default is False.
        :param bool force: Flag which if True will not raise an
            error when attempting to delete a non-existent file. Default
            is False.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        path = posixpath.normpath(path)
        enable_run_as = self.enable_run_as_for_path(path)
        self._sync(timeout=timeout)

        cmd = "rm"
        if recursive:
            cmd += " -r"
        try:
            self.shell_output(
                f"{cmd} {path}", timeout=timeout, enable_run_as=enable_run_as
            )
            self._sync(timeout=timeout)
            if self.exists(path, timeout=timeout):
                raise ADBError('rm("%s") failed to remove path.' % path)
        except ADBError as e:
            if not force and "No such file or directory" in str(e):
                raise
            if "Directory not empty" in str(e):
                raise
            if self._verbose and "No such file or directory" not in str(e):
                self._logger.error(
                    "rm %s recursive=%s force=%s timeout=%s enable_run_as=%s: %s"
                    % (path, recursive, force, timeout, enable_run_as, str(e))
                )

    def rmdir(self, path, timeout=None):
        """Delete empty directory on the device.

        :param str path: The directory name on the device.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        path = posixpath.normpath(path)
        enable_run_as = self.enable_run_as_for_path(path)
        self.shell_output(
            "rmdir %s" % path, timeout=timeout, enable_run_as=enable_run_as
        )
        self._sync(timeout=timeout)
        if self.is_dir(path, timeout=timeout):
            raise ADBError('rmdir("%s") failed to remove directory.' % path)

    # Process management methods

    def get_process_list(self, timeout=None):
        """Returns list of tuples (pid, name, user) for running
        processes on device.

        :param int timeout: The maximum time
            in seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified,
            the value set in the ADBDevice constructor is used.
        :return: list of (pid, name, user) tuples for running processes
            on the device.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        adb_process = None
        max_attempts = 2
        try:
            for attempt in range(1, max_attempts + 1):
                adb_process = self.shell("ps", timeout=timeout)
                if adb_process.timedout:
                    raise ADBTimeoutError("%s" % adb_process)
                if adb_process.exitcode:
                    raise ADBProcessError(adb_process)
                # first line is the headers
                header = adb_process.stdout_file.readline().decode()
                pid_i = -1
                user_i = -1
                els = header.split()
                for i in range(len(els)):
                    item = els[i].lower()
                    if item == "user":
                        user_i = i
                    elif item == "pid":
                        pid_i = i
                if user_i != -1 and pid_i != -1:
                    break
                # if this isn't the final attempt, don't print this as an error
                if attempt < max_attempts:
                    self._logger.info(
                        "get_process_list: attempt: %d %s" % (attempt, header)
                    )
                else:
                    raise ADBError(
                        "get_process_list: Unknown format: %s: %s"
                        % (header, adb_process)
                    )
            ret = []
            line = adb_process.stdout_file.readline().decode()
            while line:
                els = line.split()
                try:
                    ret.append([int(els[pid_i]), els[-1], els[user_i]])
                except ValueError:
                    self._logger.error(
                        "get_process_list: %s %s\n%s"
                        % (header, line, traceback.format_exc())
                    )
                    raise ADBError(f"get_process_list: {header}: {line}: {adb_process}")
                except IndexError:
                    self._logger.error(
                        "get_process_list: %s %s els %s pid_i %s user_i %s\n%s"
                        % (header, line, els, pid_i, user_i, traceback.format_exc())
                    )
                    raise ADBError(
                        "get_process_list: %s: %s els %s pid_i %s user_i %s: %s"
                        % (header, line, els, pid_i, user_i, adb_process)
                    )
                line = adb_process.stdout_file.readline().decode()
            self._logger.debug("get_process_list: %s" % ret)
            return ret
        finally:
            if adb_process and isinstance(adb_process.stdout_file, io.IOBase):
                adb_process.stdout_file.close()

    def kill(self, pids, sig=None, attempts=3, wait=5, timeout=None):
        """Kills processes on the device given a list of process ids.

        :param list pids: process ids to be killed.
        :param int sig: signal to be sent to the process.
        :param integer attempts: number of attempts to try to
            kill the processes.
        :param integer wait: number of seconds to wait after each attempt.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        pid_list = [str(pid) for pid in pids]
        for attempt in range(attempts):
            args = ["kill"]
            if sig:
                args.append("-%d" % sig)
            args.extend(pid_list)
            try:
                self.shell_output(" ".join(args), timeout=timeout)
            except ADBError as e:
                if "No such process" not in str(e):
                    raise
            pid_set = set(pid_list)
            current_pid_set = set([
                str(proc[0]) for proc in self.get_process_list(timeout=timeout)
            ])
            pid_list = list(pid_set.intersection(current_pid_set))
            if not pid_list:
                break
            self._logger.debug(
                "Attempt %d of %d to kill processes %s failed"
                % (attempt + 1, attempts, pid_list)
            )
            time.sleep(wait)

        if pid_list:
            raise ADBError("kill: processes %s not killed" % pid_list)

    def pkill(self, appname, sig=None, attempts=3, wait=5, timeout=None):
        """Kills a processes on the device matching a name.

        :param str appname: The app name of the process to
            be killed. Note that only the first 75 characters of the
            process name are significant.
        :param int sig: optional signal to be sent to the process.
        :param integer attempts: number of attempts to try to
            kill the processes.
        :param integer wait: number of seconds to wait after each attempt.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :param bool root: Flag specifying if the command should
            be executed as root.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        pids = self.pidof(appname, timeout=timeout)

        if not pids:
            return

        try:
            self.kill(pids, sig, attempts=attempts, wait=wait, timeout=timeout)
        except ADBError as e:
            if self.process_exist(appname, timeout=timeout):
                raise e

    def process_exist(self, process_name, timeout=None):
        """Returns True if process with name process_name is running on
        device.

        :param str process_name: The name of the process
            to check. Note that only the first 75 characters of the
            process name are significant.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: boolean - True if process exists.

        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if not isinstance(process_name, str):
            raise ADBError("Process name %s is not a string" % process_name)

        # Filter out extra spaces.
        parts = [x for x in process_name.split(" ") if x != ""]
        process_name = " ".join(parts)

        # Filter out the quoted env string if it exists
        # ex: '"name=value;name2=value2;etc=..." process args' -> 'process args'
        parts = process_name.split('"')
        if len(parts) > 2:
            process_name = " ".join(parts[2:]).strip()

        pieces = process_name.split(" ")
        parts = pieces[0].split("/")
        app = parts[-1]

        if self.pidof(app, timeout=timeout):
            return True
        return False

    def cp(self, source, destination, recursive=False, timeout=None):
        """Copies a file or directory on the device.

        :param source: string containing the path of the source file or
            directory.
        :param destination: string containing the path of the destination file
            or directory.
        :param recursive: optional boolean indicating if a recursive copy is to
            be performed. Required if the source is a directory. Defaults to
            False. Think cp -R source destination.
        :param int timeout: optional integer specifying the maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        source = posixpath.normpath(source)
        destination = posixpath.normpath(destination)
        enable_run_as = self.enable_run_as_for_path(
            source
        ) or self.enable_run_as_for_path(destination)
        if self._have_cp:
            r = "-R" if recursive else ""
            self.shell_output(
                f"cp {r} {source} {destination}",
                timeout=timeout,
                enable_run_as=enable_run_as,
            )
            self.chmod(destination, recursive=recursive, timeout=timeout)
            self._sync(timeout=timeout)
            return

        # Emulate cp behavior depending on if source and destination
        # already exists and whether they are a directory or file.
        if not self.exists(source, timeout=timeout):
            raise ADBError("cp: can't stat '%s': No such file or directory" % source)

        if self.is_file(source, timeout=timeout):
            if self.is_dir(destination, timeout=timeout):
                # Copy the source file into the destination directory
                destination = posixpath.join(destination, os.path.basename(source))
            self.shell_output(f"dd if={source} of={destination}", timeout=timeout)
            self.chmod(destination, recursive=recursive, timeout=timeout)
            self._sync(timeout=timeout)
            return

        if self.is_file(destination, timeout=timeout):
            raise ADBError("cp: %s: Not a directory" % destination)

        if not recursive:
            raise ADBError("cp: omitting directory '%s'" % source)

        if self.is_dir(destination, timeout=timeout):
            # Copy the source directory into the destination directory.
            destination_dir = posixpath.join(destination, os.path.basename(source))
        else:
            # Copy the contents of the source directory into the
            # destination directory.
            destination_dir = destination

        try:
            # Do not create parent directories since cp does not.
            self.mkdir(destination_dir, timeout=timeout)
        except ADBError as e:
            if "File exists" not in str(e):
                raise

        for i in self.list_files(source, timeout=timeout):
            self.cp(
                posixpath.join(source, i),
                posixpath.join(destination_dir, i),
                recursive=recursive,
                timeout=timeout,
            )
        self.chmod(destination_dir, recursive=True, timeout=timeout)

    def mv(self, source, destination, timeout=None):
        """Moves a file or directory on the device.

        :param source: string containing the path of the source file or
            directory.
        :param destination: string containing the path of the destination file
            or directory.
        :param int timeout: optional integer specifying the maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        source = posixpath.normpath(source)
        destination = posixpath.normpath(destination)
        enable_run_as = self.enable_run_as_for_path(
            source
        ) or self.enable_run_as_for_path(destination)
        self.shell_output(
            f"mv {source} {destination}",
            timeout=timeout,
            enable_run_as=enable_run_as,
        )

    def reboot(self, timeout=None):
        """Reboots the device.

        :param int timeout: optional integer specifying the maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`

        reboot() reboots the device, issues an adb wait-for-device in order to
        wait for the device to complete rebooting, then calls is_device_ready()
        to determine if the device has completed booting.

        If the device supports running adbd as root, adbd will be
        restarted running as root. Then, if the device supports
        SELinux, setenforce Permissive will be called to change
        SELinux to permissive. This must be done after adbd is
        restarted in order for the SELinux Permissive setting to
        persist.

        """
        self.command_output(["reboot"], timeout=timeout)
        self._wait_for_boot_completed(timeout=timeout)
        return self.is_device_ready(timeout=timeout)

    def get_sysinfo(self, timeout=None):
        """
        Returns a detailed dictionary of information strings about the device.

        :param int timeout: optional integer specifying the maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.

        :raises: :exc:`ADBTimeoutError`
        """
        results = {"info": self.get_info(timeout=timeout)}
        for service in (
            "meminfo",
            "cpuinfo",
            "dbinfo",
            "procstats",
            "usagestats",
            "battery",
            "batterystats",
            "diskstats",
        ):
            results[service] = self.shell_output(
                "dumpsys %s" % service, timeout=timeout, attempts=3
            )
        return results

    def get_info(self, directive=None, timeout=None):
        """
        Returns a dictionary of information strings about the device.

        :param directive: information you want to get. Options are:
             - `battery` - battery charge as a percentage
             - `disk` - total, free, available bytes on disk
             - `id` - unique id of the device
             - `os` - name of the os
             - `process` - list of running processes (same as ps)
             - `systime` - system time of the device
             - `uptime` - uptime of the device

            If `directive` is `None`, will return all available information
        :param int timeout: optional integer specifying the maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        directives = ["battery", "disk", "id", "os", "process", "systime", "uptime"]

        if directive in directives:
            directives = [directive]

        info = {}
        if "battery" in directives:
            info["battery"] = self.get_battery_percentage(timeout=timeout)
        if "disk" in directives:
            info["disk"] = self.shell_output(
                "df /data /system /sdcard", timeout=timeout, attempts=3
            ).splitlines()
        if "id" in directives:
            info["id"] = self.command_output(["get-serialno"], timeout=timeout)
        if "os" in directives:
            info["os"] = self.get_prop("ro.build.display.id", timeout=timeout)
        if "process" in directives:
            ps = self.shell_output("ps", timeout=timeout, attempts=3)
            info["process"] = ps.splitlines()
        if "systime" in directives:
            info["systime"] = self.shell_output("date", timeout=timeout, attempts=3)
        if "uptime" in directives:
            uptime = self.shell_output("uptime", timeout=timeout, attempts=3)
            if uptime:
                m = re.match(r"up time: ((\d+) days, )*(\d{2}):(\d{2}):(\d{2})", uptime)
                if m:
                    uptime = "%d days %d hours %d minutes %d seconds" % tuple([
                        int(g or 0) for g in m.groups()[1:]
                    ])
                info["uptime"] = uptime
        return info

    # Properties to manage SELinux on the device:
    # https://source.android.com/devices/tech/security/selinux/index.html
    # setenforce [ Enforcing | Permissive | 1 | 0 ]
    # getenforce returns either Enforcing or Permissive

    @property
    def selinux(self):
        """Returns True if SELinux is supported, False otherwise."""
        if self._selinux is None:
            self._selinux = self.enforcing != ""
        return self._selinux

    @property
    def enforcing(self):
        try:
            enforce = self.shell_output("getenforce", attempts=3)
        except ADBError as e:
            enforce = ""
            self._logger.warning("Unable to get SELinux enforcing due to %s." % e)
        return enforce

    @enforcing.setter
    def enforcing(self, value):
        """Set SELinux mode.
        :param str value: The new SELinux mode. Should be one of
            Permissive, 0, Enforcing, 1 but it is not validated.
        """
        try:
            self.shell_output("setenforce %s" % value, attempts=3)
            self._logger.info("Setting SELinux %s" % value)
        except ADBError as e:
            self._logger.warning("Unable to set SELinux Permissive due to %s." % e)

    # Informational methods

    def get_battery_percentage(self, timeout=None):
        """Returns the battery charge as a percentage.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: battery charge as a percentage.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        level = None
        scale = None
        percentage = 0
        cmd = "dumpsys battery"
        re_parameter = re.compile(r"\s+(\w+):\s+(\d+)")
        lines = self.shell_output(cmd, timeout=timeout, attempts=3).splitlines()
        for line in lines:
            match = re_parameter.match(line)
            if match:
                parameter = match.group(1)
                value = match.group(2)
                if parameter == "level":
                    level = float(value)
                elif parameter == "scale":
                    scale = float(value)
                if parameter is not None and scale is not None:
                    # pylint --py3k W1619
                    percentage = 100.0 * level / scale
                    break
        return percentage

    def get_top_activity(self, timeout=None):
        """Returns the name of the top activity (focused app) reported by dumpsys

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADBDevice constructor is used.
        :return: package name of top activity or None (cannot be determined)
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if self.version < version_codes.Q:
            return self._get_top_activity_P(timeout=timeout)
        return self._get_top_activity_Q(timeout=timeout)

    def _get_top_activity_P(self, timeout=None):
        """Returns the name of the top activity (focused app) reported by dumpsys
        for Android 9 and earlier.
        """
        package = None
        data = None
        cmd = "dumpsys window windows"
        verbose = self._verbose
        try:
            self._verbose = False
            data = self.shell_output(cmd, timeout=timeout, attempts=3)
        except Exception as e:
            # dumpsys intermittently fails on some platforms.
            self._logger.info(f"_get_top_activity_P: Exception {cmd}: {e}")
            return package
        finally:
            self._verbose = verbose
        m = re.search("mFocusedApp(.+)/", data)
        if not m:
            # alternative format seen on newer versions of Android
            m = re.search("FocusedApplication(.+)/", data)
        if m:
            line = m.group(0)
            # Extract package name: string of non-whitespace ending in forward slash
            m = re.search(r"(\S+)/$", line)
            if m:
                package = m.group(1)
        if self._verbose:
            self._logger.debug("get_top_activity: %s" % str(package))
        return package

    def _get_top_activity_Q(self, timeout=None):
        """Returns the name of the top activity (focused app) reported by dumpsys
        for Android 10 and later.
        """
        package = None
        data = None
        cmd = "dumpsys window"
        verbose = self._verbose
        try:
            self._verbose = False
            data = self.shell_output(cmd, timeout=timeout, attempts=3)
        except Exception as e:
            # dumpsys intermittently fails on some platforms (4.3 arm emulator)
            self._logger.info(f"_get_top_activity_Q: Exception {cmd}: {e}")
            return package
        finally:
            self._verbose = verbose
        m = re.search(r"mFocusedWindow=Window{\S+ \S+ (\S+)/\S+}", data)
        if m:
            package = m.group(1)
        if self._verbose:
            self._logger.debug("get_top_activity: %s" % str(package))
        return package

    # System control methods

    def is_device_ready(self, timeout=None):
        """Checks if a device is ready for testing.

        This method uses the android only package manager to check for
        readiness.

        :param int timeout: The maximum time
            in seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        # command_output automatically inserts a 'wait-for-device'
        # argument to adb. Issuing an empty command is the same as adb
        # -s <device> wait-for-device. We don't send an explicit
        # 'wait-for-device' since that would add duplicate
        # 'wait-for-device' arguments which is an error in newer
        # versions of adb.
        self._wait_for_boot_completed(timeout=timeout)
        pm_error_string = "Error: Could not access the Package Manager"
        ready_path = os.path.join(self.test_root, "ready")
        for attempt in range(self._device_ready_retry_attempts):
            failure = "Unknown failure"
            success = True
            try:
                state = self.get_state(timeout=timeout)
                if state != "device":
                    failure = "Device state: %s" % state
                    success = False
                else:
                    if self.enforcing != "Permissive":
                        self.enforcing = "Permissive"
                    if self.is_dir(ready_path, timeout=timeout):
                        self.rmdir(ready_path, timeout=timeout)
                    self.mkdir(ready_path, timeout=timeout)
                    self.rmdir(ready_path, timeout=timeout)
                    # Invoke the pm list packages command to see if it is up and
                    # running.
                    data = self.shell_output(
                        "pm list packages org.mozilla", timeout=timeout, attempts=3
                    )
                    if pm_error_string in data:
                        failure = data
                        success = False
            except ADBError as e:
                success = False
                failure = str(e)

            if not success:
                self._logger.debug(
                    "Attempt %s of %s device not ready: %s"
                    % (attempt + 1, self._device_ready_retry_attempts, failure)
                )
                time.sleep(self._device_ready_retry_wait)

        return success

    def power_on(self, timeout=None):
        """Sets the device's power stayon value.

        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        try:
            self.shell_output("svc power stayon true", timeout=timeout, attempts=3)
        except ADBError as e:
            # Executing this via adb shell errors, but not interactively.
            # Any other exitcode is a real error.
            if "exitcode: 137" not in str(e):
                raise
            self._logger.warning("Unable to set power stayon true: %s" % e)

    # Application management methods

    def add_change_device_settings(self, app_name, timeout=None):
        """
        Allows the test to change Android device settings.
        :param str: app_name: Name of application (e.g. `org.mozilla.fennec`)
        """
        self.shell_output(
            "appops set %s android:write_settings allow" % app_name,
            timeout=timeout,
            enable_run_as=False,
            attempts=3,
        )

    def add_mock_location(self, app_name, timeout=None):
        """
        Allows the Android device to use mock locations.
        :param str: app_name: Name of application (e.g. `org.mozilla.fennec`)
        """
        self.shell_output(
            "appops set %s android:mock_location allow" % app_name,
            timeout=timeout,
            enable_run_as=False,
            attempts=3,
        )

    def grant_runtime_permissions(self, app_name, timeout=None):
        """
        Grant required runtime permissions to the specified app
        (typically org.mozilla.fennec_$USER).

        :param str: app_name: Name of application (e.g. `org.mozilla.fennec`)
        """
        if self.version >= version_codes.M:
            permissions = [
                "android.permission.ACCESS_COARSE_LOCATION",
                "android.permission.ACCESS_FINE_LOCATION",
                "android.permission.CAMERA",
                "android.permission.RECORD_AUDIO",
            ]
            if self.version < version_codes.R:
                # WRITE_EXTERNAL_STORAGE is no longer available in Android 11+
                permissions.append("android.permission.WRITE_EXTERNAL_STORAGE")
            if self.version < version_codes.TIRAMISU:
                # READ_EXTERNAL_STORAGE is no longer available in Android 13+
                permissions.append("android.permission.READ_EXTERNAL_STORAGE")
            self._logger.info("Granting important runtime permissions to %s" % app_name)
            for permission in permissions:
                try:
                    self.shell_output(
                        f"pm grant {app_name} {permission}",
                        timeout=timeout,
                        enable_run_as=False,
                        attempts=3,
                    )
                except ADBError as e:
                    self._logger.warning(
                        "Unable to grant runtime permission %s to %s due to %s"
                        % (permission, app_name, e)
                    )

    def install_app_bundle(self, bundletool, bundle_path, java_home=None, timeout=None):
        """Installs an app bundle (AAB) on the device.

        :param str bundletool: Path to the bundletool jar
        :param str bundle_path: The aab file name to be installed.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :param str java_home: Path to the JDK location. Will default to
            $JAVA_HOME when not specififed.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        device_serial = self._device_serial or os.environ.get("ANDROID_SERIAL")
        java_home = java_home or os.environ.get("JAVA_HOME")
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            # bundletool doesn't come with a debug-key so we need to provide
            # one ourselves.
            keystore_path = os.path.join(temporaryDirectory, "debug.keystore")
            keytool_path = os.path.join(java_home, "bin", "keytool")
            key_gen = [
                keytool_path,
                "-genkey",
                "-v",
                "-keystore",
                keystore_path,
                "-alias",
                "androiddebugkey",
                "-storepass",
                "android",
                "-keypass",
                "android",
                "-keyalg",
                "RSA",
                "-validity",
                "14000",
                "-dname",
                "cn=Unknown, ou=Unknown, o=Unknown, c=Unknown",
            ]
            self._logger.info("key_gen: %s" % key_gen)
            try:
                subprocess.check_call(key_gen, timeout=timeout)
            except subprocess.TimeoutExpired:
                raise ADBTimeoutError("ADBDevice: unable to generate key")

            apks_path = f"{temporaryDirectory}/tmp.apks"
            java_path = os.path.join(java_home, "bin", "java")
            build_apks = [
                java_path,
                "-jar",
                bundletool,
                "build-apks",
                f"--bundle={bundle_path}",
                f"--output={apks_path}",
                "--connected-device",
                f"--device-id={device_serial}",
                f"--adb={self._adb_path}",
                f"--ks={keystore_path}",
                "--ks-key-alias=androiddebugkey",
                "--key-pass=pass:android",
                "--ks-pass=pass:android",
            ]
            self._logger.info("build_apks: %s" % build_apks)

            try:
                subprocess.check_call(build_apks, timeout=timeout)
            except subprocess.TimeoutExpired:
                raise ADBTimeoutError("ADBDevice: unable to generate apks")
            install_apks = [
                java_path,
                "-jar",
                bundletool,
                "install-apks",
                f"--apks={apks_path}",
                f"--device-id={device_serial}",
                f"--adb={self._adb_path}",
            ]
            self._logger.info("install_apks: %s" % install_apks)

            try:
                subprocess.check_call(install_apks, timeout=timeout)
            except subprocess.TimeoutExpired:
                raise ADBTimeoutError("ADBDevice: unable to install apks")

    def install_app(self, apk_path, replace=False, timeout=None):
        """Installs an app on the device.

        :param str apk_path: The apk file name to be installed.
        :param bool replace: If True, replace existing application.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :return: string - name of installed package.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        dump_packages = "dumpsys package packages"
        packages_before = set(self.shell_output(dump_packages, attempts=3).split("\n"))
        cmd = ["install"]
        if replace:
            cmd.append("-r")
        cmd.append(apk_path)
        data = self.command_output(cmd, timeout=timeout)
        if data.find("Success") == -1:
            raise ADBError(f"install failed for {apk_path}. Got: {data}")
        packages_after = set(self.shell_output(dump_packages, attempts=3).split("\n"))
        packages_diff = packages_after - packages_before
        package_name = None
        re_pkg = re.compile(r"\s+pkg=Package{[^ ]+ (.*)}")
        for diff in packages_diff:
            match = re_pkg.match(diff)
            if match:
                package_name = match.group(1)
                break
        return package_name

    def install_app_baseline_profile(self, apk_path, replace=False, timeout=None):
        """Installs an app on the device. Utilize profgen to first extract out the
        .dm profile and use `adb install-multiple` to properly install the apk along
        with the generated baseline profile.

        :param str apk_path: The apk file name to be installed.
        :param bool replace: If True, replace existing application.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :return: string - name of installed package.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        # dm files will always share the same stem as the apk file.
        dm_path = apk_path[:-3] + "dm"
        command = [
            "profgen",
            "extractProfile",
            "--apk",
            apk_path,
            "--output-dex-metadata",
            dm_path,
            "--profile-format",
            "V0_1_5_S",
        ]
        subprocess.run(command, check=True)
        dump_packages = "dumpsys package packages"
        packages_before = set(self.shell_output(dump_packages, attempts=3).split("\n"))
        cmd = ["install-multiple"]
        if replace:
            cmd.append("-r")
        cmd.append(apk_path)
        cmd.append(dm_path)
        data = self.command_output(cmd, timeout=timeout)
        if data.find("Success") == -1:
            raise ADBError(f"install failed for {apk_path}. Got: {data}")
        packages_after = set(self.shell_output(dump_packages, attempts=3).split("\n"))
        packages_diff = packages_after - packages_before
        package_name = None
        re_pkg = re.compile(r"\s+pkg=Package{[^ ]+ (.*)}")
        for diff in packages_diff:
            match = re_pkg.match(diff)
            if match:
                package_name = match.group(1)
                break
        output = self.shell_output(f"dumpsys package dexopt | grep -A 1 {package_name}")
        print(output)
        if "status=speed-profile" not in output:
            raise Exception(
                f"{package_name} did not install the baseline profile correctly"
            )

        return package_name

    def is_app_installed(self, app_name, timeout=None):
        """Returns True if an app is installed on the device.

        :param str app_name: name of the app to be checked.
        :param int timeout: maximum time in seconds for any spawned
            adb process to complete before throwing an ADBTimeoutError.
            This timeout is per adb call. If it is not specified,
            the value set in the ADB constructor is used.

        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        pm_error_string = "Error: Could not access the Package Manager"
        data = self.shell_output(
            "pm list package %s" % app_name,
            timeout=timeout,
            enable_run_as=False,
            attempts=3,
        )
        if pm_error_string in data:
            raise ADBError(pm_error_string)
        output = [line for line in data.splitlines() if line.strip()]
        return any([f"package:{app_name}" == out for out in output])

    def launch_application(
        self,
        app_name,
        activity_name,
        intent,
        url=None,
        extras=None,
        wait=True,
        fail_if_running=True,
        grant_runtime_permissions=True,
        timeout=None,
        is_service=False,
    ):
        """Launches an Android application

        :param str app_name: Name of application (e.g. `com.android.chrome`)
        :param str activity_name: Name of activity to launch (e.g. `.Main`)
        :param str intent: Intent to launch application with
        :param str url: URL to open
        :param dict extras: Extra arguments for application.
        :param bool wait: If True, wait for application to start before
            returning.
        :param bool fail_if_running: Raise an exception if instance of
            application is already running.
        :param bool grant_runtime_permissions: Grant special runtime
            permissions.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :param bool is_service: Whether we want to launch a service or not.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        # If fail_if_running is True, we throw an exception here. Only one
        # instance of an application can be running at once on Android,
        # starting a new instance may not be what we want depending on what
        # we want to do
        if fail_if_running and self.process_exist(app_name, timeout=timeout):
            raise ADBError("Only one instance of an application may be running at once")

        if grant_runtime_permissions:
            self.grant_runtime_permissions(app_name)

        acmd = ["am"] + ["startservice" if is_service else "start"]
        if wait:
            acmd.extend(["-W"])
        acmd.extend([
            "-n",
            f"{app_name}/{activity_name}",
        ])
        if intent:
            acmd.extend(["-a", intent])

        # Note that isinstance(True, int) and isinstance(False, int)
        # is True. This means we must test the type of the value
        # against bool prior to testing it against int in order to
        # prevent falsely identifying a bool value as an int.
        if extras:
            for key, val in extras.items():
                if isinstance(val, bool):
                    extra_type_param = "--ez"
                elif isinstance(val, int):
                    extra_type_param = "--ei"
                else:
                    extra_type_param = "--es"
                acmd.extend([extra_type_param, str(key), str(val)])

        if url:
            acmd.extend(["-d", url])

        cmd = self._escape_command_line(acmd)
        self._logger.info("launch_application: %s" % cmd)
        cmd_output = self.shell_output(cmd, timeout=timeout)
        if "Error:" in cmd_output:
            for line in cmd_output.split("\n"):
                self._logger.info(line)
            raise ADBError(f"launch_application {app_name}/{activity_name} failed")

    def launch_fennec(
        self,
        app_name,
        intent="android.intent.action.VIEW",
        moz_env=None,
        extra_args=None,
        url=None,
        wait=True,
        fail_if_running=True,
        timeout=None,
    ):
        """Convenience method to launch Fennec on Android with various
        debugging arguments

        :param str app_name: Name of fennec application (e.g.
            `org.mozilla.fennec`)
        :param str intent: Intent to launch application.
        :param str moz_env: Mozilla specific environment to pass into
            application.
        :param str extra_args: Extra arguments to be parsed by fennec.
        :param str url: URL to open
        :param bool wait: If True, wait for application to start before
            returning.
        :param bool fail_if_running: Raise an exception if instance of
            application is already running.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        extras = {}

        if moz_env is None:
            moz_env = {}
        moz_env.update(self.target_ccov_env)

        if moz_env:
            # moz_env is expected to be a dictionary of environment variables:
            # Fennec itself will set them when launched
            for env_count, (env_key, env_val) in enumerate(moz_env.items()):
                extras["env" + str(env_count)] = env_key + "=" + env_val

        # Additional command line arguments that fennec will read and use (e.g.
        # with a custom profile)
        if extra_args:
            extras["args"] = " ".join(extra_args)

        self.launch_application(
            app_name,
            "org.mozilla.gecko.BrowserApp",
            intent,
            url=url,
            extras=extras,
            wait=wait,
            fail_if_running=fail_if_running,
            timeout=timeout,
        )

    def launch_service(
        self,
        app_name,
        activity_name=None,
        intent="android.intent.action.MAIN",
        moz_env=None,
        extra_args=None,
        url=None,
        e10s=False,
        wait=True,
        grant_runtime_permissions=False,
        out_file=None,
        timeout=None,
    ):
        """Convenience method to launch a service on Android with various
        debugging arguments; convenient for geckoview apps.

        :param str app_name: Name of application (e.g.
            `org.mozilla.geckoview_example` or `org.mozilla.geckoview.test_runner`)
        :param str activity_name: Activity name, like `GeckoViewActivity`, or
            `TestRunnerActivity`.
        :param str intent: Intent to launch application.
        :param str moz_env: Mozilla specific environment to pass into
            application.
        :param str extra_args: Extra arguments to be parsed by the app.
        :param str url: URL to open
        :param bool e10s: No longer used.
        :param bool wait: If True, wait for application to start before
            returning.
        :param bool grant_runtime_permissions: Grant special runtime
            permissions.
        :param str out_file: File where to redirect the output to
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        extras = {}

        if moz_env is None:
            moz_env = {}
        moz_env.update(self.target_ccov_env)

        if moz_env:
            # moz_env is expected to be a dictionary of environment variables:
            # geckoview_example itself will set them when launched
            for env_count, (env_key, env_val) in enumerate(moz_env.items()):
                extras["env" + str(env_count)] = env_key + "=" + env_val

        # Additional command line arguments that the app will read and use (e.g.
        # with a custom profile)
        if extra_args:
            for arg_count, arg in enumerate(extra_args):
                extras["arg" + str(arg_count)] = arg

        extras["out_file"] = out_file
        self.launch_application(
            app_name,
            f"{app_name}.{activity_name}",
            intent,
            url=url,
            extras=extras,
            wait=wait,
            grant_runtime_permissions=grant_runtime_permissions,
            timeout=timeout,
            is_service=True,
            fail_if_running=False,
        )

    def launch_activity(
        self,
        app_name,
        activity_name=None,
        intent="android.intent.action.MAIN",
        moz_env=None,
        extra_args=None,
        url=None,
        e10s=False,
        wait=True,
        fail_if_running=True,
        timeout=None,
    ):
        """Convenience method to launch an application on Android with various
        debugging arguments; convenient for geckoview apps.

        :param str app_name: Name of application (e.g.
            `org.mozilla.geckoview_example` or `org.mozilla.geckoview.test_runner`)
        :param str activity_name: Activity name, like `GeckoViewActivity`, or
            `TestRunnerActivity`.
        :param str intent: Intent to launch application.
        :param str moz_env: Mozilla specific environment to pass into
            application.
        :param str extra_args: Extra arguments to be parsed by the app.
        :param str url: URL to open
        :param bool e10s: No longer used.
        :param bool wait: If True, wait for application to start before
            returning.
        :param bool fail_if_running: Raise an exception if instance of
            application is already running.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        extras = {}

        if moz_env is None:
            moz_env = {}
        moz_env.update(self.target_ccov_env)

        if moz_env:
            # moz_env is expected to be a dictionary of environment variables:
            # geckoview_example itself will set them when launched
            for env_count, (env_key, env_val) in enumerate(moz_env.items()):
                extras["env" + str(env_count)] = env_key + "=" + env_val

        # Additional command line arguments that the app will read and use (e.g.
        # with a custom profile)
        if extra_args:
            for arg_count, arg in enumerate(extra_args):
                extras["arg" + str(arg_count)] = arg

        self.launch_application(
            app_name,
            f"{app_name}.{activity_name}",
            intent,
            url=url,
            extras=extras,
            wait=wait,
            fail_if_running=fail_if_running,
            timeout=timeout,
        )

    def stop_application(self, app_name, timeout=None):
        """Stops the specified application

        For Android 3.0+, we use the "am force-stop" to do this, which
        is reliable and does not require root. For earlier versions of
        Android, we simply try to manually kill the processes started
        by the app repeatedly until none is around any more. This is
        less reliable and does require root.

        :param str app_name: Name of application (e.g. `com.android.chrome`)
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :param bool root: Flag specifying if the command should be
            executed as root.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if self.version >= version_codes.HONEYCOMB:
            self.shell_output(
                "am force-stop %s" % app_name, timeout=timeout, attempts=3
            )
        else:
            num_tries = 0
            max_tries = 5
            while self.process_exist(app_name, timeout=timeout):
                if num_tries > max_tries:
                    raise ADBError(
                        "Couldn't successfully kill %s after %s "
                        "tries" % (app_name, max_tries)
                    )
                self.pkill(app_name, timeout=timeout)
                num_tries += 1

                # sleep for a short duration to make sure there are no
                # additional processes in the process of being launched
                # (this is not 100% guaranteed to work since it is inherently
                # racey, but it's the best we can do)
                time.sleep(1)

    def confirm_clear_app_data(self, app_name):
        """Confirm with the user that the app's data may be cleared.

        On non-rooted devices the app's data (i.e. profile) must be wiped with
        `pm clear` since the shell can't remove app-owned files. This prompts
        the user to confirm that destruction once and caches the response in a
        module-level global so subsequent callers in the same process reuse it
        instead of prompting again. The prompt is skipped (returning True) when
        the device is rooted or when running in CI (MOZ_AUTOMATION is set). The
        MOZ_ADB_PM_CLEAR_CHECK environment variable, when defined, overrides the
        prompt: "1" allows clearing the app data and "0" forbids it.

        :param str app_name: The name of the app whose data will be cleared.
        :return: True if clearing the app data is confirmed, False otherwise.
        """
        global _CLEAR_APP_DATA_CONFIRMED  # noqa

        if self.is_rooted or "MOZ_AUTOMATION" in os.environ:
            return True

        env_check = os.environ.get("MOZ_ADB_PM_CLEAR_CHECK")
        if env_check is not None:
            return env_check.strip() == "1"

        if _CLEAR_APP_DATA_CONFIRMED is not None:
            return _CLEAR_APP_DATA_CONFIRMED

        self._logger.info(
            f"WARNING: Device is not rooted, and the app data (i.e. profile) for "
            f"{app_name} will be cleared."
        )
        self._logger.info(
            "Confirm that you understand the full app data for the app will be "
            "removed. The entire profile of the app will be deleted (Yes/No): "
        )
        answer = input().strip().lower()
        _CLEAR_APP_DATA_CONFIRMED = answer in ("yes", "y")
        return _CLEAR_APP_DATA_CONFIRMED

    def uninstall_app(self, app_name, reboot=False, timeout=None):
        """Uninstalls an app on the device.

        :param str app_name: The name of the app to be
            uninstalled.
        :param bool reboot: Flag indicating that the device should
            be rebooted after the app is uninstalled. No reboot occurs
            if the app is not installed.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        if self.is_app_installed(app_name, timeout=timeout):
            data = self.command_output(["uninstall", app_name], timeout=timeout)
            if data.find("Success") == -1:
                self._logger.debug("uninstall_app failed: %s" % data)
                raise ADBError(f"uninstall failed for {app_name}. Got: {data}")
            self.run_as_package = None
            if reboot:
                self.reboot(timeout=timeout)

    def update_app(self, apk_path, timeout=None):
        """Updates an app on the device and reboots.

        :param str apk_path: The apk file name to be
            updated.
        :param int timeout: The maximum time in
            seconds for any spawned adb process to complete before
            throwing an ADBTimeoutError.
            This timeout is per adb call. The total time spent
            may exceed this value. If it is not specified, the value
            set in the ADB constructor is used.
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        cmd = ["install", "-r"]
        if self.version >= version_codes.M:
            cmd.append("-g")
        cmd.append(apk_path)
        output = self.command_output(cmd, timeout=timeout)
        self.reboot(timeout=timeout)
        return output

    def enable_notifications(self, package_id):
        """Using pm grant we enable notifications for an app

        :param str package_id: The package_id for the app we are enabling notifications for
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self.shell(f"pm grant {package_id} android.permission.POST_NOTIFICATIONS")

    def disable_notifications(self, package_id):
        """Using pm revoke we disable notifications for an app

        :param str package_id: The package_id for the app we are disabling notifications for
        :raises: :exc:`ADBTimeoutError`
                 :exc:`ADBError`
        """
        self.shell(f"pm revoke {package_id} android.permission.POST_NOTIFICATIONS")
