# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import math
import os
import sys

import mozunit
import pytest

# need this so raptor imports work both from /raptor and via mach
here = os.path.abspath(os.path.dirname(__file__))

raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import filters
from filters import geometric_mean


@pytest.mark.parametrize(
    "series, expected",
    [
        ([100.0, 200.0, 400.0], 200.0),
        ([3, 12], 6.0),
        ([42.0], 42.0),
        ([5.0, 0.0, 7.0], 0.0),
        ([0, 0], 0.0),
    ],
)
def test_geometric_mean(series, expected):
    assert geometric_mean(series) == pytest.approx(expected)


@pytest.mark.parametrize("series", [[], [4.0, -1.0]])
def test_geometric_mean_is_nan_for_empty_or_negative_input(series):
    assert math.isnan(geometric_mean(series))


def test_filter_apply_passes_saved_args():
    """Check that Filter.apply passes its saved positional and keyword args to the function."""
    f = filters.Filter(filters.ignore_first, 2)
    assert f.apply([1, 2, 3, 4]) == [3, 4]
    f = filters.Filter(filters.ignore, function=max)
    assert f.apply([1, 9, 3]) == [1, 3]


def test_prepare_returns_single_filter_tuple():
    """Check that prepare returns one-Filter tuples that can be chained."""
    prepared = filters.ignore_first.prepare(1) + filters.median.prepare()
    assert len(prepared) == 2
    assert all(isinstance(p, filters.Filter) for p in prepared)
    data = [10, 1, 2, 3]
    for p in prepared:
        data = p.apply(data)
    assert data == 2


def test_registered_filters_lookup():
    """Check that filters() returns registered functions by name and rejects unknown names."""
    assert filters.filters("mean", "median", "ignore_max") == [
        filters.mean,
        filters.median,
        filters.ignore_max,
    ]
    assert filters.filters() == []
    with pytest.raises(KeyError):
        filters.filters("does_not_exist")


def test_registry_contains_all_filters():
    """Check that every filter in the module is registered under its function name."""
    assert set(filters._FILTERS) == {
        "mean",
        "median",
        "variance",
        "stddev",
        "dromaeo",
        "dromaeo_chunks",
        "geometric_mean",
        "ignore_first",
        "ignore",
        "ignore_max",
        "ignore_min",
        "ignore_negative",
        "v8_subtest",
    }


def test_apply_chains_filters():
    """Check that apply runs filters in order and returns data unchanged with no filters."""
    assert filters.apply([5, 1, 2, 3], [filters.ignore_first, filters.mean]) == 2.0
    assert filters.apply([5, 1], []) == [5, 1]


@pytest.mark.parametrize(
    "string, expected",
    [
        ("median", ["median", []]),
        ("ignore_first:5", ["ignore_first", [5]]),
        ("foo:1.5,2,-3", ["foo", [1.5, 2, -3]]),
    ],
)
def test_parse(string, expected):
    """Check that parse splits the filter name from its int or float arguments."""
    result = filters.parse(string)
    assert result == expected
    assert [type(d) for d in result[1]] == [type(d) for d in expected[1]]


def test_parse_rejects_non_numeric_args():
    """Check that parse raises ValueError for non-numeric arguments."""
    with pytest.raises(ValueError):
        filters.parse("foo:abc")


@pytest.mark.parametrize(
    "series, expected",
    [([1, 2, 3, 4], 2.5), ([7], 7.0), ([-2, 2], 0.0), ([0.5, 1.5, 4.0], 2.0)],
)
def test_mean(series, expected):
    """Check that mean returns the arithmetic mean."""
    assert filters.mean(series) == pytest.approx(expected)


def test_mean_empty_raises():
    """Check that mean raises on empty input, which its docstring disallows."""
    with pytest.raises(ZeroDivisionError):
        filters.mean([])


@pytest.mark.parametrize(
    "series, expected",
    [
        ([3, 1, 2], 2),
        ([4, 1, 3, 2], 2.5),
        ([7], 7),
        ([-5, 10], 2.5),
        ([1, 1, 100, 100, 100], 100),
    ],
)
def test_median(series, expected):
    """Check that median returns the middle value or the average of the two middle values."""
    assert filters.median(series) == expected


def test_median_does_not_mutate_input():
    """Check that median leaves the input list unsorted."""
    series = [3, 1, 2]
    filters.median(series)
    assert series == [3, 1, 2]


def test_median_empty_raises():
    """Check that median raises on empty input, which its docstring disallows."""
    with pytest.raises(IndexError):
        filters.median([])


@pytest.mark.parametrize(
    "series, var, std",
    [
        ([2, 4, 4, 4, 5, 5, 7, 9], 4.0, 2.0),
        ([5], 0.0, 0.0),
        ([1, 3], 1.0, 1.0),
        ([-1, -3, -5], 8 / 3, (8 / 3) ** 0.5),
    ],
)
def test_variance_and_stddev(series, var, std):
    """Check that variance and stddev compute the population statistics."""
    assert filters.variance(series) == pytest.approx(var)
    assert filters.stddev(series) == pytest.approx(std)


def test_dromaeo_chunks():
    """Check that dromaeo_chunks yields fixed-size chunks with a shorter last chunk."""
    assert list(filters.dromaeo_chunks(list(range(7)), 3)) == [
        [0, 1, 2],
        [3, 4, 5],
        [6],
    ]
    assert list(filters.dromaeo_chunks([], 5)) == []


@pytest.mark.parametrize(
    "series, expected",
    [
        ([1, 2, 3, 4, 5], 3.0),
        (list(range(1, 11)), math.sqrt(3 * 8)),
        ([1, 2, 3, 4, 5, 6, 7], math.sqrt(3 * 6.5)),
        ([2.0], 2.0),
    ],
)
def test_dromaeo(series, expected):
    """Check that dromaeo returns the geometric mean of the means of 5-value chunks."""
    assert filters.dromaeo(series) == pytest.approx(expected)


def test_dromaeo_empty_is_nan():
    """Check that dromaeo returns nan for empty input."""
    assert math.isnan(filters.dromaeo([]))


@pytest.mark.parametrize(
    "series, number, expected",
    [
        ([1, 2, 3], 1, [2, 3]),
        ([1, 2, 3, 4], 2, [3, 4]),
        ([1], 1, [1]),
        ([1, 2], 2, [1, 2]),
        ([], 1, []),
        ([1, 2], 0, [1, 2]),
    ],
)
def test_ignore_first(series, number, expected):
    """Check that ignore_first drops the first values unless the series is too short."""
    assert filters.ignore_first(series, number) == expected


def test_ignore_first_default_number():
    """Check that ignore_first drops one value by default."""
    assert filters.ignore_first([9, 8, 7]) == [8, 7]


def test_ignore_removes_first_match_without_mutating():
    """Check that ignore removes only the first matching value and copies the input."""
    series = [1, 5, 3, 5]
    assert filters.ignore(series, max) == [1, 3, 5]
    assert series == [1, 5, 3, 5]


@pytest.mark.parametrize("series", [[], [4]])
def test_ignore_short_series_unchanged(series):
    """Check that ignore returns series of one value or fewer as is."""
    assert filters.ignore(series, max) is series


@pytest.mark.parametrize(
    "func, series, expected",
    [
        (filters.ignore_max, [3, 9, 1], [3, 1]),
        (filters.ignore_max, [2, 2], [2]),
        (filters.ignore_max, [5], [5]),
        (filters.ignore_min, [3, 9, 1], [3, 9]),
        (filters.ignore_min, [-4, 0, -4], [0, -4]),
        (filters.ignore_min, [], []),
    ],
)
def test_ignore_max_min(func, series, expected):
    """Check that ignore_max and ignore_min drop one extreme value."""
    assert func(series) == expected


@pytest.mark.parametrize(
    "series, expected",
    [
        ([-1, 2, -3, 0, 4], [2, 0, 4]),
        ([-1, -2], []),
        ([], []),
        ([1.5, 2.5], [1.5, 2.5]),
    ],
)
def test_ignore_negative(series, expected):
    """Check that ignore_negative drops negative values without mutating the input."""
    original = list(series)
    assert filters.ignore_negative(series) == expected
    assert series == original


@pytest.mark.parametrize(
    "name, series, expected",
    [
        ("Splay", [4.0, 16.0], 81491.0 / 8.0),
        ("Richards", [35302.0], 1.0),
        ("Encrypt", [1.0, 100.0], 266181.0 / 10.0),
    ],
)
def test_v8_subtest(name, series, expected):
    """Check that v8_subtest divides the reference score by the geometric mean."""
    assert filters.v8_subtest(series, name) == pytest.approx(expected)


def test_v8_subtest_unknown_name_raises():
    """Check that v8_subtest raises KeyError for an unknown benchmark name."""
    with pytest.raises(KeyError):
        filters.v8_subtest([1.0], "Crypto")


if __name__ == "__main__":
    mozunit.main()
