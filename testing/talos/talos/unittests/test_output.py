# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
from unittest import mock

import mozunit
import pytest

from talos import filter, output, results


@pytest.fixture(autouse=True)
def log():
    with mock.patch.object(output, "LOG", new=mock.MagicMock()) as log:
        yield log


def make_results(name="mytest", runs=(), config=None, using_xperf=False):
    test_config = {"name": name, "filters": filter.median.prepare()}
    test_config.update(config or {})
    test = results.TestResults(test_config, framework="talos")
    test.browser_name = "Firefox"
    test.browser_version = "99.0"
    test.using_xperf = using_xperf
    for cycle in runs:
        r = results.Results()
        r.results = [
            {"index": i, "page": page, "runs": list(vals)}
            for i, (page, vals) in enumerate(cycle)
        ]
        test.results.append(r)
    talos_results = results.TalosResults()
    talos_results.add(test)
    return talos_results, test


def run(talos_results):
    return output.Output(talos_results, results.Results)()


def test_output_has_application_and_framework_header():
    talos_results, _ = make_results(runs=[[("NULL", [1])]])
    data = run(talos_results)
    assert data["application"] == {"name": "firefox", "version": "99.0"}
    assert data["framework"] == {"name": "talos"}


def test_single_null_page_merges_cycles():
    talos_results, _ = make_results(
        runs=[[("NULL", [1, 2])], [("NULL", [3])]],
        config={"unit": "ms", "suite_should_alert": False},
    )
    talos_results.add_extra_option("e10s")
    (suite,) = run(talos_results)["suites"]
    assert suite["name"] == "mytest"
    assert suite["extraOptions"] == ["e10s"]
    assert suite["shouldAlert"] is False
    assert "value" not in suite
    assert suite["subtests"] == [
        {
            "name": "mytest",
            "value": 2,
            "replicates": [1, 2, 3],
            "unit": "ms",
            "shouldAlert": False,
        }
    ]


def test_multiple_pages_summary_and_alert_settings():
    talos_results, _ = make_results(
        runs=[[("a", [1, 3]), ("b", [7])], [("a", [5]), ("b", [7])]],
        config={
            "lower_is_better": True,
            "alert_threshold": 2.0,
            "subtest_alerts": False,
        },
    )
    (suite,) = run(talos_results)["suites"]
    assert suite["lowerIsBetter"] is True
    assert suite["alertThreshold"] == 2.0
    assert suite["value"] == pytest.approx(filter.geometric_mean([3, 7]))
    assert [s["name"] for s in suite["subtests"]] == ["a", "b"]
    a = suite["subtests"][0]
    assert a["value"] == 3
    assert a["replicates"] == [1, 3, 5]
    assert a["lowerIsBetter"] is True
    assert a["alertThreshold"] == 2.0
    assert a["shouldAlert"] is False


def test_base_and_ref_replicates_kept():
    talos_results, test = make_results(runs=[[("a", [1])]])
    test.results[0].results[0].update(base_runs=[5], ref_runs=[4])
    (subtest,) = run(talos_results)["suites"][0]["subtests"]
    assert subtest["base_replicates"] == [5]
    assert subtest["ref_replicates"] == [4]


def test_negative_values_dropped():
    talos_results, _ = make_results(runs=[[("a", [-1]), ("b", [2])]])
    (suite,) = run(talos_results)["suites"]
    assert [s["name"] for s in suite["subtests"]] == ["b"]


def test_counters_mean_and_skipped_entries():
    talos_results, test = make_results(runs=[[("a", [1])]])
    test.all_counter_results = [
        {
            "rss": [1, "3"],
            "empty": [],
            "mainthread_readcount": [[1, "f"]],
            "mainthreadio": ["x"],
        }
    ]
    suites = run(talos_results)["suites"]
    assert len(suites) == 2
    assert suites[1]["name"] == "mytest"
    assert suites[1]["subtests"] == [
        {"name": "rss", "value": 2.0},
        {"name": "empty", "value": 0.0},
    ]


def test_xperf_counters_only():
    talos_results, test = make_results(runs=[[("a", [1])]], using_xperf=True)
    test.all_counter_results = [{"main_startup_fileio": [12, 13], "none": []}]
    (suite,) = run(talos_results)["suites"]
    assert suite["subtests"] == [
        {"name": "main_startup_fileio", "value": 12},
        {"name": "none", "value": 0.0},
    ]


def test_output_writes_file_and_logs_perfherder_data(tmp_path, log):
    talos_results, _ = make_results(runs=[[("a", [1])]])
    out = output.Output(talos_results, results.Results)
    data = out()
    path = tmp_path / "out.json"
    out.output(data, str(path))
    assert json.loads(path.read_text()) == data
    assert log.info.call_args[0][0].startswith("PERFHERDER_DATA: ")
    assert json.loads(log.info.call_args[0][0][len("PERFHERDER_DATA: ") :]) == data


def test_output_gecko_profile_disables_alerts(tmp_path, log):
    talos_results, _ = make_results(runs=[[("a", [1]), ("b", [2])]])
    talos_results.add_extra_option("gecko-profile")
    out = output.Output(talos_results, results.Results)
    data = out()
    out.output(data, str(tmp_path / "out.json"))
    log.info.assert_any_call("gecko-profile enabled")
    for suite in data["suites"]:
        assert suite["shouldAlert"] is False
        assert all(s["shouldAlert"] is False for s in suite["subtests"])


def test_output_http_uses_post():
    talos_results, _ = make_results(runs=[[("a", [1])]])
    out = output.Output(talos_results, results.Results)
    with pytest.raises(NotImplementedError, match="Abstract"):
        out.output({"suites": []}, "https://example.com/submit")


def test_output_unsupported_scheme():
    talos_results, _ = make_results(runs=[[("a", [1])]])
    out = output.Output(talos_results, results.Results)
    with pytest.raises(NotImplementedError, match="only http"):
        out.output({"suites": []}, "ftp://example.com/x")


def test_check_is_noop():
    assert output.Output.check(["x"]) is None


def test_short_name_and_memory_metric():
    assert output.Output.shortName("% Processor Time") == "%cpu"
    assert output.Output.shortName("XRes") == "xres"
    assert output.Output.shortName("other") == "other"
    assert output.Output.isMemoryMetric("main_xres") is True
    assert output.Output.isMemoryMetric("rss") is False


def test_metric_functions_compute_scores_from_value_pairs():
    vals = [[1, "a"], [7, "geomean"], [3, "geomean"]]
    assert output.Output.v8_Metric(vals) == pytest.approx(
        100 * filter.geometric_mean([1, 7, 3])
    )
    assert output.Output.JS_Metric(vals) == 11
    assert output.Output.benchmark_score(vals) == 5
    assert output.Output.damp_score(vals) == 100


@pytest.mark.parametrize(
    "testname,expected",
    [
        ("v8_7", 100 * filter.geometric_mean([1, 3])),
        ("kraken", 4),
        ("ares6", 3),
        ("jetstream", 3),
        ("damp", 100),
        ("other", filter.geometric_mean([1, 3])),
    ],
)
def test_construct_results_scores_geomean_per_test_name(testname, expected):
    out = output.Output(None, None)
    assert out.construct_results(
        [[1, "x"], [3, "geomean"]], testname=testname
    ) == pytest.approx(expected)


def test_construct_results_single_value():
    assert output.Output(None, None).construct_results([[4, "x"]], "other") == 4


if __name__ == "__main__":
    mozunit.main()
