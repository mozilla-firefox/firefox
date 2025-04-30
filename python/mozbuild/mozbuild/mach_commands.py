# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import errno
import itertools
import json
import logging
import operator
import os
import os.path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from os import path
from pathlib import Path

import mozpack.path as mozpath
from mach.decorators import (
    Command,
    CommandArgument,
    CommandArgumentGroup,
    SubCommand,
)
from mozfile import load_source

from mozbuild.base import (
    BinaryNotFoundException,
    BuildEnvironmentNotFoundException,
    MozbuildObject,
)
from mozbuild.base import MachCommandConditions as conditions
from mozbuild.util import MOZBUILD_METRICS_PATH, ForwardingArgumentParser

here = os.path.abspath(os.path.dirname(__file__))

EXCESSIVE_SWAP_MESSAGE = """
===================
PERFORMANCE WARNING

Your machine experienced a lot of swap activity during the build. This is
possibly a sign that your machine doesn't have enough physical memory or
not enough available memory to perform the build. It's also possible some
other system activity during the build is to blame.

If you feel this message is not appropriate for your machine configuration,
please file a Firefox Build System :: General bug at
https://bugzilla.mozilla.org/enter_bug.cgi?product=Firefox%20Build%20System&component=General
and tell us about your machine and build configuration so we can adjust the
warning heuristic.
===================
"""


class StoreDebugParamsAndWarnAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        sys.stderr.write(
            "The --debugparams argument is deprecated. Please "
            + "use --debugger-args instead.\n\n"
        )
        setattr(namespace, self.dest, values)


@Command(
    "watch",
    category="post-build",
    description="Watch and re-build (parts of) the tree.",
    conditions=[conditions.is_firefox],
    virtualenv_name="watch",
)
@CommandArgument(
    "-v",
    "--verbose",
    action="store_true",
    help="Verbose output for what commands the watcher is running.",
)
def watch(command_context, verbose=False):
    """Watch and re-build (parts of) the source tree."""
    if not conditions.is_artifact_build(command_context):
        print(
            "WARNING: mach watch only rebuilds the `mach build faster` parts of the tree!"
        )

    if not command_context.substs.get("WATCHMAN", None):
        print(
            "mach watch requires watchman to be installed and found at configure time. See "
            "https://developer.mozilla.org/docs/Mozilla/Developer_guide/Build_Instructions/Incremental_builds_with_filesystem_watching"  # noqa
        )
        return 1

    from mozbuild.faster_daemon import Daemon

    daemon = Daemon(command_context.config_environment)

    try:
        return daemon.watch()
    except KeyboardInterrupt:
        # Suppress ugly stack trace when user hits Ctrl-C.
        sys.exit(3)


CARGO_CONFIG_NOT_FOUND_ERROR_MSG = """\
The sub-command {subcommand} is not currently configured to be used with ./mach cargo.
To do so, add the corresponding file in <mozilla-root-dir>/build/cargo, following other examples in this directory"""


def _cargo_config_yaml_schema():
    from voluptuous import All, Boolean, Required, Schema

    def starts_with_cargo(s):
        if s.startswith("cargo-"):
            return s
        else:
            raise ValueError

    return Schema(
        {
            # The name of the command (not checked for now, but maybe
            #  later)
            Required("command"): All(str, starts_with_cargo),
            # Whether `make` should stop immediately in case
            # of error returned by the command. Default: False
            "continue_on_error": Boolean,
            # Whether this command requires pre_export and export build
            # targets to have run. Defaults to bool(cargo_build_flags).
            "requires_export": Boolean,
            # Build flags to use.  If this variable is not
            # defined here, the build flags are generated automatically and are
            # the same as for `cargo build`. See available substitutions at the
            # end.
            "cargo_build_flags": [str],
            # Extra build flags to use. These flags are added
            # after the cargo_build_flags both when they are provided or
            # automatically generated. See available substitutions at the end.
            "cargo_extra_flags": [str],
            # Available substitutions for `cargo_*_flags`:
            # * {arch}: architecture target
            # * {crate}: current crate name
            # * {directory}: Directory of the current crate within the source tree
            # * {features}: Rust features (for `--features`)
            # * {manifest}: full path of `Cargo.toml` file
            # * {target}: `--lib` for library, `--bin CRATE` for executables
            # * {topsrcdir}: Top directory of sources
        }
    )


@Command(
    "cargo",
    category="build",
    description="Run `cargo <cargo_command>` on a given crate.  Defaults to gkrust.",
    metrics_path=MOZBUILD_METRICS_PATH,
)
@CommandArgument(
    "cargo_command",
    default=None,
    help="Target to cargo, must be one of the commands in build/cargo/",
)
@CommandArgument(
    "--all-crates",
    action="store_true",
    help="Check all of the crates in the tree.",
)
@CommandArgument(
    "-p", "--package", default=None, help="The specific crate name to check."
)
@CommandArgument(
    "--jobs",
    "-j",
    default="0",
    nargs="?",
    metavar="jobs",
    type=int,
    help="Run the tests in parallel using multiple processes.",
)
@CommandArgument("-v", "--verbose", action="store_true", help="Verbose output.")
@CommandArgument(
    "--message-format-json",
    action="store_true",
    help="Emit error messages as JSON.",
)
@CommandArgument(
    "--continue-on-error",
    action="store_true",
    help="Do not return an error exit code if the subcommands errors out.",
)
@CommandArgument(
    "subcommand_args",
    nargs=argparse.REMAINDER,
    help="These arguments are passed as-is to the cargo subcommand.",
)
def cargo(
    command_context,
    cargo_command,
    all_crates=None,
    package=None,
    jobs=0,
    verbose=False,
    message_format_json=False,
    continue_on_error=False,
    subcommand_args=[],
):
    import yaml

    from mozbuild.controller.building import BuildDriver

    command_context.log_manager.enable_all_structured_loggers()

    topsrcdir = Path(mozpath.normpath(command_context.topsrcdir))
    cargodir = Path(topsrcdir / "build" / "cargo")

    cargo_command_basename = "cargo-" + cargo_command + ".yaml"
    cargo_command_fullname = Path(cargodir / cargo_command_basename)
    if path.exists(cargo_command_fullname):
        with open(cargo_command_fullname) as fh:
            yaml_config = yaml.load(fh, Loader=yaml.FullLoader)
            schema = _cargo_config_yaml_schema()
            schema(yaml_config)
        if not yaml_config:
            yaml_config = {}
    else:
        print(CARGO_CONFIG_NOT_FOUND_ERROR_MSG.format(subcommand=cargo_command))
        return 1

    # print("yaml_config = ", yaml_config)

    yaml_config.setdefault("continue_on_error", False)
    continue_on_error = continue_on_error or yaml_config["continue_on_error"] is True

    cargo_build_flags = yaml_config.get("cargo_build_flags")
    if cargo_build_flags is not None:
        cargo_build_flags = " ".join(cargo_build_flags)
    cargo_extra_flags = yaml_config.get("cargo_extra_flags")
    if cargo_extra_flags is not None:
        cargo_extra_flags = " ".join(cargo_extra_flags)
    requires_export = yaml_config.get("requires_export", bool(cargo_build_flags))

    ret = 0
    if requires_export:
        # This directory is created during export. If it's not there,
        # export hasn't run already.
        deps = Path(command_context.topobjdir) / ".deps"
        if not deps.exists():
            build = command_context._spawn(BuildDriver)
            ret = build.build(
                command_context.metrics,
                what=["pre-export", "export"],
                jobs=jobs,
                verbose=verbose,
                mach_context=command_context._mach_context,
            )
    else:
        try:
            command_context.config_environment
        except BuildEnvironmentNotFoundException:
            build = command_context._spawn(BuildDriver)
            ret = build.configure(
                command_context.metrics,
                buildstatus_messages=False,
            )
    if ret != 0:
        return ret

    if command_context.substs.get("commtopsrcdir"):
        # Thunderbird uses its own gkrust, located in its own workspace.
        gkrust_path = "comm/rust/gkrust"
        gtest_path = "comm/rust/gtest"
    else:
        gkrust_path = "toolkit/library/rust"
        gtest_path = "toolkit/library/gtest/rust"

    # XXX duplication with `mach vendor rust`
    crates_and_roots = {
        "gkrust": {"directory": gkrust_path, "library": True},
        "gkrust-gtest": {"directory": gtest_path, "library": True},
        "geckodriver": {"directory": "testing/geckodriver", "library": False},
    }

    if all_crates:
        crates = crates_and_roots.keys()
    elif package:
        crates = [package]
    else:
        crates = ["gkrust"]

    if subcommand_args:
        subcommand_args = " ".join(subcommand_args)

    for crate in crates:
        crate_info = crates_and_roots.get(crate, None)
        if not crate_info:
            print(
                "Cannot locate crate %s.  Please check your spelling or "
                "add the crate information to the list." % crate
            )
            return 1

        targets = [
            "force-cargo-library-%s" % cargo_command,
            "force-cargo-host-library-%s" % cargo_command,
            "force-cargo-program-%s" % cargo_command,
            "force-cargo-host-program-%s" % cargo_command,
        ]

        directory = crate_info["directory"]
        # you can use these variables in 'cargo_build_flags'
        subst = {
            "arch": '"$(RUST_TARGET)"',
            "crate": crate,
            "directory": directory,
            "features": '"$(RUST_LIBRARY_FEATURES)"',
            "manifest": str(Path(topsrcdir / directory / "Cargo.toml")),
            "target": "--lib" if crate_info["library"] else "--bin " + crate,
            "topsrcdir": str(topsrcdir),
        }

        if subcommand_args:
            targets = targets + [
                "cargo_extra_cli_flags=%s" % (subcommand_args.format(**subst))
            ]
        if cargo_build_flags:
            targets = targets + [
                "cargo_build_flags=%s" % (cargo_build_flags.format(**subst))
            ]

        append_env = {}
        if cargo_extra_flags:
            append_env["CARGO_EXTRA_FLAGS"] = cargo_extra_flags.format(**subst)
        if message_format_json:
            append_env["USE_CARGO_JSON_MESSAGE_FORMAT"] = "1"
        if continue_on_error:
            append_env["CARGO_CONTINUE_ON_ERROR"] = "1"
        if cargo_build_flags:
            append_env["CARGO_NO_AUTO_ARG"] = "1"
        else:
            append_env["ADD_RUST_LTOABLE"] = (
                f"force-cargo-library-{cargo_command:s} force-cargo-program-{cargo_command:s}"
            )

        ret = command_context._run_make(
            srcdir=False,
            directory=directory,
            ensure_exit_code=0,
            silent=not verbose,
            print_directory=False,
            target=targets,
            num_jobs=jobs,
            append_env=append_env,
        )
        if ret != 0:
            return ret

    return 0


@SubCommand(
    "cargo",
    "vet",
    description="Run `cargo vet`.",
)
@CommandArgument("arguments", nargs=argparse.REMAINDER)
def cargo_vet(command_context, arguments, stdout=None, env=os.environ):
    from mozbuild.bootstrap import bootstrap_toolchain

    # Logging of commands enables logging from `bootstrap_toolchain` that we
    # don't want to expose. Disable them temporarily.
    logger = logging.getLogger("gecko_taskgraph.generator")
    level = logger.getEffectiveLevel()
    logger.setLevel(logging.ERROR)

    env = env.copy()
    cargo_vet = bootstrap_toolchain("cargo-vet")
    if cargo_vet:
        env["PATH"] = os.pathsep.join([cargo_vet, env["PATH"]])
    logger.setLevel(level)
    try:
        cargo = command_context.substs["CARGO"]
    except (BuildEnvironmentNotFoundException, KeyError):
        # Default if this tree isn't configured.
        from mozfile import which

        cargo = which("cargo", path=env["PATH"])
        if not cargo:
            raise OSError(
                errno.ENOENT,
                (
                    "Could not find 'cargo' on your $PATH. "
                    "Hint: have you run `mach build` or `mach configure`?"
                ),
            )

    topsrcdir = Path(command_context.topsrcdir)
    config_toml_in = topsrcdir / ".cargo/config.toml.in"
    cargo_vet_dir = topsrcdir

    try:
        # When run for Thunderbird, configure must run first
        if override_config_toml_in := command_context.substs.get(
            "MOZ_OVERRIDE_CARGO_CONFIG"
        ):
            config_toml_in = Path(override_config_toml_in).absolute()
            cargo_vet_dir = config_toml_in.parent.parent
    except BuildEnvironmentNotFoundException:
        pass

    config_toml = config_toml_in.parent / config_toml_in.stem
    command_context.log(logging.INFO, "cargo-vet", {}, f"[INFO] Using {config_toml}.")

    locked = "--locked" in arguments
    if locked:
        # The use of --locked requires .cargo/config.toml to exist, but other things,
        # like cargo update, don't want it there, so remove it once we're done.
        shutil.copyfile(config_toml_in, config_toml)
    try:
        res = subprocess.run(
            [cargo, "vet"] + arguments,
            cwd=cargo_vet_dir,
            stdout=stdout,
            env=env,
        )
    finally:
        if locked:
            config_toml.unlink()

    # When the function is invoked without stdout set (the default when running
    # as a mach subcommand), exit with the returncode from cargo vet.
    # When the function is invoked with stdout (direct function call), return
    # the full result from subprocess.run.
    return res if stdout else res.returncode


@Command(
    "doctor",
    category="devenv",
    description="Diagnose and fix common development environment issues.",
)
@CommandArgument(
    "--fix",
    default=False,
    action="store_true",
    help="Attempt to fix found problems.",
)
@CommandArgument(
    "--verbose",
    default=False,
    action="store_true",
    help="Print verbose information found by checks.",
)
def doctor(command_context, fix=False, verbose=False):
    """Diagnose common build environment problems"""
    from mozbuild.doctor import run_doctor

    return run_doctor(
        topsrcdir=command_context.topsrcdir,
        topobjdir=command_context.topobjdir,
        configure_args=command_context.mozconfig["configure_args"],
        fix=fix,
        verbose=verbose,
    )


CLOBBER_CHOICES = {"objdir", "python", "gradle"}


@Command(
    "clobber",
    category="build",
    description="Clobber the tree (delete the object directory).",
    no_auto_log=True,
)
@CommandArgument(
    "what",
    default=["objdir", "python"],
    nargs="*",
    help="Target to clobber, must be one of {{{}}} (default "
    "objdir and python).".format(", ".join(CLOBBER_CHOICES)),
)
@CommandArgument("--full", action="store_true", help="Perform a full clobber")
def clobber(command_context, what, full=False):
    """Clean up the source and object directories.

    Performing builds and running various commands generate various files.

    Sometimes it is necessary to clean up these files in order to make
    things work again. This command can be used to perform that cleanup.

    The `objdir` target removes most files in the current object directory
    (where build output is stored). Some files (like Visual Studio project
    files) are not removed by default. If you would like to remove the
    object directory in its entirety, run with `--full`.

    The `python` target will clean up Python's generated files (virtualenvs,
    ".pyc", "__pycache__", etc).

    The `gradle` target will remove the "gradle" subdirectory of the object
    directory.

    By default, the command clobbers the `objdir` and `python` targets.
    """
    what = set(what)
    invalid = what - CLOBBER_CHOICES
    if invalid:
        print(
            "Unknown clobber target(s): {}. Choose from {{{}}}".format(
                ", ".join(invalid), ", ".join(CLOBBER_CHOICES)
            )
        )
        return 1

    ret = 0
    if "objdir" in what:
        from mozbuild.controller.clobber import Clobberer

        try:
            substs = command_context.substs
        except BuildEnvironmentNotFoundException:
            substs = {}

        try:
            Clobberer(
                command_context.topsrcdir, command_context.topobjdir, substs
            ).remove_objdir(full)
        except OSError as e:
            if sys.platform.startswith("win"):
                if isinstance(e, WindowsError) and e.winerror in (5, 32):
                    command_context.log(
                        logging.ERROR,
                        "file_access_error",
                        {"error": e},
                        "Could not clobber because a file was in use. If the "
                        "application is running, try closing it. {error}",
                    )
                    return 1
            raise

    if "python" in what:
        if conditions.is_hg(command_context):
            cmd = [
                "hg",
                "--config",
                "extensions.purge=",
                "purge",
                "--all",
                "-I",
                "glob:**.py[cdo]",
                "-I",
                "glob:**/__pycache__",
            ]
        elif conditions.is_git(command_context):
            cmd = ["git", "clean", "-d", "-f", "-x", "*.py[cdo]", "*/__pycache__/*"]
        else:
            cmd = ["find", ".", "-type", "f", "-name", "*.py[cdo]", "-delete"]
            subprocess.call(cmd, cwd=command_context.topsrcdir)
            cmd = [
                "find",
                ".",
                "-type",
                "d",
                "-name",
                "__pycache__",
                "-empty",
                "-delete",
            ]
        ret = subprocess.call(cmd, cwd=command_context.topsrcdir)

        # We'll keep this around to delete the legacy "_virtualenv" dir folders
        # so that people don't get confused if they see it and try to manipulate
        # it but it has no effect.
        shutil.rmtree(
            mozpath.join(command_context.topobjdir, "_virtualenvs"),
            ignore_errors=True,
        )
        from mach.util import get_virtualenv_base_dir

        virtualenv_dir = Path(get_virtualenv_base_dir(command_context.topsrcdir))

        for specific_venv in virtualenv_dir.iterdir():
            if specific_venv.name == "mach":
                # We can't delete the "mach" virtualenv with clobber
                # since it's the one doing the clobbering. It always
                # has to be removed manually.
                pass
            else:
                shutil.rmtree(specific_venv, ignore_errors=True)

    if "gradle" in what:
        shutil.rmtree(
            mozpath.join(command_context.topobjdir, "gradle"), ignore_errors=True
        )

    return ret


@Command(
    "show-log", category="post-build", description="Display mach logs", no_auto_log=True
)
@CommandArgument(
    "log_file",
    nargs="?",
    type=argparse.FileType("rb"),
    help="Filename to read log data from. Defaults to the log of the last "
    "mach command.",
)
def show_log(command_context, log_file=None):
    """Show mach logs
    If we're in a terminal context, the log is piped to 'less'
    for more convenient viewing.
    (https://man7.org/linux/man-pages/man1/less.1.html)
    """
    if not log_file:
        path = command_context._get_state_filename("last_log.json")
        log_file = open(path, "rb")

    if os.isatty(sys.stdout.fileno()):
        env = dict(os.environ)
        if "LESS" not in env:
            # Sensible default flags if none have been set in the user environment.
            env["LESS"] = "FRX"
        less = subprocess.Popen(
            ["less"], stdin=subprocess.PIPE, env=env, encoding="UTF-8"
        )

        try:
            # Create a new logger handler with the stream being the stdin of our 'less'
            # process so that we can pipe the logger output into 'less'
            less_handler = logging.StreamHandler(stream=less.stdin)
            less_handler.setFormatter(
                command_context.log_manager.terminal_handler.formatter
            )
            less_handler.setLevel(command_context.log_manager.terminal_handler.level)

            # replace the existing terminal handler with the new one for 'less' while
            # still keeping the original one to set back later
            original_handler = command_context.log_manager.replace_terminal_handler(
                less_handler
            )

            # Save this value so we can set it back to the original value later
            original_logging_raise_exceptions = logging.raiseExceptions

            # We need to explicitly disable raising exceptions inside logging so
            # that we can catch them here ourselves to ignore the ones we want
            logging.raiseExceptions = False

            # Parses the log file line by line and streams
            # (to less.stdin) the relevant records we want
            handle_log_file(command_context, log_file)

            # At this point we've piped the entire log file to
            # 'less', so we can close the input stream
            less.stdin.close()

            # Wait for the user to manually terminate `less`
            less.wait()
        except OSError as os_error:
            # (POSIX)   errno.EPIPE: BrokenPipeError: [Errno 32] Broken pipe
            # (Windows) errno.EINVAL: OSError:        [Errno 22] Invalid argument
            if os_error.errno == errno.EPIPE or os_error.errno == errno.EINVAL:
                # If the user manually terminates 'less' before the entire log file
                # is piped (without scrolling close enough to the bottom) we will get
                # one of these errors (depends on the OS) because the logger will still
                # attempt to stream to the now invalid less.stdin. To prevent a bunch
                # of errors being shown after a user terminates 'less', we just catch
                # the first of those exceptions here, and stop parsing the log file.
                pass
            else:
                raise
        except Exception:
            raise
        finally:
            # Ensure these values are changed back to the originals, regardless of outcome
            command_context.log_manager.replace_terminal_handler(original_handler)
            logging.raiseExceptions = original_logging_raise_exceptions
    else:
        # Not in a terminal context, so just handle the log file with the
        # default stream without piping it to a pager (less)
        handle_log_file(command_context, log_file)


def handle_log_file(command_context, log_file):
    start_time = 0
    for line in log_file:
        created, action, params = json.loads(line)
        if not start_time:
            start_time = created
            command_context.log_manager.terminal_handler.formatter.start_time = created
        if "line" in params:
            record = logging.makeLogRecord(
                {
                    "created": created,
                    "name": command_context._logger.name,
                    "levelno": logging.INFO,
                    "msg": "{line}",
                    "params": params,
                    "action": action,
                }
            )
            command_context._logger.handle(record)


# Provide commands for inspecting warnings.


def database_path(command_context):
    return command_context._get_state_filename("warnings.json")


def get_warnings_database(command_context):
    from mozbuild.compilation.warnings import WarningsDatabase

    path = database_path(command_context)

    database = WarningsDatabase()

    if os.path.exists(path):
        database.load_from_file(path)

    return database


@Command(
    "warnings-summary",
    category="post-build",
    description="Show a summary of compiler warnings.",
)
@CommandArgument(
    "-C",
    "--directory",
    default=None,
    help="Change to a subdirectory of the build directory first.",
)
@CommandArgument(
    "report",
    default=None,
    nargs="?",
    help="Warnings report to display. If not defined, show the most recent report.",
)
def summary(command_context, directory=None, report=None):
    database = get_warnings_database(command_context)

    if directory:
        dirpath = join_ensure_dir(command_context.topsrcdir, directory)
        if not dirpath:
            return 1
    else:
        dirpath = None

    type_counts = database.type_counts(dirpath)
    sorted_counts = sorted(type_counts.items(), key=operator.itemgetter(1))

    total = 0
    for k, v in sorted_counts:
        print("%d\t%s" % (v, k))
        total += v

    print("%d\tTotal" % total)


@Command(
    "warnings-list",
    category="post-build",
    description="Show a list of compiler warnings.",
)
@CommandArgument(
    "-C",
    "--directory",
    default=None,
    help="Change to a subdirectory of the build directory first.",
)
@CommandArgument(
    "--flags", default=None, nargs="+", help="Which warnings flags to match."
)
@CommandArgument(
    "report",
    default=None,
    nargs="?",
    help="Warnings report to display. If not defined, show the most recent report.",
)
def list_warnings(command_context, directory=None, flags=None, report=None):
    database = get_warnings_database(command_context)

    by_name = sorted(database.warnings)

    topsrcdir = mozpath.normpath(command_context.topsrcdir)

    if directory:
        directory = mozpath.normsep(directory)
        dirpath = join_ensure_dir(topsrcdir, directory)
        if not dirpath:
            return 1

    if flags:
        # Flatten lists of flags.
        flags = set(itertools.chain(*[flaglist.split(",") for flaglist in flags]))

    for warning in by_name:
        filename = mozpath.normsep(warning["filename"])

        if filename.startswith(topsrcdir):
            filename = filename[len(topsrcdir) + 1 :]

        if directory and not filename.startswith(directory):
            continue

        if flags and warning["flag"] not in flags:
            continue

        if warning["column"] is not None:
            print(
                "%s:%d:%d [%s] %s"
                % (
                    filename,
                    warning["line"],
                    warning["column"],
                    warning["flag"],
                    warning["message"],
                )
            )
        else:
            print(
                "%s:%d [%s] %s"
                % (filename, warning["line"], warning["flag"], warning["message"])
            )


def join_ensure_dir(dir1, dir2):
    dir1 = mozpath.normpath(dir1)
    dir2 = mozpath.normsep(dir2)
    joined_path = mozpath.join(dir1, dir2)
    if os.path.isdir(joined_path):
        return joined_path
    print("Specified directory not found.")
    return None


@Command("gtest", category="testing", description="Run GTest unit tests (C++ tests).")
@CommandArgument(
    "gtest_filter",
    default="*",
    nargs="?",
    metavar="gtest_filter",
    help="test_filter is a ':'-separated list of wildcard patterns "
    "(called the positive patterns), optionally followed by a '-' "
    "and another ':'-separated pattern list (called the negative patterns)."
    "Test names are of the format SUITE.NAME. Use --list-tests to see all.",
)
@CommandArgument("--list-tests", action="store_true", help="list all available tests")
@CommandArgument(
    "--jobs",
    "-j",
    default="1",
    nargs="?",
    metavar="jobs",
    type=int,
    help="Run the tests in parallel using multiple processes.",
)
@CommandArgument(
    "--tbpl-parser",
    "-t",
    action="store_true",
    help="Output test results in a format that can be parsed by TBPL.",
)
@CommandArgument(
    "--shuffle",
    "-s",
    action="store_true",
    help="Randomize the execution order of tests.",
)
@CommandArgument(
    "--enable-webrender",
    action="store_true",
    default=False,
    dest="enable_webrender",
    help="Enable the WebRender compositor in Gecko.",
)
@CommandArgument(
    "--enable-inc-origin-init",
    action="store_true",
    default=False,
    dest="enable_inc_origin_init",
    help="Enable the incremental origin initialization in Gecko.",
)
@CommandArgumentGroup("filter sets")
@CommandArgument(
    "--filter-set",
    default=None,
    dest="filter_set",
    type=str,
    group="filter sets",
    help="Use a predefined gtest filter (this overrides the gtest_filter).",
)
@CommandArgument(
    "--list-filter-sets",
    action="store_true",
    dest="list_filter_sets",
    group="filter sets",
    help="List available predefined gtest filters.",
)
@CommandArgumentGroup("Android")
@CommandArgument(
    "--package",
    default="org.mozilla.geckoview.test_runner",
    group="Android",
    help="Package name of test app.",
)
@CommandArgument(
    "--adbpath", dest="adb_path", group="Android", help="Path to adb binary."
)
@CommandArgument(
    "--deviceSerial",
    dest="device_serial",
    group="Android",
    help="adb serial number of remote device. "
    "Required when more than one device is connected to the host. "
    "Use 'adb devices' to see connected devices.",
)
@CommandArgument(
    "--remoteTestRoot",
    dest="remote_test_root",
    group="Android",
    help="Remote directory to use as test root (eg. /data/local/tmp/test_root).",
)
@CommandArgument(
    "--libxul", dest="libxul_path", group="Android", help="Path to gtest libxul.so."
)
@CommandArgument(
    "--no-install",
    action="store_true",
    default=False,
    group="Android",
    help="Skip the installation of the APK.",
)
@CommandArgumentGroup("debugging")
@CommandArgument(
    "--debug",
    action="store_true",
    group="debugging",
    help="Enable the debugger. Not specifying a --debugger option will result in "
    "the default debugger being used.",
)
@CommandArgument(
    "--debugger",
    default=None,
    type=str,
    group="debugging",
    help="Name of debugger to use.",
)
@CommandArgument(
    "--debugger-args",
    default=None,
    metavar="params",
    type=str,
    group="debugging",
    help="Command-line arguments to pass to the debugger itself; "
    "split as the Bourne shell would.",
)
def gtest(
    command_context,
    shuffle,
    jobs,
    gtest_filter,
    list_tests,
    tbpl_parser,
    enable_webrender,
    enable_inc_origin_init,
    filter_set,
    list_filter_sets,
    package,
    adb_path,
    device_serial,
    remote_test_root,
    libxul_path,
    no_install,
    debug,
    debugger,
    debugger_args,
):
    # We lazy build gtest because it's slow to link
    try:
        command_context.config_environment
    except Exception:
        print("Please run |./mach build| before |./mach gtest|.")
        return 1

    res = command_context._mach_context.commands.dispatch(
        "build", command_context._mach_context, what=["recurse_gtest"]
    )
    if res:
        print("Could not build xul-gtest")
        return res

    if command_context.substs.get("MOZ_WIDGET_TOOLKIT") == "cocoa":
        command_context._run_make(
            directory="browser/app", target="repackage", ensure_exit_code=True
        )

    cwd = os.path.join(command_context.topobjdir, "_tests", "gtest")

    if not os.path.isdir(cwd):
        os.makedirs(cwd)

    if conditions.is_android(command_context):
        if jobs != 1:
            print("--jobs is not supported on Android and will be ignored")
        if enable_inc_origin_init:
            print(
                "--enable-inc-origin-init is not supported on Android and will"
                "be ignored"
            )
        if filter_set:
            print("--filter-set is not supported on Android and will be ignored")
        if list_filter_sets:
            print("--list-filter-sets is not supported on Android and will be ignored")
        if debug or debugger or debugger_args:
            print("--debug options are not supported on Android and will be ignored")
        from mozrunner.devices.android_device import InstallIntent

        return android_gtest(
            command_context,
            cwd,
            shuffle,
            gtest_filter,
            package,
            adb_path,
            device_serial,
            remote_test_root,
            libxul_path,
            InstallIntent.NO if no_install else InstallIntent.YES,
        )

    if (
        package
        or adb_path
        or device_serial
        or remote_test_root
        or libxul_path
        or no_install
    ):
        print("One or more Android-only options will be ignored")

    app_path = command_context.get_binary_path("app")
    args = [app_path, "-unittest", "--gtest_death_test_style=threadsafe"]

    if (
        sys.platform.startswith("win")
        and "MOZ_LAUNCHER_PROCESS" in command_context.defines
    ):
        args.append("--wait-for-browser")

    if list_tests:
        args.append("--gtest_list_tests")

    if debug or debugger or debugger_args:
        args = _prepend_debugger_args(args, debugger, debugger_args)
        if not args:
            return 1

    # Use GTest environment variable to control test execution
    # For details see:
    # https://google.github.io/googletest/advanced.html#running-test-programs-advanced-options
    gtest_env = {"GTEST_FILTER": gtest_filter}

    # Note: we must normalize the path here so that gtest on Windows sees
    # a MOZ_GMP_PATH which has only Windows dir seperators, because
    # nsIFile cannot open the paths with non-Windows dir seperators.
    xre_path = os.path.join(os.path.normpath(command_context.topobjdir), "dist", "bin")
    gtest_env["MOZ_XRE_DIR"] = xre_path
    gtest_env["MOZ_GMP_PATH"] = os.pathsep.join(
        os.path.join(xre_path, p, "1.0") for p in ("gmp-fake", "gmp-fakeopenh264")
    )

    gtest_env["MOZ_RUN_GTEST"] = "True"

    if shuffle:
        gtest_env["GTEST_SHUFFLE"] = "True"

    if tbpl_parser:
        gtest_env["MOZ_TBPL_PARSER"] = "True"

    if enable_webrender:
        gtest_env["MOZ_WEBRENDER"] = "1"
        gtest_env["MOZ_ACCELERATED"] = "1"
    else:
        gtest_env["MOZ_WEBRENDER"] = "0"

    if enable_inc_origin_init:
        gtest_env["MOZ_ENABLE_INC_ORIGIN_INIT"] = "1"
    else:
        gtest_env["MOZ_ENABLE_INC_ORIGIN_INIT"] = "0"

    if filter_set or list_filter_sets:
        filter_sets_mod_path = os.path.join("testing", "gtest", "gtest_filter_sets.py")
        load_source("gtest_filter_sets", filter_sets_mod_path)

        import gtest_filter_sets

        if filter_set:
            gtest_filter_for_filter_set = gtest_filter_sets.get(filter_set)
            if gtest_filter_for_filter_set:
                gtest_env["GTEST_FILTER"] = gtest_filter_for_filter_set
            else:
                print("Unknown filter set.")
                return 1
        else:
            gtest_filter_sets.list()
            return 1

    if jobs == 1:
        return command_context.run_process(
            args=args,
            append_env=gtest_env,
            cwd=cwd,
            ensure_exit_code=False,
            pass_thru=True,
        )

    import functools

    from mozprocess import ProcessHandlerMixin

    def handle_line(job_id, line):
        # Prepend the jobId
        line = "[%d] %s" % (job_id + 1, line.strip())
        command_context.log(logging.INFO, "GTest", {"line": line}, "{line}")

    gtest_env["GTEST_TOTAL_SHARDS"] = str(jobs)
    processes = {}
    for i in range(0, jobs):
        gtest_env["GTEST_SHARD_INDEX"] = str(i)
        processes[i] = ProcessHandlerMixin(
            [app_path, "-unittest"],
            cwd=cwd,
            env=gtest_env,
            processOutputLine=[functools.partial(handle_line, i)],
            universal_newlines=True,
        )
        processes[i].run()

    exit_code = 0
    for process in processes.values():
        status = process.wait()
        if status:
            exit_code = status

    # Clamp error code to 255 to prevent overflowing multiple of
    # 256 into 0
    if exit_code > 255:
        exit_code = 255

    return exit_code


def android_gtest(
    command_context,
    test_dir,
    shuffle,
    gtest_filter,
    package,
    adb_path,
    device_serial,
    remote_test_root,
    libxul_path,
    install,
):
    # setup logging for mozrunner
    from mozlog.commandline import setup_logging

    format_args = {"level": command_context._mach_context.settings["test"]["level"]}
    default_format = command_context._mach_context.settings["test"]["format"]
    setup_logging("mach-gtest", {}, {default_format: sys.stdout}, format_args)

    # ensure that a device is available and test app is installed
    from mozrunner.devices.android_device import get_adb_path, verify_android_device

    verify_android_device(
        command_context, install=install, app=package, device_serial=device_serial
    )

    if not adb_path:
        adb_path = get_adb_path(command_context)
    if not libxul_path:
        libxul_path = os.path.join(
            command_context.topobjdir, "dist", "bin", "gtest", "libxul.so"
        )

    # run gtest via remotegtests.py
    exit_code = 0

    path = os.path.join("testing", "gtest", "remotegtests.py")
    load_source("remotegtests", path)

    import remotegtests

    tester = remotegtests.RemoteGTests()
    if not tester.run_gtest(
        test_dir,
        shuffle,
        gtest_filter,
        package,
        adb_path,
        device_serial,
        remote_test_root,
        libxul_path,
        None,
    ):
        exit_code = 1
    tester.cleanup()

    return exit_code


@Command(
    "package",
    category="post-build",
    description="Package the built product for distribution as an APK, DMG, etc.",
)
@CommandArgument(
    "-v",
    "--verbose",
    action="store_true",
    help="Verbose output for what commands the packaging process is running.",
)
def package(command_context, verbose=False):
    """Package the built product for distribution."""
    ret = command_context._run_make(
        directory=".", target="package", silent=not verbose, ensure_exit_code=False
    )
    if ret == 0:
        command_context.notify("Packaging complete")
        _print_package_name(command_context)
    return ret


def _print_package_name(command_context):
    dist_path = mozpath.join(command_context.topobjdir, "dist")
    package_name_path = mozpath.join(dist_path, "package_name.txt")
    if not os.path.exists(package_name_path):
        return

    with open(package_name_path) as f:
        package_name = f.read().strip()
    package_path = mozpath.join(dist_path, package_name)

    if not os.path.exists(package_path):
        return

    command_context.log(logging.INFO, "package", {}, f"Created package: {package_path}")


def _get_android_install_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--app",
        default="org.mozilla.geckoview_example",
        help="Android package to install (default: org.mozilla.geckoview_example)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Print verbose output when installing.",
    )
    parser.add_argument(
        "--aab",
        action="store_true",
        help="Install as AAB (Android App Bundle)",
    )
    return parser


def setup_install_parser():
    build = MozbuildObject.from_environment(cwd=here)
    if conditions.is_android(build):
        return _get_android_install_parser()
    return argparse.ArgumentParser()


@Command(
    "install",
    category="post-build",
    conditions=[conditions.has_build_or_shell],
    parser=setup_install_parser,
    description="Install the package on the machine (or device in the case of Android).",
)
def install(command_context, **kwargs):
    """Install a package."""
    if conditions.is_android(command_context):
        from mozrunner.devices.android_device import (
            InstallIntent,
            verify_android_device,
        )

        ret = (
            verify_android_device(
                command_context,
                install=InstallIntent.YES,
                **kwargs,
            )
            == 0
        )
    elif conditions.is_jsshell(command_context) and conditions.is_android_cpu(
        command_context
    ):
        # Push a shell build to the phone.
        # It would be nice to just use `ADBDevice``, but what's nice about the
        # mozrunner android stuff is that it nicely will handle starting an emulator for you and
        # doing the work there
        from mozrunner.devices.android_device import get_android_device

        [device, device_serial] = get_android_device(command_context)

        if not device:
            command_context.log(
                logging.INFO, "install", {}, "No device found, aborting install"
            )
            return 1

        command_context.log(
            logging.INFO,
            "install",
            {},
            "Installing shell binary and support libarary to /data/local/tmp",
        )

        # The path to the binary and libraries
        distpath = command_context.topobjdir + "/dist/bin/"

        # Don't need to push much.
        paths_to_push = [distpath + "js", distpath + "libmozglue.so"]

        # Push
        for path in paths_to_push:
            device.push(path, "/data/local/tmp")

        command_context.log(
            logging.INFO,
            "install",
            {},
            "Install complete: find the binary in `/data/local/tmp` on the device",
        )
        command_context.log(
            logging.INFO,
            "install",
            {},
            "Run with /data/local/tmp/js on the device",
        )

        return 0

    else:
        ret = command_context._run_make(
            directory=".", target="install", ensure_exit_code=False
        )

    if ret == 0:
        command_context.notify("Install complete")
    return ret


def _get_android_run_parser():
    parser = argparse.ArgumentParser()
    group = parser.add_argument_group("Compiled Program Environment Options")
    group.add_argument(
        "--app",
        default="org.mozilla.geckoview_example",
        help="Android package to run (default: org.mozilla.geckoview_example)",
    )
    group.add_argument(
        "--intent",
        default="android.intent.action.VIEW",
        help="Android intent action to launch with "
        "(default: android.intent.action.VIEW)",
    )
    group.add_argument(
        "--setenv",
        dest="env",
        action="append",
        default=[],
        help="Set target environment variable, like FOO=BAR",
    )
    group.add_argument(
        "--profile",
        "-P",
        default=None,
        help="Path to Gecko profile, like /path/to/host/profile "
        "or /path/to/target/profile",
    )
    group.add_argument(
        "--aab",
        action="store_true",
        default=False,
        help="Install app as Android App Bundle (AAB).",
    )
    group.add_argument(
        "--no-install",
        action="store_true",
        default=False,
        help="Do not try to install application on device before running "
        "(default: False)",
    )
    group.add_argument(
        "--no-uninstall",
        action="store_true",
        default=False,
        help="Do not try to uninstall application on device before reinstalling"
        "(default: False)",
    )
    group.add_argument(
        "--no-wait",
        action="store_true",
        default=False,
        help="Do not wait for application to start before returning "
        "(default: False)",
    )
    group.add_argument(
        "--enable-fission",
        action="store_true",
        help="Run the program with Fission (site isolation) enabled.",
    )
    group.add_argument(
        "--fail-if-running",
        action="store_true",
        default=False,
        help="Fail if application is already running (default: False)",
    )
    group.add_argument(
        "--restart",
        action="store_true",
        default=False,
        help="Stop the application if it is already running (default: False)",
    )

    group = parser.add_argument_group("Debugging")
    group.add_argument("--debug", action="store_true", help="Enable the lldb debugger.")
    group.add_argument(
        "--debugger",
        default=None,
        type=str,
        help="Name of lldb compatible debugger to use.",
    )
    group.add_argument(
        "--debugger-args",
        default=None,
        metavar="params",
        type=str,
        help="Command-line arguments to pass to the debugger itself; "
        "split as the Bourne shell would.",
    )
    group.add_argument(
        "--no-attach",
        action="store_true",
        default=False,
        help="Start the debugging servers on the device but do not "
        "attach any debuggers.",
    )
    group.add_argument(
        "--use-existing-process",
        action="store_true",
        default=False,
        help="Select an existing process to debug.",
    )

    group = parser.add_argument_group("Compiled Program Options")
    group.add_argument("url", nargs="?", default=None, help="URL to open")

    return parser


def _get_jsshell_run_parser():
    parser = ForwardingArgumentParser()
    group = parser.add_argument_group("Compiled Program Environment Options")

    group = parser.add_argument_group("debugging")
    group.add_argument(
        "--debug",
        action="store_true",
        help="Enable the debugger. Not specifying a --debugger option will result "
        "in the default debugger being used.",
    )
    group.add_argument(
        "--debugger", default=None, type=str, help="Name of debugger to use."
    )
    group.add_argument(
        "--debugger-args",
        default=None,
        metavar="params",
        type=str,
        help="Command-line arguments to pass to the debugger itself; "
        "split as the Bourne shell would.",
    )
    group.add_argument(
        "--debugparams",
        action=StoreDebugParamsAndWarnAction,
        default=None,
        type=str,
        dest="debugger_args",
        help=argparse.SUPPRESS,
    )

    parser.add_forwarding_group(
        title="Compiled Program Options",
        dest="params",
        help="Command-line arguments to be passed through to the program. "
        "Omitting --profile or -P results in a temporary profile being used.",
        forwarding_help="Display the program help.",
    )

    return parser


def _get_desktop_run_parser():

    parser = ForwardingArgumentParser()
    group = parser.add_argument_group("Compiled Program Environment Options")
    group.add_argument("--packaged", action="store_true", help="Run a packaged build.")
    group.add_argument(
        "--app", help="Path to executable to run (default: output of ./mach build)"
    )
    group.add_argument(
        "--background",
        "-b",
        action="store_true",
        help="Do not pass the --foreground argument by default on Mac.",
    )
    group.add_argument(
        "--noprofile",
        "-n",
        action="store_true",
        help="Do not pass the --profile argument by default.",
    )
    group.add_argument(
        "--disable-e10s",
        action="store_true",
        help="Run the program with electrolysis disabled.",
    )
    group.add_argument(
        "--enable-crash-reporter",
        action="store_true",
        help="Run the program with the crash reporter enabled.",
    )
    group.add_argument(
        "--disable-fission",
        action="store_true",
        help="Run the program with Fission (site isolation) disabled.",
    )
    group.add_argument(
        "--setpref",
        action="append",
        default=[],
        help="Set the specified pref before starting the program. Can be set "
        "multiple times. Prefs can also be set in ~/.mozbuild/machrc in the "
        "[runprefs] section - see `./mach settings` for more information.",
    )
    group.add_argument(
        "--temp-profile",
        action="store_true",
        help="Run the program using a new temporary profile created inside "
        "the objdir.",
    )
    group.add_argument(
        "--macos-open",
        action="store_true",
        help="On macOS, run the program using the open(1) command. Per open(1), "
        "the browser is launched \"just as if you had double-clicked the file's "
        'icon". The browser can not be launched under a debugger with this '
        "option.",
    )

    group = parser.add_argument_group("Debugging")
    group.add_argument(
        "--debug",
        action="store_true",
        help="Enable the debugger. Not specifying a --debugger option will result "
        "in the default debugger being used.",
    )
    group.add_argument(
        "--debugger", default=None, type=str, help="Name of debugger to use."
    )
    group.add_argument(
        "--debugger-args",
        default=None,
        metavar="params",
        type=str,
        help="Command-line arguments to pass to the debugger itself; "
        "split as the Bourne shell would.",
    )
    group.add_argument(
        "--debugparams",
        action=StoreDebugParamsAndWarnAction,
        default=None,
        type=str,
        dest="debugger_args",
        help=argparse.SUPPRESS,
    )

    group = parser.add_argument_group("DMD")
    group.add_argument(
        "--dmd",
        action="store_true",
        help="Enable DMD. The following arguments have no effect without this.",
    )
    group.add_argument(
        "--mode",
        choices=["live", "dark-matter", "cumulative", "scan"],
        help="Profiling mode. The default is 'dark-matter'.",
    )
    group.add_argument(
        "--stacks",
        choices=["partial", "full"],
        help="Allocation stack trace coverage. The default is 'partial'.",
    )
    group.add_argument(
        "--show-dump-stats", action="store_true", help="Show stats when doing dumps."
    )

    parser.add_forwarding_group(
        title="Compiled Program Options",
        dest="params",
        help="Command-line arguments to be passed through to the program. "
        "Omitting --profile or -P results in a temporary profile being used.",
        forwarding_help="Display the program help.",
    )
    return parser


def setup_run_parser():
    build = MozbuildObject.from_environment(cwd=here)
    if conditions.is_android(build):
        return _get_android_run_parser()
    if conditions.is_jsshell(build):
        return _get_jsshell_run_parser()
    return _get_desktop_run_parser()


@Command(
    "run",
    category="post-build",
    conditions=[conditions.has_build_or_shell],
    parser=setup_run_parser,
    description="Run the compiled program, possibly under a debugger or DMD.",
)
def run(command_context, **kwargs):
    """Run the compiled program."""
    if conditions.is_android(command_context):
        return _run_android(command_context, **kwargs)
    if conditions.is_jsshell(command_context):
        return _run_jsshell(command_context, **kwargs)
    return _run_desktop(command_context, **kwargs)


def _run_android(
    command_context,
    app="org.mozilla.geckoview_example",
    intent=None,
    env=[],
    profile=None,
    url=None,
    aab=False,
    no_install=None,
    no_uninstall=None,
    no_wait=None,
    fail_if_running=None,
    restart=None,
    enable_fission=False,
    debug=False,
    debugger=None,
    debugger_args=None,
    no_attach=False,
    use_existing_process=False,
):
    from mozrunner.devices.android_device import (
        InstallIntent,
        UninstallIntent,
        _get_device,
        metadata_for_app,
        verify_android_device,
    )
    from six.moves import shlex_quote

    metadata = metadata_for_app(app)

    if not metadata.activity_name:
        raise RuntimeError(f"Application not recognized: {app}")

    # If we want to debug an existing process, we implicitly do not want
    # to kill it and pave over its installation with a new one.
    if debug and use_existing_process:
        no_install = True

    # `verify_android_device` respects `DEVICE_SERIAL` if it is set and sets it otherwise.
    verify_android_device(
        command_context,
        app=metadata.package_name,
        aab=aab,
        debugger=debug,
        install=InstallIntent.NO if no_install else InstallIntent.YES,
        uninstall=UninstallIntent.NO if no_uninstall else UninstallIntent.YES,
    )
    device_serial = os.environ.get("DEVICE_SERIAL")
    if not device_serial:
        print("Unable to find ready Android device")
        return 1

    device = _get_device(command_context.substs, device_serial=device_serial)

    if debug:
        # This will terminate any existing processes, so we skip it when we
        # want to attach to an existing one.
        if not use_existing_process:
            command_context.log(
                logging.INFO,
                "run",
                {"app": metadata.package_name},
                "Setting {app} as the device debug app",
            )
            device.shell("am set-debug-app -w --persistent %s" % metadata.package_name)
    else:
        # Make sure that the app doesn't block waiting for jdb
        device.shell("am clear-debug-app")

    if not debug or not use_existing_process:
        args = []
        if profile:
            if os.path.isdir(profile):
                host_profile = profile
                # Always /data/local/tmp, rather than `device.test_root`, because
                # GeckoView only takes its configuration file from /data/local/tmp,
                # and we want to follow suit.
                target_profile = f"/data/local/tmp/{metadata.package_name}-profile"
                device.rm(target_profile, recursive=True, force=True)
                device.push(host_profile, target_profile)
                command_context.log(
                    logging.INFO,
                    "run",
                    {
                        "host_profile": host_profile,
                        "target_profile": target_profile,
                    },
                    'Pushed profile from host "{host_profile}" to '
                    'target "{target_profile}"',
                )
            else:
                target_profile = profile
                command_context.log(
                    logging.INFO,
                    "run",
                    {"target_profile": target_profile},
                    'Using profile from target "{target_profile}"',
                )

            args = ["--profile", shlex_quote(target_profile)]

        # FIXME: When android switches to using Fission by default,
        # MOZ_FORCE_DISABLE_FISSION will need to be configured correctly.
        if enable_fission:
            env.append("MOZ_FORCE_ENABLE_FISSION=1")

        extras = {}
        for i, e in enumerate(env):
            extras[f"env{i}"] = e
        if args:
            extras["args"] = " ".join(args)

        if env or args:
            restart = True

        if restart:
            fail_if_running = False
            command_context.log(
                logging.INFO,
                "run",
                {"app": metadata.package_name},
                "Stopping {app} to ensure clean restart.",
            )
            device.stop_application(metadata.package_name)

        # We'd prefer to log the actual `am start ...` command, but it's not trivial
        # to wire the device's logger to mach's logger.
        command_context.log(
            logging.INFO,
            "run",
            {
                "app": metadata.package_name,
                "activity_name": metadata.activity_name,
            },
            "Starting {app}/{activity_name}.",
        )

        device.launch_application(
            app_name=metadata.package_name,
            activity_name=metadata.activity_name,
            intent=intent,
            extras=extras,
            url=url,
            wait=not no_wait,
            fail_if_running=fail_if_running,
        )

    if not debug:
        return 0

    from mozrunner.devices.android_device import run_lldb_server

    socket_file = run_lldb_server(
        metadata.package_name, command_context.substs, device_serial
    )
    if not socket_file:
        command_context.log(
            logging.ERROR,
            "run",
            {"msg": "Failed to obtain a socket file!"},
            "{msg}",
        )
        return 1

    # Give lldb-server a chance to start
    command_context.log(
        logging.INFO,
        "run",
        {"msg": "Pausing to ensure lldb-server has started..."},
        "{msg}",
    )
    time.sleep(1)

    if use_existing_process:

        def _is_geckoview_process(proc_name, pkg_name):
            if not proc_name.startswith(pkg_name):
                # Definitely not our package
                return False
            if len(proc_name) == len(pkg_name):
                # Parent process from our package
                return True
            if proc_name[len(pkg_name)] == ":":
                # Child process from our package
                return True
            # Process name is a prefix of our package name
            return False

        # If we're going to attach to an existing process, we need to know
        # who we're attaching to. Obtain a list of all processes associated
        # with our desired app.
        proc_list = [
            proc[:-1]
            for proc in device.get_process_list()
            if _is_geckoview_process(proc[1], metadata.package_name)
        ]

        if not proc_list:
            command_context.log(
                logging.ERROR,
                "run",
                {"app": metadata.package_name},
                "No existing {app} processes found",
            )
            return 1
        elif len(proc_list) == 1:
            pid = proc_list[0][0]
        else:
            # Prompt the user to determine which process we should use
            entries = [
                "%2d: %6d %s" % (n, p[0], p[1])
                for n, p in enumerate(proc_list, start=1)
            ]
            prompt = "\n".join(["\nPlease select a process:\n"] + entries) + "\n\n"
            valid_range = range(1, len(proc_list) + 1)

            while True:
                response = int(input(prompt).strip())
                if response in valid_range:
                    break
                command_context.log(
                    logging.ERROR, "run", {"msg": "Invalid response"}, "{msg}"
                )
            pid = proc_list[response - 1][0]
    else:
        # We're not using an existing process, so there should only be our
        # parent process at this time.
        pids = device.pidof(app_name=metadata.package_name)
        if len(pids) != 1:
            command_context.log(
                logging.ERROR,
                "run",
                {"msg": "Not sure which pid to attach to!"},
                "{msg}",
            )
            return 1
        pid = pids[0]

    command_context.log(
        logging.INFO, "run", {"pid": str(pid)}, "Debuggee pid set to {pid}..."
    )

    lldb_connect_url = "unix-abstract-connect://" + socket_file
    local_jdb_port = device.forward("tcp:0", "jdwp:%d" % pid)

    if no_attach:
        command_context.log(
            logging.INFO,
            "run",
            {"pid": str(pid), "url": lldb_connect_url},
            "To debug native code, connect lldb to {url} and attach to pid {pid}",
        )
        command_context.log(
            logging.INFO,
            "run",
            {"port": str(local_jdb_port)},
            "To debug Java code, connect jdb using tcp to localhost:{port}",
        )
        return 0

    # Beyond this point we want to be able to automatically clean up after ourselves,
    # so we enter the following try block.
    try:
        command_context.log(
            logging.INFO, "run", {"msg": "Starting debugger..."}, "{msg}"
        )

        if not use_existing_process:
            # The app is waiting for jdb to attach and will not continue running
            # until we do so.
            def _jdb_ping(local_jdb_port):
                java_bin_path = os.path.dirname(command_context.substs["JAVA"])
                jdb_path = os.path.join(java_bin_path, "jdb")
                jdb_process = subprocess.Popen(
                    [jdb_path, "-attach", "localhost:%d" % local_jdb_port],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    encoding="utf-8",
                )
                # Wait a bit to provide enough time for jdb and lldb to connect
                # to the debuggee
                time.sleep(5)
                # NOTE: jdb cannot detach while the debuggee is frozen in lldb,
                # so its process might not necessarily exit immediately once the
                # quit command has been issued.
                jdb_process.communicate(input="quit\n")

            # We run this in the background while lldb attaches in the foreground
            from threading import Thread

            jdb_thread = Thread(target=_jdb_ping, args=[local_jdb_port])
            jdb_thread.start()

        LLDBINIT = """
settings set target.inline-breakpoint-strategy always
settings append target.exec-search-paths {obj_xul}
settings append target.exec-search-paths {obj_mozglue}
settings append target.exec-search-paths {obj_nss}
platform select remote-android
platform connect {connect_url}
process attach {continue_flag}-p {pid!s}
""".lstrip()

        obj_xul = os.path.join(command_context.topobjdir, "toolkit", "library", "build")
        obj_mozglue = os.path.join(command_context.topobjdir, "mozglue", "build")
        obj_nss = os.path.join(command_context.topobjdir, "security")

        if use_existing_process:
            continue_flag = ""
        else:
            # Tell lldb to continue after attaching; instead we'll break at
            # the initial SEGVHandler, similarly to how things work when we
            # attach using Android Studio. Doing this gives Android a chance
            # to dismiss the "Waiting for Debugger" dialog.
            continue_flag = "-c "

        try:
            # Write out our lldb startup commands to a temp file. We'll pass its
            # name to lldb on its command line.
            with tempfile.NamedTemporaryFile(
                mode="wt", encoding="utf-8", newline="\n", delete=False
            ) as tmp:
                tmp_lldb_start_script = tmp.name
                tmp.write(
                    LLDBINIT.format(
                        obj_xul=obj_xul,
                        obj_mozglue=obj_mozglue,
                        obj_nss=obj_nss,
                        connect_url=lldb_connect_url,
                        continue_flag=continue_flag,
                        pid=pid,
                    )
                )

            our_debugger_args = "-s %s" % tmp_lldb_start_script
            if debugger_args:
                full_debugger_args = " ".join([debugger_args, our_debugger_args])
            else:
                full_debugger_args = our_debugger_args

            args = _prepend_debugger_args([], debugger, full_debugger_args)
            if not args:
                return 1

            return command_context.run_process(
                args=args, ensure_exit_code=False, pass_thru=True
            )
        finally:
            os.remove(tmp_lldb_start_script)
    finally:
        device.remove_forwards("tcp:%d" % local_jdb_port)
        device.shell("pkill -f lldb-server", enable_run_as=True)
        if not use_existing_process:
            device.shell("am clear-debug-app")


def _run_jsshell(command_context, params, debug, debugger, debugger_args):
    try:
        binpath = command_context.get_binary_path("app")
    except BinaryNotFoundException as e:
        command_context.log(logging.ERROR, "run", {"error": str(e)}, "ERROR: {error}")
        command_context.log(logging.INFO, "run", {"help": e.help()}, "{help}")
        return 1

    args = [binpath]

    if params:
        args.extend(params)

    extra_env = {"RUST_BACKTRACE": "full"}

    if debug or debugger or debugger_args:
        if "INSIDE_EMACS" in os.environ:
            command_context.log_manager.terminal_handler.setLevel(logging.WARNING)

        import mozdebug

        if not debugger:
            # No debugger name was provided. Look for the default ones on
            # current OS.
            debugger = mozdebug.get_default_debugger_name(
                mozdebug.DebuggerSearch.KeepLooking
            )

        if debugger:
            debuggerInfo = mozdebug.get_debugger_info(debugger, debugger_args)

        if not debugger or not debuggerInfo:
            print("Could not find a suitable debugger in your PATH.")
            return 1

        # Prepend the debugger args.
        args = [debuggerInfo.path] + debuggerInfo.args + args

    return command_context.run_process(
        args=args, ensure_exit_code=False, pass_thru=True, append_env=extra_env
    )


def _run_desktop(
    command_context,
    params,
    packaged,
    app,
    background,
    noprofile,
    disable_e10s,
    enable_crash_reporter,
    disable_fission,
    setpref,
    temp_profile,
    macos_open,
    debug,
    debugger,
    debugger_args,
    dmd,
    mode,
    stacks,
    show_dump_stats,
):
    from mozprofile import Preferences, Profile

    try:
        if packaged:
            binpath = command_context.get_binary_path(where="staged-package")
        else:
            binpath = app or command_context.get_binary_path("app")
    except BinaryNotFoundException as e:
        if packaged:
            ret = command_context._run_make(
                directory=".",
                target="stage-package",
                silent=True,
                ensure_exit_code=False,
            )

            if ret == 0:
                try:
                    binpath = command_context.get_binary_path(where="staged-package")
                except BinaryNotFoundException:
                    command_context.log(
                        logging.ERROR, "run", {"error": str(e)}, "ERROR: {error}"
                    )
                    ret = 1

            if ret != 0:
                command_context.log(
                    logging.INFO,
                    "run",
                    {
                        "help": "It looks like your build failed to package properly? "
                        "You can run |./mach package| to package it manually."
                    },
                    "{help}",
                )
                return 1
        else:
            command_context.log(
                logging.ERROR, "run", {"error": str(e)}, "ERROR: {error}"
            )
            command_context.log(logging.INFO, "run", {"help": e.help()}, "{help}")
            return 1

    args = []
    if macos_open:
        if debug:
            print(
                "The browser can not be launched in the debugger "
                "when using the macOS open command."
            )
            return 1
        try:
            m = re.search(r"^.+\.app", binpath)
            apppath = m.group(0)
            args = ["open", apppath, "--args"]
        except Exception as e:
            print(
                "Couldn't get the .app path from the binary path. "
                "The macOS open option can only be used on macOS"
            )
            print(e)
            return 1
    else:
        args = [binpath]

    if params:
        args.extend(params)

    if not background and sys.platform == "darwin":
        args.append("-foreground")

    if (
        sys.platform.startswith("win")
        and "MOZ_LAUNCHER_PROCESS" in command_context.defines
    ):
        args.append("-wait-for-browser")

    no_profile_option_given = all(
        p not in params for p in ["-profile", "--profile", "-P"]
    )
    no_backgroundtask_mode_option_given = all(
        p not in params for p in ["-backgroundtask", "--backgroundtask"]
    )
    if (
        no_profile_option_given
        and no_backgroundtask_mode_option_given
        and not noprofile
    ):
        prefs = {
            "browser.aboutConfig.showWarning": False,
            "browser.shell.checkDefaultBrowser": False,
            "general.warnOnAboutConfig": False,
        }
        prefs.update(command_context._mach_context.settings.runprefs)
        prefs.update([p.split("=", 1) for p in setpref])
        for pref in prefs:
            prefs[pref] = Preferences.cast(prefs[pref])

        tmpdir = os.path.join(command_context.topobjdir, "tmp")
        if not os.path.exists(tmpdir):
            os.makedirs(tmpdir)

        if temp_profile:
            path = tempfile.mkdtemp(dir=tmpdir, prefix="profile-")
        else:
            path = os.path.join(tmpdir, "profile-default")

        profile = Profile(path, preferences=prefs)
        args.append("-profile")
        args.append(profile.profile)

    if not no_profile_option_given and setpref:
        print("setpref is only supported if a profile is not specified")
        return 1

    some_debugging_option = debug or debugger or debugger_args

    # By default, because Firefox is a GUI app, on Windows it will not
    # 'create' a console to which stdout/stderr is printed. This means
    # printf/dump debugging is invisible. We default to adding the
    # -attach-console argument to fix this. We avoid this if we're launched
    # under a debugger (which can do its own picking up of stdout/stderr).
    # We also check for both the -console and -attach-console flags:
    # -console causes Firefox to create a separate window;
    # -attach-console just ends us up with output that gets relayed via mach.
    # We shouldn't override the user using -console. For more info, see
    # https://bugzilla.mozilla.org/show_bug.cgi?id=1257155
    if (
        sys.platform.startswith("win")
        and not some_debugging_option
        and "-console" not in args
        and "--console" not in args
        and "-attach-console" not in args
        and "--attach-console" not in args
    ):
        args.append("-attach-console")

    extra_env = {
        "MOZ_DEVELOPER_REPO_DIR": command_context.topsrcdir,
        "MOZ_DEVELOPER_OBJ_DIR": command_context.topobjdir,
        "RUST_BACKTRACE": "full",
    }

    if not enable_crash_reporter:
        extra_env["MOZ_CRASHREPORTER_DISABLE"] = "1"
    else:
        extra_env["MOZ_CRASHREPORTER"] = "1"

    if disable_e10s:
        extra_env["MOZ_FORCE_DISABLE_E10S"] = "1"

    if disable_fission:
        extra_env["MOZ_FORCE_DISABLE_FISSION"] = "1"

    if some_debugging_option:
        if "INSIDE_EMACS" in os.environ:
            command_context.log_manager.terminal_handler.setLevel(logging.WARNING)

        import mozdebug

        if not debugger:
            # No debugger name was provided. Look for the default ones on
            # current OS.
            debugger = mozdebug.get_default_debugger_name(
                mozdebug.DebuggerSearch.KeepLooking
            )

        if debugger:
            debuggerInfo = mozdebug.get_debugger_info(debugger, debugger_args)

        if not debugger or not debuggerInfo:
            print("Could not find a suitable debugger in your PATH.")
            return 1

        # Parameters come from the CLI. We need to convert them before
        # their use.
        if debugger_args:
            from mozbuild import shellutil

            try:
                debugger_args = shellutil.split(debugger_args)
            except shellutil.MetaCharacterException as e:
                print(
                    "The --debugger-args you passed require a real shell to parse them."
                )
                print("(We can't handle the %r character.)" % e.char)
                return 1

        # Prepend the debugger args.
        args = [debuggerInfo.path] + debuggerInfo.args + args

    if dmd:
        dmd_params = []

        if mode:
            dmd_params.append("--mode=" + mode)
        if stacks:
            dmd_params.append("--stacks=" + stacks)
        if show_dump_stats:
            dmd_params.append("--show-dump-stats=yes")

        if dmd_params:
            extra_env["DMD"] = " ".join(dmd_params)
        else:
            extra_env["DMD"] = "1"

    return command_context.run_process(
        args=args, ensure_exit_code=False, pass_thru=True, append_env=extra_env
    )


@Command(
    "buildsymbols",
    category="post-build",
    description="Produce a package of Breakpad-format symbols.",
)
def buildsymbols(command_context):
    """Produce a package of debug symbols suitable for use with Breakpad."""
    return command_context._run_make(
        directory=".", target="buildsymbols", ensure_exit_code=False
    )


@Command(
    "environment",
    category="build-dev",
    description="Show info about the mach and build environment.",
)
@CommandArgument(
    "--format",
    default="pretty",
    choices=["pretty", "json"],
    help="Print data in the given format.",
)
@CommandArgument("--output", "-o", type=str, help="Output to the given file.")
@CommandArgument("--verbose", "-v", action="store_true", help="Print verbose output.")
def environment(command_context, format, output=None, verbose=False):
    func = {"pretty": _environment_pretty, "json": _environment_json}[
        format.replace(".", "_")
    ]

    if output:
        # We want to preserve mtimes if the output file already exists
        # and the content hasn't changed.
        from mozbuild.util import FileAvoidWrite

        with FileAvoidWrite(output) as out:
            return func(command_context, out, verbose)
    return func(command_context, sys.stdout, verbose)


def _environment_pretty(command_context, out, verbose):
    state_dir = command_context._mach_context.state_dir

    print("platform:\n\t%s" % platform.platform(), file=out)
    print("python version:\n\t%s" % sys.version, file=out)
    print("python prefix:\n\t%s" % sys.prefix, file=out)
    print("mach cwd:\n\t%s" % command_context._mach_context.cwd, file=out)
    print("os cwd:\n\t%s" % os.getcwd(), file=out)
    print("mach directory:\n\t%s" % command_context._mach_context.topdir, file=out)
    print("state directory:\n\t%s" % state_dir, file=out)

    print("object directory:\n\t%s" % command_context.topobjdir, file=out)

    if command_context.mozconfig["path"]:
        print("mozconfig path:\n\t%s" % command_context.mozconfig["path"], file=out)
        if command_context.mozconfig["configure_args"]:
            print("mozconfig configure args:", file=out)
            for arg in command_context.mozconfig["configure_args"]:
                print("\t%s" % arg, file=out)

        if command_context.mozconfig["make_extra"]:
            print("mozconfig extra make args:", file=out)
            for arg in command_context.mozconfig["make_extra"]:
                print("\t%s" % arg, file=out)

        if command_context.mozconfig["make_flags"]:
            print("mozconfig make flags:", file=out)
            for arg in command_context.mozconfig["make_flags"]:
                print("\t%s" % arg, file=out)

    config = None

    try:
        config = command_context.config_environment

    except Exception:
        pass

    if config:
        print("config topsrcdir:\n\t%s" % config.topsrcdir, file=out)
        print("config topobjdir:\n\t%s" % config.topobjdir, file=out)

        if verbose:
            print("config substitutions:", file=out)
            for k in sorted(config.substs):
                print("\t%s: %s" % (k, config.substs[k]), file=out)

            print("config defines:", file=out)
            for k in sorted(config.defines):
                print("\t%s" % k, file=out)


def _environment_json(command_context, out, verbose):
    import json

    class EnvironmentEncoder(json.JSONEncoder):
        def default(self, obj):
            if isinstance(obj, MozbuildObject):
                result = {
                    "topsrcdir": obj.topsrcdir,
                    "topobjdir": obj.topobjdir,
                    "mozconfig": obj.mozconfig,
                }
                if verbose:
                    result["substs"] = obj.substs
                    result["defines"] = obj.defines
                return result
            elif isinstance(obj, set):
                return list(obj)
            return json.JSONEncoder.default(self, obj)

    json.dump(command_context, cls=EnvironmentEncoder, sort_keys=True, fp=out)


@Command(
    "repackage",
    category="misc",
    description="Repackage artifacts into different formats.",
)
def repackage(command_context):
    """Repackages artifacts into different formats.

    This is generally used after packages are signed by the signing
    scriptworkers in order to bundle things up into shippable formats, such as a
    .dmg on OSX or an installer exe on Windows.
    """
    print("Usage: ./mach repackage [dmg|pkg|installer|mar] [args...]")


@SubCommand(
    "repackage",
    "deb",
    description="Repackage a tar file into a .deb for Linux",
    virtualenv_name="repackage-deb",
)
@CommandArgument(
    "--input", "-i", type=str, required=True, help="Input tarfile filename"
)
@CommandArgument("--output", "-o", type=str, required=True, help="Output .deb filename")
@CommandArgument("--arch", type=str, required=True, help="One of ['x86', 'x86_64']")
@CommandArgument(
    "--version",
    type=str,
    required=True,
    help="The Firefox version used to create the installer",
)
@CommandArgument(
    "--build-number",
    type=str,
    required=True,
    help="The release's build number",
)
@CommandArgument(
    "--templates",
    type=str,
    required=True,
    help="Location of the templates used to generate the debian/ directory files",
)
@CommandArgument(
    "--release-product",
    type=str,
    required=True,
    help="The product being shipped. Used to disambiguate beta/devedition etc.",
)
@CommandArgument(
    "--release-type",
    type=str,
    required=True,
    help="The release being shipped. Used to disambiguate nightly/try etc.",
)
def repackage_deb(
    command_context,
    input,
    output,
    arch,
    version,
    build_number,
    templates,
    release_product,
    release_type,
):
    if not os.path.exists(input):
        print("Input file does not exist: %s" % input)
        return 1

    template_dir = os.path.join(
        command_context.topsrcdir,
        templates,
    )

    from fluent.runtime.fallback import FluentLocalization, FluentResourceLoader

    from mozbuild.repackaging.deb import repackage_deb

    repackage_deb(
        command_context.log,
        input,
        output,
        template_dir,
        arch,
        version,
        build_number,
        release_product,
        release_type,
        FluentLocalization,
        FluentResourceLoader,
    )


@SubCommand(
    "repackage",
    "deb-l10n",
    description="Repackage a .xpi langpack file into a .deb for Linux",
)
@CommandArgument(
    "--input-xpi-file", type=str, required=True, help="Path to the XPI file"
)
@CommandArgument(
    "--input-tar-file",
    type=str,
    required=True,
    help="Path to tar archive that contains application.ini",
)
@CommandArgument(
    "--version",
    type=str,
    required=True,
    help="The Firefox version used to create the installer",
)
@CommandArgument(
    "--build-number",
    type=str,
    required=True,
    help="The release's build number",
)
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
@CommandArgument(
    "--templates",
    type=str,
    required=True,
    help="Location of the templates used to generate the debian/ directory files",
)
@CommandArgument(
    "--release-product",
    type=str,
    required=True,
    help="The product being shipped. Used to disambiguate beta/devedition etc.",
)
def repackage_deb_l10n(
    command_context,
    input_xpi_file,
    input_tar_file,
    output,
    version,
    build_number,
    templates,
    release_product,
):
    for input_file in (input_xpi_file, input_tar_file):
        if not os.path.exists(input_file):
            print("Input file does not exist: %s" % input_file)
            return 1

    template_dir = os.path.join(
        command_context.topsrcdir,
        templates,
    )

    from mozbuild.repackaging.deb import repackage_deb_l10n

    repackage_deb_l10n(
        input_xpi_file,
        input_tar_file,
        output,
        template_dir,
        version,
        build_number,
        release_product,
    )


@SubCommand(
    "repackage",
    "rpm",
    description="Repackage a tar file into a .rpm for Linux",
    virtualenv_name="repackage-desktop-file",
)
@CommandArgument(
    "--input", "-i", type=str, required=True, help="Input tarfile filename"
)
@CommandArgument(
    "--input-xpi-dir",
    "-x",
    type=str,
    required=True,
    help="Directory which contains the .xpi langpacks",
)
@CommandArgument(
    "--output",
    "-o",
    type=str,
    required=True,
    help="Output directory for the .rpm files",
)
@CommandArgument("--arch", type=str, required=True, help="One of ['x86', 'x86_64']")
@CommandArgument(
    "--version",
    type=str,
    required=True,
    help="The Firefox version used to create the installer",
)
@CommandArgument(
    "--build-number",
    type=str,
    required=True,
    help="The release's build number",
)
@CommandArgument(
    "--templates",
    type=str,
    required=True,
    help="Location of the templates used to generate the rpm/ directory files",
)
@CommandArgument(
    "--release-product",
    type=str,
    required=True,
    help="The product being shipped. Used to disambiguate beta/devedition etc.",
)
@CommandArgument(
    "--release-type",
    type=str,
    required=True,
    help="The release being shipped. Used to disambiguate nightly/try etc.",
)
def repackage_rpm(
    command_context,
    input,
    input_xpi_dir,
    output,
    arch,
    version,
    build_number,
    templates,
    release_product,
    release_type,
):
    if not os.path.exists(input):
        print("Input file does not exist: %s" % input)
        return 1

    template_dir = os.path.join(
        command_context.topsrcdir,
        templates,
    )

    from fluent.runtime.fallback import FluentLocalization, FluentResourceLoader

    from mozbuild.repackaging.rpm import repackage_rpm

    repackage_rpm(
        command_context.log,
        input,
        input_xpi_dir,
        output,
        template_dir,
        arch,
        version,
        build_number,
        release_product,
        release_type,
        FluentLocalization,
        FluentResourceLoader,
    )


@SubCommand("repackage", "dmg", description="Repackage a tar file into a .dmg for OSX")
@CommandArgument("--input", "-i", type=str, required=True, help="Input filename")
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
@CommandArgument(
    "--attribution_sentinel", type=str, required=False, help="DMGs with attribution."
)
@CommandArgument(
    "--compression",
    type=str,
    required=False,
    help="Use alternative compression algorithm",
)
def repackage_dmg(command_context, input, output, attribution_sentinel, compression):
    if not os.path.exists(input):
        print("Input file does not exist: %s" % input)
        return 1

    from mozbuild.repackaging.dmg import repackage_dmg

    repackage_dmg(input, output, attribution_sentinel, compression)


@SubCommand("repackage", "pkg", description="Repackage a tar file into a .pkg for OSX")
@CommandArgument("--input", "-i", type=str, required=True, help="Input filename")
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
def repackage_pkg(command_context, input, output):
    if not os.path.exists(input):
        print("Input file does not exist: %s" % input)
        return 1

    from mozbuild.repackaging.pkg import repackage_pkg

    repackage_pkg(input, output)


@SubCommand(
    "repackage", "installer", description="Repackage into a Windows installer exe"
)
@CommandArgument(
    "--tag",
    type=str,
    required=True,
    help="The .tag file used to build the installer",
)
@CommandArgument(
    "--setupexe",
    type=str,
    required=True,
    help="setup.exe file inside the installer",
)
@CommandArgument(
    "--package",
    type=str,
    required=False,
    help="Optional package .zip for building a full installer",
)
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
@CommandArgument(
    "--package-name",
    type=str,
    required=False,
    help="Name of the package being rebuilt",
)
@CommandArgument(
    "--sfx-stub", type=str, required=True, help="Path to the self-extraction stub."
)
@CommandArgument(
    "--use-upx",
    required=False,
    action="store_true",
    help="Run UPX on the self-extraction stub.",
)
def repackage_installer(
    command_context,
    tag,
    setupexe,
    package,
    output,
    package_name,
    sfx_stub,
    use_upx,
):
    from mozbuild.repackaging.installer import repackage_installer

    repackage_installer(
        topsrcdir=command_context.topsrcdir,
        tag=tag,
        setupexe=setupexe,
        package=package,
        output=output,
        package_name=package_name,
        sfx_stub=sfx_stub,
        use_upx=use_upx,
    )


@SubCommand("repackage", "msi", description="Repackage into a MSI")
@CommandArgument(
    "--wsx",
    type=str,
    required=True,
    help="The wsx file used to build the installer",
)
@CommandArgument(
    "--version",
    type=str,
    required=True,
    help="The Firefox version used to create the installer",
)
@CommandArgument(
    "--locale", type=str, required=True, help="The locale of the installer"
)
@CommandArgument(
    "--arch", type=str, required=True, help="The architecture you are building."
)
@CommandArgument("--setupexe", type=str, required=True, help="setup.exe installer")
@CommandArgument("--candle", type=str, required=False, help="location of candle binary")
@CommandArgument("--light", type=str, required=False, help="location of light binary")
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
def repackage_msi(
    command_context,
    wsx,
    version,
    locale,
    arch,
    setupexe,
    candle,
    light,
    output,
):
    from mozbuild.repackaging.msi import repackage_msi

    repackage_msi(
        topsrcdir=command_context.topsrcdir,
        wsx=wsx,
        version=version,
        locale=locale,
        arch=arch,
        setupexe=setupexe,
        candle=candle,
        light=light,
        output=output,
    )


@SubCommand("repackage", "msix", description="Repackage into an MSIX")
@CommandArgument(
    "--input",
    type=str,
    help="Package (ZIP) or directory to repackage. Defaults to $OBJDIR/dist/bin",
)
@CommandArgument(
    "--version",
    type=str,
    help="The Firefox version used to create the package "
    "(Default: generated from package 'application.ini')",
)
@CommandArgument(
    "--channel",
    type=str,
    choices=["official", "beta", "aurora", "nightly", "unofficial"],
    help="Release channel.",
)
@CommandArgument(
    "--distribution-dir",
    metavar="DISTRIBUTION",
    nargs="*",
    dest="distribution_dirs",
    default=[],
    help="List of distribution directories to include.",
)
@CommandArgument(
    "--arch",
    type=str,
    choices=["x86", "x86_64", "aarch64"],
    help="The architecture you are building.",
)
@CommandArgument(
    "--vendor",
    type=str,
    default="Mozilla",
    required=False,
    help="The vendor to use in the Package/Identity/Name string to use in the App Manifest."
    + " Defaults to 'Mozilla'.",
)
@CommandArgument(
    "--identity-name",
    type=str,
    default=None,
    required=False,
    help="The Package/Identity/Name string to use in the App Manifest."
    + " Defaults to '<vendor>.Firefox', '<vendor>.FirefoxBeta', etc.",
)
@CommandArgument(
    "--publisher",
    type=str,
    # This default is baked into enough places under `browser/` that we need
    # not extract a constant.
    default="CN=Mozilla Corporation, OU=MSIX Packaging",
    required=False,
    help="The Package/Identity/Publisher string to use in the App Manifest."
    + " It must match the subject on the certificate used for signing.",
)
@CommandArgument(
    "--publisher-display-name",
    type=str,
    default="Mozilla Corporation",
    required=False,
    help="The Package/Properties/PublisherDisplayName string to use in the App Manifest. "
    + " Defaults to 'Mozilla Corporation'.",
)
@CommandArgument(
    "--makeappx",
    type=str,
    default=None,
    help="makeappx/makemsix binary name (required if you haven't run configure)",
)
@CommandArgument(
    "--verbose",
    default=False,
    action="store_true",
    help="Be verbose.  (Default: false)",
)
@CommandArgument(
    "--output", "-o", type=str, help="Output filename (Default: auto-generated)"
)
@CommandArgument(
    "--sign",
    default=False,
    action="store_true",
    help="Sign repackaged MSIX with self-signed certificate for local testing. "
    "(Default: false)",
)
@CommandArgument(
    "--unsigned",
    default=False,
    action="store_true",
    help="Support `Add-AppxPackage ... -AllowUnsigned` on Windows 11."
    "(Default: false)",
)
def repackage_msix(
    command_context,
    input,
    version=None,
    channel=None,
    distribution_dirs=[],
    arch=None,
    identity_name=None,
    vendor=None,
    publisher=None,
    publisher_display_name=None,
    verbose=False,
    output=None,
    makeappx=None,
    sign=False,
    unsigned=False,
):
    from mozbuild.repackaging.msix import repackage_msix

    command_context._set_log_level(verbose)

    firefox_to_msix_channel = {
        "release": "official",
        "beta": "beta",
        "aurora": "aurora",
        "nightly": "nightly",
    }

    if not input:
        if os.path.exists(command_context.bindir):
            input = command_context.bindir
        else:
            command_context.log(
                logging.ERROR,
                "repackage-msix-no-input",
                {},
                "No build found in objdir, please run ./mach build or pass --input",
            )
            return 1

    if not os.path.exists(input):
        command_context.log(
            logging.ERROR,
            "repackage-msix-invalid-input",
            {"input": input},
            "Input file or directory for msix repackaging does not exist: {input}",
        )
        return 1

    if not channel:
        # Only try to guess the channel when this is clearly a local build.
        if input.endswith("bin"):
            channel = firefox_to_msix_channel.get(
                command_context.defines.get("MOZ_UPDATE_CHANNEL"), "unofficial"
            )
        else:
            command_context.log(
                logging.ERROR,
                "repackage-msix-invalid-channel",
                {},
                "Could not determine channel, please set --channel",
            )
            return 1

    if not arch:
        # Only try to guess the arch when this is clearly a local build.
        if input.endswith("bin"):
            if command_context.substs["TARGET_CPU"] in ("x86", "x86_64", "aarch64"):
                arch = command_context.substs["TARGET_CPU"]

        if not arch:
            command_context.log(
                logging.ERROR,
                "repackage-msix-couldnt-detect-arch",
                {},
                "Could not automatically detect architecture for msix repackaging. "
                "Please pass --arch",
            )
            return 1

    if unsigned:
        if sign:
            command_context.log(
                logging.ERROR,
                "repackage-msix-signed-and-unsigned",
                {},
                "--sign and --unsigned are mutually exclusive",
            )
            return 1

        # Support `Add-AppxPackage ... -AllowUnsigned` on Windows 11.  See
        # https://github.com/MicrosoftDocs/msix-docs/blob/769dee9364df2b6fd0b78000774f8d14de8fe814/msix-src/package/unsigned-package.md.
        publisher = f"{publisher}, OID.2.25.311729368913984317654407730594956997722=1"

    output = repackage_msix(
        input,
        command_context.topsrcdir,
        channel=channel,
        arch=arch,
        displayname=identity_name,
        vendor=vendor,
        publisher=publisher,
        publisher_display_name=publisher_display_name,
        version=version,
        distribution_dirs=distribution_dirs,
        # Configure this run.
        force=True,
        verbose=verbose,
        log=command_context.log,
        output=output,
        makeappx=makeappx,
    )

    if sign:
        repackage_sign_msix(command_context, output, force=False, verbose=verbose)

    command_context.log(
        logging.INFO,
        "msix",
        {"output": output},
        "Wrote MSIX: {output}",
    )


@SubCommand("repackage", "sign-msix", description="Sign an MSIX for local testing")
@CommandArgument("--input", type=str, required=True, help="MSIX to sign.")
@CommandArgument(
    "--force",
    default=False,
    action="store_true",
    help="Force recreating self-signed certificate.  (Default: false)",
)
@CommandArgument(
    "--verbose",
    default=False,
    action="store_true",
    help="Be verbose.  (Default: false)",
)
def repackage_sign_msix(command_context, input, force=False, verbose=False):
    from mozbuild.repackaging.msix import sign_msix

    command_context._set_log_level(verbose)

    sign_msix(input, force=force, log=command_context.log, verbose=verbose)

    return 0


@SubCommand("repackage", "mar", description="Repackage into complete MAR file")
@CommandArgument("--input", "-i", type=str, required=True, help="Input filename")
@CommandArgument("--mar", type=str, required=True, help="Mar binary path")
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
@CommandArgument(
    "--arch", type=str, required=True, help="The architecture you are building."
)
@CommandArgument("--mar-channel-id", type=str, help="Mar channel id")
def repackage_mar(command_context, input, mar, output, arch, mar_channel_id):
    from mozbuild.repackaging.mar import repackage_mar

    repackage_mar(
        command_context.topsrcdir,
        input,
        mar,
        output,
        arch=arch,
        mar_channel_id=mar_channel_id,
    )


@SubCommand(
    "repackage",
    "snap",
    description="Repackage into Snap format for developer testing",
    virtualenv_name="repackage-snap",
)
@CommandArgument(
    "--snapcraft",
    metavar="FILENAME",
    help="Path to the snapcraft command (default: search $PATH and /snap/bin)",
)
@CommandArgument(
    "--snap-name",
    default="firefox-devel",
    required=True,
    help="Name of the snap to generate (default: firefox-devel)",
)
@CommandArgument(
    "--upstream-repo",
    default="canonical/firefox-snap",
    required=False,
    help="Name of the firefox-snap github repo to use (default: canonical/firefox-snap)",
)
@CommandArgument(
    "--upstream-branch",
    default="nightly",
    required=False,
    help="Name of the firefox-snap github branch to use (default: nightly)",
)
@CommandArgument(
    "--output",
    metavar="FILE|DIR",
    help="File or directory where the snap file will be written;"
    " by default, it's left in the staging directory",
)
@CommandArgument(
    "--input",
    metavar="FILENAME",
    dest="input_pkg",
    help="Repack an existing package instead of a local build;"
    " implies --clean and requires --output",
)
@CommandArgument(
    "--tmp-dir",
    metavar="FILENAME",
    default=tempfile.gettempdir,
    help="Temp dir for --input (default: tempfile.gettempdir; note that /tmp may not work)",
)
@CommandArgument(
    "--clean",
    action="store_true",
    help="Delete staging directory afterwards; requires --output",
)
@CommandArgument(
    "--install",
    action="store_true",
    help="Install the snap afterwards (as with `mach repackage snap-install`)",
)
@CommandArgument(
    "--dry-run",
    action="store_true",
    help="Prepare everything but stop before actually calling snapcraft. Useful for debugging generated YAML definition.",
)
@CommandArgument(
    "--wmclass",
    type=str,
    required=False,
    help="The StartupWMClass entry key for the desktop file.",
)
def repackage_snap(
    command_context,
    snapcraft=None,
    snap_name=None,
    upstream_repo=None,
    upstream_branch=None,
    output=None,
    input_pkg=None,
    tmp_dir=None,
    clean=False,
    install=False,
    dry_run=False,
    wmclass=None,
):
    from mozfile import which

    from mozbuild.repackaging.snap import (
        repackage_snap,
        unpack_tarball,
    )

    # Validate arguments / environment
    if not snapcraft:
        snapcraft = which("snapcraft", extra_search_dirs=["/snap/bin"])

    if not snapcraft:
        command_context.log(
            logging.ERROR,
            "repackage-snap-no-snapcraft",
            {},
            "Couldn't find the `snapcraft` command; if it's installed, try"
            " adjusting your $PATH or using the --snapcraft option",
        )
        return 1

    if not conditions.is_firefox(command_context):
        command_context.log(
            logging.ERROR,
            "repackage-snap-unsupported-product",
            {},
            "Snap repackaging is currently supported only for Firefox",
        )
        return 1

    if input_pkg:
        clean = True

    if clean and not output:
        command_context.log(
            logging.ERROR,
            "repackage-snap-no-output",
            {},
            "When --input or --clean is used, --output is required",
        )
        return 1

    if not input_pkg and not os.path.exists(command_context.bindir):
        command_context.log(
            logging.ERROR,
            "repackage-snap-no-input",
            {},
            "No build found in objdir; please run ./mach build or pass --input",
        )
        return 1

    # Set up the staging dir and unpack or copy the payload
    if input_pkg:
        # This mode of operation isn't about the current build, so the
        # package is staged in a secure temp dir from mkdtemp instead
        # of something under the objdir.  But when snapcraft runs
        # itself under multipass (the default), the VM will be rebuilt
        # whenever the staging dir changes, and this means it will
        # change every time.  So that's not ideal, but it's not clear
        # how to improve the experience.
        snapdir = tempfile.mkdtemp(dir=tmp_dir, prefix="snap-repackage-")
        command_context.log(
            logging.INFO,
            "repackage-snap-tmp-dir",
            {"path": snapdir},
            "Using temp dir: {path}",
        )
        unpack_tarball(
            input_pkg, os.path.join(snapdir, "source", "usr", "lib", "firefox")
        )
    else:
        # Deploy the current build for packaging, into the directory
        # where snapcraft will expect it
        command_context._run_make(
            directory=".",
            target="stage-package",
            append_env={"MOZ_PKG_DIR": "snap/source/usr/lib/firefox"},
        )
        snapdir = os.path.join(command_context.distdir, "snap")

    # Handle the most common cases of arch:
    mozarch = command_context.substs["TARGET_CPU"]
    if mozarch == "x86":
        arch = "i386"
    elif mozarch == "x86_64":
        arch = "amd64"
    elif mozarch == "aarch64":
        arch = "arm64"
    else:
        command_context.log(
            logging.ERROR,
            "repackage-snap-arch-unknown",
            {},
            "Could not automatically detect architecture for Snap "
            "repackaging; please pass --arch",
        )
        return 1

    # Create the package
    snappath = repackage_snap(
        log=command_context.log,
        srcdir=command_context.topsrcdir,
        objdir=command_context.topobjdir,
        snapdir=snapdir,
        snapcraft=snapcraft,
        appname=snap_name,
        reponame=upstream_repo,
        branchname=upstream_branch,
        arch=arch,
        dry_run=dry_run,
    )

    if dry_run:
        command_context.log(
            logging.INFO,
            "repackage-snap-show-output",
            {"path": snappath},
            "Snap package prepared: {path}",
        )

        return 0

    # Cleanup: move the output, delete temp files, inform the user
    if output:
        if os.path.isdir(output):
            output = os.path.join(output, os.path.basename(snappath))
        shutil.copyfile(snappath, output)
        snappath = output

    if clean:
        command_context.log(
            logging.INFO,
            "repackage-snap-clean",
            {"path": snapdir},
            "Deleting staging dir: {path}",
        )
        shutil.rmtree(snapdir)

    command_context.log(
        logging.INFO,
        "repackage-snap-show-output",
        {"path": snappath},
        "Snap package created: {path}",
    )

    if install:
        return repackage_snap_install(
            command_context,
            snap_file=snappath,
            snap_name=snap_name,
        )

    return 0


@SubCommand(
    "repackage",
    "snap-install",
    description="Install an unofficial Snap package and, if needed, enable"
    " its connections",
)
@CommandArgument(
    "--snap-file",
    metavar="FILENAME",
    help="Snap file to install; defaults to the last one built by"
    " `mach repackage snap` (without `--output`)",
)
@CommandArgument(
    "--sudo",
    metavar="COMMAND",
    default=None,
    help="Wrapper to run commands as root (default: sudo or doas)",
)
def repackage_snap_install(command_context, snap_file, snap_name, sudo=None):
    from mozfile import which

    from mozbuild.repackaging.snap import missing_connections

    if not sudo:
        for candidate in ["sudo", "doas"]:
            if which(candidate):
                sudo = candidate
                break

    if not sudo:
        command_context.log(
            logging.ERROR,
            "repackage-snap-install-no-sudo",
            {},
            "Couldn't find a command to run snap as root; please use the"
            " --sudo option",
        )

    if not snap_file:
        snap_file = os.path.join(command_context.distdir, "snap/latest.snap")
        if not os.path.exists(snap_file):
            command_context.log(
                logging.ERROR,
                "repackage-snap-install-no-dfl-snap",
                {},
                "No snap file found; please run `./mach repackage snap` first"
                " or use --snap-file",
            )
            return 1

    # Install
    command_context.run_process(
        # The `--dangerous` flag skips signature checks but doesn't
        # turn off sandboxing (contrast `--devmode`), because if you
        # need to test under Snap instead of normally, it may be
        # because their sandbox broke something.
        [sudo, "snap", "install", "--dangerous", snap_file],
        pass_thru=True,
    )

    # Fix up connections if needed
    # (Ideally this wouldn't hard-code the app name....)
    for conn in missing_connections(snap_name):
        command_context.run_process(
            [sudo, "snap", "connect", conn],
            pass_thru=True,
        )

    # A little help
    command_context.log(
        logging.INFO,
        "repackage-snap-install-howto-run",
        {},
        f"Example usage: snap run {snap_name}",
    )

    return 0


@SubCommand(
    "repackage",
    "desktop-file",
    description="Prepare a firefox.desktop file",
    virtualenv_name="repackage-desktop-file",
)
@CommandArgument("--output", type=str, required=True, help="Output desktop file")
@CommandArgument(
    "--flavor",
    type=str,
    required=True,
    choices=["snap", "flatpak"],
    help="Desktop file flavor to generate.",
)
@CommandArgument(
    "--release-product",
    type=str,
    required=True,
    help="The product being shipped. Used to disambiguate beta/devedition etc.",
)
@CommandArgument(
    "--release-type",
    type=str,
    required=True,
    help="The release being shipped. Used to disambiguate nightly/try etc.",
)
@CommandArgument(
    "--wmclass",
    type=str,
    required=False,
    help="The StartupWMClass entry key for the desktop file.",
)
def repackage_desktop_file(
    command_context,
    output,
    flavor,
    release_product,
    release_type,
    wmclass,
):
    desktop = None
    if flavor == "flatpak":
        from fluent.runtime.fallback import FluentLocalization, FluentResourceLoader

        from mozbuild.repackaging.desktop_file import generate_browser_desktop_entry

        # This relies in existing build variables usage inherited from the
        # debian repackage code that serves the same purpose on Flatpak, so
        # it is just directly re-used here.
        build_variables = {
            "PKG_NAME": release_product,
            "DBusActivatable": "false",
            "Icon": "org.mozilla.firefox",
            "StartupWMClass": release_product,
        }

        desktop = "\n".join(
            generate_browser_desktop_entry(
                command_context.log,
                build_variables,
                release_product,
                release_type,
                FluentLocalization,
                FluentResourceLoader,
            )
        )

    if flavor == "snap":
        from mozbuild.repackaging.snapcraft_transform import (
            SnapDesktopFile,
        )

        desktop = SnapDesktopFile(
            command_context.log,
            appname=release_product,
            branchname=release_type,
            wmclass=wmclass,
        ).repack()

    if desktop is None:
        raise NotImplementedError(
            f"Couldn't generate a desktop file. Unknown flavor: {flavor}"
        )

    with open(output, "w") as desktop_file:
        desktop_file.write(desktop)


@Command(
    "package-multi-locale",
    category="post-build",
    description="Package a multi-locale version of the built product "
    "for distribution as an APK, DMG, etc.",
)
@CommandArgument(
    "--locales",
    metavar="LOCALES",
    nargs="+",
    required=True,
    help="List of locales to package",
)
@CommandArgument(
    "--verbose", action="store_true", help="Log informative status messages."
)
def package_l10n(command_context, verbose=False, locales=[]):
    if "RecursiveMake" not in command_context.substs["BUILD_BACKENDS"]:
        print(
            "Artifact builds do not support localization. "
            "If you know what you are doing, you can use:\n"
            "ac_add_options --disable-compile-environment\n"
            "export BUILD_BACKENDS=FasterMake,RecursiveMake\n"
            "in your mozconfig."
        )
        return 1

    locales = sorted(locale for locale in locales if locale != "en-US")

    append_env = {
        # We are only (re-)packaging, we don't want to (re-)build
        # anything inside Gradle.
        "GRADLE_INVOKED_WITHIN_MACH_BUILD": "1",
        "MOZ_CHROME_MULTILOCALE": " ".join(locales),
    }

    command_context.log(
        logging.INFO,
        "package-multi-locale",
        {"locales": locales},
        "Processing chrome Gecko resources for locales {locales}",
    )
    command_context._run_make(
        directory=command_context.topobjdir,
        target=[f"chrome-{locale}" for locale in locales],
        append_env=append_env,
        pass_thru=False,
        print_directory=False,
        ensure_exit_code=True,
    )

    if command_context.substs["MOZ_BUILD_APP"] == "browser":
        command_context.log(
            logging.INFO, "package-multi-locale", {}, "Repackaging browser"
        )
        command_context._run_make(
            directory=mozpath.join(command_context.topobjdir, "browser", "app"),
            target=["tools"],
            append_env=append_env,
            pass_thru=True,
            ensure_exit_code=True,
        )

    command_context.log(
        logging.INFO,
        "package-multi-locale",
        {},
        "Invoking multi-locale `mach package`",
    )
    target = ["package"]
    if command_context.substs["MOZ_BUILD_APP"] == "mobile/android":
        target.append("AB_CD=multi")

    command_context._run_make(
        directory=command_context.topobjdir,
        target=target,
        append_env=append_env,
        pass_thru=True,
        ensure_exit_code=True,
    )

    if command_context.substs["MOZ_BUILD_APP"] == "mobile/android":
        command_context.log(
            logging.INFO,
            "package-multi-locale",
            {},
            "Invoking `mach android archive-geckoview`",
        )
        command_context.run_process(
            [
                sys.executable,
                mozpath.join(command_context.topsrcdir, "mach"),
                "android",
                "archive-geckoview",
            ],
            append_env=append_env,
            pass_thru=True,
            ensure_exit_code=True,
            cwd=mozpath.join(command_context.topsrcdir),
        )

        # This is tricky: most Android build commands will regenerate the
        # omnijar, producing a `res/multilocale.txt` that does not contain the
        # set of locales packaged by this command.  To avoid regenerating, we
        # set a special environment variable.
        print(
            "Execute `env MOZ_CHROME_MULTILOCALE='{}' ".format(
                append_env["MOZ_CHROME_MULTILOCALE"]
            )
            + "mach android install-geckoview_example` "
            + "to install the multi-locale geckoview_example and test APKs."
        )

    return 0


def _prepend_debugger_args(args, debugger, debugger_args):
    """
    Given an array with program arguments, prepend arguments to run it under a
    debugger.

    :param args: The executable and arguments used to run the process normally.
    :param debugger: The debugger to use, or empty to use the default debugger.
    :param debugger_args: Any additional parameters to pass to the debugger.
    """

    import mozdebug

    if not debugger:
        # No debugger name was provided. Look for the default ones on
        # current OS.
        debugger = mozdebug.get_default_debugger_name(
            mozdebug.DebuggerSearch.KeepLooking
        )

    if debugger:
        debuggerInfo = mozdebug.get_debugger_info(debugger, debugger_args)

    if not debugger or not debuggerInfo:
        print("Could not find a suitable debugger in your PATH.")
        return None

    # Parameters come from the CLI. We need to convert them before
    # their use.
    if debugger_args:
        from mozbuild import shellutil

        try:
            debugger_args = shellutil.split(debugger_args)
        except shellutil.MetaCharacterException as e:
            print("The --debugger_args you passed require a real shell to parse them.")
            print("(We can't handle the %r character.)" % e.char)
            return None

    # Prepend the debugger args.
    args = [debuggerInfo.path] + debuggerInfo.args + args
    return args


@SubCommand(
    "repackage",
    "flatpak",
    description="Repackage a tar file into a flatpak",
    virtualenv_name="repackage-desktop-file",
)
@CommandArgument("--input", "-i", type=str, required=True, help="Input filename")
@CommandArgument("--output", "-o", type=str, required=True, help="Output filename")
@CommandArgument("--name", type=str, required=True, help="flatpak package name")
@CommandArgument("--arch", type=str, required=True, help="flatpak architecture")
@CommandArgument("--version", type=str, required=True, help="package version")
@CommandArgument("--product", type=str, required=True, help="release product")
@CommandArgument(
    "--release-type",
    type=str,
    required=True,
    help="The release being shipped. Used to disambiguate nightly/try etc.",
)
@CommandArgument(
    "--flatpak-branch",
    type=str,
    required=True,
    help="flatpak branch",
)
@CommandArgument(
    "--template-dir", type=str, required=True, help="path to template directory"
)
@CommandArgument(
    "--langpack-pattern",
    type=str,
    help="shell pattern matching language packs to include in the flatpak",
)
def repackage_flatpak(
    command_context,
    input,
    output,
    name,
    arch,
    version,
    product,
    release_type,
    flatpak_branch,
    template_dir,
    langpack_pattern,
):
    if not os.path.exists(input):
        print("Input file does not exist: %s" % input)
        return 1

    from mozbuild.repackaging.flatpak import repackage_flatpak

    repackage_flatpak(
        command_context.log,
        input,
        output,
        arch,
        version,
        product,
        release_type,
        name,
        flatpak_branch,
        template_dir,
        langpack_pattern,
    )
