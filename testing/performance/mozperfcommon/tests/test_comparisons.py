#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import mozunit
import pytest
from mozperfcommon import comparisons
from mozperfcommon.comparisons import (
    HISTORICAL_MIN_NUMS,
    MAD_SCALE,
    ZERO_VALUE_THRESHOLD,
    Comparisons,
    UnknownComparisonError,
    compare_to_mean,
    compare_to_median,
    median_absolute_deviation,
    remove_outliers,
)


@pytest.fixture(autouse=True)
def historical_min_nums(monkeypatch):
    monkeypatch.setattr(comparisons, "HISTORICAL_MIN_NUMS", 2)


def test_compare_to_mean():
    assert compare_to_mean([14.0, 16.0], [10.0, 11.0, 15.0]) == {
        "new_mean": 15.0,
        "historical_mean": 12.0,
        "historical_stdev": pytest.approx(7**0.5),
        "difference": 3.0,
        "percentage": 25.0,
        "exceeds_variance": False,
        "new_outliers": 0,
    }


def test_compare_to_mean_ignores_outliers():
    result = compare_to_mean([14.0, 16.0, 500.0], [10.0, 11.0, 15.0])
    assert (result["new_mean"], result["new_outliers"]) == (15.0, 1)
    assert result["exceeds_variance"] is False

    # Most of the new values are worse, so none of them is an outlier.
    result = compare_to_mean([14.0, 400.0, 500.0], [10.0, 11.0, 15.0])
    assert (result["new_outliers"], result["exceeds_variance"]) == (0, True)


def test_compare_to_mean_exceeds_variance():
    result = compare_to_mean([40.0], [10.0, 11.0, 15.0])
    assert (result["difference"], result["exceeds_variance"]) == (28.0, True)


def test_compare_to_mean_threshold():
    assert compare_to_mean([16.0], [10.0, 11.0, 15.0])["exceeds_variance"] is False
    assert (
        compare_to_mean([16.0], [10.0, 11.0, 15.0], threshold=1)["exceeds_variance"]
        is True
    )


def test_compare_to_median():
    assert compare_to_median([12.0, 15.0, 40.0], [10.0, 11.0, 30.0]) == {
        "new_median": 15.0,
        "historical_median": 11.0,
        "historical_mad": pytest.approx(MAD_SCALE),
        "difference": 4.0,
        "percentage": pytest.approx(400 / 11),
        "exceeds_variance": False,
    }


def test_compare_to_median_exceeds_variance():
    # The outlier of 30 does not widen the median absolute deviation.
    result = compare_to_median([12.0, 26.0, 40.0], [10.0, 11.0, 30.0])
    assert (result["difference"], result["exceeds_variance"]) == (15.0, True)


def test_compare_to_median_keeps_outliers():
    result = compare_to_median([14.0, 16.0, 500.0], [10.0, 11.0, 15.0])
    assert result["new_median"] == 16.0
    assert "new_outliers" not in result


def test_remove_outliers():
    assert remove_outliers([10.0, 11.0, 300.0]) == [10.0, 11.0]
    assert remove_outliers([10.0, 200.0, 300.0]) == [10.0, 200.0, 300.0]
    assert remove_outliers([10.0, 300.0]) == [10.0, 300.0]
    assert remove_outliers([10.0]) == [10.0]


def test_remove_outliers_only_removes_worse_values():
    values = [1.0, 100.0, 101.0]
    assert remove_outliers(values) == values
    assert remove_outliers(values, lower_is_better=False) == [100.0, 101.0]


def test_remove_outliers_with_zero_spread():
    assert remove_outliers([10.0, 10.0, 10.0, 15.0, 16.0]) == [10.0, 10.0, 10.0, 15.0]
    below, above = ZERO_VALUE_THRESHOLD, ZERO_VALUE_THRESHOLD + 1
    assert remove_outliers([0.0, 0.0, 0.0, below, above]) == [0.0, 0.0, 0.0, below]


def test_median_absolute_deviation():
    assert median_absolute_deviation([1.0, 2.0, 3.0, 4.0, 100.0]) == pytest.approx(
        MAD_SCALE
    )


@pytest.mark.parametrize(
    ("compare", "field", "spread", "extra"),
    [
        (compare_to_mean, "mean", "stdev", {"new_outliers": 0}),
        (compare_to_median, "median", "mad", {}),
    ],
)
def test_comparison_without_history(compare, field, spread, extra):
    assert compare([15.0], []) == {
        f"new_{field}": 15.0,
        f"historical_{field}": None,
        f"historical_{spread}": None,
        "difference": None,
        "percentage": None,
        "exceeds_variance": None,
        **extra,
    }


@pytest.mark.parametrize(
    ("compare", "field", "spread", "extra"),
    [
        (compare_to_mean, "mean", "stdev", {"new_outliers": 0}),
        (compare_to_median, "median", "mad", {}),
    ],
)
def test_comparison_with_one_historical_value(compare, field, spread, extra):
    assert compare([15.0], [10.0]) == {
        f"new_{field}": 15.0,
        f"historical_{field}": 10.0,
        f"historical_{spread}": None,
        "difference": 5.0,
        "percentage": 50.0,
        "exceeds_variance": None,
        **extra,
    }


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_historical_min_nums(compare, monkeypatch):
    monkeypatch.setattr(comparisons, "HISTORICAL_MIN_NUMS", HISTORICAL_MIN_NUMS)
    history = [10.0, 11.0, 12.0] * HISTORICAL_MIN_NUMS
    assert (
        compare([30.0], history[: HISTORICAL_MIN_NUMS - 1])["exceeds_variance"] is None
    )
    assert compare([30.0], history[:HISTORICAL_MIN_NUMS])["exceeds_variance"] is True


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_only_flags_regressions(compare):
    history = [10.0, 11.0, 12.0]
    assert compare([30.0], history)["exceeds_variance"] is True
    assert compare([-10.0], history)["exceeds_variance"] is False
    assert compare([30.0], history, lower_is_better=False)["exceeds_variance"] is False
    assert compare([-10.0], history, lower_is_better=False)["exceeds_variance"] is True


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_without_threshold(compare):
    result = compare([30.0], [10.0, 11.0, 15.0], threshold=None)
    assert result["difference"] is not None
    assert result["exceeds_variance"] is None


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_without_historical_variance(compare):
    assert compare([10.0], [10.0, 10.0])["exceeds_variance"] is False
    assert compare([15.0], [10.0, 10.0])["exceeds_variance"] is False
    assert compare([16.0], [10.0, 10.0])["exceeds_variance"] is True
    assert (
        compare([4.0], [10.0, 10.0], lower_is_better=False)["exceeds_variance"] is True
    )


def test_compare_to_median_without_historical_variance():
    result = compare_to_median([15.0], [10.0, 10.0, 10.0, 12.0])
    assert (result["historical_mad"], result["exceeds_variance"]) == (0.0, False)
    result = compare_to_median([16.0], [10.0, 10.0, 10.0, 12.0])
    assert (result["historical_mad"], result["exceeds_variance"]) == (0.0, True)


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_with_none_or_iterable_history(compare):
    assert compare([15.0], None) == compare([15.0], [])
    assert compare([15.0], iter([10.0, 11.0, 15.0])) == compare(
        [15.0], [10.0, 11.0, 15.0]
    )


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_percentage_with_negative_historical_value(compare):
    result = compare([-5.0], [-10.0, -10.0])
    assert (result["difference"], result["percentage"]) == (5.0, 50.0)


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_percentage_without_historical_value(compare):
    result = compare([5.0], [0.0, 0.0])
    assert (result["difference"], result["percentage"]) == (5.0, None)


@pytest.mark.parametrize("compare", [compare_to_mean, compare_to_median])
def test_comparison_with_zero_historical_values(compare):
    history = [0.0, 0.0, 0.0]
    below, above = ZERO_VALUE_THRESHOLD, ZERO_VALUE_THRESHOLD + 1
    assert compare([0.0], history)["exceeds_variance"] is False
    assert compare([below], history)["exceeds_variance"] is False
    assert compare([above], history)["exceeds_variance"] is True
    assert compare([-above], history)["exceeds_variance"] is False
    assert compare([-above], history, lower_is_better=False)["exceeds_variance"]
    assert compare([above], history, threshold=None)["exceeds_variance"] is None


def test_comparisons_get():
    assert Comparisons.get("mean") is compare_to_mean
    assert Comparisons.get("median") is compare_to_median
    with pytest.raises(UnknownComparisonError, match="Unknown comparison: unknown"):
        Comparisons.get("unknown")


def test_comparisons_add():
    with mock.patch.dict(Comparisons._methods):

        @Comparisons.add("maximum")
        def compare_to_maximum(
            new_values, historical_values, threshold, lower_is_better
        ):
            return {"historical_maximum": max(historical_values)}

        assert Comparisons.get("maximum") is compare_to_maximum
    assert "maximum" not in Comparisons._methods


if __name__ == "__main__":
    mozunit.main()
