# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Transforms in this file manage the gradual migration of performance tests
# from Ubuntu 18.04 to Ubuntu 24.04 (Bug 1983694). Once all tests have been
# migrated, this file and its registrations in test/__init__.py and
# perftest.py should be removed.

# Talos tests that must remain on linux1804
TALOS_LINUX_1804_TESTS = {
    "talos-damp-inspector",
    "talos-damp-webconsole",
    "talos-chrome",
}

# Perftest jobs that must remain on linux1804
PERFTEST_LINUX_1804_TESTS = {
    "tr8ns-perf-base",
    "tr8ns-perf-basememory",
    "tr8ns-perf-tiny",
}


def restrict_tests_to_2404(config, tasks):
    """
    Bug 2021939 - Restrict most perf tests to Ubuntu 24.04 by dropping linux1804
    tasks that are not in the explicit exception lists. Allowed tasks are kept
    so the downstream restrict_failing_tests_to_1804 transform can remove their
    linux2404 counterparts.
    """
    for task in tasks:
        if "linux1804" not in task.get("test-platform", ""):
            yield task
            continue

        test_name = task.get("test-name", "")

        if task.get("suite") == "talos":
            if test_name in TALOS_LINUX_1804_TESTS:
                yield task
            continue

        yield task


def restrict_failing_tests_to_1804(config, tasks):
    """
    Temporary workaround for Bug 1983694 - Restrict talos tests that fail on
    Ubuntu 24.04 to run on Ubuntu 18.04 hardware only.

    This transform filters out linux2404 test tasks for tests with known
    failures on Ubuntu 24.04.

    This transform should be removed once all tests are fixed or disabled.
    """
    for task in tasks:
        test_platform = task.get("test-platform", "")
        test_name = task.get("test-name", "")

        if "linux2404" not in test_platform:
            yield task
            continue

        if test_name in TALOS_LINUX_1804_TESTS:
            continue

        yield task


def restrict_perftest_to_2404(config, jobs):
    """
    Bug 2021939 - Restrict most perftest jobs to Ubuntu 24.04 by removing
    linux1804 from non-allowed jobs' platform list.
    """
    for job in jobs:
        job_name = job.get("name", "")
        platforms = job.get("platform")

        if job_name not in PERFTEST_LINUX_1804_TESTS and isinstance(platforms, list):
            filtered = [p for p in platforms if "linux1804" not in p]
            if len(filtered) < len(platforms):
                job["platform"] = filtered

        yield job


def restrict_perftest_to_1804(config, jobs):
    """
    Temporary workaround for Bug 1983694 - Restrict perftest jobs that fail
    on Ubuntu 24.04 to run on Ubuntu 18.04 hardware only.

    Perftest jobs have a different structure than test tasks - they use a
    'platform' field that is a list of platforms, and 'name' instead of
    'test-name'. This function filters the platform list.
    """
    for job in jobs:
        job_name = job.get("name", "")
        platforms = job.get("platform")

        if job_name in PERFTEST_LINUX_1804_TESTS and isinstance(platforms, list):
            filtered_platforms = [p for p in platforms if "linux2404" not in p]
            if len(filtered_platforms) < len(platforms):
                job["platform"] = filtered_platforms

        yield job
