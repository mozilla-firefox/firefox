# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import os
import re
import subprocess
from copy import deepcopy
from pathlib import Path
from typing import Any, Optional

import mozpack.path as mozpath
from mozfile import which
from mozlint import result
from mozlint.pathutils import expand_exclusions
from packaging.version import Version


def to_str_affected(info: dict[str, Any]) -> str:
    """Converts affected platform info into a string.

    Args:
        info: JSON dictionary containing affected platform info.

    Returns:
        A string representing the vulnerability's affected platforms.
    """
    affected = info.get("affected")
    if not affected:
        return ""

    lines = []
    for key, label in [
        ("arch", "architectures"),
        ("os", "operating systems"),
        ("functions", "functions"),
    ]:
        if affected.get(key):
            lines.append(f"\nAffected {label}: {json.dumps(affected[key], indent=2)}")
    return "".join(lines)


def to_str_versions(info: dict[str, Any]) -> str:
    """Convert versioning info into a string.

    Args:
        info: JSON dictionary containing vulnerability versioning info.

    Returns:
        A string summarizing the vulnerability's version info.
    """

    versions = info.get("versions")
    if not versions:
        return ""

    lines = []
    for key in ("patched", "unaffected"):
        if versions.get(key):
            lines.append(
                f"\n{key.capitalize()} versions: {json.dumps(versions[key], indent=2)}"
            )
    return "".join(lines)


def to_str_advisory(advisory: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    """Create a string summary of the "advisory" section in a vulnerability description.

    Args:
        advisory: a dictionary describing an advisory.

    Returns:
        If the advisory is empty, returns a tuple containing a string describing the
        most important parts of the advisory and a dictionary containing the
        leftover parts.

        If the advisory section is empty, the returned string and dictionary will both
        be empty.
    """
    message = ""
    if advisory:
        advisory = deepcopy(advisory)
        message = f"{advisory.pop('title')}\nPackage: {advisory.pop('package')}\nID: {advisory.pop('id')}"

        cvss = advisory.pop("cvss", False)
        if cvss:
            message += f"\nCVSS: {cvss}"

        message += (
            f"\nReport date: {advisory.pop('date')}\n{advisory.pop('description')}"
        )

        url = advisory.pop("url", False)
        if url:
            message += f"\nURL: {url}"

    return (message, advisory)


def dump_leftover_advisory(leftovers: dict[str, Any]) -> str:
    """Convert remaining advisory info to a string.

    Args:
        leftovers: A JSON dictionary containing the remaining advisory data.

    Returns:
        A string containing the remaining advisory data.
    """
    message = ""
    if leftovers:
        message += f"Advisory metadata: {json.dumps(leftovers, indent=2)}"

    return message


def build_message(kind: str, info: dict[str, Any], verbose: bool) -> str:
    """Build a useful message describing a vulnerability.

    Args:
        kind: A string representing the category of the vulnerability.
        info: JSON dictionary representation of a vulnerability from the cargo-audit output.
        verbose: A bool indicating if we should print more information.

    Returns:
        A string summarizing the vulnerability.
    """
    package = info["package"]
    message = f"Crate depends on a {kind} version of {package['name']}."

    advisory = info.get("advisory", False)
    leftover_advisory = None
    if advisory:
        (temp, leftover_advisory) = to_str_advisory(advisory)
        message += f"\n\nAdvisory:\n{temp}"

    message += to_str_versions(info)
    message += to_str_affected(info)

    if verbose and leftover_advisory is not None:
        message += "\n" + dump_leftover_advisory(leftover_advisory)

    return message + "\n\n" + f"Package info: {json.dumps(package, indent=2)}"


def build_issue(config, path, message, level) -> Any:
    return result.from_config(
        config,
        **{
            "path": path,
            "message": message,
            "lineno": -1,
            "column": -1,
            "level": level,
        },
    )


def run_process(args: list[str]) -> str:
    try:
        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            # Allow failure if output exists (e.g., vulnerabilities found)
            if result.stdout:
                return result.stdout
            else:
                message = [
                    f"cargo-audit failed (exit code {result.returncode}) while running:",
                    f"  {' '.join(args)}",
                ]
                if result.stderr.strip():
                    message.append("\nstderr:")
                    message.append(result.stderr.rstrip())
                # A non-zero exit code with no output usually means cargo-audit
                # could not load the RustSec advisory database (for example
                # because it contains an advisory in a format the pinned
                # cargo-audit version cannot parse). That is not a regression
                # caused by a specific push, so point at the tracking bug.
                message.append(
                    "\nThis usually means cargo-audit could not load the RustSec "
                    "advisory database, which evolves independently of this "
                    "repository. If cargo-audit needs updating to handle it, "
                    "please file a new bug blocking bug 1747536."
                )
                raise RuntimeError("\n".join(message))
        return result.stdout
    except FileNotFoundError as e:
        raise RuntimeError(
            f"Executable not found when running command: {' '.join(args)}\n"
            f"Original error: {e}\n"
            "Make sure the executable is installed and in your system PATH."
        )


def is_excluded(message: str, exclusions: list[str]) -> bool:
    return any(exclusion in message for exclusion in exclusions)


def lint(paths, config, log, **lintargs) -> list[Any]:
    results = []
    files = list(expand_exclusions(paths, config, lintargs["root"]))
    args = ["cargo-audit", "audit", "--json"]
    for f in files:
        tail = ["--file", f]

        raw = run_process(args + tail)
        try:
            cargo_audit = json.loads(raw)
        except json.JSONDecodeError:
            log.warn(f"Could not parse cargo-audit output for file: {f}")
            log.debug(f"cargo-audit output for {f}: {raw}")
        else:
            show_verbose = lintargs.get("show_verbose", False)
            vulnerabilities = cargo_audit["vulnerabilities"]
            if vulnerabilities["found"]:
                for vulnerability in vulnerabilities["list"]:
                    message = build_message("vulnerable", vulnerability, show_verbose)
                    exclusions = config.get("exclude-error", [])
                    if not is_excluded(message, exclusions):
                        results.append(build_issue(config, f, message, "error"))

            warning_categories = cargo_audit["warnings"]
            for kind, warnings in warning_categories.items():
                for warning in warnings:
                    message = build_message(kind, warning, show_verbose)
                    exclusions = config.get("exclude-error", [])
                    if not is_excluded(message, exclusions):
                        results.append(build_issue(config, f, message, "warning"))

    return results


def get_audit_version() -> str:
    """Get the version number of the cargo-audit installation.

    Returns:
        str: A string representing the cargo-audit version.
    """
    return run_process(["cargo-audit", "--version"]).strip().split()[1]


def get_rust_version() -> Optional[str]:
    """Get the version number from rustc.

    Returns:
        str: A string representing the rust version or None if it couldn't be
            detected.
    """
    proc = subprocess.run(
        ["rustc", "--version"], capture_output=True, text=True, check=False
    )
    if proc.returncode != 0:
        return

    m = re.match(r"^rustc (\S+) \(.+\)$", proc.stdout)
    if not m:
        return

    version = m.group(1)
    if "-" in version:
        version = version.rsplit("-", 1)[0]
    return version


def setup(root, log, **lintargs) -> int:
    min_rust_version = "1.85.0"
    rust_version = get_rust_version()
    if not rust_version or Version(rust_version) < Version(min_rust_version):
        version_was = f"{rust_version} was" if rust_version else "was not"
        log.error(
            f"cargo-audit requires at least Rust {min_rust_version}, but Rust {version_was} detected!"
        )
        return 1

    binary = which("cargo-audit")

    with Path(__file__).parent.joinpath("cargo-audit_version.txt").open() as f:
        desired_version = f.read().strip()

        installed_version = False
        if binary and os.path.exists(binary):
            binary = mozpath.normsep(binary)
            installed_version = get_audit_version()

        if not installed_version or installed_version != desired_version:
            output = run_process([
                "cargo",
                "install",
                "--locked",
                "--version",
                desired_version,
                "--color",
                "never",
                "cargo-audit",
            ])
            if not which("cargo-audit") or get_audit_version() != desired_version:
                log.error(f"Could not install cargo-audit:\n{output}")
                return 1
    return 0
