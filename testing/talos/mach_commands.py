# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Integrates Talos mozharness with mach

import json
import logging
import os
import socket
import sys

from mach.decorators import Command
from mach.util import get_state_dir
from mozbuild.base import BinaryNotFoundException, MozbuildObject

HERE = os.path.dirname(os.path.realpath(__file__))


class TalosRunner(MozbuildObject):
    def run_test(self, talos_args):
        """
        We want to do couple of things before running Talos
        1. Clone mozharness
        2. Make config for Talos Mozharness
        3. Run mozharness
        """
        # Validate that the user is using a supported python version before doing anything else
        max_py_major, max_py_minor = 3, 13
        sys_maj, sys_min = sys.version_info.major, sys.version_info.minor
        if sys_min > max_py_minor:
            raise PythonVersionException(
                print(
                    f"\tPlease downgrade your Python version as talos does not yet support Python "
                    f"versions greater than {max_py_major}.{max_py_minor}."
                    f"\n\tYou seem to currently be using Python {sys_maj}.{sys_min}."
                    f"\n\tSee here for a possible solution in debugging your python environment: "
                    f"https://firefox-source-docs.mozilla.org/testing/perfdocs/"
                    f"debugging.html#debugging-local-python-environment"
                )
            )
        try:
            self.init_variables(talos_args)
        except BinaryNotFoundException as e:
            self.log(logging.ERROR, "talos", {"error": str(e)}, "ERROR: {error}")
            self.log(logging.INFO, "raptor", {"help": e.help()}, "{help}")
            return 1

        self.make_config()
        self.write_config()
        self.make_args()
        return self.run_mozharness()

    def init_variables(self, talos_args):
        self.talos_dir = os.path.join(self.topsrcdir, "testing", "talos")
        self.mozharness_dir = os.path.join(self.topsrcdir, "testing", "mozharness")
        self.talos_json = os.path.join(self.talos_dir, "talos.json")
        self.config_file_path = os.path.join(
            self._topobjdir, "testing", "talos-in_tree_conf.json"
        )
        self.binary_path = self.get_binary_path()
        self.virtualenv_script = os.path.join(
            self.topsrcdir, "third_party", "python", "virtualenv", "virtualenv.py"
        )
        self.virtualenv_path = os.path.join(self._topobjdir, "testing", "talos-venv")
        self.python_interp = sys.executable
        self.talos_args = talos_args

    def make_config(self):
        default_actions = ["populate-webroot"]
        default_actions.extend([
            "create-virtualenv",
            "run-tests",
        ])
        self.config = {
            "run_local": True,
            "talos_json": self.talos_json,
            "binary_path": self.binary_path,
            "repo_path": self.topsrcdir,
            "obj_path": self.topobjdir,
            "log_name": "talos",
            "virtualenv_path": self.virtualenv_path,
            "pypi_url": "http://pypi.python.org/simple",
            "base_work_dir": self.mozharness_dir,
            "exes": {
                "python": self.python_interp,
                "virtualenv": [self.python_interp, self.virtualenv_script],
            },
            "title": socket.gethostname(),
            "default_actions": default_actions,
            "talos_extra_options": ["--develop"] + self.talos_args,
            "python3_manifest": {
                "win32": "python3.manifest",
                "win64": "python3_x64.manifest",
            },
            "mozbuild_path": get_state_dir(),
        }

    def make_args(self):
        self.args = {
            "config": {},
            "initial_config_file": self.config_file_path,
        }

    def write_config(self):
        try:
            with open(self.config_file_path, "wb") as config_file:
                config_file.write(json.dumps(self.config).encode("utf-8"))
        except OSError as e:
            err_str = "Error writing to Talos Mozharness config file {0}:{1}"
            print(err_str.format(self.config_file_path, str(e)))
            raise e

    def run_mozharness(self):
        sys.path.insert(0, self.mozharness_dir)
        from mozharness.mozilla.testing.talos import Talos

        talos_mh = Talos(
            config=self.args["config"],
            initial_config_file=self.args["initial_config_file"],
        )
        return talos_mh.run()


def create_parser():
    sys.path.insert(0, HERE)  # allow to import the talos package
    from talos.cmdline import create_parser

    return create_parser(mach_interface=True)


TALOS_PDFS_INDEX = "gecko.cache.level-3.toolchains.v3.talos-pdfs.latest"
TALOS_PDFS_ARTIFACT = "project/gecko/talos-pdfs/talos-pdfs.tar.zst"


def setup_toolchain_artifacts(args, command_context):
    if not any(arg.lower() == "pdfpaint" for arg in args):
        return

    from mozbuild.util import (
        TASKCLUSTER_ROOT_URL,
        find_task_from_index,
        set_taskcluster_root_url,
    )

    set_taskcluster_root_url()
    try:
        task_id = find_task_from_index([TALOS_PDFS_INDEX])
    except Exception:
        task_id = None

    if task_id:
        link = f"{TASKCLUSTER_ROOT_URL}/tasks/{task_id}"
    else:
        namespace, _, name = TALOS_PDFS_INDEX.rpartition(".")
        link = f"{TASKCLUSTER_ROOT_URL}/tasks/index/{namespace}/{name}"

    local_task_id = None
    state_dir = get_state_dir()
    pdfs_dir = os.path.join(state_dir, "talos-pdfs")
    if os.path.isdir(pdfs_dir):
        task_id_path = os.path.join(pdfs_dir, "task-id.txt")
        if os.path.isfile(task_id_path):
            with open(task_id_path) as f:
                local_task_id = f.read().strip()

    if task_id and local_task_id == task_id:
        return

    raise Exception(
        "The PDFs for the pdfpaint test are missing or outdated (local task "
        f"{local_task_id or 'unknown'}, latest task {task_id}). "
        "They come from a private repository, so they can't be downloaded automatically.\n"
        f"Sign in to Taskcluster, and download the {TALOS_PDFS_ARTIFACT} "
        f"artifact from:\n\n    {link}\n\n"
        "Then extract it so that its talos-pdfs folder ends up in "
        f"{state_dir}, for instance with:\n\n"
        f"    tar --zstd -xf talos-pdfs.tar.zst -C {state_dir}\n"
    )


@Command(
    "talos-test",
    category="testing",
    description="Run talos tests (performance testing).",
    parser=create_parser,
)
def run_talos_test(command_context, **kwargs):
    talos = command_context._spawn(TalosRunner)

    try:
        args = sys.argv[2:]
        setup_toolchain_artifacts(args, command_context)
        return talos.run_test(args)
    except Exception as e:
        print(str(e))
        return 1


class PythonVersionException(Exception):
    pass
