# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import runpy
import sys

import mozunit
import pytest
from awsy import parse_about_memory
from conftest import KIND_HEAP, process_reports, report

MAIN = "Main Process (pid 1)"
WEB = "web (pid 2)"


def two_processes():
    return {
        "reports": process_reports(MAIN, explicit_heap=100, heap_allocated=500)
        + process_reports(WEB, explicit_heap=40, heap_allocated=200, scale=2)
    }


def test_path_total_exact_match():
    totals = parse_about_memory.path_total(two_processes(), "resident")
    assert totals == {MAIN: 1000, WEB: 2000}


def test_path_total_exact_match_ignores_children():
    data = {"reports": [report(MAIN, "js-main-runtime/gc", 5)]}
    assert parse_about_memory.path_total(data, "js-main-runtime") == {}


def test_path_total_prefix_match():
    totals = parse_about_memory.path_total(two_processes(), "explicit/images/")
    assert totals == {MAIN: 11, WEB: 22}


def test_path_total_no_match():
    assert parse_about_memory.path_total(two_processes(), "nothing/") == {}


def test_path_total_explicit_adds_heap_unclassified():
    totals = parse_about_memory.path_total(two_processes(), "explicit/")
    main_reported = 100 + 7 + 11
    main_heap_unclassified = 500 - (100 + 11)
    web_reported = 80 + 14 + 22
    web_heap_unclassified = 400 - (80 + 22)
    assert totals[MAIN] == main_reported + main_heap_unclassified
    assert totals[WEB] == web_reported + web_heap_unclassified


def test_path_total_heap_unclassified():
    totals = parse_about_memory.path_total(
        two_processes(), "explicit/heap-unclassified"
    )
    assert totals == {MAIN: 389, WEB: 298}


def test_path_total_heap_unclassified_ignores_non_explicit_heap():
    data = {
        "reports": [
            report(MAIN, "explicit/a", 10, KIND_HEAP),
            report(MAIN, "other/heap", 1000, KIND_HEAP),
            report(MAIN, "heap-allocated", 30),
        ]
    }
    totals = parse_about_memory.path_total(data, "explicit/heap-unclassified")
    assert totals == {MAIN: 20}


def test_path_total_heap_unclassified_requires_heap_allocated():
    data = {"reports": [report(MAIN, "explicit/a", 10, KIND_HEAP)]}
    with pytest.raises(AssertionError):
        parse_about_memory.path_total(data, "explicit/heap-unclassified")


@pytest.mark.parametrize("gzipped", [True, False])
def test_calculate_memory_report_values(write_report, gzipped):
    path = write_report("report.json.gz", two_processes()["reports"], gzipped)
    totals = parse_about_memory.calculate_memory_report_values(str(path), "resident")
    assert totals == {MAIN: 1000, WEB: 2000}


def test_calculate_memory_report_values_filters_by_process_name_substring(write_report):
    path = write_report("report.json.gz", two_processes()["reports"])
    totals = parse_about_memory.calculate_memory_report_values(
        str(path), "resident", ["Main"]
    )
    assert totals == {MAIN: 1000}

    totals = parse_about_memory.calculate_memory_report_values(
        str(path), "resident", ["nope", "web"]
    )
    assert totals == {WEB: 2000}

    totals = parse_about_memory.calculate_memory_report_values(
        str(path), "resident", []
    )
    assert totals == {}


def run_main(monkeypatch, *args):
    monkeypatch.setattr(sys, "argv", ["parse_about_memory.py", *args])
    runpy.run_path(parse_about_memory.__file__, run_name="__main__")


def test_main_prints_bytes_sorted_descending_and_skips_empty(
    monkeypatch, capsys, write_report
):
    reports = two_processes()["reports"] + [report("empty (pid 3)", "resident", 0)]
    path = write_report("report.json.gz", reports)
    run_main(monkeypatch, str(path), "resident")
    out = capsys.readouterr().out
    assert "2000 bytes" in out
    assert "1000 bytes" in out
    assert out.index(WEB) < out.index(MAIN)
    assert "empty" not in out
    assert "MiB" not in out


def test_main_mebi_flag_and_proc_filter_limit_output(monkeypatch, capsys, write_report):
    reports = [report(MAIN, "resident", 3 * 1024 * 1024), report(WEB, "resident", 1)]
    path = write_report("report.json.gz", reports)
    run_main(monkeypatch, str(path), "resident", "--mebi", "--proc-filter", "Main")
    out = capsys.readouterr().out
    assert "3.00 MiB" in out
    assert WEB not in out
    assert "bytes" not in out


if __name__ == "__main__":
    mozunit.main()
