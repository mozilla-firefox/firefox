# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import statistics
import sys
import types
from calendar import day_name
from datetime import datetime, timedelta
from unittest import mock

import mozunit
import pytest

REPORT_PY = os.path.join(
    os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "scripts", "report.py"
)


@pytest.fixture(scope="module")
def compare():
    return types.SimpleNamespace(
        branch_map={"Inbound": {"pgo": {"id": 1}, "nonpgo": {"id": 2}}},
        test_map={"tsvg": {"id": 10}, "tp5o": {"id": 11}},
        platform_map={"OSX10": 100, "Win7": 200},
        getGraphData=mock.Mock(),
    )


@pytest.fixture(scope="module")
def report(compare):
    numpy = types.SimpleNamespace(var=statistics.pvariance, average=statistics.mean)
    spec = importlib.util.spec_from_file_location("talos_report", REPORT_PY)
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(
        sys.modules, {"compare": compare, "numpy": numpy}
    ), mock.patch.object(sys, "path", list(sys.path)):
        spec.loader.exec_module(module)
    return module


def test_get_branch_maps_platform_to_branch_id(report):
    assert report.get_branch("OSX10") == 1
    assert report.get_branch("Win7") == 2


def test_get_tuple_returns_row_for_test_and_platform(report):
    assert report.get_tuple("tsvg", "Win7") == [(10, 2, 200, "tsvg", "Win7")]


def test_get_all_test_tuples(report):
    assert report.get_all_test_tuples() == [
        (10, 1, 100, "tsvg", "OSX10"),
        (10, 2, 200, "tsvg", "Win7"),
        (11, 1, 100, "tp5o", "OSX10"),
        (11, 2, 200, "tp5o", "Win7"),
    ]


@pytest.mark.parametrize(
    "week,outliers",
    [
        ([10, 10, 10, 10, 10, 10, 10], []),
        ([10, 10, 10, 10, 10, 5, 20], [5, 6]),
        ([10, 30, 10, 10, 10, 15, 15], [1, 5, 6]),
        ([10, 10, 10, 30, 10, 12, 13], [3, 6]),
    ],
)
def test_is_normal_flags_weeks_with_outliers(report, week, outliers):
    assert report.is_normal(week) == outliers


def week_of_points(start, counts):
    points = []
    for day, count in enumerate(counts):
        ts = (start + timedelta(days=day)).timestamp()
        points.extend([(0, 0, ts, 1.0)] * count)
    return points


def test_generate_report_count_mode(report, compare, tmp_path):
    monday = datetime(2024, 1, 1, 12)
    assert monday.strftime("%A") == "Monday"
    points = week_of_points(monday, [1] * 7)
    compare.getGraphData.side_effect = [{"test_runs": points}, None]
    tuples = [(10, 2, 200, "tsvg", "Win7"), (11, 2, 200, "tp5o", "Win7")]
    path = tmp_path / "report.csv"
    with mock.patch.object(report.csv, "writer") as writer:
        report.generate_report(tuples, str(path), mode="count")
    assert compare.getGraphData.call_args_list == [
        mock.call(10, 2, 200),
        mock.call(11, 2, 200),
    ]
    rows = [c.args[0] for c in writer.return_value.writerow.call_args_list]
    assert rows[0] == ["test-platform"] + list(day_name)
    assert rows[1:] == [["tsvg-Win7"] + ["1.000"] * 7]
    assert path.exists()


def test_main_passes_platform_test_and_mode_to_generate_report(report, monkeypatch):
    monkeypatch.setattr(
        sys,
        "argv",
        ["report.py", "--platform", "Win7", "--test", "tsvg", "--mode", "count"],
    )
    with mock.patch.object(report, "generate_report") as generate:
        report.main()
    generate.assert_called_once_with(
        [(10, 2, 200, "tsvg", "Win7")],
        filepath="report-Win7-tsvg-count.csv",
        mode="count",
    )


def test_main_without_args_generates_variance_report_for_all(report, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["report.py"])
    with mock.patch.object(report, "generate_report") as generate:
        report.main()
    assert len(generate.call_args.args[0]) == 4
    assert generate.call_args.kwargs == {
        "filepath": "report-variance.csv",
        "mode": "variance",
    }


if __name__ == "__main__":
    mozunit.main()
