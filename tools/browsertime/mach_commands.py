# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

r"""Make it easy to install and run [browsertime](https://github.com/sitespeedio/browsertime).

Browsertime is a harness for running performance tests, similar to
Mozilla's Raptor testing framework.  Browsertime is written in Node.js
and uses Selenium WebDriver to drive multiple browsers including
Chrome, Chrome for Android, Firefox, and (pending the resolution of
[Bug 1525126](https://bugzilla.mozilla.org/show_bug.cgi?id=1525126)
and similar tickets) Firefox for Android and GeckoView-based vehicles.

Right now a custom version of browsertime and the underlying
geckodriver binary are needed to support GeckoView-based vehicles;
this module accommodates those in-progress custom versions.

To get started, run
```
./mach browsertime --setup [--clobber]
```
This will populate `tools/browsertime/node_modules`.

To invoke browsertime, run
```
./mach browsertime [ARGS]
```
All arguments are passed through to browsertime.
"""

import argparse
import collections
import contextlib
import json
import logging
import os
import platform
import re
import stat
import subprocess
import sys
import time
from io import StringIO

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument
from mozbuild.base import BinaryNotFoundException, MachCommandBase
from mozbuild.dirutils import mkdir

AUTOMATION = "MOZ_AUTOMATION" in os.environ
BROWSERTIME_ROOT = os.path.dirname(__file__)

PILLOW_VERSION = "8.4.0"  # version 8.4.0 currently supports python 3.6 to 3.10
PYSSIM_VERSION = "0.4"
SCIPY_VERSION = "1.2.3"
NUMPY_VERSION = "1.16.1"
OPENCV_VERSION = "4.5.4.60"

py3_minor = sys.version_info.minor
if py3_minor > 7:
    SCIPY_VERSION = "1.9.3"
    NUMPY_VERSION = "1.23.5"
    PILLOW_VERSION = "9.2.0"
    OPENCV_VERSION = "4.6.0.66"
if py3_minor > 11:
    NUMPY_VERSION = "2.2.3"
    PILLOW_VERSION = "11.1.0"
    SCIPY_VERSION = "1.15.2"
    PYSSIM_VERSION = "0.7"
    OPENCV_VERSION = "4.11.0.86"

MIN_NODE_VERSION = "16.0.0"

IS_APPLE_SILICON = sys.platform.startswith(
    "darwin"
) and platform.processor().startswith("arm")


@contextlib.contextmanager
def silence():
    oldout, olderr = sys.stdout, sys.stderr
    try:
        sys.stdout, sys.stderr = StringIO(), StringIO()
        yield
    finally:
        sys.stdout, sys.stderr = oldout, olderr


def node_path(command_context):
    import platform

    from mozbuild.nodeutil import find_node_executable
    from packaging.version import Version

    state_dir = command_context._mach_context.state_dir
    cache_path = os.path.join(state_dir, "browsertime", "node-16")

    NODE_FAILURE_MSG = (
        f"Could not locate a node binary that is at least version {MIN_NODE_VERSION}. "
        + "Please run `./mach raptor --browsertime -t amazon` to install it "
        + "from the Taskcluster Toolchain artifacts."
    )

    # Check standard locations first
    node_exe = find_node_executable(min_version=Version(MIN_NODE_VERSION))
    if node_exe and (node_exe[0] is not None):
        return os.path.abspath(node_exe[0])
    if not os.path.exists(cache_path):
        raise Exception(NODE_FAILURE_MSG)

    # Check the browsertime-specific node location next
    node_name = "node"
    if platform.system() == "Windows":
        node_name = "node.exe"
        node_exe_path = os.path.join(
            state_dir,
            "browsertime",
            "node-16",
            "node",
        )
    else:
        node_exe_path = os.path.join(
            state_dir,
            "browsertime",
            "node-16",
            "node",
            "bin",
        )

    node_exe = os.path.join(node_exe_path, node_name)
    if not os.path.exists(node_exe):
        raise Exception(NODE_FAILURE_MSG)

    return os.path.abspath(node_exe)


def package_path():
    """The path to the `browsertime` directory.

    Override the default with the `BROWSERTIME` environment variable."""
    override = os.environ.get("BROWSERTIME", None)
    if override:
        return override

    return mozpath.join(BROWSERTIME_ROOT, "node_modules", "browsertime")


def browsertime_path():
    """The path to the `browsertime.js` script."""
    # On Windows, invoking `node_modules/.bin/browsertime{.cmd}`
    # doesn't work when invoked as an argument to our specific
    # binary.  Since we want our version of node, invoke the
    # actual script directly.
    return mozpath.join(package_path(), "bin", "browsertime.js")


def visualmetrics_path():
    """The path to the `visualmetrics.py` script."""
    return mozpath.join(package_path(), "visualmetrics", "visualmetrics-portable.py")


def host_platform():
    is_64bits = sys.maxsize > 2**32

    if sys.platform.startswith("win"):
        if is_64bits:
            return "win64"
    elif sys.platform.startswith("linux"):
        if is_64bits:
            return "linux64"
    elif sys.platform.startswith("darwin"):
        return "darwin"

    raise ValueError(f"sys.platform is not yet supported: {sys.platform}")


# Map from `host_platform()` to a `fetch`-like syntax.
host_fetches = {
    "darwin": {
        "ffmpeg": {
            "type": "static-url",
            "url": "https://github.com/mozilla/perf-automation/releases/download/FFMPEG-v4.4.1/ffmpeg-macos.zip",  # noqa
            # An extension to `fetch` syntax.
            "path": "ffmpeg-macos",
        },
    },
    "linux64": {
        "ffmpeg": {
            "type": "static-url",
            "url": "https://github.com/mozilla/perf-automation/releases/download/FFMPEG-v4.4.1/ffmpeg-4.4.1-i686-static.tar.xz",  # noqa
            # An extension to `fetch` syntax.
            "path": "ffmpeg-4.4.1-i686-static",
        },
    },
    "win64": {
        "ffmpeg": {
            "type": "static-url",
            "url": "https://github.com/mozilla/perf-automation/releases/download/FFMPEG-v4.4.1/ffmpeg-4.4.1-full_build.zip",  # noqa
            # An extension to `fetch` syntax.
            "path": "ffmpeg-4.4.1-full_build",
        },
    },
}


def artifact_cache_path(command_context):
    r"""Downloaded artifacts will be kept here."""
    # The convention is $MOZBUILD_STATE_PATH/cache/$FEATURE.
    return mozpath.join(command_context._mach_context.state_dir, "cache", "browsertime")


def state_path(command_context):
    r"""Unpacked artifacts will be kept here."""
    # The convention is $MOZBUILD_STATE_PATH/$FEATURE.
    return mozpath.join(command_context._mach_context.state_dir, "browsertime")


def setup_prerequisites(command_context):
    r"""Install browsertime and visualmetrics.py prerequisites."""

    from mozbuild.action.tooltool import unpack_file
    from mozbuild.artifact_cache import ArtifactCache

    # Download the visualmetrics-portable.py requirements.
    artifact_cache = ArtifactCache(
        artifact_cache_path(command_context),
        log=command_context.log,
        skip_cache=False,
    )

    fetches = host_fetches[host_platform()]
    for tool, fetch in sorted(fetches.items()):
        archive = artifact_cache.fetch(fetch["url"])
        # TODO: assert type, verify sha256 (and size?).

        if fetch.get("unpack", True):
            cwd = os.getcwd()
            try:
                mkdir(state_path(command_context))
                os.chdir(state_path(command_context))
                command_context.log(
                    logging.INFO,
                    "browsertime",
                    {"path": archive},
                    "Unpacking temporary location {path}",
                )

                unpack_file(archive)

                # Make sure the expected path exists after extraction
                path = os.path.join(state_path(command_context), fetch.get("path"))
                if not os.path.exists(path):
                    raise Exception("Cannot find an extracted directory: %s" % path)

                try:
                    # Some archives provide binaries that don't have the
                    # executable bit set so we need to set it here
                    for root, dirs, files in os.walk(path):
                        for edir in dirs:
                            loc_to_change = os.path.join(root, edir)
                            st = os.stat(loc_to_change)
                            os.chmod(loc_to_change, st.st_mode | stat.S_IEXEC)
                        for efile in files:
                            loc_to_change = os.path.join(root, efile)
                            st = os.stat(loc_to_change)
                            os.chmod(loc_to_change, st.st_mode | stat.S_IEXEC)
                except Exception as e:
                    raise Exception(
                        "Could not set executable bit in %s, error: %s" % (path, str(e))
                    )
            finally:
                os.chdir(cwd)


def setup_browsertime(
    command_context,
    should_clobber=False,
    new_upstream_url="",
    install_vismet_reqs=False,
):
    r"""Install browsertime and visualmetrics.py prerequisites and the Node.js package."""

    sys.path.append(mozpath.join(command_context.topsrcdir, "tools", "lint", "eslint"))
    import setup_helper

    if not new_upstream_url:
        setup_prerequisites(command_context)

    if new_upstream_url:
        package_json_path = os.path.join(BROWSERTIME_ROOT, "package.json")

        command_context.log(
            logging.INFO,
            "browsertime",
            {
                "new_upstream_url": new_upstream_url,
                "package_json_path": package_json_path,
            },
            "Updating browsertime node module version in {package_json_path} "
            "to {new_upstream_url}",
        )

        if not re.search("/tarball/[a-f0-9]{40}$", new_upstream_url):
            raise ValueError(
                "New upstream URL does not end with /tarball/[a-f0-9]{40}: '%s'"
                % new_upstream_url
            )

        with open(package_json_path) as f:
            existing_body = json.loads(
                f.read(), object_pairs_hook=collections.OrderedDict
            )

        existing_body["devDependencies"]["browsertime"] = new_upstream_url

        updated_body = json.dumps(existing_body, indent=2)

        with open(package_json_path, "w") as f:
            f.write(updated_body)

    # Install the browsertime Node.js requirements.
    if not setup_helper.check_node_executables_valid():
        return 1

    # To use a custom `geckodriver`, set
    # os.environ[b"GECKODRIVER_BASE_URL"] = bytes(url)
    # to an endpoint with binaries named like
    # https://github.com/sitespeedio/geckodriver/blob/master/install.js#L31.
    if AUTOMATION:
        os.environ["CHROMEDRIVER_SKIP_DOWNLOAD"] = "true"
        os.environ["GECKODRIVER_SKIP_DOWNLOAD"] = "true"

    if install_vismet_reqs:
        # Hide this behind a flag so we don't install them by default in Raptor
        command_context.log(
            logging.INFO, "browsertime", {}, "Installing python requirements"
        )
        activate_browsertime_virtualenv(command_context)

    command_context.log(
        logging.INFO,
        "browsertime",
        {"package_json": mozpath.join(BROWSERTIME_ROOT, "package.json")},
        "Installing browsertime node module from {package_json}",
    )

    # Add the mozbuild Node binary path to the OS environment in Apple Silicon.
    # During the browesertime installation, it seems installation of sitespeed.io
    # sub dependencies look for a global Node rather than the mozbuild Node binary.
    # Normally `--scripts-prepend-node-path` should prevent this but it seems to still
    # look for a system Node in the environment. This logic ensures the same Node is used.
    node_dir = os.path.dirname(node_path(command_context))
    if IS_APPLE_SILICON and node_dir not in os.environ["PATH"]:
        os.environ["PATH"] += os.pathsep + node_dir

    status = setup_helper.package_setup(
        BROWSERTIME_ROOT,
        "browsertime",
        should_update=new_upstream_url != "",
        should_clobber=should_clobber,
        no_optional=new_upstream_url or AUTOMATION,
    )

    if status:
        return status
    if new_upstream_url or AUTOMATION:
        return 0
    if install_vismet_reqs:
        return check(command_context)

    return 0


def node(command_context, args):
    r"""Invoke node (interactively) with the given arguments."""
    return command_context.run_process(
        [node_path(command_context)] + args,
        append_env=append_env(command_context),
        pass_thru=True,  # Allow user to run Node interactively.
        ensure_exit_code=False,  # Don't throw on non-zero exit code.
        cwd=mozpath.join(command_context.topsrcdir),
    )


def append_env(command_context, append_path=True):
    fetches = host_fetches[host_platform()]

    # Ensure that `ffmpeg` is found and added to the environment
    path = os.environ.get("PATH", "").split(os.pathsep) if append_path else []
    path_to_ffmpeg = mozpath.join(
        state_path(command_context), fetches["ffmpeg"]["path"]
    )

    path.insert(
        0,
        (
            path_to_ffmpeg
            if host_platform().startswith("linux")
            else mozpath.join(path_to_ffmpeg, "bin")
        ),
    )  # noqa

    # Ensure that bare `node` and `npm` in scripts, including post-install
    # scripts, finds the binary we're invoking with.  Without this, it's
    # easy for compiled extensions to get mismatched versions of the Node.js
    # extension API.
    node_dir = os.path.dirname(node_path(command_context))
    path = [node_dir] + path

    append_env = {
        "PATH": os.pathsep.join(path),
        # Bug 1560193: The JS library browsertime uses to execute commands
        # (execa) will muck up the PATH variable and put the directory that
        # node is in first in path. If this is globally-installed node,
        # that means `/usr/bin` will be inserted first which means that we
        # will get `/usr/bin/python` for `python`.
        #
        # Our fork of browsertime supports a `PYTHON` environment variable
        # that points to the exact python executable to use.
        "PYTHON": command_context.virtualenv_manager.python_path,
    }

    return append_env


def _need_install(command_context, package):
    from pip._internal.req.constructors import install_req_from_line

    req = install_req_from_line(package)
    req.check_if_exists(use_user_site=False)
    if req.satisfied_by is None:
        return True
    venv_site_lib = os.path.abspath(
        os.path.join(command_context.virtualenv_manager.bin_path, "..", "lib")
    )
    site_packages = os.path.abspath(req.satisfied_by.location)
    return not site_packages.startswith(venv_site_lib)


def activate_browsertime_virtualenv(command_context, *args, **kwargs):
    r"""Activates virtualenv.

    This function will also install Pillow and pyssim if needed.
    It will raise an error in case the install failed.
    """
    # TODO: Remove `./mach browsertime` completely, as a follow up to Bug 1758990
    MachCommandBase.activate_virtualenv(command_context, *args, **kwargs)

    # installing Python deps on the fly
    for dep in (
        "Pillow==%s" % PILLOW_VERSION,
        "pyssim==%s" % PYSSIM_VERSION,
        "scipy==%s" % SCIPY_VERSION,
        "numpy==%s" % NUMPY_VERSION,
        "opencv-python==%s" % OPENCV_VERSION,
    ):
        if _need_install(command_context, dep):
            subprocess.check_call(
                [
                    command_context.virtualenv_manager.python_path,
                    "-m",
                    "pip",
                    "install",
                    dep,
                ]
            )


def check(command_context):
    r"""Run `visualmetrics.py --check`."""
    command_context.activate_virtualenv()

    args = ["--check"]
    status = command_context.run_process(
        [command_context.virtualenv_manager.python_path, visualmetrics_path()] + args,
        # For --check, don't allow user's path to interfere with path testing except on Linux
        append_env=append_env(
            command_context, append_path=host_platform().startswith("linux")
        ),
        pass_thru=True,
        ensure_exit_code=False,  # Don't throw on non-zero exit code.
        cwd=mozpath.join(command_context.topsrcdir),
    )

    sys.stdout.flush()
    sys.stderr.flush()

    if status:
        return status

    # Avoid logging the command (and, on Windows, the environment).
    command_context.log_manager.terminal_handler.setLevel(logging.CRITICAL)
    print("browsertime version:", end=" ")

    sys.stdout.flush()
    sys.stderr.flush()

    return node(command_context, [browsertime_path()] + ["--version"])


def extra_default_args(command_context, args=[]):
    # Add Mozilla-specific default arguments.  This is tricky because browsertime is quite
    # loose about arguments; repeat arguments are generally accepted but then produce
    # difficult to interpret type errors.

    def extract_browser_name(args):
        "Extracts the browser name if any"
        # These are BT arguments, it's BT job to check them
        # here we just want to extract the browser name
        res = re.findall(r"(--browser|-b)[= ]([\w]+)", " ".join(args))
        if res == []:
            return None
        return res[0][-1]

    def matches(args, *flags):
        "Return True if any argument matches any of the given flags (maybe with an argument)."
        for flag in flags:
            if flag in args or any(arg.startswith(flag + "=") for arg in args):
                return True
        return False

    extra_args = []

    # Default to Firefox.  Override with `-b ...` or `--browser=...`.
    specifies_browser = matches(args, "-b", "--browser")
    if not specifies_browser:
        extra_args.extend(("-b", "firefox"))

    # Default to not collect HAR.  Override with `--skipHar=false`.
    specifies_har = matches(args, "--har", "--skipHar", "--gzipHar")
    if not specifies_har:
        extra_args.append("--skipHar")

    if not matches(args, "--android"):
        # If --firefox.binaryPath is not specified, default to the objdir binary
        # Note: --firefox.release is not a real browsertime option, but it will
        #       silently ignore it instead and default to a release installation.
        specifies_binaryPath = matches(
            args,
            "--firefox.binaryPath",
            "--firefox.release",
            "--firefox.nightly",
            "--firefox.beta",
            "--firefox.developer",
        )

        if not specifies_binaryPath:
            specifies_binaryPath = extract_browser_name(args) == "chrome"

        if not specifies_binaryPath:
            try:
                extra_args.extend(
                    ("--firefox.binaryPath", command_context.get_binary_path())
                )
            except BinaryNotFoundException as e:
                command_context.log(
                    logging.ERROR,
                    "browsertime",
                    {"error": str(e)},
                    "ERROR: {error}",
                )
                command_context.log(
                    logging.INFO,
                    "browsertime",
                    {},
                    "Please run |./mach build| "
                    "or specify a Firefox binary with --firefox.binaryPath.",
                )
                return 1

    if extra_args:
        command_context.log(
            logging.DEBUG,
            "browsertime",
            {"extra_args": extra_args},
            "Running browsertime with extra default arguments: {extra_args}",
        )

    return extra_args


def _verify_node_install(command_context):
    # check if Node is installed
    sys.path.append(mozpath.join(command_context.topsrcdir, "tools", "lint", "eslint"))
    import setup_helper

    with silence():
        node_valid = setup_helper.check_node_executables_valid()
    if not node_valid:
        print("Can't find Node. did you run ./mach bootstrap ?")
        return False

    # check if the browsertime package has been deployed correctly
    # for this we just check for the browsertime directory presence
    if not os.path.exists(browsertime_path()):
        print("Could not find browsertime.js, try ./mach browsertime --setup")
        print("If that still fails, try ./mach browsertime --setup --clobber")
        return False

    return True


@Command(
    "browsertime",
    category="testing",
    description="Run [browsertime](https://github.com/sitespeedio/browsertime) "
    "performance tests.",
)
@CommandArgument(
    "--verbose",
    action="store_true",
    help="Verbose output for what commands the build is running.",
)
@CommandArgument("--update-upstream-url", default="")
@CommandArgument("--setup", default=False, action="store_true")
@CommandArgument("--clobber", default=False, action="store_true")
@CommandArgument(
    "--skip-cache",
    default=False,
    action="store_true",
    help="Skip all local caches to force re-fetching remote artifacts.",
)
@CommandArgument("--check-browsertime", default=False, action="store_true")
@CommandArgument(
    "--install-vismet-reqs",
    default=False,
    action="store_true",
    help="Add this flag to get the visual metrics requirements installed.",
)
@CommandArgument(
    "--browsertime-help",
    default=False,
    action="store_true",
    help="Show the browsertime help message.",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def browsertime(
    command_context,
    args,
    verbose=False,
    update_upstream_url="",
    setup=False,
    clobber=False,
    skip_cache=False,
    check_browsertime=False,
    browsertime_help=False,
    install_vismet_reqs=False,
):
    command_context._set_log_level(verbose)

    # Output a message before going further to make sure the
    # user knows that this tool is unsupported by the perftest
    # team and point them to our supported tools. Pause a bit to
    # make sure the user sees this message.
    command_context.log(
        logging.INFO,
        "browsertime",
        {},
        "[INFO] This command should be used for browsertime setup only.\n"
        "If you are looking to run performance tests on your patch, use "
        "`./mach raptor --browsertime` instead.\n\nYou can get visual-metrics "
        "by using the --browsertime-video and --browsertime-visualmetrics. "
        "Here is a sample command for raptor-browsertime: \n\t`./mach raptor "
        "--browsertime -t amazon --browsertime-video --browsertime-visualmetrics`\n\n"
        "See this wiki page for more information if needed: "
        "https://wiki.mozilla.org/TestEngineering/Performance/Raptor/Browsertime\n\n",
    )
    time.sleep(5)

    if update_upstream_url:
        return setup_browsertime(
            command_context,
            new_upstream_url=update_upstream_url,
            install_vismet_reqs=install_vismet_reqs,
        )
    elif setup:
        return setup_browsertime(
            command_context,
            should_clobber=clobber,
            install_vismet_reqs=install_vismet_reqs,
        )
    else:
        if not _verify_node_install(command_context):
            return 1

    if check_browsertime:
        return check(command_context)

    if browsertime_help:
        args.append("--help")

    activate_browsertime_virtualenv(command_context)
    default_args = extra_default_args(command_context, args)
    if default_args == 1:
        return 1
    return node(command_context, [browsertime_path()] + default_args + args)
