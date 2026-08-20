# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This module provides functionality for the command-line build tool
# (mach). It is packaged as a module because everything is a library.

import argparse
import io
import logging
import os
import sys
import traceback
from pathlib import Path

from .base import (
    CommandContext,
    FailedCommandError,
    MachError,
    NoCommandError,
    UnknownCommandError,
    UnrecognizedArgumentError,
)
from .config import ConfigSettings
from .dispatcher import CommandAction
from .logging import LoggingManager
from .registrar import Registrar
from .sentry import NoopErrorReporter
from .util import UserError, setenv

SUGGEST_MACH_BUSTED_TEMPLATE = r"""
You can invoke ``./mach busted`` to check if this issue is already on file. If it
isn't, please use ``./mach busted file %s`` to report it. If ``./mach busted`` is
misbehaving, you can also inspect the dependencies of bug 1543241.
""".lstrip()

MACH_ERROR_TEMPLATE = (
    r"""
The error occurred in mach itself. This is likely a bug in mach itself or a
fundamental problem with a loaded module.

""".lstrip()
    + SUGGEST_MACH_BUSTED_TEMPLATE
)

ERROR_FOOTER = r"""
If filing a bug, please include the full output of mach, including this error
message.

The details of the failure are as follows:
""".lstrip()

USER_ERROR = r"""
This is a user error and does not appear to be a bug in mach.
""".lstrip()

COMMAND_ERROR_TEMPLATE = (
    r"""
The error occurred in the implementation of the invoked mach command.

This should never occur and is likely a bug in the implementation of that
command.
""".lstrip()
    + SUGGEST_MACH_BUSTED_TEMPLATE
)

MODULE_ERROR_TEMPLATE = (
    r"""
The error occurred in code that was called by the mach command. This is either
a bug in the called code itself or in the way that mach is calling it.
""".lstrip()
    + SUGGEST_MACH_BUSTED_TEMPLATE
)

NO_COMMAND_ERROR = r"""
It looks like you tried to run mach without a command.

Run ``mach help`` to show a list of commands.
""".lstrip()

UNKNOWN_COMMAND_ERROR = r"""
It looks like you are trying to %s an unknown mach command: %s
%s
Run ``mach help`` to show a list of commands.
""".lstrip()

SUGGESTED_COMMANDS_MESSAGE = r"""
Did you want to %s any of these commands instead: %s?
"""

UNRECOGNIZED_ARGUMENT_ERROR = r"""
It looks like you passed an unrecognized argument into mach.

The %s command does not accept the arguments: %s
""".lstrip()


class ArgumentParser(argparse.ArgumentParser):
    """Custom implementation argument parser to make things look pretty."""

    def error(self, message):
        """Custom error reporter to give more helpful text on bad commands."""
        if not message.startswith("argument command: invalid choice"):
            argparse.ArgumentParser.error(self, message)
            assert False

        print("Invalid command specified. The list of commands is below.\n")
        self.print_help()
        sys.exit(1)

    def format_help(self):
        text = argparse.ArgumentParser.format_help(self)

        # Strip out the silly command list that would preceed the pretty list.
        #
        # Commands:
        #   {foo,bar}
        #     foo  Do foo.
        #     bar  Do bar.
        search = "Commands:\n  {"
        start = text.find(search)

        if start != -1:
            end = text.find("}\n", start)
            assert end != -1

            real_start = start + len("Commands:\n")
            real_end = end + len("}\n")

            text = text[0:real_start] + text[real_end:]

        return text


class ContextWrapper:
    def __init__(self, context, handler):
        object.__setattr__(self, "_context", context)
        object.__setattr__(self, "_handler", handler)

    def __getattribute__(self, key):
        try:
            return getattr(object.__getattribute__(self, "_context"), key)
        except AttributeError as e:
            try:
                ret = object.__getattribute__(self, "_handler")(key)
            except (AttributeError, TypeError):
                # TypeError is in case the handler comes from old code not
                # taking a key argument.
                raise e
            setattr(self, key, ret)
            return ret

    def __setattr__(self, key, value):
        setattr(object.__getattribute__(self, "_context"), key, value)


class Mach:
    """Main mach driver type.

    This type is responsible for holding global mach state and dispatching
    a command from arguments.

    The following attributes may be assigned to the instance to influence
    behavior:

        populate_context_handler -- If defined, it must be a callable. The
            callable signature is the following:
            populate_context_handler(key=None)
            It acts as a fallback getter for the mach.base.CommandContext
            instance.
            This allows to augment the context instance with arbitrary data
            for use in command handlers.

        require_conditions -- If True, commands that do not have any condition
            functions applied will be skipped. Defaults to False.

        settings_paths -- A list of files or directories in which to search
            for settings files to load.

    """

    USAGE = """%(prog)s [global arguments] command [command arguments]

mach (German for "do") is the main interface to the Mozilla build system and
common developer tasks.

You tell mach the command you want to perform and it does it for you.

Some common commands are:

    %(prog)s build     Build/compile the source tree.
    %(prog)s help      Show full help, including the list of all commands.

To see more help for a specific command, run:

  %(prog)s help <command>
"""

    def __init__(self, cwd: str):
        assert Path(cwd).is_dir()

        self.cwd = cwd
        self.log_manager = LoggingManager()
        self.logger = logging.getLogger(__name__)
        self.settings = ConfigSettings()
        self.settings_paths = []
        self.settings_loaded = False
        self.command_site_manager = None
        self._telemetry_future = None

        if "MACHRC" in os.environ:
            self.settings_paths.append(os.environ["MACHRC"])

        self.log_manager.register_structured_logger(self.logger)
        self.populate_context_handler = None

    def define_category(self, name, title, description, priority=50):
        """Provide a description for a named command category."""

        Registrar.register_category(name, title, description, priority)

    @property
    def require_conditions(self):
        return Registrar.require_conditions

    @require_conditions.setter
    def require_conditions(self, value):
        Registrar.require_conditions = value

    def run(self, argv, stdin=None, stdout=None, stderr=None):
        """Runs mach with arguments provided from the command line.

        Returns the integer exit code that should be used. 0 means success. All
        other values indicate failure.
        """
        sentry = NoopErrorReporter()

        stdin = sys.stdin if stdin is None else stdin
        stdout = sys.stdout if stdout is None else stdout
        stderr = sys.stderr if stderr is None else stderr

        orig_stdin = sys.stdin
        orig_stdout = sys.stdout
        orig_stderr = sys.stderr

        sys.stdin = stdin
        sys.stdout = stdout
        sys.stderr = stderr

        for fh in (sys.stdout, sys.stderr):
            if isinstance(fh, io.TextIOWrapper):
                fh.reconfigure(encoding="utf-8", errors="replace")

        orig_env = dict(os.environ)

        try:
            self.load_settings()

            # Allow invoked processes (which may not have a handle on the
            # original stdout file descriptor) to know if the original stdout
            # is a TTY. This provides a mechanism to allow said processes to
            # enable emitting code codes, for example.
            if os.isatty(orig_stdout.fileno()):
                setenv("MACH_STDOUT_ISATTY", "1")

            return self._run(argv)
        except KeyboardInterrupt:
            print("mach interrupted by signal or user action. Stopping.")
            return 1

        except Exception:
            # _run swallows exceptions in invoked handlers and converts them to
            # a proper exit code. So, the only scenario where we should get an
            # exception here is if _run itself raises. If _run raises, that's a
            # bug in mach (or a loaded command module being silly) and thus
            # should be reported differently.
            self._print_error_header(argv, sys.stdout)
            print(MACH_ERROR_TEMPLATE % "general")

            exc_type, exc_value, exc_tb = sys.exc_info()
            stack = traceback.extract_tb(exc_tb)

            sentry_event_id = sentry.report_exception(exc_value)
            self._print_exception(
                sys.stdout, exc_type, exc_value, stack, sentry_event_id=sentry_event_id
            )

            return 1

        finally:
            os.environ.clear()
            os.environ.update(orig_env)

            sys.stdin = orig_stdin
            sys.stdout = orig_stdout
            sys.stderr = orig_stderr

    def _run(self, argv):
        if self.populate_context_handler:
            topsrcdir = Path(self.populate_context_handler("topdir"))
            from .sentry import register_sentry

            sentry = register_sentry(argv, self.settings, topsrcdir)
        else:
            sentry = NoopErrorReporter()

        context = CommandContext(
            cwd=self.cwd,
            settings=self.settings,
            log_manager=self.log_manager,
            commands=Registrar,
        )

        if self.populate_context_handler:
            context = ContextWrapper(context, self.populate_context_handler)

        context._telemetry_init_done = None
        context._telemetry_start_time_ns = None

        parser = get_argument_parser(context)
        context.global_parser = parser

        if not len(argv):
            # We don't register the usage until here because if it is globally
            # registered, argparse always prints it. This is not desired when
            # running with --help.
            parser.usage = Mach.USAGE
            parser.print_usage()
            return 0

        try:
            args = parser.parse_args(argv)
        except NoCommandError:
            print(NO_COMMAND_ERROR)
            return 1
        except UnknownCommandError as e:
            suggestion_message = (
                SUGGESTED_COMMANDS_MESSAGE % (e.verb, ", ".join(e.suggested_commands))
                if e.suggested_commands
                else ""
            )
            print(UNKNOWN_COMMAND_ERROR % (e.verb, e.command, suggestion_message))
            return 1
        except UnrecognizedArgumentError as e:
            print(UNRECOGNIZED_ARGUMENT_ERROR % (e.command, " ".join(e.arguments)))
            return 1

        if not hasattr(args, "mach_handler"):
            raise MachError("ArgumentParser result missing mach handler info.")

        context.is_interactive = (
            args.is_interactive
            and sys.__stdout__.isatty()
            and sys.__stderr__.isatty()
            and not os.environ.get("MOZ_AUTOMATION", None)
        )
        handler = getattr(args, "mach_handler")

        import time

        from .telemetry_interface import NoopTelemetry

        context.telemetry = NoopTelemetry(False)
        context._telemetry_start_time_ns = time.monotonic_ns()

        # Glean SDK initialization was started early in mach_initialize.py
        # via driver._telemetry_future. Retrieve the result here (non-blocking
        # if already complete) and report invocation metrics.
        if self._telemetry_future is not None:
            import threading

            context._telemetry_init_done = threading.Event()

            def _finish_telemetry_init(future):
                from .telemetry import report_invocation_metrics

                context.telemetry = future.result()
                report_invocation_metrics(context.telemetry, handler.name)
                context._telemetry_init_done.set()

            self._telemetry_future.add_done_callback(_finish_telemetry_init)

        # Add JSON logging to a file if requested.
        if args.logfile:
            self.log_manager.add_json_handler(args.logfile)

        # Up the logging level if requested.
        log_level = logging.INFO
        if args.verbose:
            log_level = logging.DEBUG
        elif args.quiet:
            log_level = logging.ERROR

        self.log_manager.register_structured_logger(logging.getLogger("mach"))

        write_times = True
        if (
            args.log_no_times
            or "MACH_NO_WRITE_TIMES" in os.environ
            or "MOZ_AUTOMATION" in os.environ
        ):
            write_times = False

        # Always enable terminal logging. The log manager figures out if we are
        # actually in a TTY or are a pipe and does the right thing.
        self.log_manager.add_terminal_logging(
            level=log_level, write_interval=args.log_interval, write_times=write_times
        )

        if args.settings_file:
            # Argument parsing has already happened, so settings that apply
            # to command line handling (e.g alias, defaults) will be ignored.
            self.load_settings_by_file([Path(args.settings_file)])

        try:
            return Registrar._run_command_handler(
                handler,
                context,
                self.command_site_manager,
                debug_command=args.debug_command,
                profile_command=args.profile_command,
                **vars(args.command_args),
            )
        except KeyboardInterrupt as ki:
            raise ki
        except FailedCommandError as e:
            print(e)
            return e.exit_code
        except UserError:
            # We explicitly don't report UserErrors to Sentry.
            exc_type, exc_value, exc_tb = sys.exc_info()
            # The first two frames are us and are never used.
            stack = traceback.extract_tb(exc_tb)[2:]
            self._print_error_header(argv, sys.stdout)
            print(USER_ERROR)
            self._print_exception(sys.stdout, exc_type, exc_value, stack)
            return 1
        except Exception:
            exc_type, exc_value, exc_tb = sys.exc_info()
            sentry_event_id = sentry.report_exception(exc_value)

            # The first two frames are us and are never used.
            stack = traceback.extract_tb(exc_tb)[2:]

            # If we have nothing on the stack, the exception was raised as part
            # of calling the @Command method itself. This likely means a
            # mismatch between @CommandArgument and arguments to the method.
            # e.g. there exists a @CommandArgument without the corresponding
            # argument on the method. We handle that here until the module
            # loader grows the ability to validate better.
            if not len(stack):
                print(COMMAND_ERROR_TEMPLATE % handler.name)
                self._print_exception(
                    sys.stdout,
                    exc_type,
                    exc_value,
                    traceback.extract_tb(exc_tb),
                    sentry_event_id=sentry_event_id,
                )
                return 1

            # Split the frames into those from the module containing the
            # command and everything else.
            command_frames = []
            other_frames = []

            initial_file = stack[0][0]

            for frame in stack:
                if frame[0] == initial_file:
                    command_frames.append(frame)
                else:
                    other_frames.append(frame)

            # If the exception was in the module providing the command, it's
            # likely the bug is in the mach command module, not something else.
            # If there are other frames, the bug is likely not the mach
            # command's fault.
            self._print_error_header(argv, sys.stdout)

            if other_frames:
                print(MODULE_ERROR_TEMPLATE % handler.name)
            else:
                print(COMMAND_ERROR_TEMPLATE % handler.name)

            self._print_exception(
                sys.stdout, exc_type, exc_value, stack, sentry_event_id=sentry_event_id
            )

            return 1
        finally:
            if context._telemetry_init_done is not None:
                context._telemetry_init_done.wait()

    def log(self, level, action, params, format_str):
        """Helper method to record a structured log event."""
        self.logger.log(level, format_str, extra={"action": action, "params": params})

    def _print_error_header(self, argv, fh):
        fh.write("Error running mach:\n\n")
        fh.write("    ")
        fh.write("mach " + " ".join(argv))
        fh.write("\n\n")

    def _print_exception(self, fh, exc_type, exc_value, stack, sentry_event_id=None):
        fh.write(ERROR_FOOTER)
        fh.write("\n")

        for l in traceback.format_exception(
            exc_type, exc_value, exc_value.__traceback__
        ):
            fh.write(l)

        if not sentry_event_id:
            return

        fh.write(f"\nSentry event ID: {sentry_event_id}\n")

    def load_settings(self):
        if not self.settings_loaded:
            import mach.settings  # noqa need @SettingsProvider hook to execute

            for provider in Registrar.settings_providers:
                self.settings.register_provider(provider)

            setting_paths_to_pass = [Path(path) for path in self.settings_paths]
            self.load_settings_by_file(setting_paths_to_pass)
            self.settings_loaded = True

    def load_settings_by_file(self, paths: list[Path]):
        """Load the specified settings files.

        If a directory is specified, the following basenames will be
        searched for in this order:

            machrc, .machrc
        """
        from mach.util import MACHRC_NAMES

        def find_in_dir(base: Path):
            if base.is_file():
                return base

            for name in MACHRC_NAMES:
                path = base / name
                if path.is_file():
                    return path

        files = map(find_in_dir, paths)
        files = filter(bool, files)

        self.settings.load_files(list(files))


def get_argument_parser(context=None, action=CommandAction, topsrcdir=None):
    """Returns an argument parser for the command-line interface."""

    parser = ArgumentParser(
        add_help=False,
        usage="%(prog)s [global arguments] command [command arguments]",
    )

    # WARNING!!! If you add a global argument here, also add it to the
    # global argument handling in the top-level `mach` script.
    # Order is important here as it dictates the order the auto-generated
    # help messages are printed.
    global_group = parser.add_argument_group("Global Arguments")

    verbosity = global_group.add_mutually_exclusive_group()
    verbosity.add_argument(
        "-v",
        "--verbose",
        dest="verbose",
        action="store_true",
        default=False,
        help="Print verbose output.",
    )
    verbosity.add_argument(
        "-q",
        "--quiet",
        dest="quiet",
        action="store_true",
        default=False,
        help="Don't print as much output.",
    )
    global_group.add_argument(
        "-l",
        "--log-file",
        dest="logfile",
        metavar="FILENAME",
        type=argparse.FileType("a"),
        help="Filename to write log data to.",
    )
    global_group.add_argument(
        "--log-interval",
        dest="log_interval",
        action="store_true",
        default=False,
        help="Prefix log line with interval from last message rather "
        "than relative time. Note that this is NOT execution time "
        "if there are parallel operations.",
    )
    global_group.add_argument(
        "--no-interactive",
        dest="is_interactive",
        action="store_false",
        help="Automatically selects the default option on any "
        "interactive prompts. If the output is not a terminal, "
        "then --no-interactive is assumed.",
    )
    suppress_log_by_default = False
    if "INSIDE_EMACS" in os.environ:
        suppress_log_by_default = True
    global_group.add_argument(
        "--log-no-times",
        dest="log_no_times",
        action="store_true",
        default=suppress_log_by_default,
        help="Do not prefix log lines with times. By default, "
        "mach will prefix each output line with the time since "
        "command start.",
    )
    global_group.add_argument(
        "-h",
        "--help",
        dest="help",
        action="store_true",
        default=False,
        help="Show this help message.",
    )
    global_group.add_argument(
        "--debug-command",
        action="store_true",
        help="Start a Python debugger when command is dispatched.",
    )
    global_group.add_argument(
        "--profile-command",
        action="store_true",
        help="Capture a Python profile of the mach process as command is dispatched.",
    )
    global_group.add_argument(
        "--settings",
        dest="settings_file",
        metavar="FILENAME",
        default=None,
        help="Path to settings file.",
    )

    if context:
        # We need to be last because CommandAction swallows all remaining
        # arguments and argparse parses arguments in the order they were added.
        parser.add_argument(
            "command", action=CommandAction, registrar=Registrar, context=context
        )
    else:
        parser.add_argument("command", topsrcdir=topsrcdir, action=action)

    return parser
