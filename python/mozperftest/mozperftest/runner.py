# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Pure Python runner so we can execute perftest in the CI without
depending on a full mach toolchain, that is not fully available in
all worker environments.

This runner can be executed in two different ways:

- by calling run_tests() from the mach command
- by executing this module directly

When the module is executed directly, if the --on-try option is used,
it will fetch arguments from Tascluster's parameters.
"""

import json
import logging
import os
import shutil
import sys
from pathlib import Path

TASKCLUSTER = "TASK_ID" in os.environ.keys()
RUNNING_TESTS = "RUNNING_TESTS" in os.environ.keys()
HERE = Path(__file__).parent
SRC_ROOT = Path(HERE, "..", "..", "..").resolve()


# XXX need to make that for all systems flavors
if "SHELL" not in os.environ:
    os.environ["SHELL"] = "/bin/bash"


def _activate_virtualenvs(flavor):
    """Adds all available dependencies in the path.

    This is done so the runner can be used with no prior
    install in all execution environments.
    """

    # We need the "mach" module to access the logic to parse virtualenv
    # requirements. Since that depends on "packaging", we add that to the path too.
    sys.path[0:0] = [
        os.path.join(SRC_ROOT, module)
        for module in (
            os.path.join("python", "mach"),
            os.path.join("testing", "mozbase", "mozfile"),
            os.path.join("third_party", "python", "packaging"),
        )
    ]

    from mach.site import (
        CommandSiteManager,
        ExternalPythonSite,
        MachSiteManager,
        SitePackagesSource,
        resolve_requirements,
    )
    from mach.util import get_state_dir, get_virtualenv_base_dir

    mach_site = MachSiteManager(
        str(SRC_ROOT),
        None,
        resolve_requirements(str(SRC_ROOT), "mach"),
        ExternalPythonSite(sys.executable),
        SitePackagesSource.NONE,
    )
    mach_site.activate()

    command_site_manager = CommandSiteManager.from_environment(
        str(SRC_ROOT),
        lambda: os.path.normpath(get_state_dir(True, topsrcdir=str(SRC_ROOT))),
        "common",
        get_virtualenv_base_dir(str(SRC_ROOT)),
    )

    command_site_manager.activate()

    if TASKCLUSTER:
        # In CI, the directory structure is different: xpcshell code is in
        # "$topsrcdir/xpcshell/" rather than "$topsrcdir/testing/xpcshell". The
        # same is true for mochitest. It also needs additional settings for some
        # dependencies.
        if flavor == "xpcshell":
            print("Setting up xpcshell python paths...")
            sys.path.append("xpcshell")
        elif flavor == "mochitest":
            print("Setting up mochitest python paths...")
            sys.path.append("mochitest")
            sys.path.append(str(Path("tools", "geckoprocesstypes_generator")))


def _create_artifacts_dir(kwargs, artifacts):
    from mozperftest.utils import create_path

    results_dir = kwargs.get("test_name")
    if results_dir is None:
        results_dir = "results"

    return create_path(artifacts / "artifacts" / kwargs["tool"] / results_dir)


def _save_params(kwargs, artifacts):
    with open(os.path.join(str(artifacts), "side-by-side-params.json"), "w") as file:
        json.dump(kwargs, file, indent=4)


def run_tests(mach_cmd, kwargs, client_args):
    """This tests runner can be used directly via main or via Mach.

    When the --on-try option is used, the test runner looks at the
    `PERFTEST_OPTIONS` environment variable.
    """
    on_try = kwargs.pop("on_try", False)

    # trying to get the arguments from the task params
    if on_try:
        try_options = json.loads(os.environ["PERFTEST_OPTIONS"])
        print("Loading options from $PERFTEST_OPTIONS")
        print(json.dumps(try_options, indent=4, sort_keys=True))
        kwargs.update(try_options)

    from mozperftest import MachEnvironment, Metadata
    from mozperftest.hooks import Hooks
    from mozperftest.script import ScriptInfo
    from mozperftest.utils import build_test_list

    hooks_file = kwargs.pop("hooks", None)
    hooks = Hooks(mach_cmd, hooks_file)
    verbose = kwargs.get("verbose", False)
    log_level = logging.DEBUG if verbose else logging.INFO

    # If we run through mach, we just  want to set the level
    # of the existing termminal handler.
    # Otherwise, we're adding it.
    if mach_cmd.log_manager.terminal_handler is not None:
        mach_cmd.log_manager.terminal_handler.level = log_level
    else:
        mach_cmd.log_manager.add_terminal_logging(level=log_level)
        mach_cmd.log_manager.enable_all_structured_loggers()
        mach_cmd.log_manager.enable_unstructured()

    try:
        # Only pass the virtualenv to the before_iterations hook
        # so that users can install test-specific packages if needed.
        mach_cmd.activate_virtualenv()
        kwargs["virtualenv"] = mach_cmd.virtualenv_manager
        hooks.run("before_iterations", kwargs)
        del kwargs["virtualenv"]

        tests, tmp_dir = build_test_list(kwargs["tests"])

        for test in tests:
            script = ScriptInfo(test)

            # update the arguments with options found in the script, if any
            args = script.update_args(**client_args)
            # XXX this should be the default pool for update_args
            for key, value in kwargs.items():
                if key not in args:
                    args[key] = value

            # update the hooks, or use a copy of the general one
            script_hooks = Hooks(mach_cmd, args.pop("hooks", hooks_file))

            flavor = args["flavor"]
            if flavor == "doc":
                print(script)
                continue

            for iteration in range(args.get("test_iterations", 1)):
                try:
                    env = MachEnvironment(mach_cmd, hooks=script_hooks, **args)
                    metadata = Metadata(mach_cmd, env, flavor, script)
                    script_hooks.run("before_runs", env)
                    try:
                        with env.frozen() as e:
                            e.run(metadata)
                    finally:
                        script_hooks.run("after_runs", env)
                finally:
                    if tmp_dir is not None:
                        shutil.rmtree(tmp_dir)
    finally:
        hooks.cleanup()


def run_tools(mach_cmd, kwargs):
    """This tools runner can be used directly via main or via Mach.

    **TODO**: Before adding any more tools, we need to split this logic out
    into a separate file that runs the tools and sets them up dynamically
    in a similar way to how we use layers.
    """
    import subprocess

    from mozperftest.utils import ON_TRY, install_package

    mach_cmd.activate_virtualenv()
    if sys.version_info == (3, 9):
        # Bug 2033807
        # On Python 3.9, pip resolves numpy>=1.23 to numpy 2.x from the internal mirror,
        # which is only available as a source archive and fails to build
        install_package(
            mach_cmd.virtualenv_manager,
            "numpy==1.24.4",
        )

        install_package(
            mach_cmd.virtualenv_manager,
            "scipy==1.10.0",
        )

        subprocess.check_call([
            mach_cmd.virtualenv_manager.python_path,
            "-m",
            "pip",
            "install",
            "opencv-python==4.8.1.78",
            "--no-deps",
            "--no-index",
            "--find-links",
            "https://pypi.pub.build.mozilla.org/pub/",
        ])

        subprocess.check_call([
            mach_cmd.virtualenv_manager.python_path,
            "-m",
            "pip",
            "install",
            "mozperftest-tools==0.4.4",
            "--no-deps",
            "--no-index",
            "--find-links",
            "https://pypi.pub.build.mozilla.org/pub/",
        ])
    else:
        install_package(
            mach_cmd.virtualenv_manager,
            "mozperftest-tools==0.4.4",
        )

    log_level = logging.INFO
    if mach_cmd.log_manager.terminal_handler is not None:
        mach_cmd.log_manager.terminal_handler.level = log_level
    else:
        mach_cmd.log_manager.add_terminal_logging(level=log_level)
        mach_cmd.log_manager.enable_all_structured_loggers()
        mach_cmd.log_manager.enable_unstructured()

    if ON_TRY:
        artifacts = Path(os.environ.get("MOZ_FETCHES_DIR"), "..").resolve()
        artifacts = _create_artifacts_dir(kwargs, artifacts)
    else:
        artifacts = _create_artifacts_dir(kwargs, SRC_ROOT)

    _save_params(kwargs, artifacts)

    # Run the requested tool
    from mozperftest.tools import TOOL_RUNNERS

    tool = kwargs.pop("tool")
    print(f"Running {tool} tool")

    TOOL_RUNNERS[tool](artifacts, kwargs)


def main(argv=sys.argv[1:]):
    """Used when the runner is directly called from the shell"""
    flavor = "desktop-browser"
    if "--flavor" in argv:
        flavor = argv[argv.index("--flavor") + 1]
    _activate_virtualenvs(flavor)

    from mach.logging import LoggingManager
    from mach.util import get_state_dir
    from mozbuild.base import MachCommandBase, MozbuildObject
    from mozbuild.mozconfig import MozconfigLoader

    from mozperftest import PerftestArgumentParser, PerftestToolsArgumentParser

    if os.getenv("PERF_FLAGS"):
        extra_args = []
        for extra_arg in os.getenv("PERF_FLAGS").split():
            extra_args.append(f"--{os.path.expandvars(extra_arg)}")
        argv.extend(extra_args)

    mozconfig = SRC_ROOT / "browser" / "config" / "mozconfig"
    if mozconfig.exists():
        os.environ["MOZCONFIG"] = str(mozconfig)

    if "--xpcshell-mozinfo" in argv:
        mozinfo = argv[argv.index("--xpcshell-mozinfo") + 1]
        topobjdir = Path(mozinfo).parent
    else:
        topobjdir = None

    config = MozbuildObject(
        str(SRC_ROOT),
        None,
        LoggingManager(),
        topobjdir=topobjdir,
        mozconfig=MozconfigLoader.AUTODETECT,
    )
    config.topdir = config.topsrcdir
    config.cwd = os.getcwd()
    config.state_dir = get_state_dir()

    # This monkey patch forces mozbuild to reuse
    # our configuration when it tries to re-create
    # it from the environment.
    def _here(*args, **kw):
        return config

    MozbuildObject.from_environment = _here

    mach_cmd = MachCommandBase(config)

    if "tools" in argv[0]:
        if len(argv) == 1:
            raise SystemExit("No tool specified, cannot continue parsing")
        PerftestToolsArgumentParser.tool = argv[1]
        perftools_parser = PerftestToolsArgumentParser()
        args = dict(vars(perftools_parser.parse_args(args=argv[2:])))
        args["tool"] = argv[1]
        run_tools(mach_cmd, args)
    else:
        perftest_parser = PerftestArgumentParser(description="vanilla perftest")
        args = dict(vars(perftest_parser.parse_args(args=argv)))
        user_args = perftest_parser.get_user_args(args)
        run_tests(mach_cmd, args, user_args)


if __name__ == "__main__":
    sys.exit(main())
