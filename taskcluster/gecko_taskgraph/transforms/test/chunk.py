# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import functools
from math import ceil

import taskgraph
from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.copy import deepcopy
from taskgraph.util.treeherder import join_symbol, split_symbol

from gecko_taskgraph.util.chunking import (
    WPT_SUBSUITES,
    chunk_manifests,
    get_manifest_loader,
    get_runtimes,
    get_test_tags,
    guess_mozinfo_from_task,
    resolve_manifest_runtimes,
    resolver,
)
from gecko_taskgraph.util.perfile import perfile_number_of_chunks

DYNAMIC_CHUNK_DURATION = 20 * 60  # seconds
"""The approximate time each test chunk should take to run."""


transforms = TransformSequence()


@functools.cache
def _parse_test_paths(mozharness_test_paths):
    # A suite's paths can be given as a single string rather than as a list.
    return {
        suite: [paths] if isinstance(paths, str) else paths
        for suite, paths in json.loads(mozharness_test_paths).items()
    }


def _set_manifests_restricted(task):
    task.setdefault("attributes", {})["test-manifests-restricted"] = True


def _requested_test_paths(config):
    """The test paths |mach try| restricted this push to, keyed by suite."""
    try_task_config = config.params.get("try_task_config", {}) or {}
    env = try_task_config.get("env", {})
    return _parse_test_paths(env.get("MOZHARNESS_TEST_PATHS", "{}"))


@transforms.add
def set_test_verify_chunks(config, tasks):
    """Set the number of chunks we use for test-verify."""
    for task in tasks:
        if any(task["suite"].startswith(s) for s in ("test-verify", "test-coverage")):
            env = config.params.get("try_task_config", {}) or {}
            env = env.get("templates", {}).get("env", {})
            task["chunks"] = perfile_number_of_chunks(
                config.params["try_mode"] is not None,
                env.get("MOZHARNESS_TEST_PATHS", ""),
                frozenset(config.params["files_changed"]),
                task["test-name"],
            )

            # limit the number of chunks we run for test-verify mode because
            # test-verify is comprehensive and takes a lot of time, if we have
            # >30 tests changed, this is probably an import of external tests,
            # or a patch renaming/moving files in bulk
            maximum_number_verify_chunks = 3
            task["chunks"] = min(task["chunks"], maximum_number_verify_chunks)

        yield task


def _wpt_task_should_run(test_name, input_paths):
    """Return whether a web-platform-tests task should run for the given set of
    test paths.

    A task is associated with a subsuite when a subsuite name (a key of
    WPT_SUBSUITES) appears as a substring of ``test_name``. That subsuite's path
    prefixes then determine which tests it owns, matched by prefix against both
    the standard and mozilla-specific wpt test roots. A subsuite task runs when
    at least one path falls under one of its prefixes; the general
    (non-subsuite) task runs when at least one path falls outside every subsuite
    prefix.
    """
    task_subsuite = next((key for key in WPT_SUBSUITES if key in test_name), None)
    subsuite_paths = WPT_SUBSUITES.get(task_subsuite) or [
        path for paths in WPT_SUBSUITES.values() for path in paths
    ]

    for path in input_paths:
        matched = any(
            path.startswith("testing/web-platform/tests/" + subsuite_path)
            or path.startswith("testing/web-platform/mozilla/tests/" + subsuite_path)
            for subsuite_path in subsuite_paths
        )
        if task_subsuite:
            if matched:
                # This is a subsuite task, and a path matched it
                return True
        elif not matched:
            # This is a non-subsuite task, and a path matched no subsuite
            return True
    return False


@transforms.add
def set_test_manifests(config, tasks):
    """Determine the set of test manifests that should run in this task."""

    for task in tasks:
        # When a task explicitly requests no 'test_manifest_loader', test
        # resolving will happen at test runtime rather than in the taskgraph.
        if "test-manifest-loader" in task and task["test-manifest-loader"] is None:
            yield task
            continue

        # Set 'tests_grouped' to "1", so we can differentiate between suites that are
        # chunked at the test runtime and those that are chunked in the taskgraph.
        task.setdefault("tags", {})["tests_grouped"] = "1"

        if taskgraph.fast:
            # We want to avoid evaluating manifests when taskgraph.fast is set. But
            # manifests are required for dynamic chunking. Just set the number of
            # chunks to one in this case.
            if task["chunks"] == "dynamic":
                task["chunks"] = task.get("default-chunks", 1)
            yield task
            continue

        manifests = task.get("test-manifests")
        if manifests:
            if isinstance(manifests, list):
                task["test-manifests"] = {"active": manifests, "skipped": []}
            yield task
            continue

        mozinfo = guess_mozinfo_from_task(
            task,
            config.params.get("head_repository", ""),
            config.params.get("app_version", ""),
            get_test_tags(config, task.get("worker", {}).get("env", {})),
        )

        loader_name = task.pop(
            "test-manifest-loader", config.params["test_manifest_loader"]
        )
        loader = get_manifest_loader(loader_name, config.params)

        task["test-manifests"] = loader.get_manifests(
            task["suite"],
            frozenset(mozinfo.items()),
        )

        # When scheduling with test paths, we often find manifests scheduled but all tests
        # are skipped on a given config.  This will remove the task from the task set if
        # no manifests have active tests for the given task/config
        mh_test_paths = _requested_test_paths(config)
        test_tags = get_test_tags(config, task.get("worker", {}).get("env", {}))

        if (
            mh_test_paths
            and task["attributes"]["unittest_suite"] in mh_test_paths.keys()
        ):
            input_paths = mh_test_paths[task["attributes"]["unittest_suite"]]

            if "web-platform-tests" in task["test-name"]:
                # Manifest names don't hold source paths for this suite, so the
                # task keeps the whole suite and the harness does the filtering.
                if _wpt_task_should_run(task["test-name"], input_paths):
                    yield task
                continue

            # Restrict the task to the manifests that hold tests under the
            # requested paths, so that chunking is computed from what will
            # actually run rather than from the whole suite. Entries can be
            # narrower than a manifest when the requested path is (a single test
            # file, or a subdirectory of the manifest's directory).
            test_paths = resolver.get_test_paths_by_manifest(
                task["suite"], frozenset(input_paths)
            )
            suite_manifests = task["test-manifests"]["active"]
            matched = [m for m in suite_manifests if m in test_paths]
            if not matched:
                continue

            active = sorted({p for m in matched for p in test_paths[m]})
            task["test-manifests"] = {"active": active, "skipped": []}

            # Chunk counts are the number of chunks the whole suite needs; scale
            # them down to the share of the suite that will run, so that we
            # neither cram everything into a single chunk nor create empty ones.
            for key in ("chunks", "default-chunks"):
                if isinstance(task.get(key), int):
                    task[key] = min(
                        ceil(task[key] * len(matched) / len(suite_manifests)),
                        len(active),
                    )

            # A test tag runs only part of each of those manifests, and the
            # runtime data is per manifest, so it can't tell us how long that
            # is. Leave the chunking to the harness in that case.
            if not test_tags:
                _set_manifests_restricted(task)
        elif mh_test_paths:
            # we have test paths and they are not related to the test suite
            # this could be the test suite doesn't support test paths
            continue
        elif (
            test_tags
            and not task["test-manifests"]["active"]
            and not task["test-manifests"]["other_dirs"]
        ):
            # no MH_TEST_PATHS, but MH_TEST_TAG or other filters
            continue

        yield task


@transforms.add
def resolve_dynamic_chunks(config, tasks):
    """Determine how many chunks are needed to handle the given set of manifests."""

    for task in tasks:
        if task["chunks"] != "dynamic":
            yield task
            continue

        if not task.get("test-manifests"):
            task["chunks"] = task.get("default-chunks", 1)
            yield task
            continue

        suite_name = task["test-name"] + task.get("variant-suffix", "")
        all_runtimes = get_runtimes(task["test-platform"], suite_name)

        runtimes = resolve_manifest_runtimes(
            all_runtimes, task["test-manifests"]["active"]
        )

        # A manifest at 0 means the runtime data doesn't cover this
        # configuration, not that the manifest is instant. That only matters for
        # a restricted task, where the few manifests it runs can all be at 0 and
        # make the suite look instant; over a whole suite the manifests that do
        # have data dominate.
        restricted = task["attributes"].get("test-manifests-restricted", False)
        if restricted:
            runtimes = {m: r for m, r in runtimes.items() if r}

        # With no runtime data to go by, all we can do is use the number of
        # chunks the suite is configured to use.
        if not all_runtimes or (restricted and not runtimes):
            task["chunks"] = task.get("default-chunks", 1)
            yield task
            continue

        # Truncate runtimes that are above the desired chunk duration. They
        # will be assigned to a chunk on their own and the excess duration
        # shouldn't cause additional chunks to be needed.
        times = [min(DYNAMIC_CHUNK_DURATION, r) for r in runtimes.values()]
        avg = round(sum(times) / len(times), 2) if times else 0
        total = sum(times)

        # If there are manifests missing from the runtimes data, fill them in
        # with the average of all present manifests.
        missing = [m for m in task["test-manifests"]["active"] if m not in runtimes]
        total += avg * len(missing)

        # Rounding to the nearest chunk lets a chunk run for up to half the
        # target duration longer than that target. That is amortized over the
        # many chunks a whole suite needs, but a restricted task needs a handful
        # at most, where it means one chunk running half again as long.
        if restricted:
            chunks = ceil(total / DYNAMIC_CHUNK_DURATION)
        else:
            chunks = int(round(total / DYNAMIC_CHUNK_DURATION))

        # Make sure we never exceed the number of manifests, nor have a chunk
        # length of 0.
        task["chunks"] = min(chunks, len(task["test-manifests"]["active"])) or 1
        yield task


@transforms.add
def split_chunks(config, tasks):
    """Based on the 'chunks' key, split tests up into chunks by duplicating
    them and assigning 'this-chunk' appropriately and updating the treeherder
    symbol.
    """

    for task in tasks:
        # If test-manifests are set, chunk them ahead of time to avoid running
        # the algorithm more than once.
        chunked_manifests = None
        if "test-manifests" in task:
            # TODO: hardcoded to "2", ideally this should be centralized somewhere
            if (
                config.params["try_task_config"].get("new-test-config", False)
                and task["chunks"] > 1
            ):
                task["chunks"] *= 2
                task["max-run-time"] = int(task["max-run-time"] * 2)

            manifests = task["test-manifests"]
            suite_name = task["test-name"] + task.get("variant-suffix", "")
            chunked_manifests = chunk_manifests(
                suite_name,
                task["test-platform"],
                task["chunks"],
                manifests["active"],
            )

            # Add all skipped manifests to the first chunk of backstop pushes
            # so they still show up in the logs. They won't impact runtime much
            # and this way tools like ActiveData are still aware that they
            # exist.
            if (
                config.params["backstop"]
                and manifests["active"]
                and "skipped" in manifests
            ):
                chunked_manifests[0].extend([
                    m for m in manifests["skipped"] if not m.endswith(".list")
                ])
        last_chunk = task["chunks"]
        for i in range(task["chunks"]):
            this_chunk = i + 1

            # copy the test and update with the chunk number
            chunked = deepcopy(task) if this_chunk != last_chunk else task
            chunked["this-chunk"] = this_chunk

            if chunked_manifests is not None:
                chunked["test-manifests"] = sorted(chunked_manifests[i])

            group, symbol = split_symbol(chunked["treeherder-symbol"])
            if task["chunks"] > 1 or not symbol:
                # add the chunk number to the TH symbol
                symbol += str(this_chunk)
                chunked["treeherder-symbol"] = join_symbol(group, symbol)

            yield chunked
