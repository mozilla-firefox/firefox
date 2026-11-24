# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

VENV_PATH = f"{os.getcwd()}/build/venv"

config = {
    "log_name": "raptor",
    "installer_path": "installer.exe",
    "virtualenv_path": VENV_PATH,
    "title": os.uname()[1].lower().split(".")[0],
    "default_actions": [
        "clobber",
        "download-and-extract",
        "populate-webroot",
        "create-virtualenv",
        "install-chromium-distribution",
        "install",
        "run-tests",
    ],
    "run_cmd_checks_enabled": True,
    "preflight_run_cmd_suites": [
        {
            "name": "verify refresh rate",
            "cmd": [
                sys.executable,
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "macosx_resolution_refreshrate.py",
                ),
                "--check=refresh-rate",
            ],
            "architectures": ["64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "verify screen resolution",
            "cmd": [
                sys.executable,
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "macosx_resolution_refreshrate.py",
                ),
                "--check=resolution",
            ],
            "architectures": ["64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
    ],
    "postflight_run_cmd_suites": [],
    "tooltool_cache": "/builds/tooltool_cache",
}
