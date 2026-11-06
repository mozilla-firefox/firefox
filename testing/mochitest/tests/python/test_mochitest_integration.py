# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from functools import partial

import mozinfo
import mozunit
import pytest
from conftest import setup_args
from manifestparser import TestManifest
from mozharness.base.log import ERROR, INFO, WARNING
from mozharness.mozilla.automation import TBPL_FAILURE, TBPL_SUCCESS, TBPL_WARNING
from moztest.selftest.output import filter_action, get_mozharness_status

here = os.path.abspath(os.path.dirname(__file__))
get_mozharness_status = partial(get_mozharness_status, "mochitest")

# When MOZ_AUTOMATION is set, mochitest retries tests that failed unexpectedly
# on the initial run once, which causes extra output lines for those tests.
IN_CI = os.environ.get("MOZ_AUTOMATION") is not None


@pytest.fixture
def test_name(request):
    flavor = request.getfixturevalue("flavor")

    def inner(name):
        if flavor == "plain":
            return f"test_{name}.html"
        elif flavor == "browser-chrome":
            return f"browser_{name}.js"

    return inner


@pytest.fixture
def test_manifest(setup_test_harness, request):
    flavor = request.getfixturevalue("flavor")
    test_root = setup_test_harness(*setup_args, flavor=flavor)
    assert test_root

    def inner(manifestFileNames):
        return TestManifest(
            manifests=[os.path.join(test_root, name) for name in manifestFileNames],
            strict=False,
            rootdir=test_root,
        )

    return inner


@pytest.mark.parametrize(
    "flavor,manifest",
    [
        ("plain", "mochitest-args.ini"),
        ("browser-chrome", "browser-args.toml"),
    ],
)
def test_output_extra_args(flavor, manifest, runtests, test_manifest, test_name):
    # Explicitly provide a manifestFile property that includes the
    # manifest file that contains command line arguments.
    extra_opts = {
        "manifestFile": test_manifest([manifest]),
        "runByManifest": True,
    }

    results = {
        "status": 0,
        "tbpl_status": TBPL_SUCCESS,
        "log_level": (INFO, WARNING),
    }

    status, lines = runtests(test_name("pass"), **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, _ = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    # Filter log entries for the application command including the used
    # command line arguments.
    lines = filter_action("log", lines)
    command = next(
        l["message"] for l in lines if l["message"].startswith("Application command")
    )
    assert "--headless --window-size 800,600 --new-tab http://example.org" in command


@pytest.mark.parametrize("runFailures", ["selftest", ""])
@pytest.mark.parametrize("flavor", ["plain", "browser-chrome"])
def test_output_pass(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    # In selftest/runFailures mode, the passing test is unexpected (fail-if
    # condition fires), which triggers a retry in CI that doubles the output.
    lines = 2 if runFailures else 1
    if runFailures and IN_CI:
        lines *= 2
    results = {
        "status": 1 if runFailures else 0,
        "tbpl_status": TBPL_WARNING if runFailures else TBPL_SUCCESS,
        "log_level": (INFO, WARNING),
        "lines": lines,
        "line_status": "PASS",
    }
    if runFailures:
        extra_opts["runFailures"] = runFailures
        extra_opts["crashAsPass"] = True
        extra_opts["timeoutAsPass"] = True

    status, lines = runtests(test_name("pass"), **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    lines = filter_action("test_status", lines)
    # Ignore pref-leak reports that can differ between the initial and retry runs.
    lines = [
        l for l in lines if not l.get("message", "").startswith("changed preference:")
    ]
    assert len(lines) == results["lines"]
    assert lines[0]["status"] == results["line_status"]


@pytest.mark.parametrize("runFailures", ["selftest", ""])
@pytest.mark.parametrize("flavor", ["plain", "browser-chrome"])
def test_output_fail(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    # A real failure (runFailures="") triggers a retry in CI, doubling the
    # test_status lines.
    lines = 2 if not runFailures and IN_CI else 1
    results = {
        "status": 0 if runFailures else 1,
        "tbpl_status": TBPL_SUCCESS if runFailures else TBPL_WARNING,
        "log_level": (INFO, WARNING),
        "lines": lines,
        "line_status": "PASS" if runFailures else "FAIL",
    }
    if runFailures:
        extra_opts["runFailures"] = runFailures
        extra_opts["crashAsPass"] = True
        extra_opts["timeoutAsPass"] = True

    status, lines = runtests(test_name("fail"), **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    lines = filter_action("test_status", lines)
    assert len(lines) == results["lines"]
    assert lines[0]["status"] == results["line_status"]


@pytest.mark.parametrize("flavor", ["plain", "browser-chrome"])
def test_output_fail_verify(flavor, runtests, test_name, monkeypatch):
    monkeypatch.setenv("MOZ_AUTOMATION", "1")

    status, lines = runtests(test_name("fail"), verify=True)

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == TBPL_WARNING

    assert not any(l["name"] == "retry" for l in filter_action("group_start", lines))
    lines = filter_action("test_status", lines)
    assert len(lines) == 1
    assert lines[0]["status"] == "FAIL"
    assert lines[0]["expected"] == "PASS"
    assert "will_retry" not in lines[0].get("extra", {})


@pytest.mark.parametrize("runFailures", ["selftest", ""])
@pytest.mark.parametrize("flavor", ["plain", "browser-chrome"])
def test_output_restart_after_failure(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    # Both tests fail when runFailures="", so each triggers a browser restart
    # on the initial run (2 launches). In CI the first failed test is also
    # retried, adding one more launch before restartAfterFailure stops the run.
    lines = 3 if not runFailures and IN_CI else 2
    results = {
        "status": 0 if runFailures else 1,
        "tbpl_status": TBPL_SUCCESS if runFailures else TBPL_WARNING,
        "log_level": (INFO, WARNING),
        "lines": lines,
        "line_status": "PASS" if runFailures else "FAIL",
    }
    extra_opts["restartAfterFailure"] = True
    if runFailures:
        extra_opts["runFailures"] = runFailures
        extra_opts["crashAsPass"] = True
        extra_opts["timeoutAsPass"] = True

    tests = [test_name("fail"), test_name("fail2")]
    status, lines = runtests(tests, **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    # Ensure the harness cycled when failing (look for launching browser)
    start_lines = [
        line for line in lines if "Application command:" in line.get("message", "")
    ]
    if not runFailures:
        assert len(start_lines) == results["lines"]
    else:
        assert len(start_lines) == 1


@pytest.mark.skip_mozinfo("!crashreporter")
@pytest.mark.parametrize("runFailures", ["selftest", ""])
@pytest.mark.parametrize("flavor", ["plain", "browser-chrome"])
def test_output_crash(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    results = {
        "status": 0 if runFailures else 1,
        "tbpl_status": TBPL_FAILURE,
        "log_level": ERROR,
        "lines": 1,
    }
    if runFailures:
        extra_opts["runFailures"] = runFailures
        extra_opts["crashAsPass"] = True
        extra_opts["timeoutAsPass"] = True
        # bug 1443327 - we do not set MOZ_CRASHREPORTER_SHUTDOWN for browser-chrome
        # the error regex's don't pick this up as a failure
        if flavor == "browser-chrome":
            results["tbpl_status"] = TBPL_SUCCESS
            results["log_level"] = (INFO, WARNING)
    elif flavor == "browser-chrome" and not mozinfo.info["debug"]:
        # A browser-chrome crash isn't matched by mozharness's error regexes
        # (see the MOZ_CRASHREPORTER_SHUTDOWN note above), so it only registers
        # as the unexpected CRASH status, which is scored as a warning rather
        # than a failure.
        #
        # This does not apply to debug builds: there the crash also trips the
        # leak-detection checks (ShutdownLeaks and the DOCSHELL/DOMWINDOW
        # logging), which emit TEST-UNEXPECTED-FAIL error lines and make the run
        # a failure, as expected by the default above.
        results["tbpl_status"] = TBPL_WARNING
        results["log_level"] = WARNING

    status, lines = runtests(
        test_name("crash"), environment=["MOZ_CRASHREPORTER_SHUTDOWN=1"], **extra_opts
    )
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    if not runFailures:
        crash = filter_action("crash", lines)
        assert len(crash) == 1
        assert crash[0]["action"] == "crash"
        assert crash[0]["signature"]
        assert crash[0]["minidump_path"]

    lines = filter_action("test_end", lines)
    assert len(lines) == results["lines"]


@pytest.mark.skip_mozinfo("!asan")
@pytest.mark.parametrize("runFailures", [""])
@pytest.mark.parametrize("flavor", ["plain"])
def test_output_asan(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    results = {
        "status": 1,
        "tbpl_status": TBPL_FAILURE,
        "log_level": ERROR,
        "lines": 0,
    }

    status, lines = runtests(
        test_name("crash"), environment=["MOZ_CRASHREPORTER_SHUTDOWN=1"], **extra_opts
    )
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level == results["log_level"]

    crash = filter_action("crash", lines)
    assert len(crash) == results["lines"]

    process_output = filter_action("process_output", lines)
    assert any("ERROR: AddressSanitizer" in l["data"] for l in process_output)


@pytest.mark.skip_mozinfo("!debug")
@pytest.mark.parametrize("runFailures", [""])
@pytest.mark.parametrize("flavor", ["plain"])
def test_output_assertion(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    # The assertion test fails on every run, so CI retries it once: both
    # test_end and assertion_count messages appear twice, but each run still
    # reports a single assertion.
    lines = 2 if IN_CI else 1
    results = {
        "status": 1,
        "tbpl_status": TBPL_WARNING,
        "log_level": WARNING,
        "lines": lines,
        "assertions": 1,
    }

    status, lines = runtests(test_name("assertion"), **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level == results["log_level"]

    # assertion failure prints error, but still emits test-ok
    test_end = filter_action("test_end", lines)
    assert len(test_end) == results["lines"]
    assert test_end[0]["status"] == "FAIL"

    assertions = filter_action("assertion_count", lines)
    assert len(assertions) == results["lines"]
    assert assertions[0]["count"] == results["assertions"]


@pytest.mark.skip_mozinfo("!debug")
@pytest.mark.parametrize("runFailures", [""])
@pytest.mark.parametrize("flavor", ["plain"])
def test_output_leak(flavor, runFailures, runtests, test_name):
    extra_opts = {}
    results = {"status": 0, "tbpl_status": TBPL_WARNING, "log_level": WARNING}

    status, lines = runtests(test_name("leak"), **extra_opts)
    # TODO: mochitest should return non-zero here
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level == results["log_level"]

    leak_totals = filter_action("mozleak_total", lines)
    found_leaks = False
    for lt in leak_totals:
        if lt["bytes"] == 0:
            # No leaks in this process.
            assert len(lt["objects"]) == 0
            continue

        assert not found_leaks, "Only one process should have leaked"
        found_leaks = True
        assert lt["process"] == "tab"
        assert lt["bytes"] == 1
        assert lt["objects"] == ["IntentionallyLeakedObject"]

    assert found_leaks, "At least one process should have leaked"


@pytest.mark.parametrize("flavor", ["plain"])
def test_output_testfile_in_dupe_manifests(flavor, runtests, test_name, test_manifest):
    results = {
        "status": 0,
        "tbpl_status": TBPL_SUCCESS,
        "log_level": (INFO, WARNING),
        "line_status": "PASS",
        # We expect the test to be executed exactly 2 times,
        # once for each manifest where the test file has been included.
        "lines": 2,
    }

    # Explicitly provide a manifestFile property that includes the
    # two manifest files that share the same test file.
    extra_opts = {
        "manifestFile": test_manifest([
            "mochitest-dupemanifest-1.ini",
            "mochitest-dupemanifest-2.ini",
        ]),
        "runByManifest": True,
    }

    # Execute mochitest by explicitly request the test file listed
    # in two manifest files to be executed.
    status, lines = runtests(test_name("pass"), **extra_opts)
    assert status == results["status"]

    tbpl_status, log_level, summary = get_mozharness_status(lines, status)
    assert tbpl_status == results["tbpl_status"]
    assert log_level in results["log_level"]

    lines = filter_action("test_status", lines)
    assert len(lines) == results["lines"]
    assert lines[0]["status"] == results["line_status"]


if __name__ == "__main__":
    mozunit.main()
