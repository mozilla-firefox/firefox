# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
from argparse import Namespace

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import base_python_support
from base_python_support import BasePythonSupport


def make_raw(extras=None, cpu=None, android=None):
    raw = {"extras": extras if extras is not None else [{}]}
    if cpu is not None:
        raw["cpu"] = cpu
    raw["android"] = android if android is not None else {}
    return raw


@pytest.fixture
def support():
    s = BasePythonSupport()
    s.app = "firefox"
    return s


@pytest.fixture
def make_test_info():
    return {"name": "test-a", "unit": "score", "alert_threshold": 5}


@pytest.mark.parametrize(
    "label,expected",
    [
        (
            "test-windows11-64-24h2-hw-ref-shippable/opt-browsertime-tp6",
            "windows11-64-24h2-hw-ref-shippable",
        ),
        ("test-linux1804-64", "linux1804-64"),
        ("build-linux64/opt", ""),
        ("", ""),
    ],
)
def test_test_platform_from_label(label, expected):
    """Check that the test platform is parsed from test- task labels only."""
    assert base_python_support.test_platform_from_label(label) == expected


def test_init_defaults(monkeypatch):
    """Check the default attributes when no task label is set."""
    monkeypatch.delenv("RAPTOR_TASK_LABEL", raising=False)
    monkeypatch.setattr(base_python_support.platform, "system", lambda: "Plan9")
    s = BasePythonSupport(unused="ignored")
    assert s.power_test is None
    assert s.app is None
    assert s.raw_result == []
    assert s.bt_result == []
    assert s.platform == "Plan9"
    assert s.task_label == ""
    assert s.test_platform == ""


def test_init_reads_task_label(monkeypatch):
    """Check that RAPTOR_TASK_LABEL sets task_label and test_platform."""
    monkeypatch.setenv("RAPTOR_TASK_LABEL", "test-macosx1500-aarch64/opt-sp3")
    s = BasePythonSupport()
    assert s.task_label == "test-macosx1500-aarch64/opt-sp3"
    assert s.test_platform == "macosx1500-aarch64"


def test_instances_do_not_share_results():
    """Check that saved results are per instance, not shared class state."""
    a = BasePythonSupport()
    b = BasePythonSupport()
    a.save_data({"r": 1}, {"b": 1})
    assert b.raw_result == []
    assert b.bt_result == []


def test_save_data_appends_in_order(support):
    """Check that save_data appends raw and browsertime results in order."""
    support.save_data({"r": 1}, {"b": 1})
    support.save_data({"r": 2}, {"b": 2})
    assert support.raw_result == [{"r": 1}, {"r": 2}]
    assert support.bt_result == [{"b": 1}, {"b": 2}]


def test_setup_test_stores_args():
    """Check that setup_test stores power_test and app from the args."""
    s = BasePythonSupport()
    test = {"name": "x"}
    s.setup_test(test, Namespace(power_test=True, app="chrome"))
    assert s.power_test is True
    assert s.app == "chrome"
    assert test == {"name": "x"}


def test_noop_hooks(support, make_test_info):
    """Check that the default hooks leave their inputs alone and report success."""
    cmd = ["--foo"]
    env = {"A": "1"}
    suite = {"subtests": {}}
    assert support.modify_command(cmd, make_test_info) is None
    assert support.modify_env(env, make_test_info) is None
    assert support.handle_result({}, {}, last_result=True) is None
    assert support.summarize_test(make_test_info, suite) is None
    assert support.summarize_suites([suite]) is None
    assert support.clean_up() is None
    assert cmd == ["--foo"]
    assert env == {"A": "1"}
    assert suite == {"subtests": {}}
    assert support.report_test_success() is True


def test_build_standard_subtest_uses_test_defaults(support, make_test_info):
    """Check that a subtest takes unit and threshold from the test and averages replicates."""
    subtest = support._build_standard_subtest(make_test_info, [1, 2, 4], "metric")
    assert subtest == {
        "unit": "score",
        "alertThreshold": 5.0,
        "lowerIsBetter": True,
        "name": "metric",
        "replicates": [1, 2, 4],
        "shouldAlert": True,
        "value": 2.333,
    }


def test_build_standard_subtest_fallbacks(support):
    """Check the ms, 2.0 and lower-is-better fallbacks for an empty test."""
    subtest = support._build_standard_subtest({}, [3], "m")
    assert subtest["unit"] == "ms"
    assert subtest["alertThreshold"] == 2.0
    assert subtest["lowerIsBetter"] is True


def test_build_standard_subtest_overrides(support, make_test_info):
    """Check that explicit unit and should_alert arguments win over the test."""
    subtest = support._build_standard_subtest(
        make_test_info,
        [5, 5],
        "m",
        unit="uWh",
        lower_is_better=True,
        should_alert=False,
    )
    assert subtest["unit"] == "uWh"
    assert subtest["shouldAlert"] is False
    assert subtest["value"] == 5


def test_build_standard_subtest_lower_is_better_from_test(support):
    """Check that subtest_lower_is_better takes precedence over lower_is_better."""
    test = {"lower_is_better": False}
    assert support._build_standard_subtest(test, [1], "m")["lowerIsBetter"] is False
    test["subtest_lower_is_better"] = True
    assert support._build_standard_subtest(test, [1], "m")["lowerIsBetter"] is True


def test_build_standard_subtest_empty_replicates(support):
    """Check that a subtest with no replicates cannot be summarized."""
    with pytest.raises(ZeroDivisionError):
        support._build_standard_subtest({}, [], "m")


@pytest.mark.parametrize(
    "name,expected",
    [
        ("cpuTime", True),
        ("powerUsage", True),
        ("powerUsage_cpu", True),
        ("wallclock-for-tracking-only", True),
        ("perfstats-Paint", False),
        ("fcp", False),
        ("score", False),
    ],
)
def test_is_additional_metric(support, name, expected):
    """Check which measurement names count as additional metrics."""
    assert support.is_additional_metric(name) is expected


def test_gather_browser_cycles_returns_first_match(support):
    """Check that the browser cycle is the first result for the test."""
    results = [
        ("raw-b", {"name": "b"}),
        ("raw-a1", {"name": "a"}),
        ("raw-a2", {"name": "a"}),
    ]
    assert support._gather_browser_cycles({"name": "a"}, results) == [
        ("raw-a1", {"name": "a"})
    ]


def test_gather_browser_cycles_missing(support):
    """Check that a missing test raises a descriptive error."""
    with pytest.raises(Exception, match="Unable to find the test a"):
        support._gather_browser_cycles({"name": "a"}, [("r", {"name": "b"})])


def test_gather_page_cycles_skips_first(support):
    """Check that page cycles are every result for the test after the first."""
    results = [
        ("raw-a1", {"name": "a"}),
        ("raw-b", {"name": "b"}),
        ("raw-a2", {"name": "a"}),
        ("raw-a3", {"name": "a"}),
    ]
    assert support._gather_page_cycles({"name": "a"}, results) == [
        ("raw-a2", {"name": "a"}),
        ("raw-a3", {"name": "a"}),
    ]


@pytest.mark.parametrize(
    "results", [[], [("raw-a1", {"name": "a"})], [("raw-b", {"name": "b"})]]
)
def test_gather_page_cycles_none_found(support, results):
    """Check that having no page cycles for the test raises an error."""
    with pytest.raises(Exception, match="Unable to find any page cycles for test a"):
        support._gather_page_cycles({"name": "a"}, results)


def test_power_usage_pageload_scalar(support):
    """Check that pageload power values are renamed and converted to uWh."""
    raw = make_raw(
        extras=[
            {
                "powerUsagePageload": [1000000, 2500000],
                "powerUsagePageloadExtra": [4000000],
                "other": [1],
            }
        ]
    )
    assert support._gather_power_usage_measurements(raw) == {
        "powerUsage": {"unit": "uWh", "lower_is_better": True, "replicates": [1, 2.5]},
        "powerUsageExtra": {
            "unit": "uWh",
            "lower_is_better": True,
            "replicates": [4],
        },
    }


def test_power_usage_support_nested(support):
    """Check that nested support power data is flattened and takes priority."""
    raw = make_raw(
        extras=[
            {
                "powerUsageSupport": [
                    {"powerUsageSupport": {"cpu": 1000000, "gpu": 2000000}},
                    {"powerUsageSupport": {"cpu": 3000000, "gpu": 4000000}},
                ],
                "powerUsagePageload": [9000000],
            }
        ]
    )
    measurements = support._gather_power_usage_measurements(raw)
    assert set(measurements) == {"powerUsage_cpu", "powerUsage_gpu"}
    assert measurements["powerUsage_cpu"]["replicates"] == [1, 3]
    assert measurements["powerUsage_gpu"]["replicates"] == [2, 4]
    assert measurements["powerUsage_cpu"]["unit"] == "uWh"


def test_power_usage_accumulates_across_extras(support):
    """Check that power replicates accumulate across extras entries."""
    raw = make_raw(
        extras=[{"powerUsagePageload": [1000000]}, {"powerUsagePageload": [2000000]}]
    )
    measurements = support._gather_power_usage_measurements(raw)
    assert measurements["powerUsage"]["replicates"] == [1, 2]


def test_power_usage_falls_back_to_android(support):
    """Check that Android power data is used when extras have none."""
    raw = make_raw(
        android={"power": [{"powerUsage": 1230000}, {"powerUsage": 4560000}]}
    )
    assert support._gather_power_usage_measurements(raw) == {
        "powerUsage": {
            "unit": "uWh",
            "lower_is_better": True,
            "replicates": [1.23, 4.56],
        }
    }


def test_power_usage_android_ignored_when_extras_present(support):
    """Check that Android power data is ignored when extras have power data."""
    raw = make_raw(
        extras=[{"powerUsagePageload": [1000000]}],
        android={"power": [{"powerUsage": 9000000}]},
    )
    measurements = support._gather_power_usage_measurements(raw)
    assert measurements["powerUsage"]["replicates"] == [1]


def test_power_usage_none(support):
    """Check that no power measurements are produced without power data."""
    assert support._gather_power_usage_measurements(make_raw()) == {}


def test_cputime_from_extras(support):
    """Check that benchmark cpuTime from extras wins over pageload cpu data."""
    raw = make_raw(
        extras=[{"cpuTime": [10, 20], "cpuTimeOther": [99]}, {"cpuTime": [30]}],
        cpu=[1, 2],
    )
    assert support._gather_cputime_measurements(raw) == {
        "cpuTime": {"unit": "ms", "lower_is_better": True, "replicates": [10, 20, 30]}
    }


@pytest.mark.parametrize("app", ["firefox", "fenix", "geckoview"])
def test_cputime_from_pageload_firefox(support, app):
    """Check that pageload cpu data is used for Firefox apps."""
    support.app = app
    raw = make_raw(cpu=[5, 6])
    assert support._gather_cputime_measurements(raw) == {
        "cpuTime": {"unit": "ms", "lower_is_better": True, "replicates": [5, 6]}
    }


@pytest.mark.parametrize("app", ["chrome", None])
def test_cputime_pageload_ignored_for_non_firefox(support, app):
    """Check that pageload cpu data is ignored for non-Firefox apps."""
    support.app = app
    assert support._gather_cputime_measurements(make_raw(cpu=[5, 6])) == {}


def test_cputime_none(support):
    """Check that no cpuTime is produced without cpu data."""
    assert support._gather_cputime_measurements(make_raw()) == {}
    assert support._gather_cputime_measurements(make_raw(cpu=[])) == {}


def test_wallclock(support):
    """Check that only the wallclock-for-tracking-only metric is gathered."""
    raw = make_raw(
        extras=[
            {"wallclock-for-tracking-only": [100, 200], "wallclock": [1]},
            {"wallclock-for-tracking-only": [300]},
        ]
    )
    assert support._gather_wallclock_measurements(raw) == {
        "wallclock-for-tracking-only": {
            "unit": "ms",
            "lower_is_better": True,
            "replicates": [100, 200, 300],
        }
    }
    assert support._gather_wallclock_measurements(make_raw()) == {}


def test_perfstats(support):
    """Check that perfstats counters become perfstats- prefixed measurements."""
    raw = make_raw(
        extras=[
            {"perfstats": {"Paint": 5, "Layout": 7}, "other": {"x": 1}},
            {"perfstats": {"Paint": 6}},
        ]
    )
    assert support._gather_perfstats_measurements(raw) == {
        "perfstats-Paint": {
            "unit": "ms",
            "lower_is_better": True,
            "replicates": [5, 6],
        },
        "perfstats-Layout": {"unit": "ms", "lower_is_better": True, "replicates": [7]},
    }
    assert support._gather_perfstats_measurements(make_raw()) == {}


def test_gather_additional_measurements_combines_all(support):
    """Check that all additional measurement kinds are combined."""
    raw = make_raw(
        extras=[
            {
                "powerUsagePageload": [1000000],
                "cpuTime": [10],
                "wallclock-for-tracking-only": [100],
                "perfstats": {"Paint": 5},
            }
        ]
    )
    measurements = support._gather_additional_measurements(raw, {"name": "a"})
    assert set(measurements) == {
        "powerUsage",
        "cpuTime",
        "wallclock-for-tracking-only",
        "perfstats-Paint",
    }


def test_add_additional_metrics_dict_subtests(support, make_test_info):
    """Check that metrics are merged across results into dict subtests."""
    support.save_data(
        make_raw(extras=[{"cpuTime": [10], "powerUsagePageload": [2000000]}]),
        {"name": "test-a"},
    )
    support.save_data(
        make_raw(extras=[{"cpuTime": [20], "powerUsagePageload": [4000000]}]),
        {"name": "test-a"},
    )
    suite = {"subtests": {"existing": {"name": "existing"}}}
    support.add_additional_metrics(make_test_info, suite)

    assert set(suite["subtests"]) == {"existing", "cpuTime", "powerUsage"}
    cpu = suite["subtests"]["cpuTime"]
    assert cpu["replicates"] == [10, 20]
    assert cpu["value"] == 15
    assert cpu["unit"] == "ms"
    assert cpu["alertThreshold"] == 5.0
    power = suite["subtests"]["powerUsage"]
    assert power["replicates"] == [2, 4]
    assert power["unit"] == "uWh"
    assert power["value"] == 3


def test_add_additional_metrics_list_subtests_and_exclude(support, make_test_info):
    """Check that list subtests are appended and excluded metrics skipped."""
    support.save_data(
        make_raw(extras=[{"cpuTime": [10], "wallclock-for-tracking-only": [7]}]),
        {"name": "test-a"},
    )
    suite = {"subtests": []}
    support.add_additional_metrics(make_test_info, suite, exclude=["cpuTime"])
    assert [s["name"] for s in suite["subtests"]] == ["wallclock-for-tracking-only"]
    assert suite["subtests"][0]["replicates"] == [7]


def test_add_additional_metrics_forwards_kwargs(support, make_test_info):
    """Check that extra keyword arguments reach every built subtest."""
    support.save_data(
        make_raw(extras=[{"cpuTime": [10], "wallclock-for-tracking-only": [7]}]),
        {"name": "test-a"},
    )
    suite = {"subtests": {}}
    support.add_additional_metrics(make_test_info, suite, should_alert=False)
    assert suite["subtests"]["cpuTime"]["shouldAlert"] is False
    assert suite["subtests"]["wallclock-for-tracking-only"]["shouldAlert"] is False


def test_add_additional_metrics_no_data(support, make_test_info):
    """Check that the suite is unchanged when no results were saved."""
    suite = {"subtests": {}}
    support.add_additional_metrics(make_test_info, suite)
    assert suite == {"subtests": {}}


@pytest.mark.parametrize(
    "cycle_type,expected",
    [("browser-cycle", [1]), ("page-cycle", [2, 3]), ("", [1, 99, 2, 3])],
)
def test_add_additional_metrics_cycle_types(
    support, make_test_info, cycle_type, expected
):
    """Check which saved results each cycle_type selects."""
    support.save_data(make_raw(extras=[{"cpuTime": [1]}]), {"name": "test-a"})
    support.save_data(make_raw(extras=[{"cpuTime": [99]}]), {"name": "test-b"})
    support.save_data(make_raw(extras=[{"cpuTime": [2]}]), {"name": "test-a"})
    support.save_data(make_raw(extras=[{"cpuTime": [3]}]), {"name": "test-a"})
    suite = {"subtests": {}}
    support.add_additional_metrics(make_test_info, suite, cycle_type=cycle_type)
    assert suite["subtests"]["cpuTime"]["replicates"] == expected


def test_add_additional_metrics_page_cycle_missing(support, make_test_info):
    """Check that page-cycle mode raises when only one cycle exists."""
    support.save_data(make_raw(extras=[{"cpuTime": [1]}]), {"name": "test-a"})
    with pytest.raises(Exception, match="Unable to find any page cycles"):
        support.add_additional_metrics(
            make_test_info, {"subtests": {}}, cycle_type="page-cycle"
        )


def test_add_additional_metrics_merges_pageload_cpu(support, make_test_info):
    """Check that pageload cpu replicates are merged across cycles."""
    support.save_data(make_raw(cpu=[1, 2]), {"name": "test-a"})
    support.save_data(make_raw(cpu=[3]), {"name": "test-a"})
    suite = {"subtests": {}}
    support.add_additional_metrics(make_test_info, suite)
    assert suite["subtests"]["cpuTime"]["replicates"] == [1, 2, 3]


if __name__ == "__main__":
    mozunit.main()
