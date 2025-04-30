# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import os
import pathlib
import shutil
import tempfile
from unittest import mock

import mozunit
import pytest
from tryselect.selectors.perf import (
    MAX_PERF_TASKS,
    Apps,
    InvalidCategoryException,
    InvalidRegressionDetectorQuery,
    PerfParser,
    Platforms,
    Suites,
    Variants,
    run,
)
from tryselect.selectors.perf_preview import plain_display
from tryselect.selectors.perfselector.classification import (
    check_for_live_sites,
    check_for_profile,
)
from tryselect.selectors.perfselector.perfpushinfo import PerfPushInfo

here = os.path.abspath(os.path.dirname(__file__))
FTG_SAMPLE_PATH = pathlib.Path(here, "full-task-graph-perf-test.json")

TASKS = [
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-motionmark-animometer-1-3",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-godot-optimizing",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-webaudio",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-speedometer",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-misc",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-jetstream2",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-ares6",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-misc-optimizing",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-sunspider",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-matrix-react-bench",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-godot-baseline",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-twitch-animation",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-assorted-dom",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-stylebench",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-misc-baseline",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-motionmark-htmlsuite-1-3",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-firefox-unity-webgl",
    "test-linux1804-64-shippable-qr/opt-browsertime-benchmark-wasm-firefox-wasm-godot",
    "test-linux1804-64-shippable-qr/opt-browsertime-amazon",
]

# The TEST_VARIANTS, and TEST_CATEGORIES are used to force
# a particular set of categories to show up in testing. Otherwise,
# every time someone adds a category, or a variant, we'll need
# to redo all the category counts. The platforms, and apps are
# not forced because they change infrequently.
TEST_VARIANTS = {
    # Bug 1837058 - Switch this back to Variants.NO_FISSION when
    # the default flips to fission on android
    Variants.FISSION.value: {
        "query": "'nofis",
        "negation": "!nofis",
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
        "apps": list(PerfParser.apps.keys()),
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

TEST_CATEGORIES = {
    "Pageload": {
        "query": {
            Suites.RAPTOR.value: ["'browsertime 'tp6"],
        },
        "suites": [Suites.RAPTOR.value],
        "tasks": [],
        "description": "",
    },
    "Pageload (essential)": {
        "query": {
            Suites.RAPTOR.value: ["'browsertime 'tp6 'essential"],
        },
        "variant-restrictions": {Suites.RAPTOR.value: [Variants.FISSION.value]},
        "suites": [Suites.RAPTOR.value],
        "tasks": [],
        "description": "",
    },
    "Responsiveness": {
        "query": {
            Suites.RAPTOR.value: ["'browsertime 'responsive"],
        },
        "suites": [Suites.RAPTOR.value],
        "variant-restrictions": {Suites.RAPTOR.value: []},
        "tasks": [],
        "description": "",
    },
    "Benchmarks": {
        "query": {
            Suites.RAPTOR.value: ["'browsertime 'benchmark"],
        },
        "suites": [Suites.RAPTOR.value],
        "variant-restrictions": {Suites.RAPTOR.value: []},
        "tasks": [],
        "description": "",
    },
    "DAMP (Devtools)": {
        "query": {
            Suites.TALOS.value: ["'talos 'damp"],
        },
        "suites": [Suites.TALOS.value],
        "tasks": [],
        "description": "",
    },
    "Talos PerfTests": {
        "query": {
            Suites.TALOS.value: ["'talos"],
        },
        "suites": [Suites.TALOS.value],
        "tasks": [],
        "description": "",
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
        "description": "",
    },
    "Graphics, & Media Playback": {
        "query": {
            # XXX This might not be an exhaustive list for talos atm
            Suites.TALOS.value: ["'talos 'svgr | 'bcv | 'webgl"],
            Suites.RAPTOR.value: ["'browsertime 'youtube-playback"],
        },
        "suites": [Suites.TALOS.value, Suites.RAPTOR.value],
        "variant-restrictions": {Suites.RAPTOR.value: [Variants.FISSION.value]},
        "tasks": [],
        "description": "",
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
}


@contextlib.contextmanager
def category_reset():
    try:
        original_categories = PerfParser.categories
        yield
    finally:
        PerfParser.categories = original_categories


def setup_perfparser():
    PerfParser.categories = TEST_CATEGORIES
    PerfParser.variants = TEST_VARIANTS
    PerfParser.push_info = PerfPushInfo()


@pytest.mark.parametrize(
    "category_options, expected_counts, unique_categories, missing",
    [
        # Default should show the premade live category, but no chrome or android
        # The benchmark desktop category should be visible in all configurations
        # except for when there are requested apps/variants/platforms
        (
            {},
            96,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Resource Usage desktop": {
                    "awsy": ["'awsy", "!android 'shippable !-32 !clang"],
                    "raptor": [
                        "'power 'osx",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                    "talos": [
                        "'talos 'xperf | 'tp5",
                        "!android 'shippable !-32 !clang",
                        "!profil",
                        "!swr",
                    ],
                },
                "Machine Learning desktop firefox": {
                    "perftest": [
                        "'perftest '-ml-",
                        "!android",
                        "!chrom !geckoview !fenix !safari !m-car !safari-tp",
                    ],
                },
            },
            [
                "Responsiveness android-p2 geckoview",
            ],
        ),  # Default settings
        (
            {"live_sites": True},
            110,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "'live",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
            },
            [
                "Responsiveness android-p2 geckoview",
                "Benchmarks desktop firefox profiling",
                "Talos desktop live-sites",
                "Talos desktop profiling+swr",
                "Benchmarks desktop firefox live-sites+profiling"
                "Benchmarks desktop firefox live-sites",
            ],
        ),
        (
            {"live_sites": True, "safari": True},
            116,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx safari": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "'safari",
                        "!bytecode",
                        "!profil",
                    ]
                },
                "Pageload macosx safari live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "'safari",
                        "'live",
                        "!bytecode",
                        "!profil",
                    ],
                },
            },
            [
                "Pageload linux safari",
                "Pageload desktop safari",
            ],
        ),
        (
            {"safari-tp": True},
            96,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
            },
            [
                "Pageload linux safari-tp",
                "Pageload windows safari-tp",
                "Pageload desktop safari-tp",
            ],
        ),
        (
            {"live_sites": True, "chrome": True},
            146,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!profil",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload macosx live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'osx 'shippable",
                        "'live",
                        "!bytecode",
                        "!profil",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
            },
            [
                "Responsiveness android-p2 geckoview",
                "Firefox Pageload linux chrome",
                "Talos PerfTests desktop swr",
            ],
        ),
        (
            {"android": True},
            96,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
                "Responsiveness android-a55 geckoview": {
                    "raptor": [
                        "'browsertime 'responsive",
                        "'android 'a55 'shippable 'aarch64",
                        "'geckoview",
                        "!nofis",
                        "!live",
                        "!profil",
                    ],
                },
            },
            [
                "Responsiveness android-a55 chrome-m",
                "Firefox Pageload android",
            ],
        ),
        (
            {"android": True, "chrome": True},
            126,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!profil",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
                "Responsiveness android-a55 chrome-m": {
                    "raptor": [
                        "'browsertime 'responsive",
                        "'android 'a55 'shippable 'aarch64",
                        "'chrome-m",
                        "!nofis",
                        "!live",
                        "!profil",
                    ],
                },
            },
            ["Responsiveness android-p2 chrome-m", "Resource Usage android"],
        ),
        (
            {"android": True, "chrome": True, "profile": True},
            164,
            {
                "Benchmarks desktop": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!android 'shippable !-32 !clang",
                        "!bytecode",
                        "!live",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Talos PerfTests desktop profiling": {
                    "talos": [
                        "'talos",
                        "!android 'shippable !-32 !clang",
                        "'profil",
                        "!swr",
                    ]
                },
            },
            [
                "Resource Usage desktop profiling",
                "DAMP (Devtools) desktop chrome",
                "Resource Usage android",
            ],
        ),
        (
            {"android": True, "fenix": True},
            96,
            {
                "Pageload android-a55": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "!nofis",
                        "!live",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ]
                },
                "Pageload android-a55 fenix": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "!nofis",
                        "!live",
                        "!profil",
                    ]
                },
            },
            [
                "Resource Usage desktop profiling",
                "DAMP (Devtools) desktop chrome",
                "Resource Usage android",
            ],
        ),
        # Show all available windows tests, no other platform should exist
        # including the desktop catgeory
        (
            {"requested_platforms": ["windows"]},
            16,
            {
                "Benchmarks windows firefox": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "!-32 !10-64 'windows 'shippable",
                        "!chrom !geckoview !fenix !safari !m-car !safari-tp",
                        "!bytecode",
                        "!live",
                        "!profil",
                    ]
                },
            },
            [
                "Resource Usage desktop",
                "Benchmarks desktop",
                "Benchmarks linux firefox bytecode-cached+profiling",
            ],
        ),
        # Can't have fenix on the windows platform
        (
            {"requested_platforms": ["windows"], "requested_apps": ["fenix"]},
            0,
            {},
            ["Benchmarks desktop"],
        ),
        # There should be no global categories available, only fenix
        (
            {
                "requested_platforms": ["android"],
                "requested_apps": ["fenix"],
                "android": True,
            },
            10,
            {
                "Pageload android fenix": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "!nofis",
                        "!live",
                        "!profil",
                    ],
                }
            },
            ["Benchmarks desktop", "Pageload (live) android"],
        ),
        # Test with multiple apps
        (
            {
                "requested_platforms": ["android"],
                "requested_apps": ["fenix", "geckoview"],
                "android": True,
            },
            15,
            {
                "Benchmarks android geckoview": {
                    "raptor": [
                        "'browsertime 'benchmark",
                        "'android 'a55 'shippable 'aarch64",
                        "'geckoview",
                        "!nofis",
                        "!live",
                        "!profil",
                    ],
                },
                "Pageload android fenix": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "!nofis",
                        "!live",
                        "!profil",
                    ],
                },
            },
            [
                "Benchmarks desktop",
                "Pageload android no-fission",
                "Pageload android fenix live-sites",
            ],
        ),
        # Variants are inclusive, so we'll see the variant alongside the
        # base here for fenix
        (
            {
                "requested_variants": ["fission"],
                "requested_apps": ["fenix"],
                "android": True,
            },
            32,
            {
                "Pageload android-a55 fenix": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "!live",
                        "!profil",
                    ],
                },
                "Pageload android-a55 fenix fission": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'nofis",
                        "!live",
                        "!profil",
                    ],
                },
                "Pageload (essential) android fenix fission": {
                    "raptor": [
                        "'browsertime 'tp6 'essential",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'nofis",
                        "!live",
                        "!profil",
                    ],
                },
            },
            [
                "Benchmarks desktop",
                "Pageload (live) android",
                "Pageload android-p2 fenix live-sites",
            ],
        ),
        # With multiple variants, we'll see the base variant (with no combinations)
        # for each of them
        (
            {
                "requested_variants": ["fission", "live-sites"],
                "requested_apps": ["fenix"],
                "android": True,
            },
            40,
            {
                "Pageload android-a55 fenix": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "!profil",
                    ],
                },
                "Pageload android-a55 fenix fission": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'nofis",
                        "!live",
                        "!profil",
                    ],
                },
                "Pageload android-a55 fenix live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'live",
                        "!nofis",
                        "!profil",
                    ],
                },
                "Pageload (essential) android fenix fission": {
                    "raptor": [
                        "'browsertime 'tp6 'essential",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'nofis",
                        "!live",
                        "!profil",
                    ],
                },
                "Pageload android fenix fission+live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "'android 'a55 'shippable 'aarch64",
                        "'fenix",
                        "'nofis",
                        "'live",
                        "!profil",
                    ],
                },
            },
            [
                "Benchmarks desktop",
                "Pageload (live) android",
                "Pageload android-p2 fenix live-sites",
                "Pageload (essential) android fenix no-fission+live-sites",
            ],
        ),
        # Make sure that no no-fission tasks are selected when a variant cannot
        # run on a requested platform
        (
            {
                "requested_variants": ["no-fission"],
                "requested_platforms": ["windows"],
            },
            16,
            {
                "Responsiveness windows firefox": {
                    "raptor": [
                        "'browsertime 'responsive",
                        "!-32 !10-64 'windows 'shippable",
                        "!chrom !geckoview !fenix !safari !m-car !safari-tp",
                        "!bytecode",
                        "!live",
                        "!profil",
                    ],
                },
            },
            ["Benchmarks desktop", "Responsiveness windows firefox no-fisson"],
        ),
        # We should only see the base and the live-site variants here for windows
        (
            {
                "requested_variants": ["no-fission", "live-sites"],
                "requested_platforms": ["windows"],
                "android": True,
            },
            18,
            {
                "Responsiveness windows firefox": {
                    "raptor": [
                        "'browsertime 'responsive",
                        "!-32 !10-64 'windows 'shippable",
                        "!chrom !geckoview !fenix !safari !m-car !safari-tp",
                        "!bytecode",
                        "!profil",
                    ],
                },
                "Pageload windows live-sites": {
                    "raptor": [
                        "'browsertime 'tp6",
                        "!-32 !10-64 'windows 'shippable",
                        "'live",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
                "Graphics, & Media Playback windows": {
                    "raptor": [
                        "'browsertime 'youtube-playback",
                        "!-32 !10-64 'windows 'shippable",
                        "!bytecode",
                        "!profil",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                    "talos": [
                        "'talos 'svgr | 'bcv | 'webgl",
                        "!-32 !10-64 'windows 'shippable",
                        "!profil",
                        "!swr",
                    ],
                },
                "Machine Learning windows": {
                    "perftest": [
                        "'perftest '-ml-",
                        "'windows",
                        "!chrom",
                        "!safari",
                        "!m-car",
                        "!safari-tp",
                    ],
                },
            },
            [
                "Benchmarks desktop",
                "Responsiveness windows firefox no-fisson",
                "Pageload (live) android",
                "Talos desktop live-sites",
                "Talos android",
                "Graphics, & Media Playback windows live-sites",
                "Graphics, & Media Playback android no-fission",
            ],
        ),
    ],
)
def test_category_expansion(
    category_options, expected_counts, unique_categories, missing
):
    # Set the categories, and variants to expand
    PerfParser.categories = TEST_CATEGORIES
    PerfParser.variants = TEST_VARIANTS

    # Expand the categories, then either check if the unique_categories,
    # exist or are missing from the categories
    expanded_cats = PerfParser.get_categories(**category_options)

    assert len(expanded_cats) == expected_counts
    assert not any([expanded_cats.get(ucat, None) is not None for ucat in missing])
    assert all(
        [expanded_cats.get(ucat, None) is not None for ucat in unique_categories.keys()]
    )

    # Ensure that the queries are as expected
    for cat_name, cat_query in unique_categories.items():
        # Don't use get here because these fields should always exist
        assert cat_query == expanded_cats[cat_name]["queries"]


@pytest.mark.parametrize(
    "category_options, call_counts",
    [
        (
            {},
            0,
        ),
        (
            {"non_pgo": True},
            88,
        ),
    ],
)
def test_category_expansion_with_non_pgo_flag(category_options, call_counts):
    PerfParser.categories = TEST_CATEGORIES
    PerfParser.variants = TEST_VARIANTS

    expanded_cats = PerfParser.get_categories(**category_options)

    non_shippable_count = 0
    for cat_name in expanded_cats:
        queries = str(expanded_cats[cat_name].get("queries", None))
        if "!shippable !nightlyasrelease" in queries and "'shippable" not in queries:
            non_shippable_count += 1

    assert non_shippable_count == call_counts


@pytest.mark.parametrize(
    "options, call_counts, log_ind, expected_log_message",
    [
        (
            {},
            [10, 2, 2, 10, 2, 1],
            2,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=13\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=13\n"
            ),
        ),
        (
            {"query": "'Pageload 'linux 'firefox"},
            [10, 2, 2, 10, 2, 1],
            2,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=13\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=13\n"
            ),
        ),
        (
            {"cached_revision": "cached_base_revision"},
            [10, 1, 1, 10, 2, 0],
            2,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=cached_base_revision&newRev=revision&"
                "baseRepo=try&newRepo=try&framework=13\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=cached_base_revision&newProject=try&newRevision=revision&framework=13\n"
            ),
        ),
        (
            {"dry_run": True},
            [10, 1, 1, 4, 2, 0],
            2,
            (
                "If you need any help, you can find us in the #perf-help Matrix channel:\n"
                "https://matrix.to/#/#perf-help:mozilla.org\n"
            ),
        ),
        (
            {"show_all": True},
            [1, 2, 2, 8, 2, 1],
            0,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=1\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=1\n"
            ),
        ),
        (
            {"show_all": True, "query": "'shippable !32 speedometer 'firefox"},
            [1, 2, 2, 8, 2, 1],
            0,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=1\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=1\n"
            ),
        ),
        (
            {"single_run": True},
            [10, 1, 1, 4, 2, 0],
            2,
            (
                "If you need any help, you can find us in the #perf-help Matrix channel:\n"
                "https://matrix.to/#/#perf-help:mozilla.org\n"
            ),
        ),
        (
            {"detect_changes": True},
            [11, 2, 2, 10, 2, 1],
            2,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=13\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=13\n"
            ),
        ),
        (
            {"tests": ["amazon"]},
            [7, 2, 2, 10, 2, 1],
            2,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=13\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=13\n"
            ),
        ),
        (
            {"tests": ["amazon"], "alert": "000"},
            [0, 2, 2, 9, 2, 1],
            1,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=1\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=1\n"
            ),
        ),
        (
            {"tests": ["amazon"], "show_all": True},
            [1, 2, 2, 8, 2, 1],
            0,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                "baseRev=revision&newRev=revision&baseRepo=try&newRepo=try&framework=1\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                "Revision=revision&newProject=try&newRevision=revision&framework=1\n"
            ),
        ),
    ],
)
@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
def test_full_run(options, call_counts, log_ind, expected_log_message):
    with mock.patch("tryselect.selectors.perf.push_to_try") as ptt, mock.patch(
        "tryselect.selectors.perf.run_fzf"
    ) as fzf, mock.patch(
        "tryselect.selectors.perf.get_repository_object", new=mock.MagicMock()
    ), mock.patch(
        "tryselect.selectors.perf.LogProcessor.revision",
        new_callable=mock.PropertyMock,
        return_value="revision",
    ) as logger, mock.patch(
        "tryselect.selectors.perf.PerfParser.check_cached_revision",
    ) as ccr, mock.patch(
        "tryselect.selectors.perf.PerfParser.save_revision_treeherder"
    ) as srt, mock.patch(
        "tryselect.selectors.perf.print",
    ) as perf_print, mock.patch(
        "tryselect.selectors.perf.PerfParser.set_categories_for_test"
    ) as tests_mock, mock.patch(
        "tryselect.selectors.perf.requests"
    ) as requests_mock:

        def test_mock_func(*args, **kwargs):
            """Used for testing any --test functionality."""
            PerfParser.categories = {
                "test 1": {
                    "query": {"raptor": ["test 1"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
                "test 2": {
                    "query": {"raptor": ["test 2"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
                "amazon": {
                    "query": {"raptor": ["amazon"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
            }
            fzf.side_effect = [
                ["", ["test 1 windows firefox"]],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
            ]
            return ["task 1", "task 2", "amazon"]

        tests_mock.side_effect = test_mock_func

        get_mock = mock.MagicMock()
        get_mock.status_code.return_value = 200
        get_mock.json.return_value = {"tasks": ["task 1", "task 2"]}
        requests_mock.get.return_value = get_mock

        fzf_side_effects = [
            ["", ["Benchmarks linux"]],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", ["Perftest Change Detector"]],
        ]
        # Number of side effects for fzf should always be greater than
        # or equal to the number of calls expected
        assert len(fzf_side_effects) >= call_counts[0]

        fzf.side_effect = fzf_side_effects
        ccr.return_value = options.get("cached_revision", "")

        with category_reset():
            run(**options)

        assert fzf.call_count == call_counts[0]
        assert ptt.call_count == call_counts[1]
        assert logger.call_count == call_counts[2]
        assert perf_print.call_count == call_counts[3]
        assert ccr.call_count == call_counts[4]
        assert srt.call_count == call_counts[5]
        assert perf_print.call_args_list[log_ind][0][0] == expected_log_message


@pytest.mark.parametrize(
    "options, call_counts, log_ind, expected_log_message",
    [
        (
            {"tests": ["amazon"], "show_all": True},
            [1, 2, 2, 8, 2, 1],
            0,
            (
                "\n!!!NOTE!!!\n You'll be able to find a performance comparison "
                "here once the tests are complete (ensure you select the right framework):\n"
                " https://perf.compare/compare-results?"
                f"baseHash={hash('mockedmocked')}&newHash={hash('mocked100')}&baseRepo=try&newRepo=try&framework=1\n\n"
                " The old comparison tool is still available at this URL:\n"
                " https://treeherder.mozilla.org/perfherder/compare?originalProject=try&original"
                f"Hash={hash('mockedmocked')}&newProject=try&newHash={hash('mocked100')}&framework=1\n"
            ),
        ),
    ],
)
@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
def test_full_run_git_migration(options, call_counts, log_ind, expected_log_message):
    with mock.patch("tryselect.selectors.perf.push_to_try") as ptt, mock.patch(
        "tryselect.selectors.perf.run_fzf"
    ) as fzf, mock.patch(
        "tryselect.selectors.perf.get_repository_object", new=mock.MagicMock()
    ), mock.patch(
        "tryselect.selectors.perf.LogProcessor.revision",
        new_callable=mock.PropertyMock,
        return_value="revision",
    ) as logger, mock.patch(
        "tryselect.selectors.perf.PerfParser.check_cached_revision",
    ) as ccr, mock.patch(
        "tryselect.selectors.perf.PerfParser.save_revision_treeherder"
    ) as srt, mock.patch(
        "tryselect.selectors.perf.print",
    ) as perf_print, mock.patch(
        "tryselect.selectors.perf.PerfParser.set_categories_for_test"
    ) as tests_mock, mock.patch(
        "tryselect.selectors.perf.requests"
    ) as requests_mock, mock.patch(
        "tryselect.selectors.perf.HG_TO_GIT_MIGRATION_COMPLETE", return_value=True
    ), mock.patch(
        "tryselect.selectors.perf.ON_GIT", return_value=True
    ), mock.patch(
        "tryselect.selectors.perf.time.time", return_value=100
    ), mock.patch(
        "tryselect.selectors.perf.subprocess.getoutput", return_value="mocked"
    ):

        def test_mock_func(*args, **kwargs):
            """Used for testing any --test functionality."""
            PerfParser.categories = {
                "test 1": {
                    "query": {"raptor": ["test 1"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
                "test 2": {
                    "query": {"raptor": ["test 2"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
                "amazon": {
                    "query": {"raptor": ["amazon"]},
                    "suites": ["raptor"],
                    "tasks": [],
                    "description": "",
                },
            }
            fzf.side_effect = [
                ["", ["test 1 windows firefox"]],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
                ["", TASKS],
            ]
            return ["task 1", "task 2", "amazon"]

        tests_mock.side_effect = test_mock_func

        get_mock = mock.MagicMock()
        get_mock.status_code.return_value = 200
        get_mock.json.return_value = {"tasks": ["task 1", "task 2"]}
        requests_mock.get.return_value = get_mock

        fzf_side_effects = [
            ["", ["Benchmarks linux"]],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", ["Perftest Change Detector"]],
        ]
        # Number of side effects for fzf should always be greater than
        # or equal to the number of calls expected
        assert len(fzf_side_effects) >= call_counts[0]

        fzf.side_effect = fzf_side_effects
        ccr.return_value = options.get("cached_revision", "")

        with category_reset():
            run(**options)

        assert fzf.call_count == call_counts[0]
        assert ptt.call_count == call_counts[1]
        assert logger.call_count == call_counts[2]
        assert perf_print.call_count == call_counts[3]
        assert ccr.call_count == call_counts[4]
        assert srt.call_count == call_counts[5]
        assert perf_print.call_args_list[log_ind][0][0] == expected_log_message


@pytest.mark.parametrize(
    "options, call_counts, log_ind, expected_log_message, expected_failure",
    [
        (
            {"detect_changes": True},
            [11, 0, 0, 2, 1],
            1,
            (
                "Executing raptor queries: 'browsertime 'benchmark, !clang 'linux "
                "'shippable, !bytecode, !live, !profil, !chrom, !safari, !m-car, !safari-tp"
            ),
            InvalidRegressionDetectorQuery,
        ),
    ],
)
@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
def test_change_detection_task_injection_failure(
    options,
    call_counts,
    log_ind,
    expected_log_message,
    expected_failure,
):
    setup_perfparser()

    with mock.patch("tryselect.selectors.perf.push_to_try") as ptt, mock.patch(
        "tryselect.selectors.perf.run_fzf"
    ) as fzf, mock.patch(
        "tryselect.selectors.perf.get_repository_object", new=mock.MagicMock()
    ), mock.patch(
        "tryselect.selectors.perf.LogProcessor.revision",
        new_callable=mock.PropertyMock,
        return_value="revision",
    ) as logger, mock.patch(
        "tryselect.selectors.perf.PerfParser.check_cached_revision"
    ) as ccr, mock.patch(
        "tryselect.selectors.perf.print",
    ) as perf_print:
        fzf_side_effects = [
            ["", ["Benchmarks linux"]],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
            ["", TASKS],
        ]
        assert len(fzf_side_effects) >= call_counts[0]

        fzf.side_effect = fzf_side_effects

        with pytest.raises(expected_failure):
            run(**options)

        assert fzf.call_count == call_counts[0]
        assert ptt.call_count == call_counts[1]
        assert logger.call_count == call_counts[2]
        assert perf_print.call_count == call_counts[3]
        assert ccr.call_count == call_counts[4]
        assert perf_print.call_args_list[log_ind][0][0] == expected_log_message


@pytest.mark.parametrize(
    "query, should_fail",
    [
        (
            {
                "query": {
                    # Raptor has all variants available so it
                    # should fail on this category
                    "raptor": ["browsertime 'live 'no-fission"],
                }
            },
            True,
        ),
        (
            {
                "query": {
                    # Awsy has no variants defined so it shouldn't fail
                    # on a query like this
                    "awsy": ["browsertime 'live 'no-fission"],
                }
            },
            False,
        ),
    ],
)
def test_category_rules(query, should_fail):
    # Set the categories, and variants to expand
    PerfParser.categories = {"test-live": query}
    PerfParser.variants = TEST_VARIANTS

    if should_fail:
        with pytest.raises(InvalidCategoryException):
            PerfParser.run_category_checks()
    else:
        assert PerfParser.run_category_checks()

    # Reset the categories, and variants to expand
    PerfParser.categories = TEST_CATEGORIES
    PerfParser.variants = TEST_VARIANTS


@pytest.mark.parametrize(
    "apk_name, apk_content, should_fail, failure_message",
    [
        (
            "real-file",
            "file-content",
            False,
            None,
        ),
        ("bad-file", None, True, "Path does not exist:"),
    ],
)
def test_apk_upload(apk_name, apk_content, should_fail, failure_message):
    with mock.patch("tryselect.selectors.perf.subprocess") as _, mock.patch(
        "tryselect.selectors.perf.shutil"
    ) as _:
        temp_dir = None
        try:
            temp_dir = tempfile.mkdtemp()
            sample_apk = pathlib.Path(temp_dir, apk_name)
            if apk_content is not None:
                with sample_apk.open("w") as f:
                    f.write(apk_content)

            if should_fail:
                with pytest.raises(Exception) as exc_info:
                    PerfParser.setup_apk_upload("browsertime", str(sample_apk))
                assert failure_message in str(exc_info)
            else:
                PerfParser.setup_apk_upload("browsertime", str(sample_apk))
        finally:
            if temp_dir is not None:
                shutil.rmtree(temp_dir)


@pytest.mark.parametrize(
    "args, load_data, return_value, call_counts, exists_cache_file",
    [
        (
            (
                [],
                "base_commit",
            ),
            {
                "base_commit": [
                    {
                        "base_revision_treeherder": "2b04563b5",
                        "date": "2023-03-31",
                        "tasks": [],
                    },
                ],
            },
            "2b04563b5",
            [1, 0],
            True,
        ),
        (
            (
                ["task-a"],
                "subset_base_commit",
            ),
            {
                "subset_base_commit": [
                    {
                        "base_revision_treeherder": "2b04563b5",
                        "date": "2023-03-31",
                        "tasks": ["task-a", "task-b"],
                    },
                ],
            },
            "2b04563b5",
            [1, 0],
            True,
        ),
        (
            ([], "not_exist_cached_base_commit"),
            {
                "base_commit": [
                    {
                        "base_revision_treeherder": "2b04563b5",
                        "date": "2023-03-31",
                        "tasks": [],
                    },
                ],
            },
            None,
            [1, 0],
            True,
        ),
        (
            (
                ["task-a", "task-b"],
                "superset_base_commit",
            ),
            {
                "superset_base_commit": [
                    {
                        "base_revision_treeherder": "2b04563b5",
                        "date": "2023-03-31",
                        "tasks": ["task-a"],
                    },
                ],
            },
            None,
            [1, 0],
            True,
        ),
        (
            ([], None),
            {},
            None,
            [1, 1],
            True,
        ),
        (
            ([], None),
            {},
            None,
            [0, 0],
            False,
        ),
    ],
)
def test_check_cached_revision(
    args, load_data, return_value, call_counts, exists_cache_file
):
    with mock.patch("tryselect.selectors.perf.json.load") as load, mock.patch(
        "tryselect.selectors.perf.json.dump"
    ) as dump, mock.patch(
        "tryselect.selectors.perf.pathlib.Path.is_file"
    ) as is_file, mock.patch(
        "tryselect.selectors.perf.pathlib.Path.open"
    ):
        load.return_value = load_data
        is_file.return_value = exists_cache_file
        result = PerfParser.check_cached_revision(*args)

        assert load.call_count == call_counts[0]
        assert dump.call_count == call_counts[1]
        assert result == return_value


@pytest.mark.parametrize(
    "args, call_counts, exists_cache_file",
    [
        (
            ["base_commit", "base_revision_treeherder"],
            [0, 1],
            False,
        ),
        (
            ["base_commit", "base_revision_treeherder"],
            [1, 1],
            True,
        ),
    ],
)
def test_save_revision_treeherder(args, call_counts, exists_cache_file):
    with mock.patch("tryselect.selectors.perf.json.load") as load, mock.patch(
        "tryselect.selectors.perf.json.dump"
    ) as dump, mock.patch(
        "tryselect.selectors.perf.pathlib.Path.is_file"
    ) as is_file, mock.patch(
        "tryselect.selectors.perf.pathlib.Path.open"
    ):
        is_file.return_value = exists_cache_file

        PerfParser.push_info.base_revision = "base_revision_treeherder"
        PerfParser.save_revision_treeherder(TASKS, args[0])

        assert load.call_count == call_counts[0]
        assert dump.call_count == call_counts[1]


@pytest.mark.parametrize(
    "total_tasks, options, call_counts, expected_log_message, expected_failure",
    [
        (
            MAX_PERF_TASKS + 1,
            {},
            [1, 0, 0, 1],
            (
                "\n\n----------------------------------------------------------------------------------------------\n"
                f"You have selected {MAX_PERF_TASKS+1} total test runs! (selected tasks({MAX_PERF_TASKS+1}) * rebuild"
                f" count(1) \nThese tests won't be triggered as the current maximum for a single ./mach try "
                f"perf run is {MAX_PERF_TASKS}. \nIf this was unexpected, please file a bug in Testing :: Performance."
                "\n----------------------------------------------------------------------------------------------\n\n"
            ),
            True,
        ),
        (
            MAX_PERF_TASKS,
            {"show_all": True},
            [9, 0, 0, 8],
            (
                "For more information on the performance tests, see our "
                "PerfDocs here:\nhttps://firefox-source-docs.mozilla.org/testing/perfdocs/"
            ),
            False,
        ),
        (
            int((MAX_PERF_TASKS + 2) / 2),
            {
                "show_all": True,
                "try_config_params": {"try_task_config": {"rebuild": 2}},
            },
            [1, 0, 0, 1],
            (
                "\n\n----------------------------------------------------------------------------------------------\n"
                f"You have selected {int((MAX_PERF_TASKS + 2) / 2) * 2} total test runs! (selected tasks("
                f"{int((MAX_PERF_TASKS + 2) / 2)}) * rebuild"
                f" count(2) \nThese tests won't be triggered as the current maximum for a single ./mach try "
                f"perf run is {MAX_PERF_TASKS}. \nIf this was unexpected, please file a bug in Testing :: Performance."
                "\n----------------------------------------------------------------------------------------------\n\n"
            ),
            True,
        ),
        (0, {}, [1, 0, 0, 1], ("No tasks selected"), True),
    ],
)
def test_max_perf_tasks(
    total_tasks,
    options,
    call_counts,
    expected_log_message,
    expected_failure,
):
    setup_perfparser()

    with mock.patch("tryselect.selectors.perf.push_to_try") as ptt, mock.patch(
        "tryselect.selectors.perf.print",
    ) as perf_print, mock.patch(
        "tryselect.selectors.perf.LogProcessor.revision",
        new_callable=mock.PropertyMock,
        return_value="revision",
    ), mock.patch(
        "tryselect.selectors.perf.PerfParser.perf_push_to_try",
        new_callable=mock.MagicMock,
    ) as perf_push_to_try_mock, mock.patch(
        "tryselect.selectors.perf.PerfParser.get_perf_tasks"
    ) as get_perf_tasks_mock, mock.patch(
        "tryselect.selectors.perf.PerfParser.get_tasks"
    ) as get_tasks_mock, mock.patch(
        "tryselect.selectors.perf.run_fzf"
    ) as fzf, mock.patch(
        "tryselect.selectors.perf.fzf_bootstrap", return_value=mock.MagicMock()
    ):
        tasks = ["a-task"] * total_tasks
        get_tasks_mock.return_value = tasks
        get_perf_tasks_mock.return_value = tasks, [], []

        PerfParser.push_info.finished_run = not expected_failure
        run(**options)

        assert perf_push_to_try_mock.call_count == 0 if expected_failure else 1
        assert ptt.call_count == call_counts[1]
        assert perf_print.call_count == call_counts[3]
        assert fzf.call_count == 0
        assert perf_print.call_args_list[-1][0][0] == expected_log_message


@pytest.mark.parametrize(
    "try_config, selected_tasks, expected_try_config",
    [
        (
            {"use-artifact-builds": True, "disable-pgo": True},
            ["some-android-task"],
            {"use-artifact-builds": False},
        ),
        (
            {"use-artifact-builds": True},
            ["some-desktop-task"],
            {"use-artifact-builds": True},
        ),
        (
            {"use-artifact-builds": False},
            ["some-android-task"],
            {"use-artifact-builds": False},
        ),
        (
            {"use-artifact-builds": True, "disable-pgo": True},
            ["some-desktop-task", "some-android-task"],
            {"use-artifact-builds": False},
        ),
    ],
)
def test_artifact_mode_autodisable(try_config, selected_tasks, expected_try_config):
    PerfParser.setup_try_config({"try_task_config": try_config}, [], selected_tasks)
    assert (
        try_config["use-artifact-builds"] == expected_try_config["use-artifact-builds"]
    )


def test_build_category_description():
    base_cmd = ["--preview", '-t "{+f}"']

    with mock.patch("tryselect.selectors.perf.json.dump") as dump:
        PerfParser.build_category_description(base_cmd, "")

        assert dump.call_count == 1
        assert str(base_cmd).count("-d") == 1
        assert str(base_cmd).count("-l") == 1


@pytest.mark.parametrize(
    "options, call_count",
    [
        ({}, [1, 1, 2]),
        ({"show_all": True}, [0, 0, 1]),
    ],
)
def test_preview_description(options, call_count):
    with mock.patch("tryselect.selectors.perf.PerfParser.perf_push_to_try"), mock.patch(
        "tryselect.selectors.perf.fzf_bootstrap"
    ), mock.patch(
        "tryselect.selectors.perf.PerfParser.get_perf_tasks"
    ) as get_perf_tasks, mock.patch(
        "tryselect.selectors.perf.PerfParser.get_tasks"
    ), mock.patch(
        "tryselect.selectors.perf.PerfParser.build_category_description"
    ) as bcd:
        get_perf_tasks.return_value = [], [], []

        run(**options)

        assert bcd.call_count == call_count[0]

    base_cmd = ["--preview", '-t "{+f}"']
    option = base_cmd[base_cmd.index("--preview") + 1].split(" ")
    description, line = None, None
    if call_count[0] == 1:
        PerfParser.build_category_description(base_cmd, "")
        option = base_cmd[base_cmd.index("--preview") + 1].split(" ")
        description = option[option.index("-d") + 1]
        line = "Current line"

    taskfile = option[option.index("-t") + 1]

    with mock.patch("tryselect.selectors.perf_preview.open"), mock.patch(
        "tryselect.selectors.perf_preview.pathlib.Path.open"
    ), mock.patch("tryselect.selectors.perf_preview.json.load") as load, mock.patch(
        "tryselect.selectors.perf_preview.print"
    ) as preview_print:
        load.return_value = {line: "test description"}

        plain_display(taskfile, description, line)

        assert load.call_count == call_count[1]
        assert preview_print.call_count == call_count[2]


@pytest.mark.parametrize(
    "tests, tasks_found, categories_produced",
    [
        (["amazon"], 2, 1),
        (["speedometer"], 1, 1),
        (["webaudio", "speedometer"], 3, 2),
        (["webaudio", "tp5n"], 3, 2),
        (["xperf"], 1, 1),
        (["awsy"], 1, 1),
        (["awsy", "tp5n", "amazon"], 4, 3),
        (["awsy", "tp5n", "xperf"], 2, 3),
        (["non-existent"], 0, 0),
        (["perftest_finder_ml_engine_perf.js"], 1, 1),
        (["perftest/test/finder/path"], 1, 1),
        (["perftest/test/finder/path/perftest_finder_ml_engine_perf.js"], 1, 1),
    ],
)
def test_test_selection(tests, tasks_found, categories_produced):
    with mock.patch(
        "tryselect.selectors.perfselector.classification.pathlib"
    ), mock.patch(
        "tryselect.selectors.perfselector.classification.json"
    ) as mocked_json:

        def mocked_json_load(*args, **kwargs):
            return {
                "suites": {
                    "xperf": {
                        "tests": ["tp5n"],
                    }
                }
            }

        mocked_json.load.side_effect = mocked_json_load

        with category_reset():
            all_tasks = PerfParser.set_categories_for_test(FTG_SAMPLE_PATH, tests)

            assert len(all_tasks) == tasks_found
            assert len(PerfParser.categories) == categories_produced


def test_unknown_framework():
    setup_perfparser()
    PerfParser.get_majority_framework(["unknown"])
    assert PerfParser.push_info.framework == 1


if __name__ == "__main__":
    mozunit.main()
