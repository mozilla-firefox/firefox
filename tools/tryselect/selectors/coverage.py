# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import collections
import datetime
import hashlib
import json
import os
import shutil
import sqlite3
import subprocess

import requests
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozpack.files import FileFinder
from moztest.resolve import TestResolver
from mozversioncontrol import get_repository_object

from ..cli import BaseTryParser
from ..push import generate_try_task_config, push_to_try
from ..tasks import filter_tasks_by_paths, generate_tasks, resolve_tests_by_suite

here = os.path.abspath(os.path.dirname(__file__))
build = None
vcs = None
CHUNK_MAPPING_FILE = None
CHUNK_MAPPING_TAG_FILE = None


def setup_globals():
    # Avoid incurring expensive computation on import.
    global build, vcs, CHUNK_MAPPING_TAG_FILE, CHUNK_MAPPING_FILE
    build = MozbuildObject.from_environment(cwd=here)
    vcs = get_repository_object(build.topsrcdir)

    topsrcdir = build.topsrcdir
    if isinstance(topsrcdir, str):
        topsrcdir = topsrcdir.encode()
    root_hash = hashlib.sha256(os.path.abspath(topsrcdir)).hexdigest()
    cache_dir = os.path.join(get_state_dir(), "cache", root_hash, "chunk_mapping")
    if not os.path.isdir(cache_dir):
        os.makedirs(cache_dir)
    CHUNK_MAPPING_FILE = os.path.join(cache_dir, "chunk_mapping.sqlite")
    CHUNK_MAPPING_TAG_FILE = os.path.join(cache_dir, "chunk_mapping_tag.json")


# Maps from platform names in the chunk_mapping sqlite database to respective
# substrings in task names.
PLATFORM_MAP = {
    "linux": "test-linux64/opt",
    "windows": "test-windows10-64/opt",
}

# List of platform/build type combinations that are included in pushes by |mach try coverage|.
OPT_TASK_PATTERNS = [
    "macosx64/opt",
    "windows10-64/opt",
    "windows7-32/opt",
    "linux64/opt",
]


class CoverageParser(BaseTryParser):
    name = "coverage"
    arguments = []
    common_groups = ["push", "task"]
    task_configs = [
        "artifact",
        "env",
        "rebuild",
        "chemspill-prio",
        "disable-pgo",
        "worker-overrides",
    ]


def read_test_manifests():
    """Uses TestResolver to read all test manifests in the tree.

    Returns a (tests, support_files_map) tuple that describes the tests in the tree:
    tests - a set of test file paths
    support_files_map - a dict that maps from each support file to a list with
                        test files that require them it
    """
    setup_globals()
    test_resolver = TestResolver.from_environment(cwd=here)
    file_finder = FileFinder(build.topsrcdir)
    support_files_map = collections.defaultdict(list)
    tests = set()

    for test in test_resolver.resolve_tests(build.topsrcdir):
        tests.add(test["srcdir_relpath"])
        if "support-files" not in test:
            continue

        for support_file_pattern in test["support-files"].split():
            # Get the pattern relative to topsrcdir.
            if support_file_pattern.startswith("!/"):
                support_file_pattern = support_file_pattern[2:]
            elif support_file_pattern.startswith("/"):
                support_file_pattern = support_file_pattern[1:]
            else:
                support_file_pattern = os.path.normpath(
                    os.path.join(test["dir_relpath"], support_file_pattern)
                )

            # If it doesn't have a glob, then it's a single file.
            if "*" not in support_file_pattern:
                # Simple case: single support file, just add it here.
                support_files_map[support_file_pattern].append(test["srcdir_relpath"])
                continue

            for support_file, _ in file_finder.find(support_file_pattern):
                support_files_map[support_file].append(test["srcdir_relpath"])

    return tests, support_files_map


# TODO cache the output of this function
all_tests, all_support_files = read_test_manifests()


def download_coverage_mapping(base_revision):
    try:
        with open(CHUNK_MAPPING_TAG_FILE) as f:
            tags = json.load(f)
            if tags["target_revision"] == base_revision:
                return
            else:
                print("Base revision changed.")
    except (OSError, ValueError):
        print("Chunk mapping file not found.")

    CHUNK_MAPPING_URL_TEMPLATE = "https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/project.relman.code-coverage.production.cron.{}/artifacts/public/chunk_mapping.tar.xz"  # noqa
    JSON_PUSHES_URL_TEMPLATE = "https://hg.mozilla.org/mozilla-central/json-pushes?version=2&tipsonly=1&startdate={}"  # noqa

    # Get pushes from at most one month ago.
    PUSH_HISTORY_DAYS = 30
    delta = datetime.timedelta(days=PUSH_HISTORY_DAYS)
    start_time = (datetime.datetime.now() - delta).strftime("%Y-%m-%d")
    pushes_url = JSON_PUSHES_URL_TEMPLATE.format(start_time)
    pushes_data = requests.get(pushes_url + f"&tochange={base_revision}").json()
    if "error" in pushes_data:
        if "unknown revision" in pushes_data["error"]:
            print(
                f"unknown revision {base_revision}, trying with latest mozilla-central"
            )
            pushes_data = requests.get(pushes_url).json()

        if "error" in pushes_data:
            raise Exception(pushes_data["error"])

    pushes = pushes_data["pushes"]

    print("Looking for coverage data. This might take a minute or two.")
    print("Base revision:", base_revision)
    for push_id in sorted(pushes.keys())[::-1]:
        rev = pushes[push_id]["changesets"][0]
        url = CHUNK_MAPPING_URL_TEMPLATE.format(rev)
        print(f"push id: {push_id},\trevision: {rev}")

        r = requests.head(url)
        if not r.ok:
            continue

        print("Chunk mapping found, downloading...")
        r = requests.get(url, stream=True)

        CHUNK_MAPPING_ARCHIVE = os.path.join(build.topsrcdir, "chunk_mapping.tar.xz")
        with open(CHUNK_MAPPING_ARCHIVE, "wb") as f:
            r.raw.decode_content = True
            shutil.copyfileobj(r.raw, f)

        subprocess.check_call(
            [
                "tar",
                "-xJf",
                CHUNK_MAPPING_ARCHIVE,
                "-C",
                os.path.dirname(CHUNK_MAPPING_FILE),
            ]
        )
        os.remove(CHUNK_MAPPING_ARCHIVE)
        assert os.path.isfile(CHUNK_MAPPING_FILE)
        with open(CHUNK_MAPPING_TAG_FILE, "w") as f:
            json.dump(
                {
                    "target_revision": base_revision,
                    "chunk_mapping_revision": rev,
                    "download_date": start_time,
                },
                f,
            )
        return
    raise Exception("Could not find suitable coverage data.")


def is_a_test(cursor, path):
    """Checks the all_tests global and the chunk mapping database to see if a
    given file is a test file.
    """
    if path in all_tests:
        return True

    cursor.execute("SELECT COUNT(*) from chunk_to_test WHERE path=?", (path,))
    if cursor.fetchone()[0]:
        return True

    cursor.execute("SELECT COUNT(*) from file_to_test WHERE test=?", (path,))
    if cursor.fetchone()[0]:
        return True

    return False


def tests_covering_file(cursor, path):
    """Returns a set of tests that cover a given source file."""
    cursor.execute("SELECT test FROM file_to_test WHERE source=?", (path,))
    return {e[0] for e in cursor.fetchall()}


def tests_in_chunk(cursor, platform, chunk):
    """Returns a set of tests that are contained in a given chunk."""
    cursor.execute(
        "SELECT path FROM chunk_to_test WHERE platform=? AND chunk=?", (platform, chunk)
    )
    # Because of bug 1480103, some entries in this table contain both a file name and a test name,
    # separated by a space. With the split, only the file name is kept.
    return {e[0].split(" ")[0] for e in cursor.fetchall()}


def chunks_covering_file(cursor, path):
    """Returns a set of (platform, chunk) tuples with the chunks that cover a given source file."""
    cursor.execute("SELECT platform, chunk FROM file_to_chunk WHERE path=?", (path,))
    return set(cursor.fetchall())


def tests_supported_by_file(path):
    """Returns a set of tests that are using the given file as a support-file."""
    return set(all_support_files[path])


def find_tests(changed_files):
    """Finds both individual tests and test chunks that should be run to test code changes.
    Argument: a list of file paths relative to the source checkout.

    Returns: a (test_files, test_chunks) tuple with two sets.
    test_files - contains tests that should be run to verify changes to changed_files.
    test_chunks - contains (platform, chunk) tuples with chunks that should be
                  run. These chunnks do not support running a subset of the tests (like
                  cppunit or gtest), so the whole chunk must be run.
    """
    test_files = set()
    test_chunks = set()
    files_no_coverage = set()

    with sqlite3.connect(CHUNK_MAPPING_FILE) as conn:
        c = conn.cursor()
        for path in changed_files:
            # If path is a test, add it to the list and continue.
            if is_a_test(c, path):
                test_files.add(path)
                continue

            # Look at the chunk mapping and add all tests that cover this file.
            tests = tests_covering_file(c, path)
            chunks = chunks_covering_file(c, path)
            # If we found tests covering this, then it's not a support-file, so
            # save these and continue.
            if tests or chunks:
                test_files |= tests
                test_chunks |= chunks
                continue

            # Check if the path is a support-file for any test, by querying test manifests.
            tests = tests_supported_by_file(path)
            if tests:
                test_files |= tests
                continue

            # There is no coverage information for this file.
            files_no_coverage.add(path)

        files_covered = set(changed_files) - files_no_coverage
        test_files = {s.replace("\\", "/") for s in test_files}

        _print_found_tests(files_covered, files_no_coverage, test_files, test_chunks)

        remaining_test_chunks = set()
        # For all test_chunks, try to find the tests contained by them in the
        # chunk_to_test mapping.
        for platform, chunk in test_chunks:
            tests = tests_in_chunk(c, platform, chunk)
            if tests:
                for test in tests:
                    test_files.add(test.replace("\\", "/"))
            else:
                remaining_test_chunks.add((platform, chunk))

    return test_files, remaining_test_chunks


def _print_found_tests(files_covered, files_no_coverage, test_files, test_chunks):
    """Print a summary of what will be run to the user's terminal."""
    files_covered = sorted(files_covered)
    files_no_coverage = sorted(files_no_coverage)
    test_files = sorted(test_files)
    test_chunks = sorted(test_chunks)

    if files_covered:
        print(f"Found {len(files_covered)} modified source files with test coverage:")
        for covered in files_covered:
            print("\t", covered)

    if files_no_coverage:
        print(f"Found {len(files_no_coverage)} modified source files with no coverage:")
        for f in files_no_coverage:
            print("\t", f)

    if not files_covered:
        print("No modified source files are covered by tests.")
    elif not files_no_coverage:
        print("All modified source files are covered by tests.")

    if test_files:
        print(f"Running {len(test_files)} individual test files.")
    else:
        print("Could not find any individual tests to run.")

    if test_chunks:
        print(f"Running {len(test_chunks)} test chunks.")
        for platform, chunk in test_chunks:
            print("\t", platform, chunk)
    else:
        print("Could not find any test chunks to run.")


def filter_tasks_by_chunks(tasks, chunks):
    """Find all tasks that will run the given chunks."""
    selected_tasks = set()
    for platform, chunk in chunks:
        platform = PLATFORM_MAP[platform]

        selected_task = None
        for task in tasks.keys():
            if not task.startswith(platform):
                continue

            if not any(
                task[len(platform) + 1 :].endswith(c) for c in [chunk, chunk + "-e10s"]
            ):
                continue

            assert (
                selected_task is None
            ), "Only one task should be selected for a given platform-chunk couple ({} - {}), {} and {} were selected".format(  # noqa
                platform, chunk, selected_task, task
            )
            selected_task = task

        if selected_task is None:
            print("Warning: no task found for chunk", platform, chunk)
        else:
            selected_tasks.add(selected_task)

    return list(selected_tasks)


def is_opt_task(task):
    """True if the task runs on a supported platform and build type combination.
    This is used to remove -ccov/asan/pgo tasks, along with all /debug tasks.
    """
    return any(platform in task for platform in OPT_TASK_PATTERNS)


def run(
    try_config_params={},
    full=False,
    parameters=None,
    stage_changes=False,
    dry_run=False,
    message="{msg}",
    closed_tree=False,
    push_to_lando=False,
    push_to_vcs=False,
):
    setup_globals()
    download_coverage_mapping(vcs.base_ref)

    changed_sources = vcs.get_outgoing_files()
    test_files, test_chunks = find_tests(changed_sources)
    if not test_files and not test_chunks:
        print("ERROR Could not find any tests or chunks to run.")
        return 1

    tg = generate_tasks(parameters, full)
    all_tasks = tg.tasks

    tasks_by_chunks = filter_tasks_by_chunks(all_tasks, test_chunks)
    tasks_by_path = filter_tasks_by_paths(all_tasks, test_files)
    tasks = filter(is_opt_task, set(tasks_by_path) | set(tasks_by_chunks))
    tasks = list(tasks)

    if not tasks:
        print("ERROR Did not find any matching tasks after filtering.")
        return 1
    test_count_message = (
        "{test_count} test file{test_plural} that "
        + "cover{test_singular} these changes "
        + "({task_count} task{task_plural} to be scheduled)"
    ).format(
        test_count=len(test_files),
        test_plural="" if len(test_files) == 1 else "s",
        test_singular="s" if len(test_files) == 1 else "",
        task_count=len(tasks),
        task_plural="" if len(tasks) == 1 else "s",
    )
    print("Found " + test_count_message)

    # Set the test paths to be run by setting MOZHARNESS_TEST_PATHS.
    path_env = {"MOZHARNESS_TEST_PATHS": json.dumps(resolve_tests_by_suite(test_files))}
    try_config_params.setdefault("try_task_config", {}).setdefault("env", {}).update(
        path_env
    )

    # Build commit message.
    msg = "try coverage - " + test_count_message
    return push_to_try(
        "coverage",
        message.format(msg=msg),
        try_task_config=generate_try_task_config("coverage", tasks, try_config_params),
        stage_changes=stage_changes,
        dry_run=dry_run,
        closed_tree=closed_tree,
        push_to_lando=push_to_lando,
        push_to_vcs=push_to_vcs,
    )
