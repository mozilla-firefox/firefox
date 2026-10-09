# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import subprocess
import sys
from pathlib import Path

import mozunit
import pytest
from mach.registrar import Registrar


@pytest.fixture(scope="module")
def mach_commands():
    registered = "testing" not in Registrar.categories
    if registered:
        Registrar.register_category("testing", "testing", "testing")

    path = Path(__file__).resolve().parents[2] / "mach_commands.py"
    spec = importlib.util.spec_from_file_location("code_coverage_mach_commands", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    yield module

    if registered:
        del Registrar.categories["testing"]
        del Registrar.commands_by_category["testing"]
    Registrar.command_handlers.pop("coverage-report", None)


@pytest.mark.parametrize(
    "task_name,expected",
    [
        ("test-linux2404-64-ccov/opt-gtest-1proc", "linux"),
        ("test-windows11-64-24h2-ccov/opt-mochitest-plain-1", "windows"),
        ("test-macosx1500-aarch64-ccov/opt-xpcshell-1", "macos"),
        ("test-android-em-14-x86_64-ccov/opt-geckoview-gtest-1proc", "android"),
        ("source-test-python-coverage-ccov", "linux"),
    ],
)
def test_get_platform(mach_commands, task_name, expected):
    assert mach_commands._get_platform(task_name) == expected


def test_coverage_report_latest_push_gtest(tmp_path):
    topsrcdir = Path(__file__).resolve().parents[4]
    output_dir = tmp_path / "report"

    args = [
        sys.executable,
        "mach",
        "coverage-report",
        "--platform",
        "linux",
        "--suite",
        "gtest",
        "--stats",
        "--output-dir",
        str(output_dir),
    ]

    fetches_dir = os.environ.get("MOZ_FETCHES_DIR")
    if fetches_dir:
        args.extend(["--grcov", os.path.join(fetches_dir, "grcov", "grcov")])

    result = subprocess.run(
        args,
        cwd=topsrcdir,
        check=False,
        text=True,
        capture_output=True,
        timeout=600,
    )

    assert result.returncode == 0, result.stderr
    assert (output_dir / "output.json").exists()
    assert "Coverage percentage:" in result.stdout


if __name__ == "__main__":
    mozunit.main()
