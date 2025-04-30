# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import traceback
import zipfile
from collections import namedtuple
from urllib.request import urlopen

import mozfile
import mozinfo
import mozlog
import six
from redo import retriable

__all__ = [
    "check_for_crashes",
    "check_for_java_exception",
    "kill_and_get_minidump",
    "log_crashes",
    "cleanup_pending_crash_reports",
]


StackInfo = namedtuple(
    "StackInfo",
    [
        "minidump_path",
        "signature",
        "stackwalk_stdout",
        "stackwalk_stderr",
        "stackwalk_retcode",
        "stackwalk_errors",
        "extra",
        "process_type",
        "pid",
        "reason",
        "java_stack",
    ],
)


def get_logger():
    structured_logger = mozlog.get_default_logger("mozcrash")
    if structured_logger is None:
        return mozlog.unstructured.getLogger("mozcrash")
    return structured_logger


def check_for_crashes(
    dump_directory,
    symbols_path=None,
    stackwalk_binary=None,
    dump_save_path=None,
    test_name=None,
    quiet=False,
    keep=False,
):
    """
    Print a stack trace for minidump files left behind by a crashing program.

    `dump_directory` will be searched for minidump files. Any minidump files found will
    have `stackwalk_binary` executed on them, with `symbols_path` passed as an extra
    argument.

    `stackwalk_binary` should be a path to the minidump-stackwalk binary.
    If `stackwalk_binary` is not set, the MINIDUMP_STACKWALK environment variable
    will be checked and its value used if it is not empty. If neither is set, then
    ~/.mozbuild/minidump-stackwalk/minidump-stackwalk will be used.

    `symbols_path` should be a path to a directory containing symbols to use for
    dump processing. This can either be a path to a directory containing Breakpad-format
    symbols, or a URL to a zip file containing a set of symbols.

    If `dump_save_path` is set, it should be a path to a directory in which to copy minidump
    files for safekeeping after a stack trace has been printed. If not set, the environment
    variable MINIDUMP_SAVE_PATH will be checked and its value used if it is not empty.

    If `test_name` is set it will be used as the test name in log output. If not set the
    filename of the calling function will be used.

    If `quiet` is set, no PROCESS-CRASH message will be printed to stdout if a
    crash is detected.

    If `keep` is set, minidump files will not be removed after processing.

    Returns number of minidump files found.
    """

    # try to get the caller's filename if no test name is given
    if test_name is None:
        try:
            test_name = os.path.basename(sys._getframe(1).f_code.co_filename)
        except Exception:
            test_name = "unknown"

    if not quiet:
        print("mozcrash checking %s for minidumps..." % dump_directory)

    crash_info = CrashInfo(
        dump_directory,
        symbols_path,
        dump_save_path=dump_save_path,
        stackwalk_binary=stackwalk_binary,
        keep=keep,
    )

    crash_count = 0
    for info in crash_info:
        crash_count += 1
        output = None
        if info.java_stack:
            output = f"PROCESS-CRASH | {test_name} | {info.java_stack}"
        elif not quiet:
            stackwalk_output = [f"Crash dump filename: {info.minidump_path}"]
            stackwalk_output.append(f"Process type: {info.process_type}")
            stackwalk_output.append("Process pid: {}".format(info.pid or "unknown"))
            if info.reason:
                stackwalk_output.append("Mozilla crash reason: %s" % info.reason)
            if info.stackwalk_stderr:
                stackwalk_output.append("stderr from minidump-stackwalk:")
                stackwalk_output.append(info.stackwalk_stderr)
            elif info.stackwalk_stdout is not None:
                stackwalk_output.append(info.stackwalk_stdout)
            if info.stackwalk_retcode is not None and info.stackwalk_retcode != 0:
                stackwalk_output.append(
                    f"minidump-stackwalk exited with return code {info.stackwalk_retcode}"
                )
            signature = info.signature if info.signature else "unknown top frame"

            output = "PROCESS-CRASH | {reason} [{sig}] | {name}\n{out}\n{err}".format(
                reason=info.reason,
                name=test_name,
                sig=signature,
                out="\n".join(stackwalk_output),
                err="\n".join(info.stackwalk_errors),
            )
        if output is not None:
            if six.PY2 and sys.stdout.encoding != "UTF-8":
                output = output.encode("utf-8")
            print(output)

    return crash_count


def log_crashes(
    logger,
    dump_directory,
    symbols_path,
    process=None,
    test=None,
    stackwalk_binary=None,
    dump_save_path=None,
    quiet=False,
):
    """Log crashes using a structured logger"""
    crash_count = 0
    for info in CrashInfo(
        dump_directory,
        symbols_path,
        dump_save_path=dump_save_path,
        stackwalk_binary=stackwalk_binary,
    ):
        crash_count += 1
        if not quiet:
            kwargs = info._asdict()
            kwargs.pop("extra")
            logger.crash(process=process, test=test, **kwargs)
    return crash_count


# Function signatures of abort functions which should be ignored when
# determining the appropriate frame for the crash signature.
ABORT_SIGNATURES = (
    "Abort(char const*)",
    "RustMozCrash",
    "NS_DebugBreak",
    # This signature is part of Rust panic stacks on some platforms. On
    # others, it includes a template parameter containing "core::panic::" and
    # is automatically filtered out by that pattern.
    "core::ops::function::Fn::call",
    "gkrust_shared::panic_hook",
    "mozglue_static::panic_hook",
    "intentional_panic",
    "mozalloc_abort",
    "mozalloc_abort(char const* const)",
    "static void Abort(const char *)",
    "std::sys_common::backtrace::__rust_end_short_backtrace",
    "rust_begin_unwind",
    # This started showing up when we enabled dumping inlined functions
    "MOZ_Crash(char const*, int, char const*)",
    # This also appears as an inlined function after bug 1858670
    "MOZ_CrashSequence(void*, long)",
    "<alloc::boxed::Box<F,A> as core::ops::function::Fn<Args>>::call",
)

# Similar to above, but matches if the substring appears anywhere in the
# frame's signature.
ABORT_SUBSTRINGS = (
    # On some platforms, Rust panic frames unfortunately appear without the
    # std::panicking or core::panic namespaces.
    "_panic_",
    "core::panic::",
    "core::panicking::",
    "core::result::unwrap_failed",
    "std::panicking::",
)


class CrashInfo:
    """Get information about a crash based on dump files.

    Typical usage is to iterate over the CrashInfo object. This returns StackInfo
    objects, one for each crash dump file that is found in the dump_directory.

    :param dump_directory: Path to search for minidump files
    :param symbols_path: Path to a path to a directory containing symbols to use for
                         dump processing. This can either be a path to a directory
                         containing Breakpad-format symbols, or a URL to a zip file
                         containing a set of symbols.
    :param dump_save_path: Path to which to save the dump files. If this is None,
                           the MINIDUMP_SAVE_PATH environment variable will be used.
    :param stackwalk_binary: Path to the minidump-stackwalk binary. If this is None,
                             the MINIDUMP_STACKWALK environment variable will be used
                             as the path to the minidump binary. If neither is set,
                             then ~/.mozbuild/minidump-stackwalk/minidump-stackwalk
                             will be used."""

    def __init__(
        self,
        dump_directory,
        symbols_path,
        dump_save_path=None,
        stackwalk_binary=None,
        keep=False,
    ):
        self.dump_directory = dump_directory
        self.symbols_path = symbols_path
        self.remove_symbols = False
        self.brief_output = False
        self.keep = keep

        if dump_save_path is None:
            dump_save_path = os.environ.get("MINIDUMP_SAVE_PATH", None)
        self.dump_save_path = dump_save_path

        if stackwalk_binary is None:
            stackwalk_binary = os.environ.get("MINIDUMP_STACKWALK", None)
        if stackwalk_binary is None:
            # Location of minidump-stackwalk installed by "mach bootstrap".
            executable_name = "minidump-stackwalk"
            state_dir = os.environ.get(
                "MOZBUILD_STATE_PATH",
                os.path.expanduser(os.path.join("~", ".mozbuild")),
            )
            stackwalk_binary = os.path.join(state_dir, executable_name, executable_name)
            if mozinfo.isWin and not stackwalk_binary.endswith(".exe"):
                stackwalk_binary += ".exe"
            if os.path.exists(stackwalk_binary):
                # If we reach this point, then we're almost certainly
                # running on a local user's machine. Full minidump-stackwalk
                # output is a bit noisy and verbose for that use-case,
                # so we should use the --brief output.
                self.brief_output = True

        self.stackwalk_binary = stackwalk_binary

        self.logger = get_logger()
        self._dump_files = None

    @retriable(attempts=5, sleeptime=5, sleepscale=2)
    def _get_symbols(self):
        if not self.symbols_path:
            self.logger.warning(
                "No local symbols_path provided, only http symbols will be used."
            )

        # This updates self.symbols_path so we only download once.
        if mozfile.is_url(self.symbols_path):
            self.remove_symbols = True
            self.logger.info("Downloading symbols from: %s" % self.symbols_path)
            # Get the symbols and write them to a temporary zipfile
            data = urlopen(self.symbols_path)
            with tempfile.TemporaryFile() as symbols_file:
                symbols_file.write(data.read())
                # extract symbols to a temporary directory (which we'll delete after
                # processing all crashes)
                self.symbols_path = tempfile.mkdtemp()
                with zipfile.ZipFile(symbols_file, "r") as zfile:
                    mozfile.extract_zip(zfile, self.symbols_path)

    @property
    def dump_files(self):
        """List of tuple (path_to_dump_file, path_to_extra_file) for each dump
        file in self.dump_directory. The extra files may not exist."""
        if self._dump_files is None:
            paths = [self.dump_directory]
            if mozinfo.isWin:
                # Add the hard-coded paths used for minidumps recorded by
                # Windows Error Reporting in automation
                paths += [
                    "C:\\error-dumps\\",
                    "Z:\\error-dumps\\",
                ]
            self._dump_files = []
            for path in paths:
                self._dump_files += [
                    (minidump_path, os.path.splitext(minidump_path)[0] + ".extra")
                    for minidump_path in reversed(
                        sorted(glob.glob(os.path.join(path, "*.dmp")))
                    )
                ]
            max_dumps = 10
            if len(self._dump_files) > max_dumps:
                self.logger.warning(
                    "Found %d dump files -- limited to %d!"
                    % (len(self._dump_files), max_dumps)
                )
                del self._dump_files[max_dumps:]

        return self._dump_files

    @property
    def has_dumps(self):
        """Boolean indicating whether any crash dump files were found in the
        current directory"""
        return len(self.dump_files) > 0

    def __iter__(self):
        for path, extra in self.dump_files:
            rv = self._process_dump_file(path, extra)
            yield rv

        if self.remove_symbols:
            mozfile.remove(self.symbols_path)

    def _process_dump_file(self, path, extra):
        """Process a single dump file using self.stackwalk_binary, and return a
        tuple containing properties of the crash dump.

        :param path: Path to the minidump file to analyse
        :return: A StackInfo tuple with the fields::
                   minidump_path: Path of the dump file
                   signature: The top frame of the stack trace, or None if it
                              could not be determined.
                   stackwalk_stdout: String of stdout data from stackwalk
                   stackwalk_stderr: String of stderr data from stackwalk or
                                     None if it succeeded
                   stackwalk_retcode: Return code from stackwalk
                   stackwalk_errors: List of errors in human-readable form that prevented
                                     stackwalk being launched.
                   reason: The reason provided by a MOZ_CRASH() invokation (optional)
                   java_stack: The stack trace of a Java exception (optional)
                   process_type: The type of process that crashed
                   pid: The PID of the crashed process
        """
        self._get_symbols()

        errors = []
        signature = None
        out = None
        err = None
        retcode = None
        reason = None
        java_stack = None
        annotations = None
        pid = None
        process_type = "unknown"
        if (
            self.stackwalk_binary
            and os.path.exists(self.stackwalk_binary)
            and os.access(self.stackwalk_binary, os.X_OK)
        ):
            # Now build up the actual command
            command = [self.stackwalk_binary]

            # Fallback to the symbols server for unknown symbols on automation
            # (mostly for system libraries).
            if (
                "MOZ_AUTOMATION" in os.environ
                or "MOZ_STACKWALK_SYMBOLS_SERVER" in os.environ
            ):
                command.append("--symbols-url=https://symbols.mozilla.org/")

            with tempfile.TemporaryDirectory() as json_dir:
                crash_id = os.path.basename(path)[:-4]
                json_output = os.path.join(json_dir, f"{crash_id}.trace")
                # Specify the kind of output
                command.append(f"--cyborg={json_output}")
                if self.brief_output:
                    command.append("--brief")

                # The minidump path and symbols_path values are positional and come last
                # (in practice the CLI parsers are more permissive, but best not to
                # unecessarily play with fire).
                command.append(path)

                if self.symbols_path:
                    command.append(self.symbols_path)

                self.logger.info("Copy/paste: {}".format(" ".join(command)))
                # run minidump-stackwalk
                p = subprocess.Popen(
                    command, stdout=subprocess.PIPE, stderr=subprocess.PIPE
                )
                (out, err) = p.communicate()
                retcode = p.returncode
                if six.PY3:
                    out = six.ensure_str(out)
                    err = six.ensure_str(err)

                if retcode == 0:
                    processed_crash = self._process_json_output(json_output)
                    signature = processed_crash.get("signature")
                    pid = processed_crash.get("pid")

        elif not self.stackwalk_binary:
            errors.append(
                "MINIDUMP_STACKWALK not set, can't process dump. Either set "
                "MINIDUMP_STACKWALK or use mach bootstrap --no-system-changes "
                "to install minidump-stackwalk."
            )
        elif self.stackwalk_binary and not os.path.exists(self.stackwalk_binary):
            errors.append(
                "MINIDUMP_STACKWALK binary not found: %s. Use mach bootstrap "
                "--no-system-changes to install minidump-stackwalk."
                % self.stackwalk_binary
            )
        elif not os.access(self.stackwalk_binary, os.X_OK):
            errors.append("This user cannot execute the MINIDUMP_STACKWALK binary.")

        if os.path.exists(extra):
            annotations = self._parse_extra_file(extra)

            if annotations:
                reason = annotations.get("MozCrashReason")
                java_stack = annotations.get("JavaStackTrace")
                process_type = annotations.get("ProcessType") or "main"

        if self.dump_save_path:
            self._save_dump_file(path, extra)

        if os.path.exists(path) and not self.keep:
            mozfile.remove(path)
        if os.path.exists(extra) and not self.keep:
            mozfile.remove(extra)

        return StackInfo(
            path,
            signature,
            out,
            err,
            retcode,
            errors,
            extra,
            process_type,
            pid,
            reason,
            java_stack,
        )

    def _process_json_output(self, json_path):
        signature = None
        pid = None

        try:
            json_file = open(json_path)
            crash_json = json.load(json_file)
            json_file.close()

            signature = self._generate_signature(crash_json)
            pid = crash_json.get("pid")

        except Exception as e:
            traceback.print_exc()
            signature = f"an error occurred while processing JSON output: {e}"

        return {
            "pid": pid,
            "signature": signature,
        }

    def _generate_signature(self, crash_json):
        signature = None

        try:
            crashing_thread = crash_json.get("crashing_thread") or {}
            frames = crashing_thread.get("frames") or []

            flattened_frames = []
            for frame in frames:
                for inline in frame.get("inlines") or []:
                    flattened_frames.append(inline.get("function"))

                flattened_frames.append(
                    frame.get("function")
                    or "{} + {}".format(frame.get("module"), frame.get("module_offset"))
                )

            for func in flattened_frames:
                if not func:
                    continue

                signature = "@ %s" % func

                if not (
                    func in ABORT_SIGNATURES
                    or any(pat in func for pat in ABORT_SUBSTRINGS)
                ):
                    break
        except Exception as e:
            traceback.print_exc()
            signature = f"an error occurred while generating the signature: {e}"

        # Strip parameters from signature
        if signature:
            pmatch = re.search(r"(.*)\(.*\)", signature)
            if pmatch:
                signature = pmatch.group(1)

        return signature

    def _parse_extra_file(self, path):
        with open(path) as file:
            try:
                return json.load(file)
            except ValueError:
                self.logger.warning(".extra file does not contain proper json")
                return None

    def _save_dump_file(self, path, extra):
        if os.path.isfile(self.dump_save_path):
            os.unlink(self.dump_save_path)
        if not os.path.isdir(self.dump_save_path):
            try:
                os.makedirs(self.dump_save_path)
            except OSError:
                pass

        shutil.copy(path, self.dump_save_path)
        self.logger.info(
            f"Saved minidump as {os.path.join(self.dump_save_path, os.path.basename(path))}"
        )

        if os.path.isfile(extra):
            shutil.copy(extra, self.dump_save_path)
            self.logger.info(
                f"Saved app info as {os.path.join(self.dump_save_path, os.path.basename(extra))}"
            )


def check_for_java_exception(logcat, test_name=None, quiet=False):
    """
    Print a summary of a fatal Java exception, if present in the provided
    logcat output.

    Today, exceptions in geckoview are usually noted in the minidump .extra file, allowing
    java exceptions to be reported by the "normal" minidump processing, like log_crashes();
    therefore, this function may be extraneous (but maintained for now, while exception
    handling is evolving).

    Example:
    PROCESS-CRASH | <test-name> | java-exception java.lang.NullPointerException at org.mozilla.gecko.GeckoApp$21.run(GeckoApp.java:1833) # noqa

    `logcat` should be a list of strings.

    If `test_name` is set it will be used as the test name in log output. If not set the
    filename of the calling function will be used.

    If `quiet` is set, no PROCESS-CRASH message will be printed to stdout if a
    crash is detected.

    Returns True if a fatal Java exception was found, False otherwise.
    """

    # try to get the caller's filename if no test name is given
    if test_name is None:
        try:
            test_name = os.path.basename(sys._getframe(1).f_code.co_filename)
        except Exception:
            test_name = "unknown"

    found_exception = False

    for i, line in enumerate(logcat):
        # Logs will be of form:
        #
        # 01-30 20:15:41.937 E/GeckoAppShell( 1703): >>> REPORTING UNCAUGHT EXCEPTION FROM THREAD 9 ("GeckoBackgroundThread") # noqa
        # 01-30 20:15:41.937 E/GeckoAppShell( 1703): java.lang.NullPointerException
        # 01-30 20:15:41.937 E/GeckoAppShell( 1703): at org.mozilla.gecko.GeckoApp$21.run(GeckoApp.java:1833) # noqa
        # 01-30 20:15:41.937 E/GeckoAppShell( 1703): at android.os.Handler.handleCallback(Handler.java:587) # noqa
        if "REPORTING UNCAUGHT EXCEPTION" in line:
            # Strip away the date, time, logcat tag and pid from the next two lines and
            # concatenate the remainder to form a concise summary of the exception.
            found_exception = True
            if len(logcat) >= i + 3:
                logre = re.compile(r".*\): \t?(.*)")
                m = logre.search(logcat[i + 1])
                if m and m.group(1):
                    exception_type = m.group(1)
                m = logre.search(logcat[i + 2])
                if m and m.group(1):
                    exception_location = m.group(1)
                if not quiet:
                    output = f"PROCESS-CRASH | {test_name} | java-exception {exception_type} {exception_location}"
                    print(output.encode("utf-8"))
            else:
                print(
                    "Automation Error: java exception in logcat at line "
                    f"{i} of {len(logcat)}: {line}"
                )
            break

    return found_exception


if mozinfo.isWin:
    import ctypes
    import uuid

    kernel32 = ctypes.windll.kernel32
    OpenProcess = kernel32.OpenProcess
    CloseHandle = kernel32.CloseHandle

    def write_minidump(pid, dump_directory, utility_path):
        """
        Write a minidump for a process.

        :param pid: PID of the process to write a minidump for.
        :param dump_directory: Directory in which to write the minidump.
        """
        PROCESS_QUERY_INFORMATION = 0x0400
        PROCESS_VM_READ = 0x0010
        GENERIC_READ = 0x80000000
        GENERIC_WRITE = 0x40000000
        CREATE_ALWAYS = 2
        FILE_ATTRIBUTE_NORMAL = 0x80
        INVALID_HANDLE_VALUE = -1

        log = get_logger()
        file_name = os.path.join(dump_directory, str(uuid.uuid4()) + ".dmp")

        if not os.path.exists(dump_directory):
            # `kernal32.CreateFileW` can fail to create the dmp file if the dump
            # directory was deleted or doesn't exist (error code 3).
            os.makedirs(dump_directory)

        if mozinfo.info["bits"] != ctypes.sizeof(ctypes.c_voidp) * 8 and utility_path:
            # We're not going to be able to write a minidump with ctypes if our
            # python process was compiled for a different architecture than
            # firefox, so we invoke the minidumpwriter utility program.

            minidumpwriter = os.path.normpath(
                os.path.join(utility_path, "minidumpwriter.exe")
            )
            log.info(
                f"Using {minidumpwriter} to write a dump to {file_name} for [{pid}]"
            )
            if not os.path.exists(minidumpwriter):
                log.error(f"minidumpwriter not found in {utility_path}")
                return

            status = subprocess.Popen([minidumpwriter, str(pid), file_name]).wait()
            if status:
                log.error("minidumpwriter exited with status: %d" % status)
            return

        log.info(f"Writing a dump to {file_name} for [{pid}]")

        proc_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, pid)
        if not proc_handle:
            err = kernel32.GetLastError()
            log.warning("unable to get handle for pid %d: %d" % (pid, err))
            return

        if not isinstance(file_name, str):
            # Convert to unicode explicitly so our path will be valid as input
            # to CreateFileW
            file_name = str(file_name, sys.getfilesystemencoding())

        file_handle = kernel32.CreateFileW(
            file_name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            None,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            None,
        )
        if file_handle != INVALID_HANDLE_VALUE:
            if not ctypes.windll.dbghelp.MiniDumpWriteDump(
                proc_handle,
                pid,
                file_handle,
                # Dump type - MiniDumpNormal
                0,
                # Exception parameter
                None,
                # User stream parameter
                None,
                # Callback parameter
                None,
            ):
                err = kernel32.GetLastError()
                log.warning("unable to dump minidump file for pid %d: %d" % (pid, err))
            CloseHandle(file_handle)
        else:
            err = kernel32.GetLastError()
            log.warning("unable to create minidump file for pid %d: %d" % (pid, err))
        CloseHandle(proc_handle)

    def kill_pid(pid):
        """
        Terminate a process with extreme prejudice.

        :param pid: PID of the process to terminate.
        """
        PROCESS_TERMINATE = 0x0001
        SYNCHRONIZE = 0x00100000
        WAIT_OBJECT_0 = 0x0
        WAIT_FAILED = -1
        logger = get_logger()
        handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, 0, pid)
        if handle:
            if kernel32.TerminateProcess(handle, 1):
                # TerminateProcess is async; wait up to 30 seconds for process to
                # actually terminate, then give up so that clients are not kept
                # waiting indefinitely for hung processes.
                status = kernel32.WaitForSingleObject(handle, 30000)
                if status == WAIT_FAILED:
                    err = kernel32.GetLastError()
                    logger.warning(
                        "kill_pid(): wait failed (%d) terminating pid %d: error %d"
                        % (status, pid, err)
                    )
                elif status != WAIT_OBJECT_0:
                    logger.warning(
                        "kill_pid(): wait failed (%d) terminating pid %d"
                        % (status, pid)
                    )
            else:
                err = kernel32.GetLastError()
                logger.warning(
                    "kill_pid(): unable to terminate pid %d: %d" % (pid, err)
                )
            CloseHandle(handle)
        else:
            err = kernel32.GetLastError()
            logger.warning(
                "kill_pid(): unable to get handle for pid %d: %d" % (pid, err)
            )

else:

    def kill_pid(pid):
        """
        Terminate a process with extreme prejudice.

        :param pid: PID of the process to terminate.
        """
        os.kill(pid, signal.SIGKILL)


def kill_and_get_minidump(pid, dump_directory, utility_path=None):
    """
    Attempt to kill a process and leave behind a minidump describing its
    execution state.

    :param pid: The PID of the process to kill.
    :param dump_directory: The directory where a minidump should be written on
    Windows, where the dump will be written from outside the process.

    On Windows a dump will be written using the MiniDumpWriteDump function
    from DbgHelp.dll. On Linux and OS X the process will be sent a SIGABRT
    signal to trigger minidump writing via a Breakpad signal handler. On other
    platforms the process will simply be killed via SIGKILL.

    If the process is hung in such a way that it cannot respond to SIGABRT
    it may still be running after this function returns. In that case it
    is the caller's responsibility to deal with killing it.
    """
    needs_killing = True
    if mozinfo.isWin:
        write_minidump(pid, dump_directory, utility_path)
    elif mozinfo.isLinux or mozinfo.isMac:
        os.kill(pid, signal.SIGABRT)
        needs_killing = False
    if needs_killing:
        kill_pid(pid)


def cleanup_pending_crash_reports():
    """
    Delete any pending crash reports.

    The presence of pending crash reports may be reported by the browser,
    affecting test results; it is best to ensure that these are removed
    before starting any browser tests.

    Firefox stores pending crash reports in "<UAppData>/Crash Reports".
    If the browser is not running, it cannot provide <UAppData>, so this
    code tries to anticipate its value.

    See dom/system/OSFileConstants.cpp for platform variations of <UAppData>.
    """
    if mozinfo.isWin:
        location = os.path.expanduser(
            "~\\AppData\\Roaming\\Mozilla\\Firefox\\Crash Reports"
        )
    elif mozinfo.isMac:
        location = os.path.expanduser(
            "~/Library/Application Support/Firefox/Crash Reports"
        )
    else:
        location = os.path.expanduser("~/.mozilla/firefox/Crash Reports")
    logger = get_logger()
    if os.path.exists(location):
        try:
            mozfile.remove(location)
            logger.info("Removed pending crash reports at '%s'" % location)
        except Exception:
            pass


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--stackwalk-binary", "-b")
    parser.add_argument("--dump-save-path", "-o")
    parser.add_argument("--test-name", "-n")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("dump_directory")
    parser.add_argument("symbols_path")
    args = parser.parse_args()

    check_for_crashes(
        args.dump_directory,
        args.symbols_path,
        stackwalk_binary=args.stackwalk_binary,
        dump_save_path=args.dump_save_path,
        test_name=args.test_name,
        keep=args.keep,
    )
