# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

PYTHON = sys.executable
VENV_PATH = f"{os.getcwd()}/build/venv"

exes = {
    "python": PYTHON,
}
ABS_WORK_DIR = os.path.join(os.getcwd(), "build")
INSTALLER_PATH = os.path.join(ABS_WORK_DIR, "installer.tar.xz")

config = {
    "log_name": "talos",
    "installer_path": INSTALLER_PATH,
    "virtualenv_path": VENV_PATH,
    "exes": exes,
    "title": os.uname()[1].lower().split(".")[0],
    "default_actions": [
        "clobber",
        "download-and-extract",
        "populate-webroot",
        "create-virtualenv",
        "install",
        "run-tests",
    ],
    "tooltool_cache": "/builds/worker/tooltool-cache",
}
