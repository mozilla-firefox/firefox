# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import math
import os
import shutil
import sys
from importlib.abc import MetaPathFinder
from pathlib import Path

STATE_DIR_FIRST_RUN = """
Mach and the build system store shared state in a common directory
on the filesystem. The following directory will be created:

  {}

If you would like to use a different directory, rename or move it to your
desired location, and set the MOZBUILD_STATE_PATH environment variable
accordingly.
""".strip()


CATEGORIES = {
    "build": {
        "short": "Build Commands",
        "long": "Interact with the build system",
        "priority": 80,
    },
    "post-build": {
        "short": "Post-build Commands",
        "long": "Common actions performed after completing a build.",
        "priority": 70,
    },
    "testing": {
        "short": "Testing",
        "long": "Run tests.",
        "priority": 60,
    },
    "ci": {
        "short": "CI",
        "long": "Taskcluster commands",
        "priority": 59,
    },
    "devenv": {
        "short": "Development Environment",
        "long": "Set up and configure your development environment.",
        "priority": 50,
    },
    "build-dev": {
        "short": "Low-level Build System Interaction",
        "long": "Interact with specific parts of the build system.",
        "priority": 20,
    },
    "misc": {
        "short": "Potpourri",
        "long": "Potent potables and assorted snacks.",
        "priority": 10,
    },
    "release": {
        "short": "Release automation",
        "long": "Commands for used in release automation.",
        "priority": 5,
    },
    "disabled": {
        "short": "Disabled",
        "long": "The disabled commands are hidden by default. Use -v to display them. "
        "These commands are unavailable for your current context, "
        'run "mach <command>" to see why.',
        "priority": 0,
    },
}


def _activate_python_environment(topsrcdir, get_state_dir, quiet):
    from mach.site import MachSiteManager

    mach_environment = MachSiteManager.from_environment(
        topsrcdir, get_state_dir, quiet=quiet
    )
    mach_environment.activate()


def _maybe_activate_mozillabuild_environment():
    if sys.platform != "win32":
        return

    mozillabuild = Path(os.environ.get("MOZILLABUILD", r"C:\mozilla-build"))
    os.environ.setdefault("MOZILLABUILD", str(mozillabuild))
    assert mozillabuild.exists(), (
        f'MozillaBuild was not found at "{mozillabuild}".\n'
        "If it's installed in a different location, please "
        'set the "MOZILLABUILD" environment variable '
        "accordingly."
    )

    use_msys2 = (mozillabuild / "msys2").exists()
    if use_msys2:
        mozillabuild_msys_tools_path = mozillabuild / "msys2" / "usr" / "bin"
    else:
        mozillabuild_msys_tools_path = mozillabuild / "msys" / "bin"

    paths_to_add = [mozillabuild_msys_tools_path, mozillabuild / "bin"]
    existing_paths = [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep)]
    for new_path in paths_to_add:
        if new_path not in existing_paths:
            os.environ["PATH"] += f"{os.pathsep}{new_path}"


def check_for_spaces(topsrcdir):
    if " " in topsrcdir:
        raise Exception(
            f"Your checkout at path '{topsrcdir}' contains a space, which "
            f"is not supported. Please move it to somewhere that does not "
            f"have a space in the path before rerunning mach."
        )

    mozillabuild_dir = os.environ.get("MOZILLABUILD", "")
    if sys.platform == "win32" and " " in mozillabuild_dir:
        raise Exception(
            f"Your installation of MozillaBuild appears to be installed on a path that "
            f"contains a space ('{mozillabuild_dir}') which is not supported. Please "
            f"reinstall MozillaBuild on a path without a space and restart your shell"
            f"from the new installation."
        )


def initialize(topsrcdir, args=()):
    # This directory was deleted in bug 1666345, but there may be some ignored
    # files here. We can safely just delete it for the user so they don't have
    # to clean the repo themselves.
    deleted_dir = os.path.join(topsrcdir, "third_party", "python", "psutil")
    if os.path.exists(deleted_dir):
        shutil.rmtree(deleted_dir, ignore_errors=True)

    # We need the "mach" module to access the logic to parse virtualenv
    # requirements. Since that depends on "packaging", we add it to the path too.
    sys.path[0:0] = [
        os.path.join(topsrcdir, module)
        for module in (
            os.path.join("python", "mach"),
            os.path.join("testing", "mozbase", "mozfile"),
            os.path.join("third_party", "python", "packaging"),
        )
    ]

    from mach.util import get_state_dir, get_virtualenv_base_dir, setenv

    state_dir = _create_state_dir()

    check_for_spaces(topsrcdir)

    # See bug 1874208:
    # Status messages from site.py break usages of `./mach environment`.
    # We pass `quiet` only for it to work around this, so that all other
    # commands can still write status messages.
    if args and (args[0] == "environment" or "--quiet" in args):
        quiet = True
    else:
        quiet = False

    # normpath state_dir to normalize msys-style slashes.
    _activate_python_environment(
        topsrcdir,
        lambda: os.path.normpath(get_state_dir(True, topsrcdir=topsrcdir)),
        quiet=quiet,
    )
    _maybe_activate_mozillabuild_environment()

    from concurrent.futures import ThreadPoolExecutor

    # Sparse checkouts may not have all mach_commands.py files. Ignore
    # errors from missing files. Same for spidermonkey tarballs.
    def _compute_missing_ok():
        import mozversioncontrol

        try:
            repo = mozversioncontrol.get_repository_object(path=topsrcdir)
        except (mozversioncontrol.InvalidRepoPath, mozversioncontrol.MissingVCSTool):
            repo = None
        if repo == "SOURCE":
            return False
        elif repo is not None:
            return repo.sparse_checkout_present()
        else:
            return os.path.exists(os.path.join(topsrcdir, "INSTALL"))

    missing_ok_executor = ThreadPoolExecutor(max_workers=1)
    missing_ok_future = missing_ok_executor.submit(_compute_missing_ok)
    missing_ok_executor.shutdown(wait=False)

    import mach.main
    from mach.command_util import (
        MACH_COMMANDS,
        DetermineCommandVenvAction,
        load_commands_from_spec,
    )
    from mach.main import get_argument_parser

    # Set a reasonable limit to the number of open files.
    #
    # Some linux systems set `ulimit -n` to a very high number, which works
    # well for systems that run servers, but this setting causes performance
    # problems when programs close file descriptors before forking, like
    # Python's `subprocess.Popen(..., close_fds=True)` (close_fds=True is the
    # default in Python 3), or Rust's stdlib.  In some cases, Firefox does the
    # same thing when spawning processes.  We would prefer to lower this limit
    # to avoid such performance problems; processes spawned by `mach` will
    # inherit the limit set here.
    #
    # The Firefox build defaults the soft limit to 1024, except for builds that
    # do LTO, where the soft limit is 8192.  We're going to default to the
    # latter, since people do occasionally do LTO builds on their local
    # machines, and requiring them to discover another magical setting after
    # setting up an LTO build in the first place doesn't seem good.
    #
    # This code mimics the code in taskcluster/scripts/run-task.
    try:
        import resource

        # Keep the hard limit the same, though, allowing processes to change
        # their soft limit if they need to (Firefox does, for instance).
        (soft, hard) = resource.getrlimit(resource.RLIMIT_NOFILE)
        # Permit people to override our default limit if necessary via
        # MOZ_LIMIT_NOFILE, which is the same variable `run-task` uses.
        limit = os.environ.get("MOZ_LIMIT_NOFILE")
        if limit:
            limit = int(limit)
        else:
            # If no explicit limit is given, use our default if it's less than
            # the current soft limit.  For instance, the default on macOS is
            # 256, so we'd pick that rather than our default.
            limit = min(soft, 8192)
        # Now apply the limit, if it's different from the original one.
        if limit != soft:
            resource.setrlimit(resource.RLIMIT_NOFILE, (limit, hard))
    except ImportError:
        # The resource module is UNIX only.
        pass

    def resolve_repository():
        import mozversioncontrol

        try:
            # This API doesn't respect the vcs binary choices from configure.
            # If we ever need to use the VCS binary here, consider something
            # more robust.
            return mozversioncontrol.get_repository_object(path=topsrcdir)
        except (mozversioncontrol.InvalidRepoPath, mozversioncontrol.MissingVCSTool):
            return None

    def pre_dispatch_handler(context, handler, args):
        from mozbuild.path_performance import check_path_performance

        check_path_performance(topsrcdir, driver.settings)

        # If --disable-tests flag was enabled in the mozconfig used to compile
        # the build, tests will be disabled. Instead of trying to run
        # nonexistent tests then reporting a failure, this will prevent mach
        # from progressing beyond this point.
        if handler.category == "testing" and not handler.ok_if_tests_disabled:
            from mozbuild.base import BuildEnvironmentNotFoundException

            try:
                from mozbuild.base import MozbuildObject

                # all environments should have an instance of build object.
                build = MozbuildObject.from_environment()
                if build is not None and not getattr(
                    build, "substs", {"ENABLE_TESTS": True}
                ).get("ENABLE_TESTS"):
                    print(
                        "Tests have been disabled with --disable-tests.\n"
                        + "Remove the flag, and re-compile to enable tests."
                    )
                    sys.exit(1)
            except BuildEnvironmentNotFoundException:
                # likely automation environment, so do nothing.
                pass

    def post_dispatch_handler(
        context, handler, instance, success, start_time, end_time, depth, args
    ):
        """Perform global operations after command dispatch.


        For now, we will use this to handle build system telemetry.
        """

        # Don't finalize telemetry data if this mach command was invoked as part of
        # another mach command.
        if depth != 1:
            return

        # Wait for the background telemetry initialization to complete
        if context._telemetry_init_done is not None:
            context._telemetry_init_done.wait()

        _finalize_telemetry_glean(
            context.telemetry,
            handler.name == "bootstrap",
            success,
            context._telemetry_start_time_ns,
            driver._system_metrics_future,
        )

    def populate_context(key=None):
        if key is None:
            return
        if key == "state_dir":
            return state_dir

        if key == "local_state_dir":
            return get_state_dir(specific_to_topsrcdir=True)

        if key == "topdir":
            return topsrcdir

        if key == "pre_dispatch_handler":
            return pre_dispatch_handler

        if key == "post_dispatch_handler":
            return post_dispatch_handler

        if key == "repository":
            return resolve_repository()

        raise AttributeError(key)

    # Note which process is top-level so that recursive mach invocations can avoid writing
    # telemetry data.
    if "MACH_MAIN_PID" not in os.environ:
        setenv("MACH_MAIN_PID", str(os.getpid()))

    driver = mach.main.Mach(os.getcwd())
    driver.populate_context_handler = populate_context

    if not driver.settings_paths:
        # default global machrc location
        driver.settings_paths.append(state_dir)
    # always load local repository configuration
    driver.settings_paths.append(topsrcdir)
    driver.load_settings()

    # Start Glean telemetry initialization in a background thread early so
    # that the expensive Glean SDK import and setup can overlap with command
    # site activation, module loading, and arg parsing. The result is
    # consumed in _run() via driver._telemetry_future.
    def _create_telemetry():
        from mach.telemetry import create_telemetry_from_environment

        return create_telemetry_from_environment(driver.settings)

    def _precollect_system_metrics():
        from mach.telemetry import resolve_is_employee
        from mozbuild.telemetry import (
            get_cpu_brand,
            get_crowdstrike_running,
            get_distro_and_version,
            get_fleet_running,
            get_psutil_stats,
            get_shell_info,
            get_vscode_running,
        )

        cpu_brand = get_cpu_brand()
        distro, distro_version = get_distro_and_version()
        vscode_terminal, ssh_connection = get_shell_info()
        vscode_running = get_vscode_running()
        is_employee = resolve_is_employee(Path(topsrcdir), driver.settings)
        fleet_running = get_fleet_running() if is_employee else None
        crowdstrike_running = get_crowdstrike_running() if is_employee else None
        psutil_stats = get_psutil_stats()

        return {
            "cpu_brand": cpu_brand,
            "distro": distro,
            "distro_version": distro_version,
            "vscode_terminal": vscode_terminal,
            "ssh_connection": ssh_connection,
            "vscode_running": vscode_running,
            "is_employee": is_employee,
            "fleet_running": fleet_running,
            "crowdstrike_running": crowdstrike_running,
            "psutil_stats": psutil_stats,
        }

    from concurrent.futures import ThreadPoolExecutor

    if sys.platform == "win32":
        telemetry_executor = ThreadPoolExecutor(max_workers=2)
        driver._telemetry_future = telemetry_executor.submit(_create_telemetry)
        driver._system_metrics_future = telemetry_executor.submit(
            _precollect_system_metrics
        )
    else:
        telemetry_executor = ThreadPoolExecutor(max_workers=1)
        driver._telemetry_future = telemetry_executor.submit(_create_telemetry)

        class _LazyResult:
            def result(self):
                return _precollect_system_metrics()

        driver._system_metrics_future = _LazyResult()
    telemetry_executor.shutdown(wait=False)

    aliases = driver.settings.alias

    parser = get_argument_parser(
        action=DetermineCommandVenvAction,
        topsrcdir=topsrcdir,
    )
    from argparse import Namespace

    from mach.main import (
        SUGGESTED_COMMANDS_MESSAGE,
        UNKNOWN_COMMAND_ERROR,
        UnknownCommandError,
    )

    namespace_in = Namespace()
    setattr(namespace_in, "mach_command_aliases", aliases)

    try:
        namespace = parser.parse_args(args, namespace_in)
    except UnknownCommandError as e:
        suggestion_message = (
            SUGGESTED_COMMANDS_MESSAGE % (e.verb, ", ".join(e.suggested_commands))
            if e.suggested_commands
            else ""
        )
        print(UNKNOWN_COMMAND_ERROR % (e.verb, e.command, suggestion_message))
        sys.exit(1)

    command_name = getattr(namespace, "command_name", None)
    site_name = getattr(namespace, "site_name", "common")
    command_site_manager = None

    # the 'clobber' command needs to run in the 'mach' venv, so we
    # don't want to activate any other virtualenv for it.
    if command_name != "clobber":
        from mach.site import CommandSiteManager

        command_site_manager = CommandSiteManager.from_environment(
            topsrcdir,
            lambda: os.path.normpath(get_state_dir(True, topsrcdir=topsrcdir)),
            site_name,
            get_virtualenv_base_dir(topsrcdir),
            quiet=quiet,
        )

        command_site_manager.activate()

    for category, meta in CATEGORIES.items():
        driver.define_category(category, meta["short"], meta["long"], meta["priority"])

    commands_that_need_all_modules_loaded = [
        "busted",
        "help",
        "mach-commands",
        "mach-completion",
        "mach-debug-commands",
    ]

    def commands_to_load(top_level_command: str):
        visited = set()

        def find_downstream_commands_recursively(command: str):
            if not MACH_COMMANDS.get(command):
                return

            if command in visited:
                return

            visited.add(command)

            for command_dependency in MACH_COMMANDS[command].command_dependencies:
                find_downstream_commands_recursively(command_dependency)

        find_downstream_commands_recursively(top_level_command)

        return list(visited)

    if (
        command_name not in MACH_COMMANDS
        or command_name in commands_that_need_all_modules_loaded
    ):
        command_modules_to_load = MACH_COMMANDS
    else:
        command_names_to_load = commands_to_load(command_name)
        command_modules_to_load = {
            command_name: MACH_COMMANDS[command_name]
            for command_name in command_names_to_load
        }

    driver.command_site_manager = command_site_manager
    load_commands_from_spec(
        command_modules_to_load, topsrcdir, missing_ok=missing_ok_future.result()
    )

    return driver


def _finalize_telemetry_glean(
    telemetry,
    is_bootstrap,
    success,
    start_time_ns,
    system_metrics_future,
):
    """Submit telemetry collected by Glean.

    Finalizes some metrics (command success state and duration, system information) and
    requests Glean to send the collected data.
    """
    import time

    from mach.telemetry import MACH_METRICS_PATH

    moz_automation = any(e in os.environ for e in ("MOZ_AUTOMATION", "TASK_ID"))

    mach_metrics = telemetry.metrics(MACH_METRICS_PATH)

    elapsed_ns = time.monotonic_ns() - start_time_ns
    mach_metrics.mach.duration.set_raw_nanos(elapsed_ns)

    mach_metrics.mach.success.set(success)
    mach_metrics.mach.moz_automation.set(moz_automation)

    try:
        collected_metrics = system_metrics_future.result()
    except Exception as e:
        print(
            f"Warning: failed to collect system telemetry metrics: {e}",
            file=sys.stderr,
        )
        collected_metrics = None

    if collected_metrics is not None:
        system_metrics = mach_metrics.mach.system
        if collected_metrics["cpu_brand"]:
            system_metrics.cpu_brand.set(collected_metrics["cpu_brand"])
        for attr in (
            "distro",
            "distro_version",
            "vscode_terminal",
            "ssh_connection",
            "vscode_running",
        ):
            getattr(system_metrics, attr).set(collected_metrics[attr])

        if collected_metrics["fleet_running"] is not None:
            system_metrics.fleet_running.set(collected_metrics["fleet_running"])
        if collected_metrics["crowdstrike_running"] is not None:
            system_metrics.crowdstrike_running.set(
                collected_metrics["crowdstrike_running"]
            )

        has_psutil, logical_cores, physical_cores, memory_total = collected_metrics[
            "psutil_stats"
        ]
        if has_psutil:
            # psutil may not be available (we may not have been able to download
            # a wheel or build it from source).
            system_metrics.logical_cores.add(logical_cores)
            if physical_cores is not None:
                system_metrics.physical_cores.add(physical_cores)
            if memory_total is not None:
                system_metrics.memory.accumulate(
                    int(math.ceil(float(memory_total) / (1024 * 1024 * 1024)))
                )
    telemetry.submit(is_bootstrap)


def _create_state_dir():
    # Global build system and mach state is stored in a central directory. By
    # default, this is ~/.mozbuild. However, it can be defined via an
    # environment variable. We detect first run (by lack of this directory
    # existing) and notify the user that it will be created. The logic for
    # creation is much simpler for the "advanced" environment variable use
    # case. For default behavior, we educate users and give them an opportunity
    # to react.
    state_dir = os.environ.get("MOZBUILD_STATE_PATH")
    if state_dir:
        if not os.path.exists(state_dir):
            print(
                f"Creating global state directory from environment variable: {state_dir}"
            )
    else:
        state_dir = os.path.expanduser("~/.mozbuild")
        if not os.path.exists(state_dir):
            if not os.environ.get("MOZ_AUTOMATION"):
                print(STATE_DIR_FIRST_RUN.format(state_dir))

            print(f"Creating default state directory: {state_dir}")

    os.makedirs(state_dir, mode=0o770, exist_ok=True)
    return state_dir


# Hook import such that .pyc/.pyo files without a corresponding .py file in
# the source directory are essentially ignored. See further below for details
# and caveats.
# Objdirs outside the source directory are ignored because in most cases, if
# a .pyc/.pyo file exists there, a .py file will be next to it anyways.
class FinderHook(MetaPathFinder):
    def __init__(self, klass):
        # Assume the source directory is the parent directory of the one
        # containing this file.
        self._source_dir = (
            os.path.normcase(
                os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
            )
            + os.sep
        )
        self.finder_class = klass

    def find_spec(self, full_name, paths=None, target=None):
        spec = self.finder_class.find_spec(full_name, paths, target)

        # Some modules don't have an origin.
        if spec is None or spec.origin is None:
            return spec

        # Skip normalization for non-bytecode files.
        if not spec.origin.endswith((".pyc", ".pyo")):
            return spec

        # Normalize the origin path.
        path = os.path.normcase(os.path.abspath(spec.origin))

        # Ignore modules outside our source directory
        if not path.startswith(self._source_dir):
            return spec

        # If there is no .py corresponding to the .pyc/.pyo module we're
        # resolving, remove the .pyc/.pyo file, and try again.
        if not os.path.exists(spec.origin[:-1]):
            if os.path.exists(spec.origin):
                os.remove(spec.origin)
            spec = self.finder_class.find_spec(full_name, paths, target)

        return spec


# Additional hook for python >= 3.8's importlib.metadata.
class MetadataHook(FinderHook):
    def find_distributions(self, *args, **kwargs):
        return self.finder_class.find_distributions(*args, **kwargs)


def hook(finder):
    has_find_spec = hasattr(finder, "find_spec")
    has_find_distributions = hasattr(finder, "find_distributions")
    if has_find_spec and has_find_distributions:
        return MetadataHook(finder)
    elif has_find_spec:
        return FinderHook(finder)
    return finder


sys.meta_path = [hook(c) for c in sys.meta_path]
