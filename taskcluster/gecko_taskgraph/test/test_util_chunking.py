# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import re
from itertools import combinations
from unittest.mock import patch

import pytest
from mozunit import main

from gecko_taskgraph.util import chunking

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module")
def mock_manifest_runtimes():
    """Deterministically produce a list of simulated manifest runtimes.

    Args:
        manifests (list): list of manifests against which simulated manifest
            runtimes would be paired up to.

    Returns:
        dict of manifest data paired with a float value representing runtime.
    """

    def inner(manifests):
        manifests = sorted(manifests)
        # Generate deterministic runtime data.
        runtimes = [(i / 10) ** (i / 10) for i in range(len(manifests))]
        return dict(zip(manifests, runtimes))

    return inner


@pytest.fixture(scope="module")
def mock_manifest_runtimes_file():
    """Pre-populate the _load_manifest_runtimes_data memoize cache with test data."""
    mock_data = {
        "metadata": {
            "date": "2026-01-01",
            "repository": "mozilla-central",
            "generatedAt": "2026-01-01T00:00:00.000Z",
            "manifestCount": 3,
            "jobNameCount": 3,
        },
        "jobNames": [
            "test-linux2404-64-shippable/opt-crashtest",
            "test-linux2404-64-shippable/opt-reftest",
            "test-linux2404-64-shippable/opt-xpcshell",
            "test-linux2404-64-shippable/opt-web-platform-tests",
            "test-windows11-64-25h2-shippable/opt-crashtest",
            "test-windows11-64-25h2-shippable/opt-reftest",
            "test-windows11-64-25h2-shippable/opt-xpcshell",
            "test-windows11-64-25h2-shippable/opt-web-platform-tests",
            "test-android-em-14-x86_64-shippable/opt-geckoview-crashtest",
            "test-android-em-14-x86_64-shippable/opt-geckoview-reftest",
            "test-android-em-14-x86_64-shippable/opt-geckoview-xpcshell",
            "test-android-em-14-x86_64-shippable/opt-geckoview-web-platform-tests",
        ],
        "manifests": {
            "test/manifest1.toml": {
                "jobs": [0, 4, 8],
                "runtimes": [[1000, 1100], [1200], [1300, 1400, 1500]],
            },
            "test/manifest2.toml": {
                "jobs": [1, 5, 9],
                "runtimes": [[2000], [2100, 2200], [2300]],
            },
            "test/manifest3.toml": {
                "jobs": [2, 6, 10],
                "runtimes": [[3000, 3100, 3200], [3300], [3400]],
            },
            "test/manifest4.toml": {
                "jobs": [3, 7, 11],
                "runtimes": [[4000], [4100], [4200, 4300]],
            },
        },
    }

    with patch.object(chunking, "_load_manifest_runtimes_data", return_value=mock_data):
        chunking.get_runtimes.cache_clear()
        yield
    chunking.get_runtimes.cache_clear()


@pytest.fixture(scope="module")
def unchunked_manifests():
    """Produce a list of unchunked manifests to be consumed by test method.

    Args:
        length (int, optional): number of path elements to keep.
        cutoff (int, optional): number of generated test paths to remove
            from the test set if user wants to limit the number of paths.

    Returns:
        list: list of test paths.
    """
    data = ["blueberry", "nashi", "peach", "watermelon"]

    def inner(suite, length=2, cutoff=0):
        if "web-platform" in suite:
            suffix = ""
            prefix = "/"
        elif "reftest" in suite:
            suffix = ".list"
            prefix = ""
        else:
            suffix = ".ini"
            prefix = ""
        return [prefix + "/".join(p) + suffix for p in combinations(data, length)][
            cutoff:
        ]

    return inner


@pytest.fixture(scope="module")
def mock_task_definition():
    """Builds a mock task definition for use in testing.

    Args:
        os_name (str): represents the os.
        os_version (str): represents the os version
        bits (int): software bits.
        build_type (str): opt or debug.
        build_attrs (list, optional): specify build attribute(s)
        variants (list, optional): specify runtime variant(s)

    Returns:
        dict: mocked task definition.
    """

    def inner(os_name, os_version, bits, build_type, build_attrs=None, variants=None):
        setting = {
            "platform": {
                "arch": str(bits),
                "os": {
                    "name": os_name,
                    "version": os_version,
                },
            },
            "build": {
                "type": build_type,
            },
            "runtime": {},
        }

        # Optionally set build attributes and runtime variants.
        if build_attrs:
            if isinstance(build_attrs, str):
                build_attrs = [build_attrs]
            for attr in build_attrs:
                setting["build"][attr] = True

        if variants:
            if isinstance(variants, str):
                variants = [variants]
            for variant in variants:
                setting["runtime"][variant] = True
        return {"test-name": "foo", "test-setting": setting}

    return inner


@pytest.fixture(scope="module")
def mock_mozinfo():
    """Returns a mocked mozinfo object, similar to guess_mozinfo_from_task().

    Args:
        os (str): typically one of 'win, linux, mac, android'.
        processor (str): processor architecture.
        asan (bool, optional): addressanitizer build.
        bits (int, optional): defaults to 64.
        ccov (bool, optional): code coverage build.
        debug (bool, optional): debug type build.
        fission (bool, optional): process fission.
        headless (bool, optional): headless browser testing without displays.
        tsan (bool, optional): threadsanitizer build.

    Returns:
        dict: Dictionary mimickign the results from guess_mozinfo_from_task.
    """

    def inner(
        os,
        processor,
        asan=False,
        bits=64,
        ccov=False,
        debug=False,
        fission=False,
        headless=False,
        tsan=False,
        tag="[]",
        mingwclang=False,
        nightly_build=False,
        repo="try",
        crashreporter=False,
    ):
        return {
            "os": os,
            "processor": processor,
            "toolkit": "",
            "asan": asan,
            "bits": bits,
            "ccov": ccov,
            "debug": debug,
            "e10s": True,
            "fission": fission,
            "headless": headless,
            "tsan": tsan,
            "appname": "firefox",
            "condprof": False,
            "canvas": False,
            "webgpu": False,
            "webcodecs": False,
            "eme": False,
            "webrtc": False,
            "privatebrowsing": False,
            "tag": tag,
        }

    return inner


@pytest.mark.parametrize(
    "params,exception",
    [
        [("win", "7", 32, "opt"), None],
        [("win", "10", 64, "opt"), None],
        [("linux", "1804", 64, "debug"), None],
        [("macosx", "1015", 64, "debug"), None],
        [("macosx", "1100", 64, "opt"), None],
        [("android", "13.0", 64, "debug"), None],
        [("and", "", 64, "debug"), ValueError],
        [("", "", 64, "opt"), ValueError],
        [("linux", "1804", 64, "opt", ["ccov"]), None],
        [("linux", "1804", 64, "opt", ["asan"]), None],
        [("win", "10", 64, "opt", ["tsan"]), None],
        [("mac", "1100", 64, "opt", ["ccov"]), None],
        [("android", "13.0", 64, "opt", None, ["fission"]), None],
        [("win", "10", "aarch64", "opt"), None],
    ],
)
def test_guess_mozinfo_from_task(params, exception, mock_task_definition):
    """Tests the mozinfo guessing process."""
    # Set up a mocked task object.
    task = mock_task_definition(*params)

    if exception:
        with pytest.raises(exception):
            result = chunking.guess_mozinfo_from_task(task)
    else:
        expected_toolkits = {
            "android": "android",
            "linux": "gtk",
            "mac": "cocoa",
            "win": "windows",
        }
        result = chunking.guess_mozinfo_from_task(task)
        setting = task["test-setting"]

        assert str(result["bits"]) in setting["platform"]["arch"]
        assert result["os"] in ("android", "linux", "mac", "win")
        assert result["os"] in setting["platform"]["os"]["name"]
        assert result["toolkit"] == expected_toolkits[result["os"]]

        # Ensure the outcome of special build variants being present match what
        # guess_mozinfo_from_task method returns for these attributes.
        assert ("asan" in setting["build"]) == result["asan"]
        assert ("tsan" in setting["build"]) == result["tsan"]
        assert ("ccov" in setting["build"]) == result["ccov"]

        # Ensure runtime variants match
        assert ("fission" in setting["runtime"]) == result["fission"]
        assert ("1proc" in setting["runtime"]) != result["e10s"]


@pytest.mark.parametrize(
    "platform,suite",
    [
        ("linux2404-64-shippable/opt", "crashtest"),
        ("linux2404-64-shippable/opt", "reftest"),
        ("linux2404-64-shippable/opt", "web-platform-tests"),
        ("linux2404-64-shippable/opt", "xpcshell"),
        ("windows11-64-25h2-shippable/opt", "crashtest"),
        ("windows11-64-25h2-shippable/opt", "reftest"),
        ("windows11-64-25h2-shippable/opt", "web-platform-tests"),
        ("windows11-64-25h2-shippable/opt", "xpcshell"),
        ("android-em-14-x86_64-shippable/opt", "crashtest"),
        ("android-em-14-x86_64-shippable/opt", "reftest"),
        ("android-em-14-x86_64-shippable/opt", "web-platform-tests"),
        ("android-em-14-x86_64-shippable/opt", "xpcshell"),
    ],
)
def test_get_runtimes(platform, suite, mock_manifest_runtimes_file):
    """Tests that runtime information is returned for known good configurations."""
    # Clear get_runtimes cache so each parametrized test gets fresh results
    chunking.get_runtimes.cache_clear()

    result = chunking.get_runtimes(platform, suite)
    assert isinstance(result, dict)
    # With our mock data, we should get some results for all platforms
    assert len(result) > 0


@pytest.mark.parametrize(
    "platform,suite,exception",
    [
        ("unix", "", TypeError),
        ("", "", TypeError),
        ("", "nonexistent_suite", TypeError),
    ],
)
def test_get_runtimes_invalid(platform, suite, exception):
    """Ensure get_runtimes() method raises an exception if improper request is made."""
    with pytest.raises(exception):
        chunking.get_runtimes(platform, suite)


@pytest.mark.parametrize(
    "suite",
    [
        "web-platform-tests",
        "web-platform-tests-reftest",
        "web-platform-tests-wdspec",
        "web-platform-tests-crashtest",
    ],
)
@pytest.mark.parametrize("chunks", [1, 3, 6, 20])
def test_mock_chunk_manifests_wpt(unchunked_manifests, suite, chunks):
    """Tests web-platform-tests and its subsuites chunking process."""
    # Setup.
    manifests = unchunked_manifests(suite)

    # Generate the expected results, by generating list of indices that each
    # manifest should go into and then appending each item to that index.
    # This method is intentionally different from the way chunking.py performs
    # chunking for cross-checking.
    expected = [[] for _ in range(chunks)]
    indexed = zip(manifests, list(range(0, chunks)) * len(manifests))
    for i in indexed:
        expected[i[1]].append(i[0])

    # Call the method under test on unchunked manifests.
    chunked_manifests = chunking.chunk_manifests(suite, "unix", chunks, manifests)

    # Assertions and end test.
    assert chunked_manifests
    if chunks > len(manifests):
        # If chunk count exceeds number of manifests, not all chunks will have
        # manifests.
        with pytest.raises(AssertionError):
            assert all(chunked_manifests)
    else:
        assert all(chunked_manifests)
        minimum = min(len(c) for c in chunked_manifests)
        maximum = max(len(c) for c in chunked_manifests)
        assert maximum - minimum <= 1
        assert expected == chunked_manifests


@pytest.mark.parametrize(
    "suite",
    [
        "mochitest-devtools-chrome",
        "mochitest-browser-chrome",
        "mochitest-plain",
        "mochitest-chrome",
        "xpcshell",
    ],
)
@pytest.mark.parametrize("chunks", [1, 3, 6, 20])
def test_mock_chunk_manifests(
    mock_manifest_runtimes, unchunked_manifests, suite, chunks
):
    """Tests non-WPT tests and its subsuites chunking process."""
    # Setup.
    manifests = unchunked_manifests(suite)

    # Call the method under test on unchunked manifests.
    chunked_manifests = chunking.chunk_manifests(suite, "unix", chunks, manifests)

    # Assertions and end test.
    assert chunked_manifests
    if chunks > len(manifests):
        # If chunk count exceeds number of manifests, not all chunks will have
        # manifests.
        with pytest.raises(AssertionError):
            assert all(chunked_manifests)
    else:
        assert all(chunked_manifests)


@pytest.mark.parametrize(
    "suite",
    [
        "web-platform-tests",
        "web-platform-tests-reftest",
        "xpcshell",
        "mochitest-plain",
        "mochitest-devtools-chrome",
        "mochitest-browser-chrome",
        "mochitest-chrome",
    ],
)
@pytest.mark.parametrize(
    "platform",
    [
        ("mac", "x86_64"),
        ("win", "x86_64"),
        ("win", "x86"),
        ("win", "aarch64"),
        ("linux", "x86_64"),
        ("linux", "x86"),
    ],
)
def test_get_manifests(suite, platform, mock_mozinfo):
    """Tests the DefaultLoader class' ability to load manifests."""
    mozinfo = mock_mozinfo(*platform)

    loader = chunking.DefaultLoader([])
    manifests = loader.get_manifests(suite, frozenset(mozinfo.items()))

    assert manifests
    assert manifests["active"]
    if "web-platform" in suite:
        assert manifests["skipped"] == []
    else:
        assert manifests["skipped"]

    items = manifests["active"]
    if suite == "xpcshell":
        assert all([re.search(r"xpcshell(.*)?(.ini|.toml)", m) for m in items])
    if "mochitest" in suite:
        assert all([
            re.search(r"(perftest|mochitest|chrome|browser).*(.ini|.toml)", m)
            for m in items
        ])
    if "web-platform" in suite:
        # Non-testharness wpt suites (e.g. reftest) are not subsuite-partitioned,
        # so they include deeper manifests such as the webgpu CTS reftests dir.
        assert all([m.startswith("/") for m in items])


@pytest.mark.parametrize(
    "suite",
    [
        "mochitest-devtools-chrome",
        "mochitest-browser-chrome",
        "mochitest-plain",
        "mochitest-chrome",
        "web-platform-tests",
        "web-platform-tests-reftest",
        "xpcshell",
    ],
)
@pytest.mark.parametrize(
    "platform",
    [
        ("mac", "x86_64"),
        ("win", "x86_64"),
        ("linux", "x86_64"),
    ],
)
@pytest.mark.parametrize("chunks", [1, 3, 6, 20])
def test_chunk_manifests(suite, platform, chunks, mock_mozinfo):
    """Tests chunking with real manifests."""
    mozinfo = mock_mozinfo(*platform)

    loader = chunking.DefaultLoader([])
    manifests = loader.get_manifests(suite, frozenset(mozinfo.items()))

    chunked_manifests = chunking.chunk_manifests(
        suite, platform, chunks, manifests["active"]
    )

    # Assertions and end test.
    assert chunked_manifests
    assert len(chunked_manifests) == chunks
    assert all(chunked_manifests)


@pytest.mark.parametrize("subsuite", sorted(chunking.WPT_SUBSUITES))
@pytest.mark.parametrize(
    "platform",
    [
        ("mac", "x86_64"),
        ("win", "x86_64"),
        ("linux", "x86_64"),
    ],
)
def test_get_manifests_wpt_subsuite(platform, subsuite, mock_mozinfo):
    """A subsuite run only loads manifests under that subsuite's path prefixes."""
    mozinfo = mock_mozinfo(*platform)
    # Mark this as the given subsuite's run.
    mozinfo[subsuite] = True

    loader = chunking.DefaultLoader([])
    manifests = loader.get_manifests("web-platform-tests", frozenset(mozinfo.items()))

    assert manifests["active"]
    subsuite_paths = chunking.WPT_SUBSUITES[subsuite]
    assert all(
        any(
            m.startswith("/" + p) or m.startswith("/_mozilla/" + p)
            for p in subsuite_paths
        )
        for m in manifests["active"]
    ), f"Every manifest should be under one of the {subsuite} subsuite's path prefixes"


@pytest.mark.parametrize(
    "platform",
    [
        ("mac", "x86_64"),
        ("win", "x86_64"),
        ("linux", "x86_64"),
    ],
)
def test_get_manifests_wpt_general_excludes_subsuites(platform, mock_mozinfo):
    """A non-subsuite run excludes every subsuite's manifests."""
    mozinfo = mock_mozinfo(*platform)

    loader = chunking.DefaultLoader([])
    manifests = loader.get_manifests("web-platform-tests", frozenset(mozinfo.items()))

    assert manifests["active"]
    subsuite_prefixes = [
        path for paths in chunking.WPT_SUBSUITES.values() for path in paths
    ]
    for m in manifests["active"]:
        assert not any(
            m.startswith("/" + p) or m.startswith("/_mozilla/" + p)
            for p in subsuite_prefixes
        ), (
            f"{m} should have been excluded from the general web-platform-tests run, since it belongs to a subsuite"
        )


@pytest.mark.parametrize(
    "platform",
    [
        ("mac", "x86_64"),
        ("win", "x86_64"),
        ("linux", "x86_64"),
    ],
)
def test_get_manifests_non_testharness_keeps_subsuites(platform, mock_mozinfo):
    """A non-testharness wpt suite is not subsuite-partitioned, so it keeps
    subsuite manifests such as canvas reftests (which regressed when they were
    carved out of the reftest job)."""
    mozinfo = mock_mozinfo(*platform)

    loader = chunking.DefaultLoader([])
    manifests = loader.get_manifests(
        "web-platform-tests-reftest", frozenset(mozinfo.items())
    )
    active = manifests["active"]
    assert active

    # canvas reftests belong to a subsuite but must still run in the reftest job.
    assert any(m.startswith("/html/canvas") for m in active)


if __name__ == "__main__":
    main()
