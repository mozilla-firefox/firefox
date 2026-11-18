# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Download, install and run `nimbus-cli` against a local Android build.

Driven by `mach android nimbus-cli`; see https://experimenter.info/nimbus-cli
for what `nimbus-cli` itself does.
"""

from __future__ import annotations

import json
import logging
import os
import platform
import re
import shutil
import subprocess
import tempfile
import time
import zipfile
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Literal
from urllib.request import urlopen, urlretrieve


class UpdateMode(Enum):
    CHECK = "check"
    FORCE = "force"


INDEX = (
    "https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/"
    "project.application-services.v2.nimbus-cli.latest/artifacts/public/build"
)

# The targets application-services publishes prebuilt `nimbus-cli` archives for.
# Linux x86_64 uses the statically linked musl build to avoid glibc mismatches.
TARGETS = {
    ("Darwin", "arm64"): "aarch64-apple-darwin",
    ("Darwin", "x86_64"): "x86_64-apple-darwin",
    ("Linux", "aarch64"): "aarch64-unknown-linux-gnu",
    ("Linux", "x86_64"): "x86_64-unknown-linux-musl",
    ("Windows", "x86_64"): "x86_64-pc-windows-gnu",
}

# How long a recorded "latest version" lookup is trusted before checking again.
UPDATE_INTERVAL = 24 * 60 * 60


def _target_triple() -> str:
    """Return the target triple (arch-venor-os) for the current machine."""
    system = platform.system()
    machine = platform.machine()

    normalized = machine.lower()
    if normalized == "amd64":
        normalized = "x86_64"
    elif normalized == "arm64" and system != "Darwin":
        normalized = "aarch64"

    target = TARGETS.get((system, normalized))
    if not target:
        raise Exception(
            f"No prebuilt nimbus-cli is available for {system} {machine}. "
            "You can build it from source at "
            "third_party/application-services/components/support/nimbus-cli, or see "
            "https://experimenter.info/nimbus-cli/install for other options."
        )
    return target


def _fetch_latest_version() -> str:
    """Fetch the latest published version of nimbus-cli."""
    with urlopen(f"{INDEX}/nimbus-cli.json") as response:
        return json.load(response)["version"]


def _latest_version(command_context, install_root, exe, check_for_update) -> str:
    """Return the latest published nimbus-cli version.

    The answer is recorded so that repeat invocations don't trigger network
    requests.

    If the lookup fails, this will fall back to the latest locally installed
    version.
    """
    record = install_root / "latest.json"
    if not check_for_update and record.exists():
        try:
            recorded = json.loads(record.read_text())
            if time.time() - recorded["checked"] < UPDATE_INTERVAL:
                return recorded["version"]
        except (KeyError, OSError, TypeError, ValueError):
            pass

    try:
        version = _fetch_latest_version()
    except Exception as e:
        installed = _installed_versions(install_root, exe)
        if not installed:
            raise
        command_context.log(
            logging.WARN,
            "nimbus-cli",
            {"error": str(e), "version": installed[-1]},
            "Could not check for a newer nimbus-cli ({error}); using {version}.",
        )
        return installed[-1]

    install_root.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps({"version": version, "checked": time.time()}))
    return version


def _installed_versions(install_root, exe) -> list[str]:
    if not install_root.is_dir():
        return []

    versions = [
        path.name
        for path in install_root.iterdir()
        if re.fullmatch(r"\d+(\.\d+)*", path.name) and (path / exe).exists()
    ]
    return sorted(versions, key=lambda v: [int(part) for part in v.split(".")])


def _install(command_context, version, target, binary):
    archive_name = f"nimbus-cli-{target}.zip"
    url = f"{INDEX}/{archive_name}"

    command_context.log(
        logging.INFO,
        "nimbus-cli",
        {"version": version, "target": target},
        "Downloading nimbus-cli {version} for {target}...",
    )

    binary.parent.mkdir(parents=True, exist_ok=True)

    # Stage alongside the destination so the final move is atomic. An interrupted
    # download must not leave a truncated binary that later runs would reuse.
    with tempfile.TemporaryDirectory(dir=binary.parent) as tmp_dir:
        archive = os.path.join(tmp_dir, archive_name)
        urlretrieve(url, archive)

        staged = Path(tmp_dir) / binary.name
        with zipfile.ZipFile(archive) as zf:
            # The archive nests the binary under "<target>/release/".
            member = next(
                (n for n in zf.namelist() if os.path.basename(n) == binary.name), None
            )
            if member is None:
                raise Exception(f"{archive_name} does not contain {binary.name}")

            with zf.open(member) as src, open(staged, "wb") as dst:
                shutil.copyfileobj(src, dst)

        staged.chmod(staged.stat().st_mode | 0o755)

        if platform.system() == "Darwin":
            # Otherwise Gatekeeper refuses to run the freshly downloaded binary.
            subprocess.run(
                ["xattr", "-d", "com.apple.quarantine", str(staged)],
                stderr=subprocess.DEVNULL,
                check=False,
            )

        os.replace(staged, binary)

    command_context.log(
        logging.INFO,
        "nimbus-cli",
        {"path": str(binary)},
        "Installed nimbus-cli to {path}",
    )


@dataclass
class Binary:
    """Information about a nimbus-cli binary."""

    #: The path on disk to the binary.
    path: Path

    #: The version of the binary.
    version: str

    #: Whether or not the binary was updated.
    updated: bool


def _binary(command_context, update_mode: UpdateMode | None = None) -> Binary:
    """Ensure the nimbus-cli binary exists and return it.

    The binary will be downloaded if it does not already exist on-disk.
    """
    target = _target_triple()
    install_root = Path(command_context._mach_context.state_dir) / "nimbus-cli"
    exe = "nimbus-cli.exe" if platform.system() == "Windows" else "nimbus-cli"

    updated = False
    version = _latest_version(
        command_context, install_root, exe, update_mode is not None
    )
    binary_path = install_root / version / exe

    if not binary_path.exists() or update_mode == UpdateMode.FORCE:
        _install(command_context, version, target, binary_path)
        updated = True

    return Binary(
        path=binary_path,
        version=version,
        updated=updated,
    )


def run(command_context, args) -> int:
    """Install nimbus-cli if needed and then run it with `args`."""
    try:
        binary = _binary(command_context).path
    except Exception as e:
        command_context.log(logging.ERROR, "nimbus-cli", {"error": str(e)}, "{error}")
        return 1

    # mach keeps nimbus-cli up to date, so its own update check would only print
    # installation advice that conflicts with this command.
    return command_context.run_process(
        [str(binary)] + list(args),
        append_env={"NIMBUS_CLI_SUPPRESS_UPDATE_CHECK": "1"},
        pass_thru=True,
        ensure_exit_code=False,
    )


def update(command_context, update_mode: Literal["check"] | Literal["force"]) -> int:
    """Update nimbus-cli."""
    try:
        binary = _binary(command_context, update_mode=UpdateMode(update_mode))
    except Exception as e:
        command_context.log(logging.ERROR, "nimbus-cli", {"error": str(e)}, "{error}")
        return 1

    if not binary.updated:
        command_context.log(
            logging.INFO,
            "nimbus-cli",
            {"version": binary.version},
            "Already up to date (version {version})",
        )
        command_context.log(
            logging.INFO,
            "nimbus-cli",
            {
                "path": str(binary.path),
            },
            "nimbus-cli is installed at {path}",
        )

    return 0
