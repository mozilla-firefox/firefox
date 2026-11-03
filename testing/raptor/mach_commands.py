# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Originally taken from /talos/mach_commands.py

# Integrates raptor mozharness with mach

import json
import logging
import os
import socket
import subprocess
import sys

from mach.decorators import Command
from mach.util import get_state_dir
from mozbuild.base import BinaryNotFoundException, MozbuildObject
from mozbuild.base import MachCommandConditions as Conditions

HERE = os.path.dirname(os.path.realpath(__file__))

ANDROID_BROWSERS = ["geckoview", "refbrow", "fenix", "chrome-m", "cstm-car-m"]


class RaptorRunner(MozbuildObject):
    def run_test(self, raptor_args, kwargs):
        """Setup and run mozharness.

        We want to do a few things before running Raptor:

        1. Clone mozharness
        2. Make the config for Raptor mozharness
        3. Run mozharness
        """
        # Validate that the user is using a supported python version before doing anything else
        max_py_major, max_py_minor = 3, 13
        sys_maj, sys_min = sys.version_info.major, sys.version_info.minor
        if sys_min > max_py_minor:
            raise PythonVersionException(
                print(
                    f"\tPlease downgrade your Python version as Raptor does not yet support Python "
                    f"versions greater than {max_py_major}.{max_py_minor}."
                    f"\n\tYou seem to currently be using Python {sys_maj}.{sys_min}."
                    f"\n\tSee here for a possible solution in debugging your python environment: "
                    f"https://firefox-source-docs.mozilla.org/testing/perfdocs/"
                    f"debugging.html#debugging-local-python-environment"
                )
            )
        self.init_variables(raptor_args, kwargs)
        self.make_config()
        self.write_config()
        self.make_args()
        return self.run_mozharness()

    def init_variables(self, raptor_args, kwargs):
        self.raptor_args = raptor_args

        if kwargs.get("host") == "HOST_IP":
            kwargs["host"] = os.environ["HOST_IP"]
        self.host = kwargs["host"]
        self.is_release_build = kwargs["is_release_build"]
        self.live_sites = kwargs["live_sites"]
        self.disable_perf_tuning = kwargs["disable_perf_tuning"]
        self.conditioned_profile = kwargs["conditioned_profile"]
        self.device_name = kwargs["device_name"]
        self.enable_marionette_trace = kwargs["enable_marionette_trace"]
        self.browsertime_visualmetrics = kwargs["browsertime_visualmetrics"]
        self.browsertime_node = kwargs["browsertime_node"]
        self.clean = kwargs["clean"]
        self.screenshot_on_failure = kwargs["screenshot_on_failure"]

        if Conditions.is_android(self) or kwargs["app"] in ANDROID_BROWSERS:
            self.binary_path = None
        elif kwargs["app"] != "firefox":
            # Raptor picks the installed browser when --binary isn't given.
            self.binary_path = kwargs.get("binary")
        else:
            self.binary_path = kwargs.get("binary") or self.get_binary_path()

        self.python = sys.executable

        self.raptor_dir = os.path.join(self.topsrcdir, "testing", "raptor")
        self.mozharness_dir = os.path.join(self.topsrcdir, "testing", "mozharness")
        self.config_file_path = os.path.join(
            self._topobjdir, "testing", "raptor-in_tree_conf.json"
        )

        self.virtualenv_script = os.path.join(
            self.topsrcdir, "third_party", "python", "virtualenv", "virtualenv.py"
        )
        self.virtualenv_path = os.path.join(self._topobjdir, "testing", "raptor-venv")

    def make_config(self):
        default_actions = [
            "populate-webroot",
            "create-virtualenv",
            "install-chromium-distribution",
            "run-tests",
        ]
        self.config = {
            "run_local": True,
            "binary_path": self.binary_path,
            "repo_path": self.topsrcdir,
            "raptor_path": self.raptor_dir,
            "obj_path": self.topobjdir,
            "log_name": "raptor",
            "virtualenv_path": self.virtualenv_path,
            "pypi_url": "http://pypi.org/simple",
            "base_work_dir": self.mozharness_dir,
            "exes": {
                "python": self.python,
                "virtualenv": [self.python, self.virtualenv_script],
            },
            "title": socket.gethostname(),
            "default_actions": default_actions,
            "raptor_cmd_line_args": self.raptor_args,
            "host": self.host,
            "live_sites": self.live_sites,
            "disable_perf_tuning": self.disable_perf_tuning,
            "conditioned_profile": self.conditioned_profile,
            "is_release_build": self.is_release_build,
            "device_name": self.device_name,
            "enable_marionette_trace": self.enable_marionette_trace,
            "browsertime_visualmetrics": self.browsertime_visualmetrics,
            "browsertime_node": self.browsertime_node,
            "mozbuild_path": get_state_dir(),
            "clean": self.clean,
            "screenshot_on_failure": self.screenshot_on_failure,
        }

        sys.path.insert(0, os.path.join(self.topsrcdir, "tools", "browsertime"))
        try:
            import platform

            import mach_commands as browsertime

            # We don't set `browsertime_{chromedriver,geckodriver} -- those will be found by
            # browsertime in its `node_modules` directory, which is appropriate for local builds.
            # We don't set `browsertime_ffmpeg` yet: it will need to be on the path.  There is code
            # to configure the environment including the path in
            # `tools/browsertime/mach_commands.py` but integrating it here will take more effort.
            self.config.update({
                "browsertime_browsertimejs": browsertime.browsertime_path(),
                "browsertime_vismet_script": browsertime.visualmetrics_path(),
            })

            def _get_browsertime_package():
                with open(
                    os.path.join(
                        self.topsrcdir,
                        "tools",
                        "browsertime",
                        "node_modules",
                        "browsertime",
                        "package.json",
                    )
                ) as package:
                    return json.load(package)

            def _get_browsertime_resolved():
                try:
                    with open(
                        os.path.join(
                            self.topsrcdir,
                            "tools",
                            "browsertime",
                            "node_modules",
                            ".package-lock.json",
                        )
                    ) as package_lock:
                        return json.load(package_lock)["packages"][
                            "node_modules/browsertime"
                        ]["resolved"]
                except FileNotFoundError:
                    # Older versions of node/npm add this metadata to package.json
                    return _get_browsertime_package()["_from"]

            def _should_install():
                # If ffmpeg doesn't exist in the .mozbuild directory,
                # then we should install
                btime_cache = os.path.join(self.config["mozbuild_path"], "browsertime")
                if not os.path.exists(btime_cache) or not any([
                    "ffmpeg" in cache_dir for cache_dir in os.listdir(btime_cache)
                ]):
                    return True

                # If browsertime doesn't exist, install it
                if not os.path.exists(
                    self.config["browsertime_browsertimejs"]
                ) or not os.path.exists(self.config["browsertime_vismet_script"]):
                    return True

                # Browsertime exists, check if it's outdated
                with open(
                    os.path.join(self.topsrcdir, "tools", "browsertime", "package.json")
                ) as new:
                    new_pkg = json.load(new)

                return not _get_browsertime_resolved().endswith(
                    new_pkg["devDependencies"]["browsertime"]
                )

            def _get_browsertime_version():
                # Returns the (version number, current commit) used
                return (
                    _get_browsertime_package()["version"],
                    _get_browsertime_resolved(),
                )

            # Check if browsertime scripts exist and try to install them if
            # they aren't
            if _should_install():
                # TODO: Make this "integration" nicer in the near future
                print("Missing browsertime files...attempting to install")
                subprocess.check_output(
                    [
                        os.path.join(self.topsrcdir, "mach"),
                        "browsertime",
                        "--setup",
                        "--clobber",
                    ],
                    shell="windows" in platform.system().lower(),
                )
                if _should_install():
                    raise Exception(
                        "Failed installation attempt. Cannot find browsertime scripts. "
                        "Run `./mach browsertime --setup --clobber` to set it up."
                    )

            version, src = _get_browsertime_version()
            print(f"Using browsertime version {version} from {src}")

        finally:
            sys.path = sys.path[1:]

    def make_args(self):
        self.args = {
            "config": {},
            "initial_config_file": self.config_file_path,
        }

    def write_config(self):
        try:
            config_file = open(self.config_file_path, "w")
            config_file.write(json.dumps(self.config))
            config_file.close()
        except OSError as e:
            err_str = "Error writing to Raptor Mozharness config file {0}:{1}"
            print(err_str.format(self.config_file_path, str(e)))
            raise e

    def run_mozharness(self):
        sys.path.insert(0, self.mozharness_dir)
        from mozharness.mozilla.testing.raptor import Raptor

        raptor_mh = Raptor(
            config=self.args["config"],
            initial_config_file=self.args["initial_config_file"],
        )
        return raptor_mh.run()


def setup_node(command_context):
    """Fetch the pinned node toolchain and install it into the .mozbuild directory."""
    import platform

    from mozbuild.artifact_commands import artifact_toolchain
    from mozbuild.nodeutil import find_node_executable
    from packaging.version import Version

    # Pinned because node upgrades change browsertime performance numbers,
    # see bug 1942260.
    node_major = 24
    plat = platform.system()
    if plat == "Windows":
        toolchain_platform = "win64"
    elif plat == "Darwin":
        if platform.processor() == "arm":
            toolchain_platform = "macosx64-aarch64"
        else:
            toolchain_platform = "macosx64"
            # Node 24 requires macOS 13.5, so there is no macosx64-node-24.
            node_major = 22
    else:
        toolchain_platform = "linux64"
    toolchain_job = f"{toolchain_platform}-node-{node_major}"

    print("Setting up node for browsertime...")
    state_dir = get_state_dir()
    cache_path = os.path.join(state_dir, "browsertime", f"node-{node_major}")

    def __check_for_node():
        # Check standard locations first
        node_exe = find_node_executable(min_version=Version(f"{node_major}.0.0"))
        if node_exe and (node_exe[0] is not None):
            return node_exe[0]
        if not os.path.exists(cache_path):
            return None

        # Check the browsertime-specific node location next
        node_name = "node"
        if platform.system() == "Windows":
            node_name = "node.exe"
            node_exe_path = os.path.join(cache_path, "node")
        else:
            node_exe_path = os.path.join(cache_path, "node", "bin")

        node_exe = os.path.join(node_exe_path, node_name)
        if not os.path.exists(node_exe):
            return None

        return node_exe

    node_exe = __check_for_node()
    if node_exe is None:
        print(
            f"Downloading Node {node_major} from Taskcluster toolchain {toolchain_job}..."
        )

        if not os.path.exists(cache_path):
            os.makedirs(cache_path, exist_ok=True)

        # Change directories to where node should be installed
        # before installing. Otherwise, it gets installed in the
        # top level of the repo (or the current working directory).
        cur_dir = os.getcwd()
        os.chdir(cache_path)
        artifact_toolchain(
            command_context,
            verbose=False,
            from_build=[toolchain_job],
            no_unpack=False,
            retry=0,
            cache_dir=cache_path,
        )
        os.chdir(cur_dir)

        node_exe = __check_for_node()
        if node_exe is None:
            raise Exception(
                f"Could not find Node v{node_major} binary for Raptor-Browsertime"
            )

        print(f"Finished downloading Node v{node_major} from Taskcluster")

    print(f"Node v{node_major}+ found at: {node_exe}")
    return node_exe


def create_parser():
    sys.path.insert(0, HERE)  # allow to import the raptor package
    from raptor.cmdline import create_parser

    return create_parser(mach_interface=True)


@Command(
    "raptor",
    category="testing",
    description="Run Raptor performance tests.",
    parser=create_parser,
)
def run_raptor(command_context, **kwargs):
    build_obj = command_context

    # Setup node for browsertime
    kwargs["browsertime_node"] = setup_node(command_context)

    is_android = Conditions.is_android(build_obj) or kwargs["app"] in ANDROID_BROWSERS

    if is_android:
        from mozrunner.devices.android_device import (
            InstallIntent,
            verify_android_device,
        )

        install = (
            InstallIntent.NO if kwargs.pop("no_install", False) else InstallIntent.YES
        )
        verbose = False
        if (
            kwargs.get("log_mach_verbose")
            or kwargs.get("log_tbpl_level") == "debug"
            or kwargs.get("log_mach_level") == "debug"
            or kwargs.get("log_raw_level") == "debug"
        ):
            verbose = True
        if not verify_android_device(
            build_obj,
            install=install,
            app=kwargs["binary"],
            verbose=verbose,
            xre=True,
        ):  # Equivalent to 'run_local' = True.
            print(
                "****************************************************************************"
            )
            print(
                "Unable to verify device, please check your attached/connected android device"
            )
            print(
                "****************************************************************************"
            )
            return 1

    # Remove mach global arguments from sys.argv to prevent them
    # from being consumed by raptor. Treat any item in sys.argv
    # occuring before "raptor" as a mach global argument.
    argv = []
    in_mach = True
    for arg in sys.argv:
        if not in_mach:
            argv.append(arg)
        if arg.startswith("raptor"):
            in_mach = False

    raptor = command_context._spawn(RaptorRunner)

    try:
        return raptor.run_test(argv, kwargs)
    except BinaryNotFoundException as e:
        command_context.log(
            logging.ERROR, "raptor", {"error": str(e)}, "ERROR: {error}"
        )
        command_context.log(logging.INFO, "raptor", {"help": e.help()}, "{help}")
        return 1
    except Exception as e:
        print(repr(e))
        return 1


@Command(
    "raptor-test",
    category="testing",
    description="Run Raptor performance tests.",
    parser=create_parser,
)
def run_raptor_test(command_context, **kwargs):
    return run_raptor(command_context, **kwargs)


class PythonVersionException(Exception):
    pass
