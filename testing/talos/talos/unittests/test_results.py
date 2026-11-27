# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
from unittest import mock

import mozunit
import pytest

from talos import filter, output, results
from talos.utils import TalosError

TIMESTAMPS = (
    "__startTimestamp1000__endTimestamp\n"
    "__startBeforeLaunchTimestamp900__endBeforeLaunchTimestamp\n"
    "__startAfterTerminationTimestamp2000__endAfterTerminationTimestamp\n"
)


def ts_log(body):
    return f"__start_report{body}__end_report\n{TIMESTAMPS}"


def tp_log(body):
    return f"__start_tp_report{body}__end_tp_report\n{TIMESTAMPS}"


TP_BODY = (
    "\n_x_x_mozilla_page_load\n|i|pagename|runs|\n"
    "|0;a.html/index.html;10;20;30\n"
    "|1;b.html;5;6\n"
)


@pytest.fixture(autouse=True)
def isolated(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    with mock.patch.object(
        results, "__file__", str(tmp_path / "results.py")
    ), mock.patch.object(output, "LOG", new=mock.MagicMock()):
        yield tmp_path


def test_talos_results_tracks_added_results_and_extra_options():
    tr = results.TalosResults()
    assert not tr.has_results()
    tr.add("x")
    tr.add_extra_option("e10s")
    assert tr.has_results()
    assert tr.extra_options == ["e10s"]


def test_ts_results_parses_pipe_separated_numbers_as_null_page():
    r = results.TsResults("1|2|3.5")
    assert r.results == [{"index": 0, "page": "NULL", "runs": [1.0, 2.0, 3.5]}]
    assert r.raw_values() == [("NULL", [1.0, 2.0, 3.5])]


def test_ts_results_parses_comma_separated_pages_and_skips_bad_lines():
    r = results.TsResults("pageA,1,2\nskip\npageB,3,,4\n")
    assert r.results == [
        {"index": 0, "page": "pageA", "runs": [1.0, 2.0]},
        {"index": 1, "page": "pageB", "runs": [3.0, 4.0]},
    ]


def test_ts_results_parses_json_runs_by_name():
    r = results.TsResults(json.dumps({"first": 10, "second": 20}))
    assert r.results == [
        {"index": 0, "page": "NULL", "runs": {"first": [10], "second": [20]}}
    ]


def test_pageloader_results_parses_and_formats_pagenames():
    r = results.PageloaderResults(TP_BODY)
    assert r.results == [
        {"index": 0, "page": "a.html", "runs": [10.0, 20.0, 30.0]},
        {"index": 1, "page": "b.html", "runs": [5.0, 6.0]},
    ]


def test_pageloader_results_joins_split_lines():
    r = results.PageloaderResults("|0;page;\n1;2|\n", format_pagename=False)
    assert r.results == [{"index": 0, "page": "page", "runs": [1.0, 2.0]}]


def test_pageloader_results_keeps_pagename_when_asked():
    r = results.PageloaderResults("|0;a/b/;1|", format_pagename=False)
    assert r.results[0]["page"] == "a/b/"


@pytest.mark.parametrize(
    "page,expected",
    [
        ("simple.html/", "simple.html"),
        ("host/path/page.html", "host"),
        (
            "base_page_1_http://localhost:1/tests/perf-reftest/bloom.html",
            "bloom.html",
        ),
        ("ref_page_1_http://localhost:1/tests/x.html", "x.html"),
    ],
)
def test_pageloader_format_pagename(page, expected):
    assert results.PageloaderResults("").format_pagename(page) == expected


def test_filter_and_values_apply_filters_and_drop_negative_pages():
    r = results.TsResults("p1,1,2,3,10\np2,-5,-5\n")
    filters = filter.ignore_max.prepare() + filter.mean.prepare()
    assert r.filter("t", filters) == [
        [{"filtered": 2.0, "value": 2.0}, "p1"],
        [{"filtered": -5.0, "value": -5.0}, "p2"],
    ]
    assert r.values("t", filters) == [[{"filtered": 2.0, "value": 2.0}, "p1"]]


def test_results_filter_v8_subtest_uses_page():
    r = results.TsResults("Richards,1,1")
    vals = r.filter("t", filter.v8_subtest.prepare("ignored"))
    assert vals[0][0]["value"] == pytest.approx(35302.0)


def test_browser_log_ts_format_parses_times_and_returns_ts_results():
    log = results.BrowserLogResults(ts_log("1|2"))
    assert log.format == "tsformat"
    assert (log.startTime, log.beforeLaunchTime, log.endTime) == (1000, 900, 2000)
    r = log.results()
    assert isinstance(r, results.TsResults)
    assert r.results[0]["runs"] == [1.0, 2.0]
    assert not log.using_xperf


def test_browser_log_tp_format_passes_format_pagename():
    log = results.BrowserLogResults(tp_log(TP_BODY), format_pagename=False)
    r = log.results()
    assert isinstance(r, results.PageloaderResults)
    assert r.results[0]["page"] == "a.html/index.html"


def test_browser_log_fail_token():
    with pytest.raises(TalosError, match="it broke"):
        results.BrowserLogResults("__FAILit broke__FAIL")


@pytest.mark.parametrize(
    "raw,message",
    [
        (TIMESTAMPS, "Could not find report"),
        ("__start_report1__end_report", "Could not find startTime"),
        (
            "__start_report1__end_report__startTimestampabc__endTimestamp",
            "Could not cast startTime",
        ),
        (
            "__startTimestamp1__endTimestamp__start_report1__end_report",
            "found before",
        ),
        (
            "__start_report1__end_report__start_report2__end_report",
            "Multiple matches",
        ),
        ("__start_report1", "Could not find end token"),
    ],
)
def test_browser_log_parse_errors(raw, message):
    with pytest.raises(TalosError, match=message):
        results.BrowserLogResults(raw)


def test_browser_log_unknown_format():
    log = results.BrowserLogResults(ts_log("1"))
    log.format = "other"
    with pytest.raises(TalosError, match="Unable to find a results class"):
        log.results()


def test_mainthread_io_counter_reads_json_file_and_enables_xperf(isolated):
    (isolated / "mainthread_io.json").write_text("[mtio]")
    counters = {}
    log = results.BrowserLogResults(ts_log("1"), global_counters=counters)
    assert counters == {"mainthreadio": ["[mtio]"]}
    assert log.using_xperf


def test_xperf_no_counters_requested():
    counters = {"other": []}
    log = results.BrowserLogResults(ts_log("1"), global_counters=counters)
    assert counters == {"other": []}
    assert not log.using_xperf


def test_xperf_thread_stats_missing():
    with pytest.raises(TalosError, match="etl_output_thread_stats.csv"):
        results.BrowserLogResults(
            ts_log("1"), global_counters={"main_startup_fileio": []}
        )


def test_xperf_thread_stats_fills_startup_fileio_counter(isolated):
    (isolated / "etl_output_thread_stats.csv").write_text(
        "thread, stage, counter, value\n"
        "main, startup, file_io_bytes, 10\n"
        "main, normal, net_io_bytes, 20\n"
    )
    counters = {"main_startup_fileio": []}
    log = results.BrowserLogResults(ts_log("1"), global_counters=counters)
    assert counters == {"main_startup_fileio": [10.0]}
    assert log.using_xperf


def test_xperf_mainthread_counters_read_from_etl_csv(isolated):
    (isolated / "etl_output_thread_stats.csv").write_text(
        "thread,stage,counter,value\n"
    )
    (isolated / "etl_output.csv").write_text(
        "filename, readcount, readbytes, writecount, writebytes\n"
        "a.dll, 2, 0, 0, 0\n"
        "b.dll, 0, 0, 1, 7\n"
    )
    counters = {"mainthread_readcount": []}
    results.BrowserLogResults(ts_log("1"), global_counters=counters)
    assert counters == {
        "mainthread_readcount": [[2, "a.dll"]],
        "mainthread_writecount": [[1, "b.dll"]],
        "mainthread_writebytes": [[7, "b.dll"]],
    }


def test_xperf_mainthread_counters_missing_file(isolated):
    (isolated / "etl_output_thread_stats.csv").write_text(
        "thread,stage,counter,value\n"
    )
    with pytest.raises(TalosError, match="etl_output.csv"):
        results.BrowserLogResults(
            ts_log("1"), global_counters={"mainthread_readbytes": []}
        )


@pytest.mark.parametrize("contents,expected", [("h\n42.5\n", [42.5]), ("h\n", [])])
def test_xperf_session_store(isolated, contents, expected):
    (isolated / "etl_output_thread_stats.csv").write_text(
        "thread,stage,counter,value\n"
    )
    (isolated / "etl_output_session_restore_stats.csv").write_text(contents)
    counters = {"time_to_session_store_window_restored_ms": []}
    results.BrowserLogResults(ts_log("1"), global_counters=counters)
    assert counters == {"time_to_session_store_window_restored_ms": expected}


def make_test_results(config=None):
    test_config = {
        "name": "ts_test",
        "format_pagename": True,
        "mainthread": False,
        "filters": filter.mean.prepare(),
    }
    test_config.update(config or {})
    tr = results.TestResults(test_config, framework={"name": "talos"})
    tr.browser_name = "Firefox"
    tr.browser_version = "1.0"
    return tr


def test_test_results_accumulates_cycles():
    tr = make_test_results({"format_pagename": False})
    assert tr.name() == "ts_test"
    assert tr.mainthread() is False
    tr.add(ts_log("1|2"), counter_results={"c": [1]})
    tr.add(ts_log("3"))
    assert tr.format == "tsformat"
    assert len(tr.results) == 2
    assert tr.all_counter_results == [{"c": [1]}]
    assert tr.global_counters == {}
    assert not tr.using_xperf


def test_test_results_conflicting_formats():
    tr = make_test_results()
    tr.add(ts_log("1"))
    with pytest.raises(TalosError, match="Conflicting formats"):
        tr.add(tp_log(TP_BODY))


def test_talos_results_output_writes_file(isolated):
    tr = make_test_results()
    tr.add(ts_log("1|3"))
    talos_results = results.TalosResults()
    talos_results.add(tr)
    out = isolated / "local.json"
    talos_results.output({"output_urls": [str(out)]})
    data = json.loads(out.read_text())
    assert data["application"] == {"name": "firefox", "version": "1.0"}
    assert data["suites"][0]["subtests"][0]["value"] == 2.0


def test_talos_results_output_reraises_talos_error(capsys):
    talos_results = results.TalosResults()
    with mock.patch.object(output, "Output", side_effect=TalosError("bad\nthing")):
        with pytest.raises(TalosError, match="bad"):
            talos_results.output({"output_urls": ["x"]})
    assert "FAIL: bad\nRETURN:thing" in capsys.readouterr().out


if __name__ == "__main__":
    mozunit.main()
