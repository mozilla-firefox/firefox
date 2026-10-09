# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import time
import warnings
from functools import cache

from mach.decorators import Command, CommandArgument
from mozbuild.artifact_cache import ArtifactCache
from mozbuild.bootstrap import bootstrap_toolchain
from mozbuild.util import find_task_from_index, get_taskcluster_client

FINISHED_STATUSES = {"completed", "failed", "exception"}
ALL_STATUSES = FINISHED_STATUSES | {"unscheduled", "pending", "running"}
STATUS_VALUE = {"exception": 1, "failed": 2, "completed": 3}


@cache
def _tc_client(service):
    return get_taskcluster_client(service)


LATEST_PUSHES_TO_SEARCH = 30


def _get_task(branch, revision):
    if branch and revision:
        index = f"gecko.v2.{branch}.revision.{revision}.taskgraph.decision"
    else:
        index = "gecko.v2.mozilla-central.latest.taskgraph.decision"
    task = find_task_from_index([index])
    if not task:
        if branch and revision:
            raise Exception(f"Could not find decision task for {branch}@{revision}")
        raise Exception("Could not find latest mozilla-central decision task")
    return task


def _get_pushlog_id(decision_task_id):
    prefix = "index.gecko.v2.mozilla-central.pushlog-id."
    for route in _get_task_details(decision_task_id)["routes"]:
        if route.startswith(prefix) and route.endswith(".decision"):
            return int(route[len(prefix) : -len(".decision")])
    raise Exception(
        f"Could not find the pushlog ID of decision task {decision_task_id}"
    )


def _get_task_details(task_id):
    queue = _tc_client("queue")
    return queue.task(task_id)


def _get_task_artifacts(task_id):
    queue = _tc_client("queue")
    response = queue.listLatestArtifacts(task_id)
    return response["artifacts"]


def _get_tasks_in_group(group_id):
    tasks = []

    def _save(response):
        tasks.extend(response["tasks"])

    queue = _tc_client("queue")
    queue.listTaskGroup(group_id, paginationHandler=_save)
    return tasks


@cache
def _artifact_cache(cache_dir):
    return ArtifactCache(cache_dir)


def _download_artifact(task_id, artifact, cache_dir):
    queue = _tc_client("queue")
    url = queue.buildUrl("getLatestArtifact", task_id, artifact["name"])
    return _artifact_cache(cache_dir).fetch(url)


def _get_chunk(task_name):
    if task_name.startswith("build-signing-"):
        return "build-signing"
    if task_name.startswith("build-"):
        return "build"

    task_name = task_name[task_name.find("/") + 1 :]
    return "-".join(
        part
        for part in task_name.split("-")
        if part not in ("opt", "debug", "e10s", "1proc")
    )


def _get_suite(task_name):
    return "-".join(
        part for part in _get_chunk(task_name).split("-") if not part.isdigit()
    )


def _get_platform(task_name):
    if "android" in task_name:
        return "android"
    if "linux" in task_name:
        return "linux"
    if "win" in task_name:
        return "windows"
    if "mac" in task_name:
        return "macos"
    if "source-test" in task_name:
        return "linux"
    raise Exception(f"Unknown platform for {task_name}")


def _get_task_status(task_id):
    queue = _tc_client("queue")
    status = queue.status(task_id)
    return status["status"]["state"]


def _find_test_tasks(decision_task_id, suites, platforms, suites_to_ignore):
    task_data = _get_task_details(decision_task_id)

    def _is_test_task(task):
        task_name = task["task"]["metadata"]["name"]
        return "ccov" in task_name.split("/")[0].split("-")

    def _is_in_suites(task):
        task_name = task["task"]["metadata"]["name"]
        suite_name = _get_suite(task_name)
        return (suites is None or any(suite in task_name for suite in suites)) and (
            suite_name not in suites_to_ignore
        )

    def _is_in_platforms(task):
        platform = _get_platform(task["task"]["metadata"]["name"])
        return platforms is None or platform in platforms

    return [
        task
        for task in _get_tasks_in_group(task_data["taskGroupId"])
        if _is_test_task(task) and _is_in_suites(task) and _is_in_platforms(task)
    ]


def _find_latest_finished_test_tasks(suites, platforms, suites_to_ignore):
    """Find the most recent mozilla-central push whose matching coverage tasks
    have all finished, so we don't wait for (possibly hours of) pending tasks
    on the very latest push."""
    latest_task_id = _get_task(None, None)
    latest_pushlog_id = _get_pushlog_id(latest_task_id)

    for pushlog_id in range(
        latest_pushlog_id, max(latest_pushlog_id - LATEST_PUSHES_TO_SEARCH, 0), -1
    ):
        if pushlog_id == latest_pushlog_id:
            task_id = latest_task_id
        else:
            task_id = find_task_from_index([
                f"gecko.v2.mozilla-central.pushlog-id.{pushlog_id}.decision"
            ])
            if not task_id:
                continue
        test_tasks = _find_test_tasks(task_id, suites, platforms, suites_to_ignore)
        if test_tasks and all(
            task["status"]["state"] in FINISHED_STATUSES for task in test_tasks
        ):
            if pushlog_id != latest_pushlog_id:
                print(
                    f"Using mozilla-central push {pushlog_id} (latest push with "
                    "finished coverage tasks)."
                )
            return test_tasks

    raise Exception(
        "Could not find a mozilla-central push with finished coverage tasks "
        f"among the latest {LATEST_PUSHES_TO_SEARCH} pushes."
    )


def _download_coverage_artifacts(
    test_tasks,
    suites,
    cache_dir,
):
    if suites is not None:
        for suite in suites:
            if not any(
                suite in task["task"]["metadata"]["name"] for task in test_tasks
            ):
                warnings.warn(f"Suite {suite} not found")

    download_tasks = {}
    for test_task in test_tasks:
        status = test_task["status"]["state"]
        if status not in ALL_STATUSES:
            raise Exception(f"State {status} not recognized")

        while status not in FINISHED_STATUSES:
            task_id = test_task["status"]["taskId"]
            print(f"\rWaiting for task {task_id} to finish...", end="", flush=True)
            time.sleep(60)
            status = _get_task_status(task_id)
            test_task["status"]["state"] = status
            if status not in ALL_STATUSES:
                raise Exception(f"State {status} not recognized")

        chunk_name = _get_chunk(test_task["task"]["metadata"]["name"])
        platform_name = _get_platform(test_task["task"]["metadata"]["name"])
        key = (chunk_name, platform_name)

        if key not in download_tasks:
            download_tasks[key] = test_task
            continue

        previous = download_tasks[key]
        if STATUS_VALUE[status] > STATUS_VALUE[previous["status"]["state"]]:
            download_tasks[key] = test_task

    artifact_paths = []
    total = len(download_tasks)
    for idx, test_task in enumerate(download_tasks.values(), start=1):
        print(
            f"\rDownloading artifacts from {idx}/{total} test task...",
            end="",
            flush=True,
        )
        artifacts = _get_task_artifacts(test_task["status"]["taskId"])
        for artifact in artifacts:
            name = artifact["name"]
            if "code-coverage-grcov.zip" in name or "code-coverage-jsvm.zip" in name:
                artifact_paths.append(
                    _download_artifact(
                        test_task["status"]["taskId"], artifact, cache_dir
                    )
                )
    print("")
    return artifact_paths


def _generate_report(
    command_context, grcov_path, output_format, src_dir, output_path, artifact_paths
):
    cmd = [grcov_path, "-t", output_format, "-o", output_path]
    if src_dir is not None:
        cmd += ["-s", src_dir, "--ignore-not-existing"]
    if output_format in {"coveralls", "coveralls+"}:
        cmd += ["--token", "UNUSED", "--commit-sha", "UNUSED"]
    cmd.extend(artifact_paths)

    stderr_lines = []
    status = command_context.run_process(
        args=cmd,
        ensure_exit_code=False,
        stderr_line_handler=stderr_lines.append,
    )
    if status != 0:
        stderr = "".join(stderr_lines)
        raise Exception(f"Error while running grcov: {stderr}")


@Command(
    "coverage-report",
    category="testing",
    description="Generate a local code coverage report from CI artifacts.",
)
@CommandArgument(
    "--branch",
    default=None,
    help="Branch on which coverage jobs ran (for example: mozilla-central, try).",
)
@CommandArgument(
    "--revision",
    default=None,
    help="Revision hash associated with the push.",
)
@CommandArgument(
    "--grcov",
    default=None,
    help="Path to a grcov binary. If omitted, downloads the latest CI toolchain build.",
)
@CommandArgument(
    "--platform",
    nargs="+",
    default=None,
    help='Platforms to include (linux, windows, macos, android). Example: "linux windows".',
)
@CommandArgument(
    "--suite",
    nargs="+",
    default=None,
    help='Suites to include. Example: "mochitest gtest".',
)
@CommandArgument(
    "--ignore",
    nargs="+",
    default=None,
    help='Suites to ignore. Defaults to "talos awsy".',
)
@CommandArgument(
    "--stats",
    action="store_true",
    help="Generate summary coverage stats instead of an HTML report.",
)
@CommandArgument(
    "-o",
    "--output-dir",
    default=os.path.join(os.getcwd(), "ccov-report"),
    help="Output directory for the generated report.",
)
def coverage_report(
    command_context,
    branch,
    revision,
    grcov,
    platform,
    suite,
    ignore,
    stats,
    output_dir,
):
    if (branch is None) != (revision is None):
        print("Both --branch and --revision must be provided together.")
        return 1

    cache_dir = os.path.join(
        command_context._mach_context.state_dir, "cache", "coverage-report"
    )
    os.makedirs(output_dir, exist_ok=True)

    suites_to_ignore = ignore if ignore is not None else ["talos", "awsy"]
    if branch and revision:
        test_tasks = _find_test_tasks(
            _get_task(branch, revision), suite, platform, suites_to_ignore
        )
    else:
        test_tasks = _find_latest_finished_test_tasks(suite, platform, suites_to_ignore)
    artifact_paths = _download_coverage_artifacts(test_tasks, suite, cache_dir)
    if not artifact_paths:
        target = (
            f"{branch}@{revision}"
            if branch and revision
            else "the latest finished mozilla-central push"
        )
        print(
            f"No code coverage artifacts found for {target}. "
            "Ensure coverage tasks were scheduled in the push."
        )
        return 1

    grcov_path = grcov or bootstrap_toolchain("grcov/grcov")
    if not grcov_path:
        raise Exception("Could not bootstrap grcov toolchain")

    if stats:
        output_file = os.path.join(output_dir, "output.json")
        _generate_report(
            command_context,
            grcov_path,
            "coveralls",
            command_context.topsrcdir,
            output_file,
            artifact_paths,
        )
        with open(output_file, encoding="utf-8") as report_file:
            report = json.load(report_file)

        total_lines = 0
        covered_lines = 0
        for source_file in report["source_files"]:
            for count in source_file["coverage"]:
                if count is None:
                    continue
                total_lines += 1
                if count > 0:
                    covered_lines += 1

        print(f"Coverable lines: {total_lines}")
        print(f"Covered lines: {covered_lines}")
        coverage = (
            float(covered_lines) / float(total_lines) * 100 if total_lines else 0.0
        )
        print(f"Coverage percentage: {coverage:.2f}%")
        return 0

    _generate_report(
        command_context,
        grcov_path,
        "html",
        command_context.topsrcdir,
        output_dir,
        artifact_paths,
    )
    print(f"Coverage report generated in: {output_dir}")
    return 0
