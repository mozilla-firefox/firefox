# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import json
import math
import os
import runpy
import sys

import mozunit
import pytest
from awsy import process_perf_data
from conftest import process_reports

MAIN = "Main Process (pid 1)"
WEB = "web (pid 2)"


@pytest.fixture(autouse=True)
def clean_env(monkeypatch):
    for var in ("DMD", "GCOV_PREFIX", "JS_CODE_COVERAGE_OUTPUT_DIR"):
        monkeypatch.delenv(var, raising=False)


@pytest.fixture
def checkpoints():
    return copy.deepcopy(process_perf_data.CHECKPOINTS)


@pytest.fixture
def reports_dir_scaled_by_index_plus_1(tmp_path, write_report):
    for i, checkpoint in enumerate(process_perf_data.CHECKPOINTS):
        write_report(
            checkpoint["path"],
            process_reports(MAIN, explicit_heap=100, heap_allocated=500, scale=i + 1)
            + process_reports(WEB, explicit_heap=10, heap_allocated=50, scale=i + 1),
        )
    return tmp_path


def geomean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values))


@pytest.mark.parametrize(
    "values,expected",
    [([3, 1, 2], 2), ([4, 1, 3, 2], 2.5), ([5], 5), ([1, 2], 1.5)],
)
def test_median_of_odd_even_single_and_pair_lists(values, expected):
    assert process_perf_data.median(values) == expected


def test_update_checkpoint_paths_picks_first_or_last_report_per_checkpoint(checkpoints):
    files = [
        os.path.join("d", name)
        for name in (
            "memory-report-Start-1.json.gz",
            "memory-report-Start-0.json.gz",
            "memory-report-TabsOpen-0.json.gz",
            "memory-report-TabsOpen-3.json.gz",
            "memory-report-TabsOpenSettled-2.json.gz",
            "memory-report-TabsClosed-1.json.gz",
        )
    ]
    process_perf_data.update_checkpoint_paths(files, checkpoints)
    paths = {c["name"]: c["path"] for c in checkpoints}
    assert paths["Fresh start"] == files[1]
    assert paths["After tabs open"] == files[3]
    assert paths["After tabs open [+30s]"] == files[4]
    assert paths["Tabs closed"] == files[5]
    assert paths["Fresh start [+30s]"] == "memory-report-StartSettled-0.json.gz"
    assert paths["Tabs closed [+30s]"] == "memory-report-TabsClosedSettled-4.json.gz"


def test_update_checkpoint_paths_reports_missing_checkpoint_keeps_default(capsys):
    checkpoints = [{"name": "Fresh start", "path": "memory-report-Start-0.json.gz"}]
    process_perf_data.update_checkpoint_paths(
        ["memory-report-TabsOpen-0.json.gz"], checkpoints
    )
    assert "couldn't find TabsOpen-" in capsys.readouterr().out
    assert checkpoints[0]["path"] == "memory-report-Start-0.json.gz"


def test_create_suite_explicit_adds_heap_unclassified_per_checkpoint(
    reports_dir_scaled_by_index_plus_1, checkpoints
):
    suite = process_perf_data.create_suite(
        "Explicit Memory",
        "explicit/",
        str(reports_dir_scaled_by_index_plus_1),
        checkpoints,
    )
    main_explicit = (100 + 7 + 11) + (500 - (100 + 11))
    web_explicit = (10 + 7 + 11) + (50 - (10 + 11))
    per_scale = main_explicit + web_explicit
    values = [per_scale * (i + 1) for i in range(len(checkpoints))]
    assert suite["name"] == "Explicit Memory"
    assert suite["lowerIsBetter"] is True
    assert suite["unit"] == "bytes"
    assert "alertThreshold" not in suite
    assert "extraOptions" not in suite
    assert [s["name"] for s in suite["subtests"]] == [
        c["name"] for c in process_perf_data.CHECKPOINTS
    ]
    assert [s["value"] for s in suite["subtests"]] == values
    assert all(s["unit"] == "bytes" and s["lowerIsBetter"] for s in suite["subtests"])
    assert suite["value"] == pytest.approx(geomean(values))


def test_create_suite_resident_sums_main_rss_and_content_uss(
    reports_dir_scaled_by_index_plus_1, checkpoints
):
    suite = process_perf_data.create_suite(
        "Resident Memory",
        "resident",
        str(reports_dir_scaled_by_index_plus_1),
        checkpoints,
    )
    main_rss_plus_web_uss = 1000 + 300
    values = [main_rss_plus_web_uss * (i + 1) for i in range(len(checkpoints))]
    assert [s["value"] for s in suite["subtests"]] == values


def test_create_suite_adds_alert_threshold_and_dmd_extra_option(
    reports_dir_scaled_by_index_plus_1, checkpoints, monkeypatch
):
    monkeypatch.setenv("DMD", "1")
    suite = process_perf_data.create_suite(
        "JS",
        "js-main-runtime/",
        str(reports_dir_scaled_by_index_plus_1),
        checkpoints,
        alertThreshold=5.0,
        extra_opts=["fission"],
    )
    assert suite["alertThreshold"] == 5.0
    assert suite["extraOptions"] == ["fission", "dmd"]


def test_create_suite_ignores_empty_dmd_env_var(
    reports_dir_scaled_by_index_plus_1, checkpoints, monkeypatch
):
    monkeypatch.setenv("DMD", "")
    suite = process_perf_data.create_suite(
        "JS", "js-main-runtime/", str(reports_dir_scaled_by_index_plus_1), checkpoints
    )
    assert "extraOptions" not in suite


def test_create_suite_name_filter_selects_processes_and_median_option(
    tmp_path, write_report
):
    write_report(
        "memory-report-Start-0.json.gz",
        process_reports(MAIN, scale=1)
        + process_reports("web (pid 2)", scale=2)
        + process_reports("web (pid 3)", scale=4)
        + process_reports("web (pid 4)", scale=10)
        + process_reports("gpu (pid 5)", scale=100),
    )
    checkpoints = [
        {
            "name": "Start",
            "path": "memory-report-Start-0.json.gz",
            "name_filter": ["web"],
            "median": True,
        },
        {
            "name": "Start sum",
            "path": "memory-report-Start-0.json.gz",
            "name_filter": ["web"],
        },
    ]
    suite = process_perf_data.create_suite(
        "JS", "js-main-runtime/", str(tmp_path), checkpoints
    )
    assert [s["value"] for s in suite["subtests"]] == [200, 800]


def test_create_perf_data_builds_framework_blob_with_all_suites(
    reports_dir_scaled_by_index_plus_1, checkpoints
):
    perf_suites = copy.deepcopy(process_perf_data.PERF_SUITES)
    perf_suites[0]["alertThreshold"] = 3
    blob = process_perf_data.create_perf_data(
        str(reports_dir_scaled_by_index_plus_1), perf_suites, checkpoints, ["opt"]
    )
    assert blob["framework"] == {"name": "awsy"}
    assert [s["name"] for s in blob["suites"]] == [
        s["name"] for s in process_perf_data.PERF_SUITES
    ]
    assert blob["suites"][0]["alertThreshold"] == 3
    assert "alertThreshold" not in blob["suites"][1]
    assert all(s["extraOptions"] == ["opt"] for s in blob["suites"])
    assert all(len(s["subtests"]) == len(checkpoints) for s in blob["suites"])


@pytest.mark.parametrize("var", ["GCOV_PREFIX", "JS_CODE_COVERAGE_OUTPUT_DIR"])
def test_create_perf_data_returns_empty_when_collecting_code_coverage(
    var, monkeypatch, capsys, tmp_path
):
    monkeypatch.setenv(var, str(tmp_path))
    assert process_perf_data.create_perf_data(str(tmp_path)) == {}
    assert "Code coverage is being collected" in capsys.readouterr().out


def run_main(monkeypatch, *args):
    monkeypatch.setattr(sys, "argv", ["process_perf_data.py", *args])
    with pytest.raises(SystemExit) as exc:
        runpy.run_path(process_perf_data.__file__, run_name="__main__")
    return exc.value.code


def test_main_without_arguments_prints_usage_and_exits_1(monkeypatch, capsys):
    assert run_main(monkeypatch) == 1
    assert "Usage" in capsys.readouterr().out


def test_main_prints_perfherder_data_and_exits_0(
    monkeypatch, capsys, reports_dir_scaled_by_index_plus_1
):
    assert run_main(monkeypatch, str(reports_dir_scaled_by_index_plus_1)) == 0
    out = capsys.readouterr().out
    prefix = "PERFHERDER_DATA: "
    assert out.startswith(prefix)
    blob = json.loads(out[len(prefix) :])
    assert blob["framework"] == {"name": "awsy"}
    assert len(blob["suites"]) == len(process_perf_data.PERF_SUITES)


if __name__ == "__main__":
    mozunit.main()
