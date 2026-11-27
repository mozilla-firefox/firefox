#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Firebase Test Lab (Flank) test runner script for Taskcluster
# This script is used to run UI tests on Firebase Test Lab using Flank
# It requires a service account key file to authenticate with Firebase Test Lab
# It also requires the `gcloud` command line tool to be installed and configured
# Lastly it requires the `flank.jar` file to be present in the `test-tools` directory set up in the task definition
# The service account key file is stored in the `secrets` section of the task definition

# Flank: https://flank.github.io/flank/

from __future__ import annotations

import argparse
import logging
import os
import shutil
import subprocess
import sys
from enum import Enum
from pathlib import Path
from typing import Optional, Union
from urllib.parse import urlparse


# Worker paths and binaries
class Worker(Enum):
    JAVA_BIN = "/usr/bin/java"
    FLANK_BIN = "/builds/worker/test-tools/flank.jar"
    RESULTS_DIR = "/builds/worker/artifacts/results"


# Locate other scripts and configs relative to this script. The actual
# invocation of Flank will be relative to ANDROID_TEST path below.
SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
TOPSRCDIR = os.path.join(SCRIPT_DIR, "../../..")
ANDROID_TEST = os.path.join(TOPSRCDIR, "mobile/android/test_infra")


def setup_logging():
    """Configure logging for the script."""
    log_format = "%(message)s"
    logging.basicConfig(level=logging.INFO, format=log_format)


def run_command(
    command: list[Union[str, bytes]], log_path: Optional[str] = None
) -> int:
    """Execute a command, log its output, and check for errors.

    Args:
        command: The command to execute
        log_path: The path to a log file to write the command output to
    Returns:
        int: The exit code of the command
    """

    with subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=ANDROID_TEST,
    ) as process:
        if log_path:
            with open(log_path, "a") as log_file:
                for line in process.stdout:
                    sys.stdout.write(line)
                    log_file.write(line)
        else:
            for line in process.stdout:
                sys.stdout.write(line)
        process.wait()
        sys.stdout.flush()
        if process.returncode != 0:
            error_message = f"Command {' '.join(command)} failed with exit code {process.returncode}"
            logging.error(error_message)
        return process.returncode


def setup_environment():
    """Configure Google Cloud project and authenticate with the service account."""
    project_id = os.getenv("GOOGLE_PROJECT")
    credentials_file = os.getenv("GOOGLE_APPLICATION_CREDENTIALS")
    if not project_id or not credentials_file:
        logging.error(
            "Error: GOOGLE_PROJECT and GOOGLE_APPLICATION_CREDENTIALS environment variables must be set."
        )
        sys.exit(1)

    run_command(["gcloud", "config", "set", "project", project_id])
    run_command([
        "gcloud",
        "auth",
        "activate-service-account",
        "--key-file",
        credentials_file,
    ])


def execute_tests(
    flank_config: str,
    apk_app: Path,
    apk_test: Optional[Path] = None,
    baseline_profile_max_iterations: Optional[str] = None,
) -> int:
    """Run UI tests on Firebase Test Lab using Flank.

    Args:
        flank_config: The YML configuration for Flank to use e.g, automation/taskcluster/androidTest/flank-<config>.yml
        apk_app: Absolute path to a Android APK application package (optional) for robo test or instrumentation test
        apk_test: Absolute path to a Android APK androidTest package
    Returns:
        int: The exit code of the command
    """

    run_command([Worker.JAVA_BIN.value, "-jar", Worker.FLANK_BIN.value, "--version"])

    flank_command = [
        Worker.JAVA_BIN.value,
        "-jar",
        Worker.FLANK_BIN.value,
        "android",
        "run",
        "--config",
        f"{ANDROID_TEST}/flank-configs/{flank_config}",
        "--app",
        str(apk_app),
        "--local-result-dir",
        Worker.RESULTS_DIR.value,
        "--project",
        os.environ.get("GOOGLE_PROJECT"),
    ]

    # Add a client details parameter using the repository name
    # Treeherder is keyed on the hg mirror, so prefer the hg repo + changeset
    # (MOZ_SOURCE_*). On git checkouts GECKO_HEAD_* point at the GitHub repo
    # and a git SHA, which Treeherder can't resolve.
    matrixLabel = os.environ.get("MOZ_SOURCE_REPO") or os.environ.get(
        "GECKO_HEAD_REPOSITORY"
    )
    geckoRev = os.environ.get("MOZ_SOURCE_CHANGESET") or os.environ.get(
        "GECKO_HEAD_REV"
    )

    if matrixLabel is not None and geckoRev is not None:
        flank_command.extend([
            "--client-details",
            f"matrixLabel={urlparse(matrixLabel).path.rpartition('/')[-1]},geckoRev={geckoRev}",
        ])

    if "baseline-profile" in flank_config and baseline_profile_max_iterations:
        flank_command.extend([
            "--environment-variables",
            f"baselineProfileMaxIterations={baseline_profile_max_iterations}",
        ])

    # Add androidTest APK if provided (optional) as robo test or instrumentation test
    if apk_test:
        flank_command.extend(["--test", str(apk_test)])

    exit_code = run_command(flank_command, "flank.log")
    if exit_code == 0:
        logging.info("All UI test(s) have passed!")
    return exit_code


def process_results(
    flank_config: str, test_type: str = "instrumentation", artifact_type: str = None
) -> None:
    """Process and parse test results.

    Args:
        flank_config: The YML configuration for Flank to use e.g, automation/taskcluster/androidTest/flank-<config>.yml
        test_type: The type of test executed: 'instrumentation' or 'robo'
        artifact_type: The type of the artifacts to copy after the test run
    """

    parse_junit_results_artifact = os.path.join(SCRIPT_DIR, "parse-junit-results.py")
    copy_artifacts_script = os.path.join(SCRIPT_DIR, "copy-artifacts-from-ftl.py")
    generate_flaky_report_script = os.path.join(
        SCRIPT_DIR, "generate-flaky-report-from-ftl.py"
    )

    os.chmod(parse_junit_results_artifact, 0o755)
    os.chmod(copy_artifacts_script, 0o755)
    os.chmod(generate_flaky_report_script, 0o755)

    # Process the results differently based on the test type: instrumentation or robo
    #
    # Instrumentation (i.e, Android UI Tests): parse the JUnit results for CI logging
    # Robo Test (i.e, self-crawling): copy crash artifacts from Google Cloud Storage over
    if test_type == "instrumentation":
        run_command(
            [parse_junit_results_artifact, "--results", Worker.RESULTS_DIR.value],
            "flank.log",
        )
        # Generate flaky test report if flaky tests exist
        run_command(
            [generate_flaky_report_script, "--results", Worker.RESULTS_DIR.value],
            "flank.log",
        )

        # Copy artifacts if specified
        if artifact_type:
            run_command([copy_artifacts_script, artifact_type])

    if test_type == "robo":
        run_command([copy_artifacts_script, "crash_log"])


def publish_mapping(mapping: Path) -> None:
    """Copy the R8 mapping file next to the test results so stack traces from
    the run can be retraced.

    Args:
        mapping: Absolute path to the mapping.txt of the APK under test
    """
    os.makedirs(Worker.RESULTS_DIR.value, exist_ok=True)
    shutil.copy(mapping, os.path.join(Worker.RESULTS_DIR.value, "mapping.txt"))


def main():
    """Parse command line arguments and execute the test runner."""
    parser = argparse.ArgumentParser(
        description="Run UI tests on Firebase Test Lab using Flank as a test runner"
    )
    parser.add_argument(
        "flank_config",
        help="The YML configuration for Flank to use e.g, 'fenix/flank-arm-debug.yml'."
        + " This is relative to 'mobile/android/test_infra/flank-configs'.",
    )
    parser.add_argument(
        "apk_app", help="Absolute path to a Android APK application package"
    )
    parser.add_argument(
        "--apk_test",
        help="Absolute path to a Android APK androidTest package",
        default=None,
    )
    parser.add_argument(
        "--artifact_type",
        help="Type of artifact to copy after running the tests",
        default=None,
    )
    parser.add_argument(
        "--mapping",
        help="Absolute path to the R8 mapping file of the APK under test",
        default=None,
    )
    parser.add_argument(
        "--baseline-profile-max-iterations",
        help="Maximum iterations for baseline profile generation",
        default=None,
    )
    args = parser.parse_args()

    setup_environment()

    # Only resolve apk_test if it is provided
    apk_test_path = Path(args.apk_test).resolve() if args.apk_test else None
    exit_code = execute_tests(
        flank_config=args.flank_config,
        apk_app=Path(args.apk_app).resolve(),
        apk_test=apk_test_path,
        baseline_profile_max_iterations=args.baseline_profile_max_iterations,
    )

    if args.mapping:
        publish_mapping(Path(args.mapping).resolve())

    # Determine the instrumentation type to process the results differently
    instrumentation_type = "instrumentation" if args.apk_test else "robo"
    process_results(
        flank_config=args.flank_config,
        test_type=instrumentation_type,
        artifact_type=args.artifact_type,
    )

    sys.exit(exit_code)


if __name__ == "__main__":
    setup_logging()
    main()
