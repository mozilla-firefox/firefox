# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import fnmatch
import os
import pickle
import sys
from abc import ABCMeta, abstractmethod
from collections import defaultdict
from functools import cache

import mozpack.path as mozpath
from manifestparser import TestManifest, combine_fields
from manifestparser.filters import chunk_by_runtime
from mozbuild.base import MozbuildObject
from mozbuild.testing import REFTEST_FLAVORS, TEST_MANIFESTS, install_test_files
from mozpack.files import FileFinder

here = os.path.abspath(os.path.dirname(__file__))

MOCHITEST_CHUNK_BY_DIR = 4
MOCHITEST_TOTAL_CHUNKS = 5


def WebglSuite(name):
    return {
        "aliases": (name,),
        "build_flavor": "mochitest",
        "mach_command": "mochitest",
        "kwargs": {"flavor": "plain", "subsuite": name, "test_paths": None},
        "task_regex": [
            "mochitest-" + name + "($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    }


TEST_SUITES = {
    "cppunittest": {
        "aliases": ("cpp",),
        "mach_command": "cppunittest",
        "kwargs": {"test_files": None},
    },
    "crashtest": {
        "aliases": ("c", "rc"),
        "build_flavor": "crashtest",
        "mach_command": "crashtest",
        "kwargs": {"test_file": None},
        "task_regex": ["crashtest($|.*(-1|[^0-9])$)", "test-verify($|.*(-1|[^0-9])$)"],
    },
    "crashtest-qr": {
        "aliases": ("c", "rc"),
        "build_flavor": "crashtest",
        "mach_command": "crashtest",
        "kwargs": {"test_file": None},
        "task_regex": [
            "crashtest-qr($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "firefox-ui-functional": {
        "aliases": ("fxfn",),
        "mach_command": "firefox-ui-functional",
        "kwargs": {},
    },
    "firefox-ui-update": {
        "aliases": ("fxup",),
        "mach_command": "firefox-ui-update",
        "kwargs": {},
    },
    "marionette-integration": {
        "aliases": ("mn",),
        "build_flavor": "marionette",
        "mach_command": "marionette-test",
        "kwargs": {"tests": None, "subsuite": "integration"},
        "task_regex": ["marionette($|.*(-1|[^0-9])$)"],
    },
    "marionette-unittest": {
        "aliases": ("mnself",),
        "build_flavor": "marionette",
        "mach_command": "marionette-test",
        "kwargs": {"tests": None, "subsuite": "unittest"},
        "task_regex": ["marionette($|.*(-1|[^0-9])$)"],
    },
    "mochitest-a11y": {
        "aliases": ("a11y", "ally"),
        "build_flavor": "a11y",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "a11y",
            "test_paths": None,
            "e10s": False,
            "enable_fission": False,
        },
        "task_regex": [
            "mochitest-a11y($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-chrome": {
        "aliases": ("bc", "browser"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {"flavor": "browser-chrome", "test_paths": None},
        "task_regex": [
            "mochitest-browser-chrome($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-chrome-thunderbird": {
        "aliases": ("bct",),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "thunderbird",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-chrome-thunderbird($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-chrome-thunderbird-a11y": {
        "aliases": ("bct",),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "thunderbird",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-chrome-thunderbird-a11y($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-screenshots": {
        "aliases": ("ss", "screenshots-chrome"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "screenshots",
            "test_paths": None,
        },
        "task_regex": ["mochitest-browser-screenshots($|.*(-1|[^0-9])$)"],
    },
    "mochitest-chrome": {
        "aliases": ("mc",),
        "build_flavor": "chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "chrome",
            "test_paths": None,
            "e10s": False,
            "enable_fission": False,
        },
        "task_regex": [
            "mochitest-chrome($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-chrome-gpu": {
        "aliases": ("gpu",),
        "build_flavor": "chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "chrome",
            "subsuite": "gpu",
            "test_paths": None,
            "e10s": False,
            "enable_fission": False,
        },
        "task_regex": [
            "mochitest-gpu($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-devtools-chrome": {
        "aliases": ("dt", "devtools"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "devtools",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-devtools-chrome($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-devtools-compat": {
        "aliases": ("dtbc", "devtools-compat"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "devtools-compat",
            "test_paths": None,
        },
        "task_regex": [
            "devtools-compat($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-a11y": {
        "aliases": ("ba", "browser-a11y"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "a11y",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-a11y($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-media": {
        "aliases": ("mpm", "plain-media"),
        "build_flavor": "mochitest",
        "mach_command": "mochitest",
        "kwargs": {"flavor": "plain", "subsuite": "media", "test_paths": None},
        "task_regex": [
            "mochitest-media($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-media": {
        "aliases": ("bmda", "browser-mda"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "media-bc",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-media($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-translations": {
        "aliases": ("btr8ns", "browser-tr8ns"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "translations",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-translations($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-browser-chrome-ml-models": {
        "aliases": ("bc-ml", "browser-ml-models"),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "ml-models",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-browser-chrome-ml-models($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-plain": {
        "aliases": (
            "mp",
            "plain",
        ),
        "build_flavor": "mochitest",
        "mach_command": "mochitest",
        "kwargs": {"flavor": "plain", "test_paths": None},
        "task_regex": [
            "mochitest-plain($|.*(-1|[^0-9])$)",  # noqa
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-plain-gpu": {
        "aliases": ("gpu",),
        "build_flavor": "mochitest",
        "mach_command": "mochitest",
        "kwargs": {"flavor": "plain", "subsuite": "gpu", "test_paths": None},
        "task_regex": [
            "mochitest-gpu($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-remote": {
        "aliases": ("remote",),
        "build_flavor": "browser-chrome",
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "browser-chrome",
            "subsuite": "remote",
            "test_paths": None,
        },
        "task_regex": [
            "mochitest-remote($|.*(-1|[^0-9])$)",
            "test-verify($|.*(-1|[^0-9])$)",
        ],
    },
    "mochitest-webgl1-core": WebglSuite("webgl1-core"),
    "mochitest-webgl1-ext": WebglSuite("webgl1-ext"),
    "mochitest-webgl2-core": WebglSuite("webgl2-core"),
    "mochitest-webgl2-ext": WebglSuite("webgl2-ext"),
    "mochitest-webgl2-deqp": WebglSuite("webgl2-deqp"),
    "mochitest-webgpu": WebglSuite("webgpu"),
    "puppeteer": {
        "aliases": ("remote/test/puppeteer",),
        "mach_command": "puppeteer-test",
        "kwargs": {"headless": False},
    },
    "python": {
        "build_flavor": "python",
        "mach_command": "python-test",
        "kwargs": {"tests": None},
    },
    "telemetry-tests-client": {
        "aliases": ("ttc",),
        "build_flavor": "telemetry-tests-client",
        "mach_command": "telemetry-tests-client",
        "kwargs": {},
        "task_regex": ["telemetry-tests-client($|.*(-1|[^0-9])$)"],
    },
    "reftest": {
        "aliases": ("rr",),
        "build_flavor": "reftest",
        "mach_command": "reftest",
        "kwargs": {"tests": None},
        "task_regex": [
            "(opt|debug)(-geckoview)?-reftest($|.*(-1|[^0-9])$)",
            "test-verify-gpu($|.*(-1|[^0-9])$)",
        ],
    },
    "reftest-qr": {
        "aliases": ("rr",),
        "build_flavor": "reftest",
        "mach_command": "reftest",
        "kwargs": {"tests": None},
        "task_regex": [
            "(opt|debug)(-geckoview)?-reftest-qr($|.*(-1|[^0-9])$)",
            "test-verify-gpu($|.*(-1|[^0-9])$)",
        ],
    },
    "web-platform-tests": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "build_flavor": "web-platform-tests",
        "kwargs": {"subsuite": "testharness"},
        "task_regex": [
            "web-platform-tests(?!-crashtest|-reftest|-wdspec|-print|-aam)"
            "($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "web-platform-tests-crashtest": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "build_flavor": "web-platform-tests",
        "kwargs": {"subsuite": "crashtest"},
        "task_regex": [
            "web-platform-tests-crashtest($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "web-platform-tests-print-reftest": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "kwargs": {"subsuite": "print-reftest"},
        "task_regex": [
            "web-platform-tests-print-reftest($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "web-platform-tests-reftest": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "build_flavor": "web-platform-tests",
        "kwargs": {"subsuite": "reftest"},
        "task_regex": [
            "web-platform-tests-reftest($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "web-platform-tests-wdspec": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "build_flavor": "web-platform-tests",
        "kwargs": {"subsuite": "wdspec"},
        "task_regex": [
            "web-platform-tests-wdspec($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "web-platform-tests-aam": {
        "aliases": ("wpt",),
        "mach_command": "web-platform-tests",
        "build_flavor": "web-platform-tests",
        "kwargs": {"subsuite": "aamtest"},
        "task_regex": [
            "web-platform-tests-aam($|.*(-1|[^0-9])$)",
            "test-verify-wpt",
        ],
    },
    "valgrind": {
        "aliases": ("v",),
        "mach_command": "valgrind-test",
        "kwargs": {},
    },
    "xpcshell": {
        "aliases": ("x",),
        "build_flavor": "xpcshell",
        "mach_command": "xpcshell-test",
        "kwargs": {"test_file": "all"},
        "task_regex": ["xpcshell($|.*(-1|[^0-9])$)", "test-verify($|.*(-1|[^0-9])$)"],
    },
    "xpcshell-msix": {
        "aliases": ("x",),
        "build_flavor": "xpcshell",
        "mach_command": "xpcshell-test",
        "kwargs": {"test_file": "all"},
        "task_regex": ["xpcshell($|.*(-1|[^0-9])$)", "test-verify($|.*(-1|[^0-9])$)"],
    },
    "fenix": {
        "aliases": ("f",),
        "build_flavor": "fenix",
        "mach_command": "android-test",
        "kwargs": {"subproject": "fenix"},
        "root_path": "mobile/android/fenix",
        "task_regex": ["(ui-)?test-apk-fenix($|.*(-1|[^0-9])$)"],
    },
    "focus": {
        "aliases": ("f",),
        "build_flavor": "focus",
        "mach_command": "android-test",
        "kwargs": {"subproject": "focus"},
        "root_path": "mobile/android/focus-android",
        "task_regex": ["(ui-)?test-apk-focus($|.*(-1|[^0-9])$)"],
    },
    "android-components": {
        "aliases": ("ac",),
        "build_flavor": "android-components",
        "mach_command": "android-test",
        "kwargs": {"subproject": "android-components"},
        "root_path": "mobile/android/android-components",
        "task_regex": ["(build|test)-components($|.*(-1|[^0-9])$)"],
    },
    "geckoview": {
        "aliases": ("gv",),
        "build_flavor": "geckoview",
        "mach_command": "geckoview-junit",
        "kwargs": {"no_install": False, "mach_test": True},
        "root_path": "mobile/android/geckoview",
        "task_regex": ["geckoview-junit($|.*(-1|[^0-9])$)"],
    },
    "junit": {
        "aliases": ("j",),
        "build_flavor": "geckoview",
        "mach_command": "geckoview-junit",
        "kwargs": {"no_install": False, "mach_test": True},
        "task_regex": ["geckoview-junit($|.*(-1|[^0-9])$)"],
    },
}
"""Definitions of all test suites and the metadata needed to run and process
them. Each test suite definition can contain the following keys.

Arguments:
    aliases (tuple): A tuple containing shorthands used to refer to this suite.
    build_flavor (str): The flavor assigned to this suite by the build system
                        in `mozbuild.testing.TEST_MANIFESTS` (or similar).
    mach_command (str): Name of the mach command used to run this suite.
    kwargs (dict): Arguments needed to pass into the mach command.
    task_regex (list): A list of regexes used to filter task labels that run
                       this suite.
    root_path (str): Source-relative directory path for this suite's project
                     root. When ``mach test`` receives this exact path, it
                     dispatches the suite directly instead of enumerating
                     individual test files.
"""

for i in range(1, MOCHITEST_TOTAL_CHUNKS + 1):
    TEST_SUITES["mochitest-%d" % i] = {
        "aliases": ("m%d" % i,),
        "mach_command": "mochitest",
        "kwargs": {
            "flavor": "mochitest",
            "subsuite": "default",
            "chunk_by_dir": MOCHITEST_CHUNK_BY_DIR,
            "total_chunks": MOCHITEST_TOTAL_CHUNKS,
            "this_chunk": i,
            "test_paths": None,
        },
    }


WPT_TYPES = set()
for suite, data in TEST_SUITES.items():
    if suite.startswith("web-platform-tests"):
        WPT_TYPES.add(data["kwargs"]["subsuite"])


_test_flavors = {
    "a11y": "mochitest-a11y",
    "browser-chrome": "mochitest-browser-chrome",
    "chrome": "mochitest-chrome",
    "crashtest": "crashtest",
    "firefox-ui-functional": "firefox-ui-functional",
    "firefox-ui-update": "firefox-ui-update",
    "marionette": "marionette",
    "mochitest": "mochitest-plain",
    "puppeteer": "puppeteer",
    "python": "python",
    "reftest": "reftest",
    "telemetry-tests-client": "telemetry-tests-client",
    "web-platform-tests": "web-platform-tests",
    "xpcshell": "xpcshell",
    "fenix": "fenix",
    "focus": "focus",
    "android-components": "android-components",
    "geckoview": "geckoview",
}

_test_subsuites = {
    ("browser-chrome", "a11y"): "mochitest-browser-a11y",
    ("browser-chrome", "devtools"): "mochitest-devtools-chrome",
    ("browser-chrome", "devtools-compat"): "mochitest-devtools-compat",
    ("browser-chrome", "media-bc"): "mochitest-browser-media",
    ("browser-chrome", "ml-models"): "mochitest-browser-chrome-ml-models",
    ("browser-chrome", "remote"): "mochitest-remote",
    ("browser-chrome", "screenshots"): "mochitest-browser-screenshots",
    ("browser-chrome", "translations"): "mochitest-browser-translations",
    ("chrome", "gpu"): "mochitest-chrome-gpu",
    ("marionette", "integration"): "marionette-integration",
    ("marionette", "unittest"): "marionette-unittest",
    ("mochitest", "gpu"): "mochitest-plain-gpu",
    ("mochitest", "media"): "mochitest-media",
    ("mochitest", "webgl1-core"): "mochitest-webgl1-core",
    ("mochitest", "webgl1-ext"): "mochitest-webgl1-ext",
    ("mochitest", "webgl2-core"): "mochitest-webgl2-core",
    ("mochitest", "webgl2-ext"): "mochitest-webgl2-ext",
    ("mochitest", "webgl2-deqp"): "mochitest-webgl2-deqp",
    ("mochitest", "webgpu"): "mochitest-webgpu",
    ("web-platform-tests", "testharness"): "web-platform-tests",
    ("web-platform-tests", "crashtest"): "web-platform-tests-crashtest",
    ("web-platform-tests", "print-reftest"): "web-platform-tests-print-reftest",
    ("web-platform-tests", "reftest"): "web-platform-tests-reftest",
    ("web-platform-tests", "wdspec"): "web-platform-tests-wdspec",
    ("web-platform-tests", "aamtest"): "web-platform-tests-aam",
}


def get_suite_definition(flavor, subsuite=None, strict=False):
    """Return a suite definition given a flavor and optional subsuite.

    If strict is True, a subsuite must have its own entry in TEST_SUITES.
    Otherwise, the entry for 'flavor' will be returned with the 'subsuite'
    keyword arg set.

    With or without strict mode, an empty dict will be returned if no
    matching suite definition was found.
    """
    if not subsuite:
        suite_name = _test_flavors.get(flavor)
        return suite_name, TEST_SUITES.get(suite_name, {}).copy()

    suite_name = _test_subsuites.get((flavor, subsuite))
    if suite_name or strict:
        return suite_name, TEST_SUITES.get(suite_name, {}).copy()

    suite_name = _test_flavors.get(flavor)
    if suite_name not in TEST_SUITES:
        return suite_name, {}

    suite = TEST_SUITES[suite_name].copy()
    suite.setdefault("kwargs", {})
    suite["kwargs"]["subsuite"] = subsuite
    return suite_name, suite


def rewrite_test_base(test, new_base):
    """Rewrite paths in a test to be under a new base path.

    This is useful for running tests from a separate location from where they
    were defined.
    """
    test["here"] = mozpath.join(new_base, test["dir_relpath"])
    test["path"] = mozpath.join(new_base, test["file_relpath"])
    return test


def _is_under(path, prefix):
    return path == prefix or path.startswith(prefix.rstrip("/") + "/")


class TestLoader(MozbuildObject, metaclass=ABCMeta):
    @abstractmethod
    def __call__(self):
        """Generate test metadata."""


class BuildBackendLoader(TestLoader):
    def __call__(self):
        """Loads the test metadata generated by the TestManifest build backend.

        The data is stored in two files:

            - <objdir>/all-tests.pkl
            - <objdir>/test-defaults.pkl

        The 'all-tests.pkl' file is a mapping of source path to test objects. The
        'test-defaults.pkl' file maps manifests to their DEFAULT configuration.
        These manifest defaults will be merged into the test configuration of the
        contained tests.
        """
        # If installing tests is going to result in re-generating the build
        # backend, we need to do this here, so that the updated contents of
        # all-tests.pkl make it to the set of tests to run.
        if self.backend_out_of_date(
            mozpath.join(self.topobjdir, "backend.TestManifestBackend")
        ):
            print("Test configuration changed. Regenerating backend.")
            from mozbuild.gen_test_backend import gen_test_backend

            gen_test_backend()
            # Only install when a prior full build has emitted the harness
            # install manifest.
            harness_manifest = mozpath.join(
                self.topobjdir, "_build_manifests", "install", "_tests"
            )
            if os.path.isfile(harness_manifest):
                install_test_files(
                    mozpath.normpath(self.topsrcdir),
                    self.topobjdir,
                    "_tests",
                )

        all_tests = os.path.join(self.topobjdir, "all-tests.pkl")
        test_defaults = os.path.join(self.topobjdir, "test-defaults.pkl")

        with open(all_tests, "rb") as fh:
            test_data = pickle.load(fh)

        with open(test_defaults, "rb") as fh:
            defaults = pickle.load(fh)

        # The keys in defaults use platform-specific path separators.
        # self.topsrcdir was normalized to use /, revert back to \ if needed.
        topsrcdir = os.path.normpath(self.topsrcdir)

        for path, tests in test_data.items():
            for metadata in tests:
                defaults_manifests = [metadata["manifest"]]

                # For test coverage on the generation and the reading of
                # manifest_defaults with ancestor_manifest, see
                # to_ancestor_manifest_path in the following test files:
                # testing/mozbase/moztest/tests/test_resolve.py
                # python/mozbuild/mozbuild/test/backend/test_test_manifest.py
                ancestor_manifest = metadata.get("ancestor_manifest")
                if ancestor_manifest:
                    # The (ancestor manifest, included manifest) tuple
                    # contains the defaults of the included manifest, so
                    # use it instead of [metadata['manifest']].

                    # ancestor_manifest is a relative path with
                    # platform-specific path separators.
                    defaults_manifests[0] = (ancestor_manifest, metadata["manifest"])

                    # defaults_manifests contains absolute paths with
                    # platform-specific path separators.
                    defaults_manifests.append(
                        os.path.join(topsrcdir, ancestor_manifest)
                    )

                for manifest in defaults_manifests:
                    manifest_defaults = defaults.get(manifest)
                    if manifest_defaults:
                        metadata = combine_fields(manifest_defaults, metadata)

                yield metadata


class TestManifestLoader(TestLoader):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.finder = FileFinder(self.topsrcdir)
        self.reader = self.mozbuild_reader(config_mode="empty")
        self.variables = {f"{k}_MANIFESTS": v[0] for k, v in TEST_MANIFESTS.items()}
        self.variables.update({f"{f.upper()}_MANIFESTS": f for f in REFTEST_FLAVORS})

    def _load_manifestparser_manifest(self, mpath):
        mp = TestManifest(
            manifests=[mpath],
            strict=True,
            rootdir=self.topsrcdir,
            finder=self.finder,
            handle_defaults=True,
        )
        return (test for test in mp.tests)

    def _load_reftest_manifest(self, mpath):
        import reftest

        manifest = reftest.ReftestManifest(finder=self.finder)
        manifest.load(mpath)

        for test in sorted(manifest.tests, key=lambda x: x.get("path")):
            test["manifest_relpath"] = test["manifest"][len(self.topsrcdir) + 1 :]
            yield test

        # Sub-manifests with no file-based tests (e.g. those containing only
        # data: URL tests, which ReftestManifest skips) would otherwise be
        # invisible to the taskgraph manifest loader. Yield a placeholder so
        # they still get scheduled.
        manifests_with_tests = {t["manifest"] for t in manifest.tests}
        for manifest_path, info in sorted(manifest.manifests.items()):
            # Skip the top-level manifest: it is the task entry point and
            # needs no placeholder.
            if manifest_path == manifest.path:
                continue
            # has_test_lines excludes include-only manifests: manifest.sys.mjs
            # skips include recursion when MOZHARNESS_TEST_PATHS is set, so
            # they would run 0 tests if directly targeted. Their sub-manifests
            # are already scheduled independently.
            if manifest_path not in manifests_with_tests and info["has_test_lines"]:
                relpath = manifest_path[len(self.topsrcdir) + 1 :]
                placeholder = {
                    "path": manifest_path,
                    "here": os.path.dirname(manifest_path),
                    "manifest": manifest_path,
                    "manifest_relpath": relpath,
                    "name": os.path.basename(manifest_path),
                    "head": "",
                    "support-files": "",
                    "subsuite": "",
                }
                skip_if = (
                    info["tests_skip_if"]
                    if info["tests_skip_if"] is not None
                    else info["include_skip_if"]
                )
                if skip_if:
                    placeholder["skip-if"] = skip_if
                yield placeholder

    def __call__(self):
        for path, name, key, value in self.reader.find_variables_from_ast(
            self.variables
        ):
            mpath = os.path.join(self.topsrcdir, os.path.dirname(path), value)
            flavor = self.variables[name]

            if name.rsplit("_", 1)[0].lower() in REFTEST_FLAVORS:
                tests = self._load_reftest_manifest(mpath)
            else:
                tests = self._load_manifestparser_manifest(mpath)

            for test in tests:
                path = mozpath.normpath(test["path"])
                assert mozpath.basedir(path, [self.topsrcdir])
                relpath = path[len(self.topsrcdir) + 1 :]

                # Add these keys for compatibility with the build backend loader.
                test["flavor"] = flavor
                test["file_relpath"] = relpath
                test["srcdir_relpath"] = relpath
                test["dir_relpath"] = mozpath.dirname(relpath)

                yield test


class TestResolver(MozbuildObject):
    """Helper to resolve tests from the current environment to test files."""

    test_rewrites = {
        "a11y": "_tests/testing/mochitest/a11y",
        "browser-chrome": "_tests/testing/mochitest/browser",
        "chrome": "_tests/testing/mochitest/chrome",
        "mochitest": "_tests/testing/mochitest/tests",
        "xpcshell": "_tests/xpcshell",
    }

    def __init__(self, *args, **kwargs):
        loader_cls = kwargs.pop("loader_cls", BuildBackendLoader)
        super().__init__(*args, **kwargs)

        self.load_tests = self._spawn(loader_cls)
        self._tests = []
        self._reset_state()

        # These suites aren't registered in moz.build so require special handling.
        self._puppeteer_loaded = False
        self._fenix_loaded = False
        self._ac_loaded = False
        self._focus_loaded = False
        self._geckoview_junit_loaded = False
        self._tests_loaded = False
        self._wpt_loaded = False
        self.meta_tags = {}

    def _reset_state(self):
        self._tests_by_path = defaultdict(list)
        self._tests_by_flavor = defaultdict(set)
        self._tests_by_manifest = defaultdict(list)
        self._test_dirs = set()

    @property
    def tests(self):
        if not self._tests_loaded:
            self._reset_state()
            for test in self.load_tests():
                self._tests.append(test)
            self._tests_loaded = True
        return self._tests

    @property
    def tests_by_path(self):
        if not self._tests_by_path:
            for test in self.tests:
                self._tests_by_path[test["file_relpath"]].append(test)
        return self._tests_by_path

    @property
    def tests_by_flavor(self):
        if not self._tests_by_flavor:
            for test in self.tests:
                self._tests_by_flavor[test["flavor"]].add(test["file_relpath"])
        return self._tests_by_flavor

    @property
    def tests_by_manifest(self):
        if not self._tests_by_manifest:
            for test in self.tests:
                if test["flavor"] == "web-platform-tests":
                    # Use test ids instead of paths for WPT.
                    self._tests_by_manifest[test["manifest"]].append(test["name"])
                else:
                    relpath = mozpath.relpath(
                        test["path"], mozpath.dirname(test["manifest"])
                    )
                    self._tests_by_manifest[test["manifest_relpath"]].append(relpath)
        return self._tests_by_manifest

    @cache
    def get_test_paths_by_manifest(self, suite, paths):
        """Find the manifests of ``suite`` that contain tests under ``paths``.

        Args:
            suite (str): The suite to look at. Values are keys of `TEST_SUITES`.
            paths (frozenset): Source directories or test files, relative to the
                source root.

        Returns:
            A dict mapping each manifest holding at least one test under ``paths``
            to the paths that should be run from it. A manifest maps to itself when
            it lives under one of ``paths``, and to the requested paths when they
            are narrower than the manifest (a single test file, or a subdirectory
            of the manifest's directory).
        """
        suite_definition = TEST_SUITES[suite]
        kwargs = {
            "flavor": suite_definition["build_flavor"],
            "subsuite": suite_definition.get("kwargs", {}).get("subsuite", "undefined"),
        }

        by_manifest = {}
        for path in paths:
            for test in self.resolve_tests(paths=[path], **kwargs):
                manifest = chunk_by_runtime.get_manifest(test)
                # Run the manifest itself when it is entirely under the requested
                # path, and the requested path when it is the narrower of the two.
                test_path = manifest if _is_under(manifest, path) else path
                by_manifest.setdefault(manifest, set()).add(test_path)

        return {
            manifest: [manifest] if manifest in test_paths else sorted(test_paths)
            for manifest, test_paths in by_manifest.items()
        }

    @property
    def test_dirs(self):
        if not self._test_dirs:
            for test in self.tests:
                self._test_dirs.add(test["dir_relpath"])
        return self._test_dirs

    @cache
    def _get_metadata_paths(self, metadata_base, dir_path):

        paths = []
        path_parts = dir_path.split(os.path.sep) if dir_path else []

        for i in range(1, len(path_parts) + 1):
            dir_ini = os.path.join(
                metadata_base, os.path.sep.join(path_parts[:i]), "__dir__.ini"
            )
            if os.path.exists(dir_ini):
                paths.append(dir_ini)

        return tuple(paths)

    def _extract_tags_from_file(self, file_path):

        tags = []
        try:
            with open(file_path, encoding="utf-8") as f:
                for line in f:
                    if "tags: [" in line:
                        tags = line.split("[")[1].split("]")[0].split()
                        break
        except OSError:
            # File doesn't exist or isn't readable
            pass
        return tags

    def get_test_tags(self, test_tags, metadata_base, path):

        dir_path = os.path.dirname(path) if path else ""
        dir_metadata_paths = self._get_metadata_paths(metadata_base, dir_path)

        test_metadata = os.path.join(metadata_base, f"{path}.ini")
        if os.path.exists(test_metadata):
            all_paths = list(dir_metadata_paths) + [test_metadata]
        else:
            all_paths = dir_metadata_paths

        for file_path in all_paths:
            if file_path in self.meta_tags:
                test_tags.extend(self.meta_tags[file_path])
            else:
                tags = self._extract_tags_from_file(file_path)
                self.meta_tags[file_path] = tags
                test_tags.extend(tags)

        return list(set(test_tags))

    def _resolve(
        self, paths=None, flavor="", subsuite=None, under_path=None, tags=None
    ):
        """Given parameters, resolve them to produce an appropriate list of tests.

        Args:
            paths (list):
                By default, set to None. If provided as a list of paths, then
                this method will attempt to load the appropriate set of tests
                that live in this path.

            flavor (string):
                By default, an empty string. If provided as a string, then this
                method will attempt to load tests that belong to this flavor.
                Additional filtering also takes the flavor into consideration.

            subsuite (string):
                By default, set to None. If provided as a string, then this value
                is used to perform filtering of a candidate set of tests.
        """
        if tags:
            tags = set(tags)

        def fltr(tests):
            """Filters tests based on several criteria.

            Args:
                tests (list):
                    List of tests that belong to the same candidate path.

            Returns:
                test (dict):
                    If the test survived the filtering process, it is returned
                    as a valid test.
            """
            for test in tests:
                if flavor:
                    if flavor == "devtools" and test.get("flavor") != "browser-chrome":
                        continue
                    if flavor != "devtools" and test.get("flavor") != flavor:
                        continue

                    # Marionette subsuite 'unittest' runs unit-tests.toml
                    if flavor == "marionette" and test.get("subtest", "") != "unittest":
                        if "unit-tests.toml" in test.get("manifest", ""):
                            continue

                test_subsuite = test.get("subsuite", "undefined")
                if not test_subsuite:  # sometimes test['subsuite'] == ''
                    test_subsuite = "undefined"
                if subsuite and test_subsuite != subsuite:
                    continue

                test_tags = set(test.get("tags", "").split())
                if tags and not (tags & test_tags):
                    continue

                if under_path and not test["file_relpath"].startswith(under_path):
                    continue

                # Make a copy so modifications don't change the source.
                yield dict(test)

        paths = paths or []
        paths = [mozpath.normpath(p) for p in paths]
        if not paths:
            paths = [None]

        if flavor in ("", "puppeteer", None) and (
            any(self.is_puppeteer_path(p) for p in paths) or paths == [None]
        ):
            self.add_puppeteer_manifest_data()

        if flavor in ("", "web-platform-tests", None) and (
            any(self.is_wpt_path(p) for p in paths) or paths == [None]
        ):
            self.add_wpt_manifest_data()

        if flavor in ("", "fenix", None) and (
            any(self.is_fenix_path(p) for p in paths) or paths == [None]
        ):
            self.add_fenix_manifest_data()

        if flavor in ("", "focus", None) and (
            any(self.is_focus_path(p) for p in paths) or paths == [None]
        ):
            self.add_focus_manifest_data()

        if flavor in ("", "android-components", None) and (
            any(self.is_ac_path(p) for p in paths) or paths == [None]
        ):
            self.add_ac_manifest_data()

        if flavor in ("", "geckoview", "junit", None) and (
            any(self.is_geckoview_junit_path(p) for p in paths) or paths == [None]
        ):
            self.add_geckoview_junit_manifest_data()

        candidate_paths = set()

        for path in sorted(paths):
            if path is None:
                candidate_paths |= set(self.tests_by_path.keys())
                continue

            if "*" in path:
                candidate_paths |= {
                    p for p in self.tests_by_path if mozpath.match(p, path)
                }
                continue

            # If the path is a directory, or the path is a prefix of a directory
            # containing tests, pull in all tests in that directory.
            if path in self.test_dirs or any(
                p.startswith(path) for p in self.tests_by_path
            ):
                candidate_paths |= {p for p in self.tests_by_path if p.startswith(path)}
                continue

            # If the path is a manifest, add all tests defined in that manifest.
            if any(path.endswith(e) for e in (".toml", ".ini", ".list")):
                key = "manifest" if os.path.isabs(path) else "manifest_relpath"
                candidate_paths |= {
                    t["file_relpath"]
                    for t in self.tests
                    if key in t and mozpath.normpath(t[key]) == path
                }
                continue

            # If it's a test file, add just that file.
            candidate_paths |= {p for p in self.tests_by_path if path in p}

        for p in sorted(candidate_paths):
            tests = self.tests_by_path[p]
            yield from fltr(tests)

    def is_puppeteer_path(self, path):
        if path is None:
            return True
        return mozpath.match(path, "remote/test/puppeteer/test/**")

    def is_fenix_path(self, path):
        if path is None:
            return True
        return mozpath.match(path, "mobile/android/fenix/**")

    def is_focus_path(self, path):
        if path is None:
            return True
        return mozpath.match(path, "mobile/android/focus-android/**")

    def is_ac_path(self, path):
        if path is None:
            return True
        return mozpath.match(path, "mobile/android/android-components/**")

    def is_geckoview_junit_path(self, path):
        if path is None:
            return True
        return mozpath.match(path, "mobile/android/geckoview/**")

    def add_puppeteer_manifest_data(self):
        if self._puppeteer_loaded:
            return

        self._reset_state()

        test_path = os.path.join(self.topsrcdir, "remote", "test", "puppeteer", "test")
        for root, dirs, paths in os.walk(test_path):
            for filename in fnmatch.filter(paths, "*.spec.js"):
                path = os.path.join(root, filename)
                relpath = mozpath.relpath(mozpath.normpath(path), self.topsrcdir)
                self._tests.append({
                    "path": os.path.abspath(path),
                    "flavor": "puppeteer",
                    "here": os.path.dirname(path),
                    "manifest": None,
                    "name": relpath,
                    "file_relpath": relpath,
                    "head": "",
                    "support-files": "",
                    "subsuite": "puppeteer",
                    "dir_relpath": mozpath.dirname(relpath),
                    "srcdir_relpath": relpath,
                })

        self._puppeteer_loaded = True

    def add_fenix_manifest_data(self):
        if self._fenix_loaded:
            return

        self._reset_state()

        app_dir = os.path.join(self.topsrcdir, "mobile", "android", "fenix", "app")
        test_subdirs = (
            os.path.join("src", "test", "java"),
            os.path.join("src", "test", "kotlin"),
        )
        for root, dirs, paths in os.walk(app_dir):
            if any(sd in root for sd in test_subdirs):
                for filename in fnmatch.filter(paths, "*.kt"):
                    path = os.path.join(root, filename)
                    relpath = mozpath.relpath(mozpath.normpath(path), self.topsrcdir)
                    self._tests.append({
                        "path": os.path.abspath(path),
                        "flavor": "fenix",
                        "here": os.path.dirname(path),
                        "manifest": None,
                        "name": relpath,
                        "file_relpath": relpath,
                        "head": "",
                        "support-files": "",
                        "subsuite": "",
                        "dir_relpath": mozpath.dirname(relpath),
                        "srcdir_relpath": relpath,
                    })
            dirs[:] = [d for d in dirs if d != "build"]

        self._fenix_loaded = True

    def add_focus_manifest_data(self):
        if self._focus_loaded:
            return

        self._reset_state()

        test_path = os.path.join(
            self.topsrcdir,
            "mobile",
            "android",
            "focus-android",
            "app",
            "src",
            "test",
            "java",
        )
        for root, dirs, paths in os.walk(test_path):
            if "test" in root:
                for filename in fnmatch.filter(paths, "*.kt"):
                    path = os.path.join(root, filename)
                    relpath = mozpath.relpath(mozpath.normpath(path), self.topsrcdir)
                    self._tests.append({
                        "path": os.path.abspath(path),
                        "flavor": "focus",
                        "here": os.path.dirname(path),
                        "manifest": None,
                        "name": relpath,
                        "file_relpath": relpath,
                        "head": "",
                        "support-files": "",
                        "subsuite": "",
                        "dir_relpath": mozpath.dirname(relpath),
                        "srcdir_relpath": relpath,
                    })

        self._focus_loaded = True

    def add_ac_manifest_data(self):
        if self._ac_loaded:
            return

        self._reset_state()

        test_path = os.path.join(
            self.topsrcdir, "mobile", "android", "android-components"
        )

        test_subdir_path = os.path.join("src", "test", "java")
        for root, dirs, paths in os.walk(test_path):
            if test_subdir_path in root:
                for filename in fnmatch.filter(paths, "*.kt"):
                    path = os.path.join(root, filename)
                    relpath = mozpath.relpath(mozpath.normpath(path), self.topsrcdir)
                    self._tests.append({
                        "path": os.path.abspath(path),
                        "flavor": "android-components",
                        "here": os.path.dirname(path),
                        "manifest": None,
                        "name": relpath,
                        "file_relpath": relpath,
                        "head": "",
                        "support-files": "",
                        "subsuite": "",
                        "dir_relpath": mozpath.dirname(relpath),
                        "srcdir_relpath": relpath,
                    })

        self._ac_loaded = True

    def add_geckoview_junit_manifest_data(self):
        if self._geckoview_junit_loaded:
            return

        self._reset_state()

        test_path = os.path.join(
            self.topsrcdir,
            "mobile",
            "android",
            "geckoview",
            "src",
            "androidTest",
            "java",
        )
        for root, dirs, paths in os.walk(test_path):
            if "test" in root:
                for filename in fnmatch.filter(paths, "*.kt"):
                    path = os.path.join(root, filename)
                    relpath = mozpath.relpath(mozpath.normpath(path), self.topsrcdir)
                    self._tests.append({
                        "path": os.path.abspath(path),
                        "flavor": "geckoview",
                        "here": os.path.dirname(path),
                        "manifest": None,
                        "name": relpath,
                        "file_relpath": relpath,
                        "head": "",
                        "support-files": "",
                        "subsuite": "",
                        "dir_relpath": mozpath.dirname(relpath),
                        "srcdir_relpath": relpath,
                    })

        self._geckooview_junit_loaded = True

    def is_wpt_path(self, path):
        """Checks if path forms part of the known web-platform-test paths.

        Args:
            path (str or None):
                Path to check against the list of known web-platform-test paths.

        Returns:
            Boolean value. True if path is part of web-platform-tests path, or
            path is None. False otherwise.
        """
        if path is None:
            return True
        if mozpath.match(path, "testing/web-platform/tests/**"):
            return True
        if mozpath.match(path, "testing/web-platform/mozilla/tests/**"):
            return True
        return False

    def get_wpt_group(self, test, depth=3):
        """Given a test object set the group (aka manifest) that it belongs to.

        If a custom value for `depth` is provided, it will override the default
        value of 3 path components.

        Args:
            test (dict): Test object for the particular suite and subsuite.
            depth (int, optional): Custom number of path elements.

        Returns:
            str: The group the given test belongs to.
        """
        name = test["name"]

        # This takes into account that for mozilla-specific WPT tests, the path
        # contains an extra '/_mozilla' prefix that must be accounted for.
        if name.startswith("/_mozilla"):
            depth = depth + 1

        # Webdriver tests are nested in "classic" and "bidi" folders. Increase
        # the depth to avoid grouping all classic or bidi tests in one chunk.
        if name.startswith(("/webdriver", "/_mozilla/webdriver")):
            depth = depth + 1

        # Webdriver BiDi tests are nested even further as tests are grouped by
        # module but also by command / event name.
        if name.startswith(("/webdriver/tests/bidi", "/_mozilla/webdriver/bidi")):
            depth = depth + 1

        # wpt canvas tests are mostly nested under subfolders of /html/canvas,
        # increase the depth to ensure chunks can be balanced correctly.
        if name.startswith("/html/canvas"):
            depth = depth + 1

        if name.startswith("/_mozilla/webgpu"):
            depth = 9001

        # We have a leading / so the first component is always ""
        components = depth + 1

        path = name.split("?", 1)[0].split("#", 1)[0]
        return "/".join(path.rsplit("/", 1)[0].split("/")[:components])

    def add_wpt_manifest_data(self):
        """Adds manifest data for web-platform-tests into the list of available tests.

        Upon invocation, this method will download from firefox-ci the most recent
        version of the web-platform-tests manifests.

        Once manifest is downloaded, this method will add details about each test
        into the list of available tests.
        """
        if self._wpt_loaded:
            return

        self._reset_state()

        wpt_path = os.path.join(self.topsrcdir, "testing", "web-platform")
        old_path = sys.path[:]
        sys.path = [wpt_path] + sys.path

        import logging

        import manifestupdate

        logger = logging.getLogger("manifestupdate")
        logger.disabled = True

        manifests = manifestupdate.run(
            self.topsrcdir,
            self.topobjdir,
            rebuild=False,
            config_path=None,
            rewrite_config=True,
            update=True,
            logger=logger,
        )

        sys.path = old_path

        if not manifests:
            print("Loading wpt manifest failed")
            return

        for manifest, data in manifests.items():
            tests_root = data[
                "tests_path"
            ]  # full path on disk until web-platform tests directory

            for test_type, path, tests in manifest:
                if test_type not in WPT_TYPES:
                    continue

                full_path = mozpath.join(tests_root, path)  # absolute path on disk
                src_path = mozpath.relpath(full_path, self.topsrcdir)
                test_tags = self.get_test_tags([], data.get("metadata_path", ""), path)

                for test in tests:
                    testobj = {
                        "head": "",
                        "support-files": "",
                        "path": full_path,
                        "flavor": "web-platform-tests",
                        "subsuite": test_type,
                        "here": mozpath.dirname(path),
                        "name": test.id,
                        "file_relpath": src_path,
                        "srcdir_relpath": src_path,
                        "dir_relpath": mozpath.dirname(src_path),
                        "tags": " ".join(test_tags),
                    }
                    group = self.get_wpt_group(testobj)
                    testobj["manifest"] = group

                    test_root = "tests"
                    if group.startswith("/_mozilla"):
                        test_root = os.path.join("mozilla", "tests")
                        group = group[len("/_mozilla") :]

                    group = group.lstrip("/")
                    testobj["manifest_relpath"] = os.path.join(
                        wpt_path, test_root, group
                    )
                    self._tests.append(testobj)

        self._wpt_loaded = True

    def resolve_tests(self, cwd=None, **kwargs):
        """Resolve tests from an identifier.

        This is a generator of dicts describing each test. All arguments are
        optional.

        Paths in returned tests are automatically translated to the paths in
        the _tests directory under the object directory.

        Args:
            cwd (str):
                If specified, we will limit our results to tests under this
                directory. The directory should be defined as an absolute path
                under topsrcdir or topobjdir.

            paths (list):
                An iterable of values to use to identify tests to run. If an
                entry is a known test file, tests associated with that file are
                returned (there may be multiple configurations for a single
                file). If an entry is a directory, or a prefix of a directory
                containing tests, all tests in that directory are returned. If
                the string appears in a known test file, that test file is
                considered. If the path contains a wildcard pattern, tests
                matching that pattern are returned.

            under_path (str):
                If specified, will be used to filter out tests that aren't in
                the specified path prefix relative to topsrcdir or the test's
                installed dir.

            flavor (str):
                If specified, will be used to filter returned tests to only be
                the flavor specified. A flavor is something like ``xpcshell``.

            subsuite (str):
                If specified will be used to filter returned tests to only be
                in the subsuite specified. To filter only tests that *don't*
                have any subsuite, pass the string 'undefined'.

            tags (list):
                If specified, will be used to filter out tests that don't contain
                a matching tag.
        """
        if cwd:
            norm_cwd = mozpath.normpath(cwd)
            norm_srcdir = mozpath.normpath(self.topsrcdir)
            norm_objdir = mozpath.normpath(self.topobjdir)

            reldir = None

            if norm_cwd.startswith(norm_objdir):
                reldir = norm_cwd[len(norm_objdir) + 1 :]
            elif norm_cwd.startswith(norm_srcdir):
                reldir = norm_cwd[len(norm_srcdir) + 1 :]

            kwargs["under_path"] = reldir

        rewrite_base = None
        for test in self._resolve(**kwargs):
            rewrite_base = self.test_rewrites.get(test["flavor"], None)

            if rewrite_base:
                rewrite_base = os.path.join(
                    self.topobjdir, os.path.normpath(rewrite_base)
                )
                yield rewrite_test_base(test, rewrite_base)
            else:
                yield test

    @staticmethod
    @cache
    def _path_to_suite():
        """Build a reverse map from root_path to suite name from TEST_SUITES."""
        return {
            v["root_path"]: suite
            for suite, v in TEST_SUITES.items()
            if "root_path" in v
        }

    def resolve_metadata(self, what):
        """Resolve tests based on the given metadata. If not specified, metadata
        from outgoing files will be used instead.
        """
        # Parse arguments and assemble a test "plan."
        run_suites = set()
        run_tests = []

        for entry in what:
            # If the path matches the name or alias of an entire suite, run
            # the entire suite.
            if entry in TEST_SUITES:
                run_suites.add(entry)
                continue
            suitefound = False
            for suite, v in TEST_SUITES.items():
                if entry.lower() in v.get("aliases", []):
                    run_suites.add(suite)
                    suitefound = True
            if suitefound:
                continue

            # If the path matches a suite's root_path, resolve it as a suite
            # rather than enumerating individual test files.
            relpath = self._wrap_path_argument(entry).relpath()
            suite_name = self._path_to_suite().get(mozpath.normpath(relpath))
            if suite_name:
                run_suites.add(suite_name)
                continue

            # Now look for file/directory matches in the TestResolver.
            # since either path or tag can be defined (but not both), here we assume
            # one or none are defined, but not both
            tests = list(self.resolve_tests(paths=[relpath]))
            if not tests:
                tests = list(self.resolve_tests(tags=entry))
            run_tests.extend(tests)

            if not tests:
                print("UNKNOWN TEST: %s" % entry, file=sys.stderr)

        return run_suites, run_tests
