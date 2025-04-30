# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import enum
import functools
import json
import os
import pathlib
import re

from mozbuild.base import MozbuildObject

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)

RAPTOR_TEST_MATCHER = re.compile("-(t|test)=([\\S]*)")
TALOS_TEST_MATCHER = re.compile("--suite=([\\S]*)")


class ClassificationEnum(enum.Enum):
    """This class provides the ability to use Enums as array indices."""

    @property
    def value(self):
        return self._value_["value"]

    def __index__(self):
        return self._value_["index"]

    def __int__(self):
        return self._value_["index"]


class Platforms(ClassificationEnum):
    ANDROID_A55 = {"value": "android-a55", "index": 0}
    ANDROID = {"value": "android", "index": 1}
    WINDOWS = {"value": "windows", "index": 2}
    LINUX = {"value": "linux", "index": 3}
    MACOSX = {"value": "macosx", "index": 4}
    DESKTOP = {"value": "desktop", "index": 5}


class Apps(ClassificationEnum):
    FIREFOX = {"value": "firefox", "index": 0}
    CHROME = {"value": "chrome", "index": 1}
    GECKOVIEW = {"value": "geckoview", "index": 2}
    FENIX = {"value": "fenix", "index": 3}
    CHROME_M = {"value": "chrome-m", "index": 4}
    SAFARI = {"value": "safari", "index": 5}
    CHROMIUM_RELEASE = {"value": "custom-car", "index": 6}
    CHROMIUM_RELEASE_M = {"value": "cstm-car-m", "index": 7}
    SAFARI_TP = {"value": "safari-tp", "index": 8}


class Suites(ClassificationEnum):
    RAPTOR = {"value": "raptor", "index": 0}
    TALOS = {"value": "talos", "index": 1}
    AWSY = {"value": "awsy", "index": 2}
    PERFTEST = {"value": "perftest", "index": 3}


class Variants(ClassificationEnum):
    FISSION = {"value": "fission", "index": 0}
    BYTECODE_CACHED = {"value": "bytecode-cached", "index": 1}
    LIVE_SITES = {"value": "live-sites", "index": 2}
    PROFILING = {"value": "profiling", "index": 3}
    SWR = {"value": "swr", "index": 4}


"""
The following methods and constants are used for restricting
certain platforms and applications such as chrome and safari
"""


def check_for_android(android=False, **kwargs):
    return android


def check_for_chrome(chrome=False, **kwargs):
    return chrome


def check_for_custom_car(custom_car=False, **kwargs):
    return custom_car


def check_for_safari(safari=False, **kwargs):
    return safari


def check_for_safari_tp(safari_tp=False, **kwargs):
    return safari_tp


def check_for_live_sites(live_sites=False, **kwargs):
    return live_sites


def check_for_profile(profile=False, **kwargs):
    return profile


"""
The following methods are used to find the test name in a specific
task so that we can provide the ability to run specific tasks that
run a specified test.
"""


def raptor_test_finder(task_cmd, task_label, test):
    """Determine if this task runs the requested test.

    Goes through the task command to find the test specification, and
    then checks if the requested test matches it. Note that the app split
    is done because otherwise the longest common string among all found tasks
    might be something that is common to many more tasks than what we
    expect.
    """
    modified_task_label = None

    for cmd in task_cmd:
        # On windows, the cmd is a string instead of a list
        cmd_list = cmd
        if isinstance(cmd_list, str):
            cmd_list = [cmd_list]

        for option in cmd_list:
            match = RAPTOR_TEST_MATCHER.search(option)
            if not match:
                continue
            _, found_test = match.groups()
            if found_test != test:
                continue

            modified_task_label = task_label
            for app in Apps:
                task_without_app = task_label.split(app.value)
                if len(task_without_app) > 1:
                    if "android" in task_label:
                        # Some tasks don't follow the proper ordering where a test
                        # name is specified after the app name
                        if not task_without_app[-1] or task_without_app[-1] == "-nofis":
                            task_without_app = task_without_app[0].split("browsertime")
                    modified_task_label = task_without_app[-1]
                    break
            break

    return modified_task_label


@functools.lru_cache(maxsize=10)
def get_talos_json():
    with pathlib.Path(build.topsrcdir, "testing", "talos", "talos.json").open() as f:
        talos_json = json.load(f)
    return talos_json


def talos_test_finder(task_cmd, task_label, test):
    """Determine if this task runs the requested talos test.

    Uses the talos.json file for the suites mapping to individual tests, but
    it's possible to also specify suites instead of individual tests.
    """
    modified_task_label = None

    # Talos uses suites instead of the test names in the task commands
    # so we need to find the correct suite to look for first
    suite_to_find = ""
    talos_json = get_talos_json()
    for suite, suite_info in talos_json["suites"].items():
        if test.lower() in [t.lower() for t in suite_info["tests"]]:
            suite_to_find = suite
            break
        elif suite.lower() == test.lower():
            # A full suite might have been requested
            suite_to_find = test
            break
    if not suite_to_find:
        return modified_task_label

    for cmd in task_cmd:
        cmd_list = cmd
        if isinstance(cmd_list, str):
            cmd_list = [cmd_list]
        for option in cmd_list:
            match = TALOS_TEST_MATCHER.search(option)
            if not match:
                continue
            found_suite = match.group(1)
            if found_suite.lower() != suite_to_find.lower():
                continue
            modified_task_label = task_label
            break

    return modified_task_label


def perftest_test_finder(task_cmd, task_label, test):
    """
    This is a general finder for all performance tests, given
    that mozperftest names don't include things like:
    aarch64, shippable, or opt among other identifiers
    """
    modified_task_label = None
    for cmd in task_cmd:
        cmd_list = cmd
        found = any(test in arg for arg in cmd_list)
        if found:
            modified_task_label = task_label

    return modified_task_label


def awsy_test_finder(task_cmd, task_label, test):
    """AWSY doesn't mention it's test name anywhere, and only
    reports the metrics without the test name that actually triggered
    it. Also, the AWSY suite doesn't have a test specifier. Instead,
    we will select any task that has awsy in the label if the test
    is also named something like `awsy`.
    """
    if "awsy" not in test:
        return None
    if "awsy" not in task_label:
        return None
    return task_label


class ClassificationProvider:
    @property
    def platforms(self):
        return {
            Platforms.ANDROID_A55.value: {
                "query": {
                    Suites.PERFTEST.value: "'android 'a55",
                    "default": "'android 'a55 'shippable 'aarch64",
                },
                "platform": Platforms.ANDROID.value,
            },
            Platforms.ANDROID.value: {
                # The android, and android-a55 queries are expected to be the same,
                # we don't want to run the tests on other mobile platforms.
                "query": {
                    Suites.PERFTEST.value: "'android",
                    "default": "'android 'a55 'shippable 'aarch64",
                },
                "platform": Platforms.ANDROID.value,
            },
            Platforms.WINDOWS.value: {
                "query": {
                    Suites.PERFTEST.value: "'windows",
                    "default": "!-32 !10-64 'windows 'shippable",
                },
                "platform": Platforms.DESKTOP.value,
            },
            Platforms.LINUX.value: {
                "query": {
                    Suites.PERFTEST.value: "'linux",
                    "default": "!clang 'linux 'shippable",
                },
                "platform": Platforms.DESKTOP.value,
            },
            Platforms.MACOSX.value: {
                "query": {
                    Suites.PERFTEST.value: "'macosx",
                    "default": "'osx 'shippable",
                },
                "platform": Platforms.DESKTOP.value,
            },
            Platforms.DESKTOP.value: {
                "query": {
                    Suites.PERFTEST.value: "!android",
                    "default": "!android 'shippable !-32 !clang",
                },
                "platform": Platforms.DESKTOP.value,
            },
        }

    @property
    def apps(self):
        return {
            Apps.FIREFOX.value: {
                "query": "!chrom !geckoview !fenix !safari !m-car !safari-tp",
                "platforms": [Platforms.DESKTOP.value],
            },
            Apps.CHROME.value: {
                "query": "'chrome",
                "negation": "!chrom",
                "restriction": check_for_chrome,
                "platforms": [Platforms.DESKTOP.value],
            },
            Apps.GECKOVIEW.value: {
                "query": "'geckoview",
                "negation": "!geckoview",
                "platforms": [Platforms.ANDROID.value],
            },
            Apps.FENIX.value: {
                "query": "'fenix",
                "negation": "!fenix",
                "platforms": [Platforms.ANDROID.value],
            },
            Apps.CHROME_M.value: {
                "query": "'chrome-m",
                "negation": "!chrom",
                "restriction": check_for_chrome,
                "platforms": [Platforms.ANDROID.value],
            },
            Apps.SAFARI.value: {
                "query": "'safari",
                "negation": "!safari",
                "restriction": check_for_safari,
                "platforms": [Platforms.MACOSX.value],
            },
            Apps.SAFARI_TP.value: {
                "query": "'safari-tp",
                "negation": "!safari-tp",
                "restriction": check_for_safari_tp,
                "platforms": [Platforms.MACOSX.value],
            },
            Apps.CHROMIUM_RELEASE.value: {
                "query": "'m-car",
                "negation": "!m-car",
                "restriction": check_for_custom_car,
                "platforms": [
                    Platforms.LINUX.value,
                    Platforms.WINDOWS.value,
                    Platforms.MACOSX.value,
                ],
            },
            Apps.CHROMIUM_RELEASE_M.value: {
                "query": "'m-car",
                "negation": "!m-car",
                "restriction": check_for_custom_car,
                "platforms": [Platforms.ANDROID.value],
            },
        }

    @property
    def variants(self):
        return {
            Variants.FISSION.value: {
                "query": "!nofis",
                "negation": "'nofis",
                "platforms": [Platforms.ANDROID.value],
                "apps": [Apps.FENIX.value, Apps.GECKOVIEW.value],
            },
            Variants.BYTECODE_CACHED.value: {
                "query": "'bytecode",
                "negation": "!bytecode",
                "platforms": [Platforms.DESKTOP.value],
                "apps": [Apps.FIREFOX.value],
            },
            Variants.LIVE_SITES.value: {
                "query": "'live",
                "negation": "!live",
                "restriction": check_for_live_sites,
                "platforms": [Platforms.DESKTOP.value, Platforms.ANDROID.value],
                "apps": [  # XXX No live CaR tests
                    Apps.FIREFOX.value,
                    Apps.CHROME.value,
                    Apps.FENIX.value,
                    Apps.GECKOVIEW.value,
                    Apps.SAFARI.value,
                ],
            },
            Variants.PROFILING.value: {
                "query": "'profil",
                "negation": "!profil",
                "restriction": check_for_profile,
                "platforms": [Platforms.DESKTOP.value, Platforms.ANDROID.value],
                "apps": [Apps.FIREFOX.value, Apps.GECKOVIEW.value, Apps.FENIX.value],
            },
            Variants.SWR.value: {
                "query": "'swr",
                "negation": "!swr",
                "platforms": [Platforms.DESKTOP.value],
                "apps": [Apps.FIREFOX.value],
            },
        }

    @property
    def suites(self):
        return {
            Suites.RAPTOR.value: {
                "apps": list(self.apps.keys()),
                "platforms": list(self.platforms.keys()),
                "variants": [
                    Variants.FISSION.value,
                    Variants.LIVE_SITES.value,
                    Variants.PROFILING.value,
                    Variants.BYTECODE_CACHED.value,
                ],
                "task-specifier": "browsertime",
                "task-test-finder": raptor_test_finder,
                "framework": 13,
            },
            Suites.TALOS.value: {
                "apps": [Apps.FIREFOX.value],
                "platforms": [Platforms.DESKTOP.value],
                "variants": [
                    Variants.PROFILING.value,
                    Variants.SWR.value,
                ],
                "task-specifier": "talos",
                "task-test-finder": talos_test_finder,
                "framework": 1,
            },
            Suites.AWSY.value: {
                "apps": [Apps.FIREFOX.value],
                "platforms": [Platforms.DESKTOP.value],
                "variants": [],
                "task-specifier": "awsy",
                "task-test-finder": awsy_test_finder,
                "framework": 4,
            },
            Suites.PERFTEST.value: {
                "apps": list(self.apps.keys()),
                "platforms": list(self.platforms.keys()),
                "variants": [],
                "task-specifier": "perftest",
                "task-test-finder": perftest_test_finder,
                "framework": 15,
            },
        }

    """
    Here you can find the base categories that are defined for the perf
    selector. The following fields are available:
        * query: Set the queries to use for each suite you need.
        * suites: The suites that are needed for this category.
        * tasks: A hard-coded list of tasks to select.
        * platforms: The platforms that it can run on.
        * app-restrictions: A list of apps that the category can run.
        * variant-restrictions: A list of variants available for each suite.

    Note that setting the App/Variant-Restriction fields should be used to
    restrict the available apps and variants, not expand them.
    """

    @property
    def categories(self):
        return {
            "Pageload": {
                "query": {
                    Suites.RAPTOR.value: ["'browsertime 'tp6 !tp6-bench"],
                },
                "suites": [Suites.RAPTOR.value],
                "app-restrictions": {
                    Suites.RAPTOR.value: [
                        Apps.FIREFOX.value,
                        Apps.CHROME.value,
                        Apps.FENIX.value,
                        Apps.GECKOVIEW.value,
                        Apps.SAFARI.value,
                        Apps.CHROMIUM_RELEASE.value,
                        Apps.CHROMIUM_RELEASE_M.value,
                        Apps.CHROME_M.value,
                    ],
                },
                "tasks": [],
                "description": "A group of tests that measures various important pageload metrics. More information "
                "can about what is exactly measured can found here:"
                " https://firefox-source-docs.mozilla.org/testing/perfdocs/raptor.html#desktop",
            },
            "Speedometer 3": {
                "query": {
                    Suites.RAPTOR.value: ["'browsertime 'speedometer3"],
                },
                "variant-restrictions": {Suites.RAPTOR.value: [Variants.FISSION.value]},
                "suites": [Suites.RAPTOR.value],
                "app-restrictions": {},
                "tasks": [],
                "description": "A group of Speedometer3 tests on various platforms and architectures, speedometer3 is"
                "currently the best benchmark we have for a baseline on real-world web performance",
            },
            "Responsiveness": {
                "query": {
                    Suites.RAPTOR.value: ["'browsertime 'responsive"],
                },
                "suites": [Suites.RAPTOR.value],
                "variant-restrictions": {Suites.RAPTOR.value: []},
                "app-restrictions": {
                    Suites.RAPTOR.value: [
                        Apps.FIREFOX.value,
                        Apps.CHROME.value,
                        Apps.FENIX.value,
                        Apps.GECKOVIEW.value,
                    ],
                },
                "tasks": [],
                "description": "A group of tests that ensure that the interactive part of the browser stays fast and"
                "responsive",
            },
            "Benchmarks": {
                "query": {
                    Suites.RAPTOR.value: ["'browsertime 'benchmark !tp6-bench"],
                },
                "suites": [Suites.RAPTOR.value],
                "variant-restrictions": {Suites.RAPTOR.value: []},
                "app-restrictions": {
                    Suites.RAPTOR.value: [
                        Apps.FIREFOX.value,
                        Apps.CHROME.value,
                        Apps.FENIX.value,
                        Apps.GECKOVIEW.value,
                        Apps.SAFARI.value,
                        Apps.CHROMIUM_RELEASE.value,
                        Apps.CHROMIUM_RELEASE_M.value,
                        Apps.CHROME_M.value,
                    ],
                },
                "tasks": [],
                "description": "A group of tests that benchmark how the browser performs in various categories. "
                "More information about what exact benchmarks we run can be found here: "
                "https://firefox-source-docs.mozilla.org/testing/perfdocs/raptor.html#benchmarks",
            },
            "DAMP (Devtools)": {
                "query": {
                    Suites.TALOS.value: ["'talos 'damp"],
                },
                "suites": [Suites.TALOS.value],
                "tasks": [],
                "description": "The DAMP tests are a group of tests that measure the performance of the browsers "
                "devtools under certain conditiones. More information on the DAMP tests can be found"
                " here: https://firefox-source-docs.mozilla.org/devtools/tests/performance-tests"
                "-damp.html#what-does-it-do",
            },
            "Talos PerfTests": {
                "query": {
                    Suites.TALOS.value: ["'talos"],
                },
                "suites": [Suites.TALOS.value],
                "tasks": [],
                "description": "This selects all of the talos performance tests. More information can be found here: "
                "https://firefox-source-docs.mozilla.org/testing/perfdocs/talos.html#test-types",
            },
            "Resource Usage": {
                "query": {
                    Suites.TALOS.value: ["'talos 'xperf | 'tp5"],
                    Suites.RAPTOR.value: ["'power 'osx"],
                    Suites.AWSY.value: ["'awsy"],
                },
                "suites": [Suites.TALOS.value, Suites.RAPTOR.value, Suites.AWSY.value],
                "platform-restrictions": [Platforms.DESKTOP.value],
                "variant-restrictions": {
                    Suites.RAPTOR.value: [],
                    Suites.TALOS.value: [],
                },
                "app-restrictions": {
                    Suites.RAPTOR.value: [Apps.FIREFOX.value],
                    Suites.TALOS.value: [Apps.FIREFOX.value],
                },
                "tasks": [],
                "description": "A group of tests that monitor resource usage of various metrics like power, CPU, and"
                "memory",
            },
            "Graphics, & Media Playback": {
                "query": {
                    # XXX This might not be an exhaustive list for talos atm
                    Suites.TALOS.value: ["'talos 'svgr | 'bcv | 'webgl"],
                    Suites.RAPTOR.value: ["'browsertime 'youtube-playback"],
                },
                "suites": [Suites.TALOS.value, Suites.RAPTOR.value],
                "variant-restrictions": {Suites.RAPTOR.value: [Variants.FISSION.value]},
                "app-restrictions": {
                    Suites.RAPTOR.value: [
                        Apps.FIREFOX.value,
                        Apps.CHROME.value,
                        Apps.FENIX.value,
                        Apps.GECKOVIEW.value,
                    ],
                },
                "tasks": [],
                "description": "A group of tests that monitor key graphics and media metrics to keep the browser fast",
            },
            "Pageload Lite": {
                "query": {
                    Suites.RAPTOR.value: ["'browsertime 'tp6-bench"],
                },
                "suites": [Suites.RAPTOR.value],
                "platform-restrictions": [
                    Platforms.DESKTOP.value,
                    Platforms.LINUX.value,
                    Platforms.MACOSX.value,
                    Platforms.WINDOWS.value,
                ],
                "variant-restrictions": {Suites.RAPTOR.value: [Variants.FISSION.value]},
                "app-restrictions": {
                    Suites.RAPTOR.value: [Apps.FIREFOX.value],
                },
                "tasks": [],
                "description": (
                    "Similar to the Pageload category, but it provides a minimum set "
                    "of pageload tests to run for performance testing."
                ),
            },
            "Startup": {
                "query": {
                    Suites.PERFTEST.value: ["'startup"],
                    Suites.TALOS.value: ["'sessionrestore | 'other !damp"],
                },
                "suites": [Suites.PERFTEST.value, Suites.TALOS.value],
                "platform-restrictions": [
                    Platforms.ANDROID.value,
                    Platforms.LINUX.value,
                    Platforms.MACOSX.value,
                    Platforms.WINDOWS.value,
                ],
                "app-restrictions": {
                    Suites.PERFTEST.value: [
                        Apps.FENIX.value,
                        Apps.GECKOVIEW.value,
                        Apps.CHROME_M.value,
                        Apps.FIREFOX.value,
                    ],
                },
                "tasks": [],
                "description": (
                    "A group of tests that monitor startup performance of our "
                    "android and desktop browsers"
                ),
            },
            "Machine Learning": {
                "query": {
                    Suites.PERFTEST.value: ["'perftest '-ml-"],
                },
                "suites": [Suites.PERFTEST.value],
                "platform-restrictions": [
                    Platforms.DESKTOP.value,
                    Platforms.LINUX.value,
                    Platforms.MACOSX.value,
                    Platforms.WINDOWS.value,
                ],
                "app-restrictions": {
                    Suites.PERFTEST.value: [
                        Apps.FIREFOX.value,
                    ],
                },
                "tasks": [],
                "description": (
                    "A set of tests used to test machine learning performance in Firefox."
                ),
            },
            "Mobile Resource Usage": {
                "query": {
                    Suites.PERFTEST.value: ["'perftest 'resource"],
                },
                "suites": [Suites.PERFTEST.value],
                "platform-restrictions": [
                    Platforms.ANDROID.value,
                ],
                "app-restrictions": {
                    Suites.PERFTEST.value: [
                        Apps.FENIX.value,
                        Apps.CHROME_M.value,
                    ],
                },
                "tasks": [],
                "description": ("A set of tests for testing resource usage on mobile."),
            },
            "Translations": {
                "query": {
                    Suites.PERFTEST.value: ["'perftest 'tr8ns"],
                },
                "suites": [Suites.PERFTEST.value],
                "platform-restrictions": [
                    Platforms.DESKTOP.value,
                    Platforms.LINUX.value,
                    Platforms.MACOSX.value,
                    Platforms.WINDOWS.value,
                ],
                "app-restrictions": {
                    Suites.PERFTEST.value: [
                        Apps.FIREFOX.value,
                    ],
                },
                "tasks": [],
                "description": (
                    "A set of tests used to test Translations performance in Firefox."
                ),
            },
            "Critical Android Performance": {
                "query": {
                    Suites.RAPTOR.value: ["'speedometer3 | 'jetstream"],
                    Suites.PERFTEST.value: ["'applink-startup"],
                },
                "suites": [Suites.RAPTOR.value, Suites.PERFTEST.value],
                "platform-restrictions": [
                    Platforms.ANDROID_A55.value,
                ],
                "app-restrictions": {
                    Suites.PERFTEST.value: [
                        Apps.FENIX.value,
                    ],
                    Suites.RAPTOR.value: [
                        Apps.FENIX.value,
                    ],
                },
                "tasks": [],
                "description": (
                    "Our most important set of tests for android performance."
                ),
            },
        }
