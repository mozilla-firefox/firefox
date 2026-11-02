# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import errno
import functools
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
import mozshellutil
from gtest.reports import AggregatedGTestReport
from gtest.suites import get_gtest_suites, suite_filters
from mach.decorators import (
    Command,
    CommandArgument,
    CommandArgumentGroup,
    SubCommand,
)
from mozdebug import prepend_debugger_args
from mozfile import load_source
from mozlog.formatters import MachFormatter
from mozlog.handlers import ResourceHandler, StreamHandler
from mozlog.structuredlog import StructuredLogger
from mozlog.structuredlog import log_actions as get_log_actions
from packaging.version import Version

from mozbuild.base import (
    BinaryNotFoundException,
    BuildEnvironmentNotFoundException,
    MozbuildObject,
)
from mozbuild.base import MachCommandConditions as conditions
from mozbuild.util import (
    MOZBUILD_METRICS_PATH,
    ForwardingArgumentParser,
    ensure_l10n_central,
    get_latest_file,
)

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


def _substituted(args, subst):
    """Expand the placeholders in each argument, one argument at a time."""
    return [arg.format(**subst) for arg in args]


def _cargo_config_yaml_schema():
    from voluptuous import All, Boolean, Required, Schema

    def starts_with_cargo(s):
        if s.startswith("cargo-"):
            return s
        else:
            raise ValueError

    return Schema({
        # The name of the command (not checked for now, but maybe
        #  later)
        Required("command"): All(str, starts_with_cargo),
        # Whether to keep going with the remaining crates when the command
        # fails for one. Default: False
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
        # * {target}: `--lib` for library, `--bin=CRATE` for executables
        # * {topsrcdir}: Top directory of sources
    })


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
    cargo_extra_flags = yaml_config.get("cargo_extra_flags")
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
                what=["rust-prereqs"],
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
        "gkrust": {"directory": gkrust_path, "kind": "library"},
        "gkrust-gtest": {"directory": gtest_path, "kind": "library"},
        "geckodriver": {"directory": "testing/geckodriver", "kind": "host-program"},
    }

    if all_crates:
        crates = crates_and_roots.keys()
    elif package:
        crates = [package]
    else:
        crates = ["gkrust"]

    jobs = command_context.resolve_num_jobs(jobs)
    command_context.ensure_backend_current()

    for crate in crates:
        crate_info = crates_and_roots.get(crate, None)
        package_args = []
        if not crate_info:
            # Not one of the top-level crates we know how to build directly, assume it's
            # other crate in the gkrust workspace and target it explicitly via `-p`.
            #
            # gkrust's features and lib/bin targets don't apply to an individual crate,
            # so pass the target explicitly instead of the automatically-computed
            # arguments.
            crate_info = crates_and_roots["gkrust"]
            package_args = ["-p", crate, "--target={arch}"]

        ret = _run_cargo_command(
            command_context,
            crate,
            crate_info,
            cargo_command,
            package_args,
            subcommand_args,
            cargo_build_flags,
            cargo_extra_flags,
            message_format_json,
            continue_on_error,
            jobs,
            verbose,
        )
        if ret != 0:
            return ret

    return 0


def _run_cargo_command(
    command_context,
    crate,
    crate_info,
    cargo_command,
    package_args,
    subcommand_args,
    cargo_build_flags,
    cargo_extra_flags,
    message_format_json,
    continue_on_error,
    jobs,
    verbose,
):
    import os
    import subprocess
    from dataclasses import replace

    from mozfile import json
    from mozshellutil import quote as shell_quote

    from mozbuild.rust_commands import (
        CARGO_SPEC_FILES,
        CargoInvocation,
        compose_env,
        compose_mach_cargo_argv,
        load_cargo_spec,
    )

    directory = crate_info["directory"]
    kind = crate_info["kind"]
    spec_path = Path(command_context.topobjdir) / directory / CARGO_SPEC_FILES[kind]
    if not spec_path.exists():
        print(
            f"No cargo spec for {crate} at {spec_path}. "
            "Run `./mach build-backend` first."
        )
        return 1
    # Compose from the same snapshot the run_cargo action reads, so both paths
    # produce the same command for one backend generation.
    command, substs, topsrcdir, topobjdir = load_cargo_spec(
        json.loads(spec_path.read_text(encoding="utf-8"))
    )

    subst = {
        "arch": substs.get("RUST_TARGET", ""),
        "crate": crate,
        "directory": directory,
        "features": ",".join(command.features),
        "manifest": command.manifest_path,
        "target": "--lib" if crate_info["kind"] == "library" else f"--bin={crate}",
        "topsrcdir": topsrcdir,
    }

    extra_cli_flags = tuple(
        _substituted([*package_args, *(subcommand_args or ())], subst)
    )
    build_flags_override = tuple(_substituted(cargo_build_flags or (), subst))
    extra_flags = tuple(_substituted(cargo_extra_flags or (), subst))

    invocation = CargoInvocation.from_environ(os.environ)
    invocation = replace(
        invocation,
        verbose=invocation.verbose or verbose,
        json_output=message_format_json,
        cargo_extra_flags=extra_flags or invocation.cargo_extra_flags,
    )

    # Every target edge uses the LTO capable Rust flags, unless the caller
    # replaced the build flags outright, in which case none of them do.
    ltoable = not build_flags_override
    env = compose_env(
        command,
        substs,
        os.environ,
        invocation,
        topsrcdir,
        topobjdir,
        ltoable=ltoable,
        subcommand=cargo_command,
    )
    if configured_path := command_context.substs.get("PATH"):
        env["PATH"] = configured_path
    argv = compose_mach_cargo_argv(
        command,
        substs,
        invocation,
        cargo_command,
        build_flags_override=build_flags_override,
        extra_cli_flags=extra_cli_flags,
        jobs=jobs,
        auto_args=not package_args,
    )

    if verbose:
        print(shell_quote(*argv))

    rc = subprocess.run(
        argv, env=env, cwd=command.working_directory, check=False
    ).returncode
    if rc == 101:
        print(
            f"If cargo-{cargo_command} is not installed, install it using: "
            f"cargo install cargo-{cargo_command}"
        )
    if rc != 0 and not continue_on_error:
        return rc
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
            check=False,
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


CLOBBER_CHOICES = {"objdir", "python", "gradle", "artifacts", "mach_func_cache"}


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
    help="Target to clobber, must be one of {{{}}} (default objdir and python).".format(
        ", ".join(CLOBBER_CHOICES)
    ),
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
    directory and all ".gradle" cache directories in the source tree.

    The `artifacts` target will remove cached artifact files from
    ~/.mozbuild/package-frontend or $MOZBUILD_STATE_PATH/package-frontend.

    The `mach_func_cache` target will remove cached results from mach's
    function cache (used to speed up configure and other operations).

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

        if full:
            what.add("mach_func_cache")

    if "mach_func_cache" in what:
        from mach.func_cache import _cache_dir

        if _cache_dir.exists():
            shutil.rmtree(_cache_dir, ignore_errors=True)

    if "python" in what:
        topsrcdir = Path(command_context.topsrcdir)

        def _pure_python_clean(srcdir: Path):
            for file in srcdir.rglob("*.py[cod]"):
                file.unlink()
            for cache_dir in srcdir.rglob("__pycache__"):
                shutil.rmtree(cache_dir, ignore_errors=True)

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
            ret = subprocess.call(cmd, cwd=topsrcdir)
        elif conditions.is_git(command_context) or conditions.is_jj(command_context):
            cmd = ["git", "clean", "-d", "-f", "-x", "*.py[cdo]", "*/__pycache__/*"]
            result = subprocess.run(
                cmd, check=False, cwd=topsrcdir, stderr=subprocess.DEVNULL
            )
            # We assume the `jj` repo is a colocated `git` repo, if not, fall back to a pure python approach
            if conditions.is_jj(command_context) and result.returncode != 0:
                _pure_python_clean(topsrcdir)
        else:
            # If no supported vcs we use a pure python approach
            _pure_python_clean(topsrcdir)

        # We'll keep this around to delete the legacy "_virtualenv" dir folders
        # so that people don't get confused if they see it and try to manipulate
        # it, but it has no effect.
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
        topsrcdir = Path(command_context.topsrcdir)
        for gradle_cache in topsrcdir.rglob(".gradle"):
            if gradle_cache.is_dir():
                shutil.rmtree(gradle_cache, ignore_errors=True)

    if "artifacts" in what:
        from mach.util import get_state_dir

        state_dir = Path(get_state_dir(specific_to_topsrcdir=False))
        artifact_cache_dir = state_dir / "package-frontend"

        if artifact_cache_dir.exists():
            print(f"Removing artifact cache directory: {artifact_cache_dir}")
            shutil.rmtree(artifact_cache_dir, ignore_errors=True)

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
        latest_file = Path(command_context._get_state_filename("latest-command"))
        if not latest_file.exists():
            command_context.log(
                logging.WARNING,
                "show_log",
                {},
                "Could not locate latest log file. You may need to run a command first.",
            )
            return
        command_name = latest_file.read_text().strip()
        subdir = f"logs/{command_name}"
        log_dir = Path(command_context._get_state_filename("", subdir=subdir))
        log_path = get_latest_file(log_dir, command_name)
        if not log_path:
            command_context.log(
                logging.WARNING,
                "show_log",
                {},
                f"No log files found for latest '{command_name}' command. They may have been deleted.",
            )
            return
        log_file = log_path.open("rb")

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
            if os_error.errno in {errno.EPIPE, errno.EINVAL}:
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
        created, action, params, msg = json.loads(line)
        if not start_time:
            start_time = created
            command_context.log_manager.terminal_handler.formatter.start_time = created
        if "line" in params:
            record = logging.makeLogRecord({
                "created": created,
                "name": command_context._logger.name,
                "levelno": logging.INFO,
                "msg": msg,
                "params": params,
                "action": action,
            })
            command_context._logger.handle(record)


# Provide commands for inspecting warnings.


def database_path(command_context):
    return get_latest_file(command_context._build_log_dir(), "warnings")


def get_warnings_database(command_context):
    from mozbuild.compilation.warnings import WarningsDatabase

    path = database_path(command_context)

    database = WarningsDatabase()

    if path and path.exists():
        database.load_from_file(str(path))

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
    "--combine-suites",
    action="store_true",
    default=False,
    help="Run multiple test suites in the same process invocation (as opposed "
    "to the default behavior of running one process per test suite).",
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
    combine_suites,
    gtest_filter,
    list_tests,
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
        "build", command_context._mach_context, what=["gtest"]
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
        if combine_suites:
            print(
                "--combine-suites is always the behavior on Android and will be ignored"
            )
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

    is_debugging = debug or debugger or debugger_args

    if is_debugging:
        args = prepend_debugger_args(args, debugger, debugger_args)
        if not args:
            return 1

    # Use GTest environment variable to control test execution
    # For details see:
    # https://google.github.io/googletest/advanced.html#running-test-programs-advanced-options
    gtest_env = dict(os.environ)
    gtest_env["GTEST_FILTER"] = gtest_filter

    # Note: we must normalize the path here so that gtest on Windows sees
    # a MOZ_GMP_PATH which has only Windows dir seperators, because
    # nsIFile cannot open the paths with non-Windows dir seperators.
    xre_path = os.path.join(os.path.normpath(command_context.topobjdir), "dist", "bin")
    gtest_env["MOZ_XRE_DIR"] = xre_path
    gtest_env["MOZ_GMP_PATH"] = os.pathsep.join(
        os.path.join(xre_path, p, "1.0") for p in ("gmp-fake", "gmp-fakeopenh264")
    )

    gtest_env["MOZ_RUN_GTEST"] = "True"

    # For parity with CI mozharness test runner, provide Python path in an environment variable
    gtest_env["PYTHON"] = sys.executable

    if shuffle:
        gtest_env["GTEST_SHUFFLE"] = "True"

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

    log_actions = get_log_actions()
    formatter = MachFormatter(
        start_time=command_context.log_manager.start_time,
    )
    gtest_log = StructuredLogger("gtest")
    gtest_log.add_handler(StreamHandler(sys.stdout, formatter))
    gtest_log.add_handler(ResourceHandler(command_context))

    def format_gtest_line(line, prefix=None):
        line = line.rstrip()
        try:
            data = json.loads(line)
            if (
                isinstance(data, dict)
                and "action" in data
                and data["action"] in log_actions
            ):
                if "time" not in data:
                    data["time"] = int(time.time() * 1000)
                gtest_log.log_raw(data)
                return
        except (ValueError, KeyError):
            pass
        gtest_log.process_output(prefix or "gtest", line)

    # Don't bother with multiple processes if:
    # - listing tests
    # - running the debugger
    # - combining suites with one job
    if list_tests or is_debugging or (combine_suites and jobs == 1):
        if is_debugging:
            result = command_context.run_process(
                args=args,
                append_env=gtest_env,
                cwd=cwd,
                ensure_exit_code=False,
                pass_thru=True,
            )
            gtest_log.shutdown()
            return result
        result = command_context.run_process(
            args=args,
            append_env=gtest_env,
            cwd=cwd,
            ensure_exit_code=False,
            line_handler=format_gtest_line,
        )
        gtest_log.shutdown()
        return result

    report = AggregatedGTestReport()

    with report:
        from mozprocess import ProcessHandlerMixin

        processes = []

        def add_process(job_id, append_env, **kwargs):
            def log_line(line):
                format_gtest_line(line, prefix=job_id)

            env = os.environ.copy()
            # Allow the new environment to overwrite system environment variables.
            env.update(append_env)

            report.set_output_in_env(env, job_id)

            proc = ProcessHandlerMixin(
                [app_path, "-unittest"],
                cwd=cwd,
                universal_newlines=True,
                env=env,
                processOutputLine=log_line,
                **kwargs,
            )
            processes.append(proc)
            return proc

        if combine_suites:
            # Use GTest sharding to create `jobs` processes
            gtest_env["GTEST_TOTAL_SHARDS"] = str(jobs)

            for i in range(0, jobs):
                env = gtest_env.copy()
                env["GTEST_SHARD_INDEX"] = str(i)
                add_process(str(i), env).run()
        else:
            # Make one process per test suite
            suites = get_gtest_suites(args, cwd, gtest_env)

            from threading import Event, Lock

            processes_to_run = []
            all_processes_run = Event()
            running_suites = set()
            process_state_lock = Lock()

            def run_next(finished_suite=None):
                """
                Run another test suite process.

                If `finished_suite` is provided, it will be considered as finished.
                This updates the `running_suites` set and will signal the
                `all_processes_run` Event when there are no longer any test suites
                to start (though some may still be running).

                This may be safely called from different threads
                (ProcessHandlerMixin callbacks occur from separate threads).
                """
                # The changes here must be synchronized, so acquire a lock for the
                # duration of the function.
                with process_state_lock:
                    if finished_suite is not None:
                        running_suites.remove(finished_suite)
                    if len(processes_to_run) > 0:
                        next_suite, proc = processes_to_run.pop()
                        proc.run()
                        running_suites.add(next_suite)
                        command_context.log(
                            logging.DEBUG,
                            "GTest",
                            {},
                            f"Starting {next_suite} tests. {len(processes_to_run)} suites remain.",
                        )
                    else:
                        all_processes_run.set()
                    if len(running_suites) > 0:
                        command_context.log(
                            logging.INFO,
                            "GTest",
                            {},
                            f"Currently running suites: {', '.join(running_suites)}",
                        )

            for filt in suite_filters(suites):
                proc = add_process(
                    filt.suite,
                    filt(gtest_env.copy()),
                    onFinish=functools.partial(run_next, filt.suite),
                )
                processes_to_run.append((filt.suite, proc))

            # Start a number of processes according to 'jobs'. As they finish,
            # they'll each kick off another one.
            for _ in range(jobs):
                run_next()

            # Wait for all processes to have been started, then wait on completion.
            all_processes_run.wait()

        # Wait on processes and return a non-zero exit code if any process does so.
        exit_code = 0
        for process in processes:
            status = process.wait()
            if status:
                exit_code = status

        # Clamp error code to 255 to prevent overflowing multiple of
        # 256 into 0
        exit_code = min(exit_code, 255)

    # Show aggregated report information and any test errors.
    command_context.log(
        logging.INFO,
        "GTest",
        {
            "tests": report["tests"] - report["disabled"],
            "failures": report["failures"],
            "disabled": report["disabled"],
            "suites": len(report["testsuites"]),
        },
        "Ran {tests} test(s) from {suites} test suite(s) ({disabled} disabled), with {failures} failure(s).",
    )

    for suite in report["testsuites"]:
        if suite["failures"] == 0:
            continue
        for test in suite["testsuite"]:
            if "failures" not in test:
                continue
            full_name = f"{suite['name']}.{test['name']}"
            command_context.log(
                logging.ERROR,
                "GTest",
                {
                    "test": full_name,
                    "failure_count": len(test["failures"]),
                    "failures": "\n".join(e["failure"] for e in test["failures"]),
                },
                "{test} failed {failure_count} check(s):\n{failures}",
            )

    gtest_log.shutdown()
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
    "source-package",
    category="misc",
    description="Package source for distribution.",
)
@CommandArgument(
    "-o",
    "--output",
    type=str,
    default=None,
    help="Force archive name.",
)
@CommandArgument(
    "--upload",
    type=str,
    default="",
    help="Compute package check sum and move both to the given location.",
)
def source_package(command_context, output, upload):
    substs = command_context.substs
    if conditions.is_jsshell(command_context):
        if output:
            command_context.log(
                logging.ERROR,
                "source-package-unsupported-output",
                {},
                "Source package output is currently not supported for SpiderMonkey",
            )
            return 1
        js_src = os.path.join(command_context.topsrcdir, "js", "src")
        command_context.run_process(
            [sys.executable, os.path.join(js_src, "make-source-package.py")],
            cwd=js_src,
            append_env={
                "DIST": substs["DIST"],
                "MOZJS_MAJOR_VERSION": substs["MOZJS_MAJOR_VERSION"],
                "MOZJS_MINOR_VERSION": substs["MOZJS_MINOR_VERSION"],
                "MOZJS_PATCH_VERSION": substs["MOZJS_PATCH_VERSION"],
                "MOZJS_ALPHA": substs["MOZJS_ALPHA"],
            },
            ensure_exit_code=0,
        )
        if upload:
            command_context.log(
                logging.ERROR,
                "source-package-unsupported-upload",
                {},
                "Source package upload is currently supported only for Firefox",
            )
            return 1
    elif substs.get("MOZ_WIDGET_TOOLKIT"):
        if output:
            if not output.endswith(".tar.xz"):
                command_context.log(
                    logging.ERROR,
                    "source-package-unsupported-output",
                    {},
                    "Source package output must use the .tar.xz extension",
                )
                return 1

        rc = command_context._mach_context.commands.dispatch(
            "build",
            command_context._mach_context,
            what=["buildid.h"],
            directory=".",
            priority="normal",
            no_completion_messages=True,
        )
        if rc != 0:
            return rc
        with open(os.path.join(command_context.topobjdir, "buildid.h")) as fd:
            _, _, buildid = fd.read().split()

        # FIXME: don't create this in topsrcdir
        sourcestamp_path = os.path.join(command_context.topsrcdir, "sourcestamp.txt")
        if conditions.is_thunderbird(command_context):
            comm_repo = substs.get("MOZ_COMM_SOURCE_REPO")
            comm_changeset = substs.get("MOZ_COMM_SOURCE_CHANGESET")
            gecko_repo = substs.get("MOZ_GECKO_SOURCE_REPO")
            gecko_changeset = substs.get("MOZ_GECKO_SOURCE_CHANGESET")

            # Thunderbird tarball builds require sourcestamp.txt to contain
            # three lines, e.g.:
            #   20260522210329
            #   https://hg.mozilla.org/releases/comm-esr140/rev/191059ac655733d24c4fd32c94dfe6d79af7028b
            #   https://hg.mozilla.org/releases/mozilla-esr140/rev/2e36c464a92f1942683abbed6ceb442308db5eb0
            with open(sourcestamp_path, "w") as fd:
                fd.write(
                    f"{buildid}\n"
                    f"{comm_repo}/rev/{comm_changeset}\n"
                    f"{gecko_repo}/rev/{gecko_changeset}\n"
                )
        else:
            source_url = ""
            if substs.get("MOZ_INCLUDE_SOURCE_INFO"):
                repo = substs.get("MOZ_SOURCE_REPO")
                changeset = substs.get("MOZ_SOURCE_CHANGESET")
                if repo and changeset:
                    source_url = f"{repo}/rev/{changeset}"

            with open(sourcestamp_path, "w") as fd:
                fd.write(f"{buildid}\n{source_url}")

        # this should match SOURCE_TAR in packager.mk.
        archive_prefix = f"{substs['MOZ_APP_NAME']}-{substs['MOZ_APP_VERSION']}"
        archive_path = os.path.join(
            substs["DIST"], output or f"{archive_prefix}.tar.xz"
        )

        # FIXME: this list would not exist if we relying on vcs output
        excludes = [
            "--exclude=./.hg*",
            "--exclude=./.git",
            "--exclude=./.gitattributes",
            "--exclude=./.gitkeep",
            "--exclude=./.gitmodules",
            "--exclude=CVS",
            "--exclude=.cvs*",
            "--exclude=./.mozconfig*",
            "--exclude=*.pyc",
            "--exclude=./Makefile",
            f"--exclude=./{substs['DIST']}",
            f"--exclude={os.path.basename(command_context.topobjdir)}",
        ]
        from mozbuild.action.package import is_gnu_tar

        tar = substs["TAR"]
        if is_gnu_tar(tar):
            tar_flags = ["--mode=go-w", f"--transform=s,^./,{archive_prefix}/,"]
        else:
            tar_flags = ["-s", f",^./,{archive_prefix}/,"]
        command_context.run_process(
            [
                tar,
                "-cJv",
                "--owner=0",
                "--group=0",
                "--numeric-owner",
                *excludes,
                *tar_flags,
                "-f",
                archive_path,
                "./",
            ],
            ensure_exit_code=True,
            pass_thru=True,
        )
        if upload:
            checksum_path = archive_path[: -len(".tar.xz")] + ".checksums"
            command_context._run_make(
                target=[
                    "upload",
                    f"UPLOAD_PATH={upload}",
                    f"UPLOAD_FILES={archive_path}",
                    f"CHECKSUM_FILE={checksum_path}",
                ],
                ensure_exit_code=True,
            )

    else:
        command_context.log(
            logging.ERROR,
            "source-package-unsupported-product",
            {},
            "Source packaging is not supported for the current project",
        )
        return 1
    command_context.notify("Packaging complete")


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

    elif conditions.is_ios(command_context):
        from mozrunner.devices.ios_device import verify_ios_device

        ret = verify_ios_device(command_context, install=True, **kwargs) == 0
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
        help="Do not wait for application to start before returning (default: False)",
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
    appdata_group = group.add_mutually_exclusive_group()
    appdata_group.add_argument(
        "--appdata",
        "-a",
        nargs="?",
        const=True,
        default=None,
        help="Overrides the application data storage area. Without an argument, "
        "defaults to a temporary location in the object directory. When passed "
        "explicitly, also implies --noprofile. This override is enabled by "
        "default even without -a; pass --default-appdata to disable it.",
    )
    appdata_group.add_argument(
        "--default-appdata",
        action="store_true",
        default=False,
        help="Use the system default application data directory instead of "
        "overriding it to a location in the object directory.",
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
        help="Run the program using a new temporary profile created inside the objdir.",
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
    from shlex import quote as shlex_quote

    from mozrunner.devices.android_device import (
        InstallIntent,
        UninstallIntent,
        _get_device,
        metadata_for_app,
        verify_android_device,
    )

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

            our_debugger_args = mozshellutil.quote("-s", tmp_lldb_start_script)
            if debugger_args:
                full_debugger_args = " ".join([debugger_args, our_debugger_args])
            else:
                full_debugger_args = our_debugger_args

            args = prepend_debugger_args([], debugger, full_debugger_args)
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


def _run_ios(command_context, no_install=None, debug=False):
    from mozdevice.ios import IosDevice
    from mozrunner.devices.ios_device import (
        verify_ios_device,
    )

    app = "org.mozilla.ios.GeckoTestBrowser"

    # `verify_ios_device` respects sets `DEVICE_UUID`
    verify_ios_device(
        command_context,
        app=app,
        install=not no_install,
    )
    device_serial = os.environ.get("DEVICE_UUID")
    if not device_serial:
        print("No iOS devices connected.")
        return 1

    device = IosDevice.select_device(conditions.is_ios_simulator(command_context))
    if debug:
        print("Application will pause after starting until a debugger is connected...")
    proc = device.launch_process(
        app,
        wait_for_debugger=debug,
        stdout=None,
        stderr=None,
    )
    proc.run()
    proc.wait()


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

        args = prepend_debugger_args(args, debugger, debugger_args)
        if not args:
            return 1

    return command_context.run_process(
        args=args, ensure_exit_code=False, pass_thru=True, append_env=extra_env
    )


MOZILLA_MACOS_TEAM_ID = "43AQ936H96"


def _macos_is_signed_by_mozilla(path):
    try:
        proc = subprocess.run(
            ["codesign", "-dvv", path],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return False
    m = re.search(r"^TeamIdentifier=(.+)$", proc.stderr, re.MULTILINE)
    return bool(m) and m.group(1) == MOZILLA_MACOS_TEAM_ID


def _run_desktop(
    command_context,
    params,
    packaged,
    app,
    background,
    noprofile,
    appdata,
    default_appdata,
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

    if default_appdata:
        use_appdata = False
    elif appdata is None:
        use_appdata = True
    else:
        use_appdata = appdata

    skip_profile = appdata is not None

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

    tmpdir = os.path.join(command_context.topobjdir, "tmp")
    if not os.path.exists(tmpdir):
        os.makedirs(tmpdir)

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
        and not skip_profile
    ):
        prefs = {
            "browser.aboutConfig.showWarning": False,
            "browser.shell.checkDefaultBrowser": False,
        }
        prefs.update(command_context._mach_context.settings.runprefs)
        prefs.update([p.split("=", 1) for p in setpref])
        for pref in prefs:
            prefs[pref] = Preferences.cast(prefs[pref])

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

    if (
        not use_appdata
        and sys.platform == "darwin"
        and conditions.is_firefox(command_context)
        and "MOZ_APP_DATA" not in os.environ
        and Version(platform.mac_ver()[0]) >= Version("27")
    ):
        # Starting with macOS 27, Firefox's data directory (by default
        # `~/Library/Application Support/Firefox` unless overriden by the
        # environment variable MOZ_APP_DATA) is not accessible unless the
        # application is signed by Mozilla or given permission to read
        # Firefox data in macOS Privacy & Security system settings. For
        # `./mach run`, which launches Firefox by bare executable (unless
        # --macos-open is used), the terminal application is the
        # "responsible process" and therefore giving it permission to read
        # Firefox data is sufficient for `./mach run` to work normally.
        # The extended attribute `com.apple.macl` indicates protection is
        # enabled for the directory, but reading the attribute is prevented
        # when the protection is enabled. For `--macos-open`, either an
        # alternate app data directory or a signed build must be used.
        if macos_open:
            app_data_protected = not _macos_is_signed_by_mozilla(apppath)
        else:
            app_data_dir = os.path.expanduser("~/Library/Application Support/Firefox")
            app_data_protected = os.path.isdir(app_data_dir) and not os.access(
                app_data_dir, os.R_OK
            )

        if app_data_protected:
            command_context.log(
                logging.ERROR,
                "run",
                {},
                "Firefox's application data directory could not be read "
                "due to macOS application data protections. Allow the "
                "terminal access to Firefox data in macOS Privacy & "
                "Security -> Files & Folders settings to allow builds launched "
                "from the CLI to access profile data. Alternatively, remove "
                "`--default-appdata` OR set MOZ_APP_DATA & MOZ_LOCAL_APP_DATA "
                "environment variables to use an alternate app directory for "
                "all instances launched from the terminal. See bug 2068208 for "
                "more information.",
            )

    if use_appdata:
        appdata_dir = use_appdata if isinstance(use_appdata, str) else tmpdir

        extra_env["MOZ_APP_DATA"] = os.path.normpath(
            os.path.join(appdata_dir, "AppData", "Roaming")
        )
        command_context.log(
            logging.INFO,
            "run",
            {"app_data": extra_env["MOZ_APP_DATA"]},
            "Overriding application data directory to {app_data}",
        )
        extra_env["MOZ_LOCAL_APP_DATA"] = os.path.normpath(
            os.path.join(appdata_dir, "Local")
        )
        command_context.log(
            logging.INFO,
            "run",
            {"local_app_data": extra_env["MOZ_LOCAL_APP_DATA"]},
            "Overriding local application data directory to {local_app_data}",
        )

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

        args = prepend_debugger_args(args, debugger, debugger_args)
        if not args:
            return 1

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
    "--product",
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
    product,
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
        product,
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
    "--product",
    type=str,
    required=True,
    help="The product being shipped. Used to disambiguate beta/devedition etc.",
)
@CommandArgument(
    "--extensions-dir",
    type=str,
    required=True,
    help="Path to extensions.",
)
def repackage_deb_l10n(
    command_context,
    input_xpi_file,
    input_tar_file,
    output,
    version,
    build_number,
    templates,
    product,
    extensions_dir,
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
        product,
        extensions_dir,
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
    "--product",
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
    product,
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
        product,
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
    choices=["official", "beta", "esr", "aurora", "nightly", "unofficial"],
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
    help="Support `Add-AppxPackage ... -AllowUnsigned` on Windows 11.(Default: false)",
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
            "Couldn't find a command to run snap as root; please use the --sudo option",
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
    description="Prepare a firefox.desktop file for snap",
    virtualenv_name="repackage-desktop-file",
)
@CommandArgument("--output", type=str, required=True, help="Output desktop file")
@CommandArgument(
    "--flavor",
    type=str,
    required=False,
    choices=["snap"],
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
    from mozbuild.repackaging.snapcraft_transform import (
        SnapDesktopFile,
    )

    desktop = SnapDesktopFile(
        command_context.log,
        appname=release_product,
        branchname=release_type,
        wmclass=wmclass,
    ).repack()

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
@CommandArgument(
    "--package-tests",
    action="store_true",
    help="Package the multi-locale test archives after packaging.",
)
def package_l10n(command_context, locales, verbose=False, package_tests=False):
    locales = sorted(locale for locale in locales if locale != "en-US")

    append_env = {
        # We are only (re-)packaging, we don't want to (re-)build
        # anything inside Gradle.
        "GRADLE_INVOKED_WITHIN_MACH_BUILD": "1",
        "MOZ_CHROME_MULTILOCALE": " ".join(locales),
    }

    ensure_l10n_central(command_context)

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

    if package_tests:
        command_context._run_make(
            directory=command_context.topobjdir,
            target=["package-tests", "AB_CD=multi"],
            pass_thru=True,
            ensure_exit_code=True,
        )

    command_context.log(
        logging.WARNING,
        "package-multi-locale",
        {"locales": locales},
        "A later `./mach build` rewrites the chrome manifests in dist/bin "
        "without the registrations for {locales}. After a build, run "
        "`./mach package-multi-locale` again before packaging.",
    )

    return 0


@SubCommand(
    "package-multi-locale",
    "upload",
    description="Copy the multi-locale package to $UPLOAD_PATH.",
)
def package_l10n_upload(command_context):
    command_context._run_make(
        directory=command_context.topobjdir,
        target=["upload", "AB_CD=multi"],
        pass_thru=True,
        ensure_exit_code=True,
    )
    return 0


def run_mach(command_context, cmd, **kwargs):
    return command_context._mach_context.commands.dispatch(
        cmd, command_context._mach_context, **kwargs
    )


@Command(
    "repackage-single-locales",
    category="post-build",
    description="Repackage single-locale versions of the built product "
    "for distribution as APKs, DMGs, etc.",
)
@CommandArgument(
    "--locales",
    metavar="LOCALES",
    nargs="+",
    required=True,
    help="List of locales to repackage",
)
@CommandArgument(
    "--verbose", action="store_true", help="Log informative status messages."
)
@CommandArgument(
    "--dest",
    default=None,
    help="Destination directory to populate with localized artifacts; "
    + "default: UPLOAD_PATH environment variable if set; '$topobjdir/dist/repackage-single-locales' if not set",
)
def repackage_single_locales(command_context, verbose=False, locales=[], dest=None):
    if not dest:
        dest = os.environ.get(
            "UPLOAD_PATH",
            mozpath.join(command_context.topobjdir, "dist", "repackage-single-locales"),
        )
    dest = os.path.abspath(dest)

    locales = sorted(locale for locale in locales if locale != "en-US")

    append_env = {
        # Simple as possible, please!
        "MOZ_SIMPLE_PACKAGE_NAME": "target",
    }

    ensure_l10n_central(command_context)

    command_context.log(
        logging.INFO,
        "repackage-single-locales",
        {"locales": locales},
        "Processing chrome Gecko resources for locales {locales}",
    )

    def prefixed_line_handler(prefix):
        def line_handler(line):
            command_context.log(
                logging.INFO,
                "repackage-single-locales",
                {"prefix": prefix, "line": line},
                "{prefix}> {line}",
            )

        return line_handler

    command_context.run_process(
        [
            sys.executable,
            mozpath.join(command_context.topsrcdir, "mach"),
            "--log-no-times",
            "build",
            "export",
        ]
        + (["-v"] if verbose else []),
        append_env=append_env,
        pass_thru=False,
        ensure_exit_code=True,
        line_handler=prefixed_line_handler("export"),
    )

    command_context.reload_config_environment()

    from mozbuild.action.l10n_repackage import uses_local_package

    en_us_package = en_us_snapshot = None
    if uses_local_package(command_context.substs):
        suffix = command_context.substs["PKG_SUFFIX"]
        package_name = append_env["MOZ_SIMPLE_PACKAGE_NAME"]
        en_us_package = (
            Path(command_context.topobjdir) / "dist" / f"{package_name}{suffix}"
        )
        if not en_us_package.is_file():
            # `MOZ_SIMPLE_PACKAGE_NAME` gives the package this fixed name, so
            # the package built here is the one every locale unpacks.
            command_context.log(
                logging.INFO,
                "repackage-single-locales",
                {"package": str(en_us_package)},
                "Building the en-US package {package}",
            )
            command_context.run_process(
                [
                    sys.executable,
                    mozpath.join(command_context.topsrcdir, "mach"),
                    "--log-no-times",
                    "package",
                ]
                + (["-v"] if verbose else []),
                append_env=append_env,
                pass_thru=False,
                ensure_exit_code=True,
                line_handler=prefixed_line_handler("package"),
            )
        en_us_snapshot = en_us_package.with_name(f"{package_name}.en-US{suffix}")
        shutil.copy2(en_us_package, en_us_snapshot)
        append_env["MOZ_ARTIFACT_FILE"] = str(en_us_snapshot)

    try:
        for locale in locales:
            command_context.log(
                logging.INFO,
                "repackage-single-locales",
                {"locale": locale},
                "Repackaging locale {locale}",
            )

            line_handler = prefixed_line_handler(locale)

            command_context.run_process(
                [
                    sys.executable,
                    mozpath.join(command_context.topsrcdir, "mach"),
                    "--log-no-times",
                    "configure",
                    f"--enable-ui-locale={locale}",
                ],
                append_env=append_env,
                pass_thru=False,
                ensure_exit_code=True,
                line_handler=line_handler,
            )

            command_context.run_process(
                [
                    sys.executable,
                    mozpath.join(command_context.topsrcdir, "mach"),
                    "--log-no-times",
                    "build",
                ]
                + (["-v"] if verbose else [])
                + [
                    f"installers-{locale}",
                ],
                append_env=append_env,
                pass_thru=False,
                ensure_exit_code=True,
                line_handler=line_handler,
            )

            append_env["UPLOAD_PATH"] = mozpath.join(dest, locale)

            command_context._run_make(
                directory=os.path.join(command_context.topobjdir),
                target=["upload", f"AB_CD={locale}"],
                append_env=append_env,
                pass_thru=False,
                print_directory=False,
                ensure_exit_code=True,
                silent=not verbose,
                # We do our own logging.
                log=False,
                line_handler=line_handler,
            )
    finally:
        if en_us_snapshot:
            shutil.move(en_us_snapshot, en_us_package)

    return 0


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
