# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import socket
import sys

PYTHON = sys.executable
VENV_PATH = os.path.join(os.getcwd(), "build/venv")

PYWIN32 = "pywin32==306"

config = {
    "log_name": "raptor",
    "installer_path": "installer.exe",
    "virtualenv_path": VENV_PATH,
    "virtualenv_modules": [PYWIN32, "raptor", "mozinstall"],
    "exes": {
        "python": PYTHON,
        "mozinstall": [
            f"{VENV_PATH}/scripts/python",
            f"{VENV_PATH}/scripts/mozinstall-script.py",
        ],
        "hg": os.path.join(os.environ["PROGRAMFILES"], "Mercurial", "hg"),
    },
    "title": socket.gethostname().split(".")[0],
    "default_actions": [
        "clobber",
        "download-and-extract",
        "populate-webroot",
        "create-virtualenv",
        "install-chromium-distribution",
        "install",
        "run-tests",
    ],
    "tooltool_cache": os.path.join("c:\\", "build", "tooltool_cache"),
    "python3_manifest": {
        "win32": "python3.manifest",
        "win64": "python3_x64.manifest",
    },
    "env": {
        # python3 requires C runtime, found in firefox installation; see bug 1361732
        "PATH": "%(PATH)s;c:\\slave\\test\\build\\application\\firefox;"
    },
}
