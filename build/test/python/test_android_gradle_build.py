# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import json
import logging
import os
import shutil
import sys
import textwrap
from collections import defaultdict
from pathlib import Path

import mozunit
import pytest
from buildconfig import topsrcdir
from mach.util import get_state_dir
from mozpack.files import JarFinder
from mozpack.mozjar import JarReader
from mozprocess import ProcessHandler

logger = logging.getLogger(__name__)


def run_mach_with_config(mozconfig, argv, cwd=None, pop_moz_automation=False):
    """Run mach with a specific mozconfig."""
    env = os.environ.copy()
    env["MOZCONFIG"] = str(mozconfig)
    env["MACH_NO_TERMINAL_FOOTER"] = "1"
    env["MACH_NO_WRITE_TIMES"] = "1"
    if pop_moz_automation:
        env.pop("MOZ_AUTOMATION", None)
    if env.get("MOZ_AUTOMATION"):
        env["MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE"] = "system"

    output_lines = []

    def pol(line):
        logger.debug(line)
        output_lines.append(line)

    proc = ProcessHandler(
        [sys.executable, "mach"] + argv,
        env=env,
        cwd=cwd or topsrcdir,
        processOutputLine=pol,
        universal_newlines=True,
    )
    proc.run()
    proc.wait()
    return proc.poll(), output_lines


@pytest.fixture(scope="session")
def run_mach(mozconfig):
    """Fixture providing run_mach bound to the default mozconfig."""

    def inner(argv, cwd=None):
        return run_mach_with_config(mozconfig, argv, cwd=cwd)

    return inner


def run_gradle(mozconfig, args, use_config_cache=True, pop_moz_automation=False):
    """Run mach Gradle with --debug flag.

    Args:
        mozconfig: Path to mozconfig file
        args: List of Gradle arguments
        use_config_cache: If False, passes --no-configuration-cache to disable
            Gradle's configuration cache (useful for testing the local cache layer)
        pop_moz_automation: If True, removes MOZ_AUTOMATION from the environment
            (needed for testing local cache since it's disabled in automation)
    """
    extra_args = ["--debug"]
    if not use_config_cache:
        extra_args.append("--no-configuration-cache")
    return run_mach_with_config(
        mozconfig, ["gradle"] + args + extra_args, pop_moz_automation=pop_moz_automation
    )


def clear_local_cache():
    """Clear the local topobjdir cache files."""
    cache_dir = Path(topsrcdir) / ".gradle" / "mach-environment-cache"
    if cache_dir.exists():
        shutil.rmtree(cache_dir)


def create_mozconfig(test_dir, name):
    """Create a mozconfig and objdir pair for testing.

    Returns (mozconfig_path, objdir_path) tuple.
    """
    objdir = test_dir / f"objdir-{name}"
    mozconfig_path = test_dir / f"mozconfig-{name}"
    mozconfig_path.parent.mkdir(parents=True, exist_ok=True)
    mozconfig_path.write_text(
        textwrap.dedent(
            f"""
                ac_add_options --enable-application=mobile/android
                ac_add_options --enable-artifact-builds
                ac_add_options --target=aarch64-linux-android
                mk_add_options MOZ_OBJDIR="{objdir.as_posix()}"
                export GRADLE_FLAGS="{os.environ.get("GRADLE_FLAGS", "")} -PbuildMetricsOutputDir={objdir.as_posix()}/gradle/build/metrics -PbuildMetricsFileSuffix=test"
            """
        )
    )
    return mozconfig_path, objdir


@pytest.fixture(scope="session")
def test_dir():
    return (
        Path(get_state_dir(specific_to_topsrcdir=True, topsrcdir=topsrcdir))
        / "android-gradle-build"
    )


@pytest.fixture(scope="session")
def primary_config(test_dir):
    return create_mozconfig(test_dir, "primary")


@pytest.fixture(scope="session")
def secondary_config(test_dir):
    return create_mozconfig(test_dir, "secondary")


@pytest.fixture(scope="session")
def objdir(primary_config):
    _, objdir = primary_config
    return objdir


@pytest.fixture(scope="session")
def mozconfig(primary_config):
    mozconfig_path, _ = primary_config
    return mozconfig_path


@pytest.fixture
def clean_objdir(objdir):
    """Clean objdir to ensure fresh state."""
    if objdir.exists():
        shutil.rmtree(objdir)

    yield


AARS = {
    "geckoview.aar": "gradle/build/mobile/android/geckoview/outputs/aar/geckoview-debug.aar",
}


APKS = {
    "test_runner.apk": "gradle/build/mobile/android/test_runner/outputs/apk/debug/test_runner-debug.apk",
    "androidTest": "gradle/build/mobile/android/geckoview/outputs/apk/androidTest/debug/geckoview-debug-androidTest.apk",
    "geckoview_example.apk": "gradle/build/mobile/android/geckoview_example/outputs/apk/debug/geckoview_example-debug.apk",
    "messaging_example.apk": "gradle/build/mobile/android/examples/messaging_example/app/outputs/apk/debug/messaging_example-debug.apk",
    "port_messaging_example.apk": "gradle/build/mobile/android/examples/port_messaging_example/app/outputs/apk/debug/port_messaging_example-debug.apk",
}


def hashes(objdir, pattern, targets={**AARS, **APKS}):
    target_to_hash = {}
    hash_to_target = defaultdict(list)
    for shortname, target in targets.items():
        finder = JarFinder(target, JarReader(str(objdir / target)))
        hasher = hashlib.blake2b()

        # We sort paths.  This allows a pattern like `classes*.dex` to capture
        # changes to any of the DEX files, no matter how they are ordered in an
        # AAR or APK.
        for p, f in sorted(finder.find(pattern), key=lambda x: x[0]):
            fp = f.open()
            while True:
                data = fp.read(8192)
                if not len(data):
                    break
                hasher.update(data)

        h = hasher.hexdigest()
        target_to_hash[shortname] = h
        hash_to_target[h].append(shortname)

    return target_to_hash, hash_to_target


def get_test_run_build_metrics(objdir):
    """Find and load the build-metrics JSON file for our test run."""
    log_dir = objdir / "gradle" / "build" / "metrics"
    if not log_dir.exists():
        return None

    suffix = "test"
    build_metrics_file = log_dir / f"build-metrics-{suffix}.json"

    try:
        with build_metrics_file.open(encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        logger.warning(f"Failed to load build metrics from {build_metrics_file}: {e}")
        return None


def assert_success(returncode, output):
    """Assert that a command succeeded, showing output on failure."""
    if returncode != 0:
        output_lines = output if isinstance(output, list) else output.splitlines()

        if os.environ.get("MOZ_AUTOMATION"):
            final_output = "\n".join(output_lines)
        else:
            tail_lines = (
                output_lines[-100:] if len(output_lines) > 100 else output_lines
            )
            final_output = (
                f"Last {len(tail_lines)} of {len(output_lines)} lines of output:\n\n"
                + "\n".join(tail_lines)
            )
        pytest.fail(f"Command failed with return code: {returncode}\n{final_output}")


def assert_all_task_statuses(objdir, acceptable_statuses):
    """Asserts that all tasks in build metrics have acceptable statuses."""

    build_metrics = get_test_run_build_metrics(objdir)
    assert build_metrics is not None, "Build metrics JSON not found"
    assert "tasks" in build_metrics, "Build metrics missing 'tasks' section"

    metrics_tasks = build_metrics.get("tasks", [])

    for task in metrics_tasks:
        task_name = task.get("path")
        actual_status = task.get("status")

        assert actual_status in acceptable_statuses, (
            f"Task {task_name} had status '{actual_status}', expected one of {acceptable_statuses}"
        )


def assert_ordered_task_outcomes(objdir, ordered_expected_task_statuses, output=None):
    """Takes a list of (task_name, expected_status) tuples and verifies that they appear
    in the build metrics in the same order with the expected statuses.
    """
    # Get build metrics and fail if not found
    build_metrics = get_test_run_build_metrics(objdir)
    assert build_metrics is not None, "Build metrics JSON not found"
    assert "tasks" in build_metrics, "Build metrics missing 'tasks' section"

    # Extract tasks from metrics in order
    metrics_tasks = build_metrics.get("tasks", [])
    expected_task_names = {task_name for task_name, _ in ordered_expected_task_statuses}
    task_order = [
        task.get("path")
        for task in metrics_tasks
        if task.get("path") in expected_task_names
    ]
    expected_order = [task_name for task_name, _ in ordered_expected_task_statuses]

    # Check that all expected tasks were found
    missing_tasks = expected_task_names - set(task_order)
    assert not missing_tasks, f"Tasks not found in build metrics: {missing_tasks}"

    # Check order matches expectation
    assert task_order == expected_order, (
        f"Task execution order mismatch. Expected: {expected_order}, Got: {task_order}"
    )

    def _format_output():
        if not output:
            return ""
        lines = output if isinstance(output, list) else output.splitlines()
        return "\n\nGradle output:\n" + "\n".join(lines)

    # Check statuses for each task
    task_lookup = {task.get("path"): task for task in metrics_tasks}
    for task_name, expected_status in ordered_expected_task_statuses:
        task_info = task_lookup[task_name]
        actual_status = task_info.get("status")
        assert actual_status == expected_status, (
            f"Task {task_name} had status '{actual_status}', expected '{expected_status}'"
            + _format_output()
        )


def test_artifact_build(objdir, mozconfig, run_mach, clean_objdir):
    assert_success(*run_mach(["build"]))
    # Order matters, since `mach build stage-package` depends on the
    # outputs of `mach build faster`.
    assert_ordered_task_outcomes(
        objdir,
        [
            (":machBuildFaster", "SKIPPED"),
            (":machStagePackage", "SKIPPED"),
        ],
    )

    _, omnijar_hash_to = hashes(objdir, "assets/omni.ja")
    assert len(omnijar_hash_to) == 1
    (omnijar_hash_orig,) = omnijar_hash_to.values()

    assert_success(*run_mach(["gradle", "geckoview_example:assembleDebug"]))
    # Order matters, since `mach build stage-package` depends on the
    # outputs of `mach build faster`.
    assert_ordered_task_outcomes(
        objdir,
        [
            (":machBuildFaster", "EXECUTED"),
            (":machStagePackage", "EXECUTED"),
        ],
    )

    _, omnijar_hash_to = hashes(objdir, "assets/omni.ja")
    assert len(omnijar_hash_to) == 1
    (omnijar_hash_new,) = omnijar_hash_to.values()

    assert omnijar_hash_orig == omnijar_hash_new


def test_mach_tasks_up_to_date(objdir, mozconfig, run_mach):
    """Test that mach Gradle tasks are correctly UP-TO-DATE or EXECUTED depending on what inputs change."""
    mozconfig_path = Path(mozconfig)
    original_content = mozconfig_path.read_text()
    # This mozconfig edit must invalidate the tracked inputs of
    # machConfigure, machBuildFaster, and machStagePackage so each
    # downstream task actually has something to re-run. --target does
    # that; a cosmetic mozconfig edit (e.g. an unused variable) would
    # only force machConfigure to re-run. --enable-debug would also
    # work, but debug toolchains aren't built on beta.
    modified_content = original_content.replace(
        "ac_add_options --target=aarch64-linux-android",
        "ac_add_options --target=arm-linux-androideabi",
    )
    assert modified_content != original_content, (
        "Expected to swap --target in mozconfig fixture content"
    )
    mozconfig_path.write_text(modified_content)
    try:
        assert_success(*run_mach(["build"]))

        # First run, get to known state.
        assert_success(*run_mach(["gradle", "machStagePackage"]))

        # Second run, no changes, everything should be UP-TO-DATE
        returncode, output = run_mach(["gradle", "machStagePackage", "--info"])
        assert_success(returncode, output)
        assert_ordered_task_outcomes(
            objdir,
            [
                (":machConfigure", "UP-TO-DATE"),
                (":machBuildFaster", "UP-TO-DATE"),
                (":machStagePackage", "UP-TO-DATE"),
            ],
            output,
        )

        assets_dir = objdir / "dist" / "geckoview" / "assets"
        if assets_dir.exists():
            shutil.rmtree(assets_dir)

        # Third run, remove outputs of machStagePackage, it should be EXECUTED
        returncode, output = run_mach(["gradle", "machStagePackage", "--info"])
        assert_success(returncode, output)
        assert_ordered_task_outcomes(
            objdir,
            [
                (":machConfigure", "UP-TO-DATE"),
                (":machBuildFaster", "UP-TO-DATE"),
                (":machStagePackage", "EXECUTED"),
            ],
            output,
        )

        mozconfig_path.write_text(original_content)

        # Fourth run, mozconfig changed, everything should be EXECUTED
        returncode, output = run_mach(["gradle", "machStagePackage", "--info"])
        assert_success(returncode, output)
        assert_ordered_task_outcomes(
            objdir,
            [
                (":machConfigure", "EXECUTED"),
                (":machBuildFaster", "EXECUTED"),
                (":machStagePackage", "EXECUTED"),
            ],
            output,
        )

        # Fifth run, no changes. machConfigure is UP-TO-DATE, but machBuildFaster
        # re-executes because its file inputs (from the backend deps file) were
        # regenerated by machConfigure in the fourth run. The new file list is read
        # at Gradle configuration time, so it differs from the fourth run's inputs.
        returncode, output = run_mach(["gradle", "machStagePackage", "--info"])
        assert_success(returncode, output)
        assert_ordered_task_outcomes(
            objdir,
            [
                (":machConfigure", "UP-TO-DATE"),
                (":machBuildFaster", "EXECUTED"),
                (":machStagePackage", "UP-TO-DATE"),
            ],
            output,
        )

        # Sixth run, everything should be UP-TO-DATE now
        returncode, output = run_mach(["gradle", "machStagePackage", "--info"])
        assert_success(returncode, output)
        assert_ordered_task_outcomes(
            objdir,
            [
                (":machConfigure", "UP-TO-DATE"),
                (":machBuildFaster", "UP-TO-DATE"),
                (":machStagePackage", "UP-TO-DATE"),
            ],
            output,
        )
    finally:
        # Tests in this module share build state to avoid a full rebuild
        # each time, so restore the mozconfig even on failure: otherwise
        # a mid-test failure leaves the mozconfig mutated and breaks
        # later tests.
        mozconfig_path.write_text(original_content)


@pytest.mark.app_build
def test_minify_fenix_incremental_build(objdir, mozconfig, run_mach):
    """Verify that minifyReleaseWithR8 is UP-TO-DATE on a subsequent
    run when there are no code changes.
    """

    assert_success(*run_mach(["build"]))
    # Ensure a clean state
    assert_success(*run_mach(["gradle", ":fenix:cleanMinifyReleaseWithR8"]))
    assert_success(*run_mach(["gradle", ":fenix:minifyReleaseWithR8"]))
    assert_ordered_task_outcomes(objdir, [(":fenix:minifyReleaseWithR8", "EXECUTED")])

    assert_success(*run_mach(["gradle", ":fenix:minifyReleaseWithR8"]))
    assert_ordered_task_outcomes(objdir, [(":fenix:minifyReleaseWithR8", "UP-TO-DATE")])


@pytest.mark.app_build
def test_geckoview_build(objdir, mozconfig, run_mach):
    assert_success(*run_mach(["build"]))
    assert_success(*run_mach(["gradle", "geckoview:clean"]))
    assert_success(*run_mach(["gradle", "geckoview:assembleDebug"]))
    assert_all_task_statuses(objdir, ["EXECUTED", "UP-TO-DATE", "SKIPPED"])

    assert_success(*run_mach(["gradle", "geckoview:assembleDebug"]))
    assert_all_task_statuses(objdir, ["UP-TO-DATE", "SKIPPED"])


@pytest.mark.app_build
def test_fenix_build(objdir, mozconfig, run_mach):
    assert_success(*run_mach(["build"]))
    assert_success(
        *run_mach(["gradle", "fenix:clean", ":components:support-base:clean"])
    )
    assert_success(*run_mach(["gradle", "fenix:assembleDebug"]))
    assert_ordered_task_outcomes(
        objdir, [(":components:support-base:generateComponentEnum", "EXECUTED")]
    )
    assert_all_task_statuses(objdir, ["EXECUTED", "UP-TO-DATE", "SKIPPED"])

    apks = list(
        (objdir / "gradle/build/mobile/android/fenix/app/outputs/apk").rglob(
            "*arm64-v8a*.apk"
        )
    )
    assert apks
    for apk in apks:
        assert "lib/arm64-v8a/libmegazord.so" in JarReader(str(apk))

    assert_success(*run_mach(["gradle", "fenix:assembleDebug"]))
    assert_ordered_task_outcomes(
        objdir, [(":components:support-base:generateComponentEnum", "UP-TO-DATE")]
    )
    assert_all_task_statuses(objdir, ["UP-TO-DATE", "SKIPPED"])


@pytest.mark.app_build
def test_focus_build(objdir, mozconfig, run_mach):
    assert_success(*run_mach(["build"]))
    assert_success(*run_mach(["gradle", "focus:clean"]))
    assert_success(*run_mach(["gradle", "focus:assembleDebug"]))
    assert_ordered_task_outcomes(
        objdir, [(":focus-android:generateLocaleList", "EXECUTED")]
    )
    assert_all_task_statuses(objdir, ["EXECUTED", "UP-TO-DATE", "SKIPPED"])

    assert_success(*run_mach(["gradle", "focus:assembleDebug"]))
    assert_ordered_task_outcomes(
        objdir, [(":focus-android:generateLocaleList", "UP-TO-DATE")]
    )
    assert_all_task_statuses(objdir, ["UP-TO-DATE", "SKIPPED"])


def test_android_export(objdir, mozconfig, run_mach):
    # To ensure a consistent state, we delete the marker file
    # to force the :verifyGleanVersion task to re-run.
    marker_file = objdir / "gradle" / "build" / "glean" / "verifyGleanVersion.marker"
    marker_file.unlink(missing_ok=True)

    bindings_dir = Path(topsrcdir) / "widget" / "android" / "bindings"
    inputs = list(bindings_dir.glob("*-classes.txt"))

    assert_success(*run_mach(["android", "export"] + [str(f) for f in inputs]))
    assert_ordered_task_outcomes(objdir, [(":verifyGleanVersion", "EXECUTED")])

    assert_success(*run_mach(["android", "export"] + [str(f) for f in inputs]))
    assert_ordered_task_outcomes(objdir, [(":verifyGleanVersion", "UP-TO-DATE")])


@pytest.fixture(scope="session")
def built_configs(primary_config, secondary_config):
    for mozconfig, objdir in (primary_config, secondary_config):
        assert_success(*run_mach_with_config(mozconfig, ["build"]))
        assert (objdir / "config.status.json").exists(), (
            f"{objdir} should have config.status.json"
        )
    return primary_config, secondary_config


def test_mach_environment_configuration_cache(built_configs):
    """Test that Gradle's configuration cache invalidates when objdir-determining inputs change."""

    def get_config_cache_status(output):
        for line in output:
            if "Reusing configuration cache" in line:
                return "reused"
        return None

    (primary_mozconfig, _), (secondary_mozconfig, _) = built_configs

    returncode, output = run_gradle(secondary_mozconfig, ["help"])
    assert_success(returncode, output)

    gradle_cache_dir = Path(topsrcdir) / ".gradle" / "configuration-cache"
    if gradle_cache_dir.exists():
        shutil.rmtree(gradle_cache_dir)

    # First run, config cache miss
    returncode, output = run_gradle(primary_mozconfig, ["help"])
    assert_success(returncode, output)
    assert get_config_cache_status(output) is None, (
        "Config cache should not be reused on first run"
    )

    # Second run, same config, expect config cache reused
    returncode, output = run_gradle(primary_mozconfig, ["help"])
    assert_success(returncode, output)
    config_status = get_config_cache_status(output)
    assert config_status == "reused", (
        f"Expected Gradle config cache 'reused' on second run, got '{config_status}'"
    )

    # Third run, switch to secondary mozconfig, expect config cache miss
    returncode, output = run_gradle(secondary_mozconfig, ["help"])
    assert_success(returncode, output)
    assert get_config_cache_status(output) is None, (
        "Config cache should be invalidated when MOZCONFIG changes"
    )

    # Fourth run, still secondary mozconfig, expect config cache reused
    returncode, output = run_gradle(secondary_mozconfig, ["help"])
    assert_success(returncode, output)
    config_status = get_config_cache_status(output)
    assert config_status == "reused", (
        f"Expected config cache 'reused' on repeat run, got '{config_status}'"
    )

    original_content = secondary_mozconfig.read_text()
    try:
        # Modify mozconfig content to invalidate config cache
        secondary_mozconfig.write_text(
            original_content + "\n# config cache invalidation test\n"
        )

        # Fifth run, config cache miss due to content change
        returncode, output = run_gradle(secondary_mozconfig, ["help"])
        assert_success(returncode, output)
        assert get_config_cache_status(output) is None, (
            "Config cache should be invalidated when mozconfig content changes"
        )

        # Sixth run, no change, config cache reused
        returncode, output = run_gradle(secondary_mozconfig, ["help"])
        assert_success(returncode, output)
        config_status = get_config_cache_status(output)
        assert config_status == "reused", (
            f"Expected config cache 'reused' after no changes, got '{config_status}'"
        )
    finally:
        secondary_mozconfig.write_text(original_content)


def test_mach_environment_local_topobjdir_cache(built_configs):
    """Test that local topobjdir caching avoids running `./mach environment` unnecessarily."""

    def get_local_cache_status(output):
        for line in output:
            if "topobjdir cache hit!" in line:
                return "hit"
            if "topobjdir cache miss!" in line:
                return "miss"
        return None

    (primary_mozconfig, _), (secondary_mozconfig, _) = built_configs

    local_cache_dir = Path(topsrcdir) / ".gradle" / "mach-environment-cache"

    clear_local_cache()

    # First run, local cache miss
    returncode, output = run_gradle(
        primary_mozconfig, ["help"], use_config_cache=False, pop_moz_automation=True
    )
    assert_success(returncode, output)
    local_status = get_local_cache_status(output)
    assert local_status == "miss", (
        f"Expected local cache 'miss' on first run, got '{local_status}'"
    )
    assert local_cache_dir.exists(), "Local cache directory should be created"
    assert (local_cache_dir / "inputs.sha256").exists(), (
        "Cache hash file should be created"
    )
    assert (local_cache_dir / "topobjdir.txt").exists(), (
        "topobjdir cache file should be created"
    )

    # Second run, same config, expect local cache hit
    returncode, output = run_gradle(
        primary_mozconfig, ["help"], use_config_cache=False, pop_moz_automation=True
    )
    assert_success(returncode, output)
    local_status = get_local_cache_status(output)
    assert local_status == "hit", (
        f"Expected local cache 'hit' on second run, got '{local_status}'"
    )

    # Third run, switch to secondary mozconfig, expect local cache miss
    returncode, output = run_gradle(
        secondary_mozconfig, ["help"], use_config_cache=False, pop_moz_automation=True
    )
    assert_success(returncode, output)
    local_status = get_local_cache_status(output)
    assert local_status == "miss", (
        f"Expected local cache 'miss' when switching mozconfig, got '{local_status}'"
    )

    # Fourth run, still secondary mozconfig, expect local cache hit
    returncode, output = run_gradle(
        secondary_mozconfig, ["help"], use_config_cache=False, pop_moz_automation=True
    )
    assert_success(returncode, output)
    local_status = get_local_cache_status(output)
    assert local_status == "hit", (
        f"Expected local cache 'hit' on repeat with secondary, got '{local_status}'"
    )

    original_content = secondary_mozconfig.read_text()
    try:
        # Modify mozconfig content to invalidate local cache
        secondary_mozconfig.write_text(original_content + "\n# local cache test\n")

        # Fifth run, local cache miss due to content change
        returncode, output = run_gradle(
            secondary_mozconfig,
            ["help"],
            use_config_cache=False,
            pop_moz_automation=True,
        )
        assert_success(returncode, output)
        local_status = get_local_cache_status(output)
        assert local_status == "miss", (
            f"Expected local cache 'miss' after mozconfig change, got '{local_status}'"
        )

        # Sixth run, no change, local cache hit
        returncode, output = run_gradle(
            secondary_mozconfig,
            ["help"],
            use_config_cache=False,
            pop_moz_automation=True,
        )
        assert_success(returncode, output)
        local_status = get_local_cache_status(output)
        assert local_status == "hit", (
            f"Expected local cache 'hit' after no changes, got '{local_status}'"
        )
    finally:
        secondary_mozconfig.write_text(original_content)


if __name__ == "__main__":
    mozunit.main()
