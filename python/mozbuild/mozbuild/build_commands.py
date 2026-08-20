# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import subprocess
from urllib.parse import quote

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument

from mozbuild.backend import backends
from mozbuild.bootstrap import bootstrap_toolchain
from mozbuild.mozconfig import MozconfigLoader
from mozbuild.util import (
    MOZBUILD_METRICS_PATH,
    ensure_l10n_central,
    get_latest_file,
    is_running_under_coding_agent,
)

BUILD_WHAT_HELP = """
What to build. Can be a top-level make target or a relative directory. If
multiple options are provided, they will be built serially. BUILDING ONLY PARTS
OF THE TREE CAN RESULT IN BAD TREE STATE. USE AT YOUR OWN RISK.
""".strip()


def _set_priority(command_context, priority, verbose):
    # Choose the Windows API structure to standardize on.
    PRIO_CLASS_BY_KEY = {
        "idle": "IDLE_PRIORITY_CLASS",
        "less": "BELOW_NORMAL_PRIORITY_CLASS",
        "normal": "NORMAL_PRIORITY_CLASS",
        "more": "ABOVE_NORMAL_PRIORITY_CLASS",
        "high": "HIGH_PRIORITY_CLASS",
    }
    try:
        prio_class = PRIO_CLASS_BY_KEY[priority]
    except KeyError:
        raise KeyError(f"priority '{priority}' not in {list(PRIO_CLASS_BY_KEY)}")

    if "nice" in dir(os):
        # Translate the Windows priority classes into niceness values.
        NICENESS_BY_PRIO_CLASS = {
            "IDLE_PRIORITY_CLASS": 19,
            "BELOW_NORMAL_PRIORITY_CLASS": 10,
            "NORMAL_PRIORITY_CLASS": 0,
            "ABOVE_NORMAL_PRIORITY_CLASS": -10,
            "HIGH_PRIORITY_CLASS": -20,
        }
        niceness = NICENESS_BY_PRIO_CLASS[prio_class]

        os.nice(niceness)
        if verbose:
            command_context.log(
                logging.INFO, "priority", {"niceness": niceness}, "os.nice({niceness})"
            )
        return True

    try:
        import psutil

        prio_class_val = getattr(psutil, prio_class)
    except ModuleNotFoundError:
        return False
    except AttributeError:
        return False

    psutil.Process().nice(prio_class_val)
    if verbose:
        command_context.log(
            logging.INFO,
            "priority",
            {"prio_class": prio_class},
            "psutil.Process().nice(psutil.{prio_class})",
        )
    return True


# Interface to build the tree.


@Command(
    "build",
    category="build",
    description="Build the tree.",
    metrics_path=MOZBUILD_METRICS_PATH,
    virtualenv_name="build",
)
@CommandArgument(
    "--jobs",
    "-j",
    default="0",
    metavar="jobs",
    type=int,
    help="Number of concurrent jobs to run. Default is based on the number of "
    "CPUs and the estimated size of the jobs (see --job-size).",
)
@CommandArgument(
    "--job-size",
    default="0",
    metavar="size",
    type=float,
    help="Estimated RAM required, in GiB, for each parallel job. Used to "
    "compute a default number of concurrent jobs.",
)
@CommandArgument(
    "-C",
    "--directory",
    default=None,
    help="Change to a subdirectory of the build directory first.",
)
@CommandArgument("what", default=None, nargs="*", help=BUILD_WHAT_HELP)
@CommandArgument(
    "-v",
    "--verbose",
    action="store_true",
    help="Verbose output for what commands the build is running. Implies "
    "--show-all-warnings.",
)
@CommandArgument(
    "-q",
    "--quiet",
    dest="quiet",
    default=False,
    action="store_true",
    help="Suppress most output, showing only errors and warnings.",
)
@CommandArgument(
    "--keep-going",
    action="store_true",
    help="Keep building after an error has occurred",
)
@CommandArgument(
    "--priority",
    default="idle",
    metavar="priority",
    type=str,
    help="idle/less/normal/more/high. (Default idle)",
)
@CommandArgument(
    "--show-all-warnings",
    default=False,
    action="store_true",
    help="Show all warnings including third-party and suppressed warnings.",
)
@CommandArgument(
    "--allow-subdirectory-build",
    default=False,
    action="store_true",
    help="Allow building subdirectories (not recommended, can result in bad tree state).",
)
@CommandArgument(
    "--no-completion-messages",
    default=False,
    action="store_true",
    help="Suppress the build completion messages (useful for nested builds).",
)
def build(
    command_context,
    what=None,
    jobs=0,
    job_size=0,
    directory=None,
    verbose=False,
    quiet=None,
    keep_going=False,
    priority="idle",
    show_all_warnings=None,
    allow_subdirectory_build=False,
    no_completion_messages=False,
):
    """Build the source tree.

    With no arguments, this will perform a full build.

    Positional arguments define targets to build. These can be make targets
    or patterns like "<dir>/<target>" to indicate a make target within a
    directory.

    There are a few special targets that can be used to perform a partial
    build faster than what `mach build` would perform:

    * binaries - compiles and links all C/C++/Obj-C/Rust sources and produces
      shared libraries and executables (binaries).

    * faster - builds JavaScript, XUL, CSS, etc files.

    "binaries" and "faster" almost fully complement each other. However,
    there are build actions not captured by either. If things don't appear to
    be rebuilding, perform a vanilla `mach build` to rebuild the world.
    """
    from mozbuild.controller.building import BuildDriver

    command_context.log_manager.enable_all_structured_loggers()

    original_log_level = command_context.log_manager.terminal_handler.level
    try:
        if quiet and (verbose or show_all_warnings):
            command_context.log(
                logging.ERROR,
                "build",
                {},
                "--quiet is mutually exclusive with --verbose and --show-all-warnings.",
            )
            return 1

        # Force verbosity on automation, unless output was explicitly quieted.
        verbose = verbose or (bool(os.environ.get("MOZ_AUTOMATION")) and not quiet)
        show_all_warnings = show_all_warnings or verbose

        from mach.logging import THIRD_PARTY_WARNING

        if not verbose and is_running_under_coding_agent():
            command_context.log(
                logging.WARNING,
                "build",
                {},
                "AI agent detected. Terminal output limited to warnings and errors.",
            )
            quiet = True

            if command_context.log_file_path:
                command_context.log(
                    logging.WARNING,
                    "build",
                    {"logfile": command_context.log_file_path},
                    "Full output: {logfile}",
                )
            else:
                command_context.log(
                    logging.WARNING,
                    "build",
                    {},
                    "Log file could not be created.",
                )

        if quiet:
            command_context.log_manager.terminal_handler.setLevel(logging.WARNING)

        if show_all_warnings:
            command_context.log_manager.terminal_handler.setLevel(THIRD_PARTY_WARNING)

        if (
            command_context.log_manager.terminal_handler.level > THIRD_PARTY_WARNING
            and not is_running_under_coding_agent()
        ):
            warnings_path = os.path.join(
                command_context.topobjdir,
                ".mozbuild",
                "logs",
                "build",
                "warnings_*.json",
            )
            command_context.log(
                logging.WARNING,
                "build",
                {},
                "Warnings in third-party code are being suppressed from the terminal output. "
                "Use --show-all-warnings or --verbose to see them.",
            )
            command_context.log(
                logging.WARNING,
                "build",
                {"warnings_path": warnings_path},
                "All warnings will still be dumped to {warnings_path} at the end of the build.",
            )

        loader = MozconfigLoader(command_context.topsrcdir)
        mozconfig = loader.read_mozconfig(loader.AUTODETECT)
        configure_args = mozconfig["configure_args"]
        doing_pgo = configure_args and "MOZ_PGO=1" in configure_args
        # Keep going by default on automation so that we exhaust as many errors as
        # possible.
        keep_going = keep_going or bool(os.environ.get("MOZ_AUTOMATION"))
        append_env = None

        # By setting the current process's priority, by default our child processes
        # will also inherit this same priority.
        if not _set_priority(command_context, priority, verbose):
            command_context.log(
                logging.WARNING,
                "priority",
                {},
                "--priority not supported on this platform.",
            )

        for target in what:
            if target.startswith("installers-"):
                ensure_l10n_central(command_context)
                break

        if doing_pgo:
            if what:
                raise Exception(
                    "Cannot specify targets (%s) in MOZ_PGO=1 builds" % what
                )
            instr = command_context._spawn(BuildDriver)
            orig_topobjdir = instr._topobjdir
            instr._topobjdir = mozpath.join(instr._topobjdir, "instrumented")

            use_extended_corpus = (
                configure_args and "MOZ_PGO_EXTENDED_CORPUS=1" in configure_args
            )

            if use_extended_corpus:
                pgo_extended_corpus = bootstrap_toolchain("pgo-extended-corpus")
                if not pgo_extended_corpus:
                    raise Exception("Cannot find pgo-extended-corpus.")

            append_env = {"MOZ_PROFILE_GENERATE": "1"}
            status = instr.build(
                command_context.metrics,
                what=what,
                jobs=jobs,
                job_size=job_size,
                directory=directory,
                verbose=verbose,
                keep_going=keep_going,
                mach_context=command_context._mach_context,
                append_env=append_env,
                allow_subdirectory_build=allow_subdirectory_build,
            )
            if status != 0:
                return status

            # Packaging the instrumented build is required to get the jarlog
            # data.
            status = instr._run_make(
                directory=".",
                target="package",
                silent=not verbose,
                ensure_exit_code=False,
                append_env=append_env,
            )
            if status != 0:
                return status

            pgo_env = os.environ.copy()
            if instr.config_environment.substs.get("CC_TYPE") in ("clang", "clang-cl"):
                pgo_env["LLVM_PROFDATA"] = instr.config_environment.substs.get(
                    "LLVM_PROFDATA"
                )
            pgo_env["JARLOG_FILE"] = mozpath.join(orig_topobjdir, "jarlog/en-US.log")
            pgo_cmd = [
                command_context.virtualenv_manager.python_path,
                mozpath.join(command_context.topsrcdir, "build/pgo/profileserver.py"),
            ]
            if use_extended_corpus:
                pgo_cmd.extend(["--extended-corpus", str(pgo_extended_corpus)])

            subprocess.check_call(pgo_cmd, cwd=instr.topobjdir, env=pgo_env)

            # Set the default build to MOZ_PROFILE_USE
            append_env = {"MOZ_PROFILE_USE": "1"}

        driver = command_context._spawn(BuildDriver)
        return driver.build(
            command_context.metrics,
            what=what,
            jobs=jobs,
            job_size=job_size,
            directory=directory,
            verbose=verbose,
            keep_going=keep_going,
            mach_context=command_context._mach_context,
            append_env=append_env,
            allow_subdirectory_build=allow_subdirectory_build,
            no_completion_messages=no_completion_messages,
        )
    finally:
        command_context.log_manager.terminal_handler.setLevel(original_log_level)


@Command(
    "configure",
    category="build",
    description="Configure the tree (run configure and config.status).",
    metrics_path=MOZBUILD_METRICS_PATH,
    virtualenv_name="build",
)
@CommandArgument(
    "options", default=None, nargs=argparse.REMAINDER, help="Configure options"
)
def configure(
    command_context,
    options=None,
    buildstatus_messages=False,
    line_handler=None,
):
    from mozbuild.controller.building import BuildDriver

    command_context.log_manager.enable_all_structured_loggers()
    driver = command_context._spawn(BuildDriver)

    return driver.configure(
        command_context.metrics,
        options=options,
        buildstatus_messages=buildstatus_messages,
        line_handler=line_handler,
    )


@Command(
    "resource-usage",
    category="post-build",
    description="Show a profile of the build in the Firefox Profiler.",
    virtualenv_name="build",
)
@CommandArgument(
    "--address",
    default="localhost",
    help="Address the HTTP server should listen on.",
)
@CommandArgument(
    "--port",
    type=int,
    default=0,
    help="Port number the HTTP server should listen on.",
)
@CommandArgument(
    "--browser",
    help="Web browser to automatically open. See webbrowser Python module.",
)
@CommandArgument("--url", help="URL of a build profile to display")
def resource_usage(command_context, address=None, port=None, browser=None, url=None):
    import webbrowser

    from mozbuild.html_build_viewer import BuildViewerServer

    server = BuildViewerServer(address, port)

    if url:
        server.add_resource_json_url("profile", url)
    else:
        from pathlib import Path

        # Find the most recent profile across all log subdirectories
        profile = None
        logs_dir = os.path.join(command_context.statedir, "logs")

        if os.path.isdir(logs_dir):
            for subdir in os.listdir(logs_dir):
                subdir_path = os.path.join(logs_dir, subdir)
                if not os.path.isdir(subdir_path):
                    continue
                latest_in_subdir = get_latest_file(subdir_path, "profile")
                if latest_in_subdir and (
                    not profile
                    or Path(latest_in_subdir).stat().st_mtime
                    > Path(profile).stat().st_mtime
                ):
                    profile = latest_in_subdir

        if not profile:
            command_context.log(
                logging.WARNING,
                "build_resources",
                {},
                "Build resources not available. If you have performed a "
                "build and receive this message, the psutil Python package "
                "likely failed to initialize properly.",
            )
            return 1

        # Display the URL to trigger VSCode Remote to autoforward the port.
        print(f"Hosting profile data server at {server.url}")

        server.add_resource_json_file("profile", str(profile))

    profiler_url = "https://profiler.firefox.com/from-url/" + quote(
        server.url + "resources/profile", ""
    )
    try:
        webbrowser.get(browser).open_new_tab(profiler_url)
    except Exception:
        command_context.log(
            logging.WARNING,
            "resource_usage",
            {},
            "Cannot get browser specified, trying the default instead.",
        )
        try:
            browser = webbrowser.get().open_new_tab(profiler_url)
        except Exception:
            command_context.log(
                logging.INFO,
                "resource_usage",
                {"url": profiler_url},
                "Please open {url} in a browser.",
            )

    server.run()


@Command(
    "build-backend",
    category="build",
    description="Generate a backend used to build the tree.",
    virtualenv_name="build",
)
@CommandArgument("-d", "--diff", action="store_true", help="Show a diff of changes.")
# It would be nice to filter the choices below based on
# conditions, but that is for another day.
@CommandArgument(
    "-b",
    "--backend",
    nargs="+",
    choices=sorted(backends),
    help="Which backend to build.",
)
@CommandArgument("-v", "--verbose", action="store_true", help="Verbose output.")
@CommandArgument(
    "-n",
    "--dry-run",
    action="store_true",
    help="Do everything except writing files out.",
)
def build_backend(command_context, backend, diff=False, verbose=False, dry_run=False):
    python = command_context.virtualenv_manager.python_path
    config_status = os.path.join(command_context.topobjdir, "config.status")

    if not os.path.exists(config_status):
        command_context.log(
            logging.ERROR,
            "build_backend",
            {"backend": backend},
            "config.status not found.  Please run |mach configure| "
            "or |mach build| prior to building the {backend} build backend.",
        )
        return 1

    args = [python, config_status]
    if backend:
        args.append("--backend")
        args.extend(backend)
    if diff:
        args.append("--diff")
    if verbose:
        args.append("--verbose")
    if dry_run:
        args.append("--dry-run")

    return command_context._run_command_in_objdir(
        args=args, pass_thru=True, ensure_exit_code=False
    )
