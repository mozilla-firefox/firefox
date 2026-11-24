# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
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
        "tooltool.py": [
            PYTHON,
            os.path.join(os.environ["MOZILLABUILD"], "tooltool.py"),
        ],
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
    "run_cmd_checks_enabled": True,
    "preflight_run_cmd_suites": [
        {
            "name": "run mouse & screen adjustment script",
            "cmd": [
                sys.executable,
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "mouse_and_screen_resolution.py",
                ),
                "--configuration-file",
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "machine-configuration.json",
                ),
                "--platform",
                "win10-hw",
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "ensure display refresh rate == 60",
            "cmd": [
                "powershell",
                "-command",
                'if (-Not ((Get-WmiObject win32_videocontroller).CurrentRefreshRate | Out-String).contains("60")) { echo "Screen refresh rate != 60: " + ((Get-WmiObject win32_videocontroller).CurrentRefreshRate | Out-String); exit 4; }',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True if (platform.uname().version == "10.0.26100") else False,
            "fatal_exit_code": 4,
        },
    ],
}
