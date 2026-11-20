# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import mozunit
from conftest import build
from mozlint.result import ResultSummary

LINTER = "android-fenix"

AC = os.path.join("mobile", "android", "android-components")
FENIX = os.path.join("mobile", "android", "fenix")
FOCUS = os.path.join("mobile", "android", "focus-android")


def write_report(topobjdir, subdir, module, name, rule, mtime):
    reports = os.path.join(topobjdir, "gradle", "build", subdir, module, "reports")
    os.makedirs(reports, exist_ok=True)
    report = os.path.join(reports, name)
    uri = f"{module}/src/main/java/File.kt"
    if subdir != AC:
        uri = f"{subdir}/{uri}"
    issue = {
        "ruleId": rule,
        "message": {"text": rule},
        "locations": [
            {
                "physicalLocation": {
                    "artifactLocation": {"uri": uri},
                    "region": {"startLine": 1},
                }
            }
        ],
    }
    with open(report, "w") as fh:
        json.dump({"runs": [{"results": [issue]}]}, fh)
    os.utime(report, (mtime, mtime))


def parse(config, topobjdir, subdir, lint_reports=None, lint_modules=None):
    from android.lints import (
        DEFAULT_LINT_MODULES,
        DEFAULT_LINT_REPORTS,
        parse_lint_report,
    )

    ResultSummary.root = build.topsrcdir

    return parse_lint_report(
        config,
        subdir,
        lint_reports or DEFAULT_LINT_REPORTS,
        lint_modules or DEFAULT_LINT_MODULES,
        root=build.topsrcdir,
        topobjdir=str(topobjdir),
    )


def test_linted_variant_wins_over_newer_stale_variant(config, tmp_path):
    write_report(tmp_path, FOCUS, "app", "lint-results-focusDebug.sarif", "Linted", 1)
    write_report(tmp_path, FOCUS, "app", "lint-results-klarDebug.sarif", "Stale", 2)

    results = parse(
        config, tmp_path, FOCUS, ("lint-results-focusDebug.sarif",), ("app",)
    )
    assert [r.rule for r in results] == ["Linted"]


def test_default_reports_skip_unlinted_variants(config, tmp_path):
    for module, name, rule, mtime in (
        ("one", "lint-results-debug.sarif", "Linted", 1),
        ("one", "lint-results-nightly.sarif", "Stale", 2),
        ("samples/browser", "lint-results-geckoDebug.sarif", "Flavored", 1),
        ("samples/browser", "lint-results-systemDebug.sarif", "Stale", 2),
    ):
        write_report(tmp_path, AC, module, name, rule, mtime)

    results = parse(config, tmp_path, AC)
    assert sorted(r.rule for r in results) == ["Flavored", "Linted"]


def test_unlinted_module_reports_are_ignored(config, tmp_path):
    write_report(tmp_path, FENIX, "app", "lint-results-debug.sarif", "App", 1)
    write_report(tmp_path, FENIX, "app/longfox", "lint-results-debug.sarif", "Lib", 2)

    results = parse(config, tmp_path, FENIX, lint_modules=("app",))
    assert [r.rule for r in results] == ["App"]
    assert results[0].relpath == "mobile/android/fenix/app/src/main/java/File.kt"


def test_reports_found_up_to_three_levels_deep(config, tmp_path):
    for module, rule, name in (
        ("one", "Depth1", "lint-results-debug.sarif"),
        ("components/two", "Depth2", "lint-results-debug.sarif"),
        ("samples/browser", "Flavored", "lint-results-geckoDebug.sarif"),
        ("a/b/three", "Depth3", "lint-results-debug.sarif"),
        ("a/b/c/four", "Depth4", "lint-results-debug.sarif"),
    ):
        write_report(tmp_path, AC, module, name, rule, 1)

    results = parse(config, tmp_path, AC)
    assert sorted(r.rule for r in results) == ["Depth1", "Depth2", "Depth3", "Flavored"]
    assert all(
        r.relpath.startswith("mobile/android/android-components/") for r in results
    )


def test_objdir_with_glob_characters(config, tmp_path):
    topobjdir = tmp_path / "obj[1]"
    write_report(topobjdir, FENIX, "app", "lint-results-debug.sarif", "Found", 1)

    results = parse(config, topobjdir, FENIX, lint_modules=("app",))
    assert [r.rule for r in results] == ["Found"]


def test_missing_report_is_a_build_failure(config, tmp_path):
    results = parse(config, tmp_path, FENIX, lint_modules=("app",))
    assert [r.rule for r in results] == ["build-failure"]


if __name__ == "__main__":
    mozunit.main()
