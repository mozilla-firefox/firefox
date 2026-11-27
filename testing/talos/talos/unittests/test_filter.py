# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import math

import mozunit
import pytest

from talos import filter


def test_prepare_returns_filter_with_bound_args():
    (f,) = filter.ignore_first.prepare(2)
    assert isinstance(f, filter.Filter)
    assert f.func is filter.ignore_first
    assert f.apply([1, 2, 3, 4]) == [3, 4]


def test_filters_lookup_and_apply():
    funcs = filter.filters("ignore_max", "mean")
    assert funcs == [filter.ignore_max, filter.mean]
    assert filter.apply([1, 2, 9], funcs) == 1.5


def test_filters_unknown_name():
    with pytest.raises(KeyError):
        filter.filters("does_not_exist")


@pytest.mark.parametrize(
    "string,expected",
    [
        ("median", ["median", []]),
        ("ignore_first:5", ["ignore_first", [5]]),
        ("foo:1,2.5", ["foo", [1, 2.5]]),
    ],
)
def test_parse_splits_name_and_numeric_arguments(string, expected):
    assert filter.parse(string) == expected


def test_mean_and_median_of_odd_and_even_series():
    assert filter.mean([1, 2, 3, 4]) == 2.5
    assert filter.median([3, 1, 2]) == 2
    assert filter.median([4, 1, 3, 2]) == 2.5


def test_variance_and_stddev_of_known_series():
    assert filter.variance([2, 4, 4, 4, 5, 5, 7, 9]) == 4.0
    assert filter.stddev([2, 4, 4, 4, 5, 5, 7, 9]) == 2.0


def test_geometric_mean():
    assert filter.geometric_mean([0, 0]) == pytest.approx(0)
    assert filter.geometric_mean([1, 7]) == pytest.approx(math.sqrt(2 * 8) - 1)


def test_dromaeo_chunks_and_score():
    assert list(filter.dromaeo_chunks(list(range(7)), 5)) == [
        [0, 1, 2, 3, 4],
        [5, 6],
    ]
    series = [1] * 5 + [3] * 5
    assert filter.dromaeo(series) == pytest.approx(filter.geometric_mean([1.0, 3.0]))


def test_ignore_first_short_series_untouched():
    data = [1]
    assert filter.ignore_first(data) is data
    assert filter.ignore_first([1, 2, 3]) == [2, 3]


def test_ignore_min_max_do_not_mutate():
    data = [5, 1, 9, 1]
    assert filter.ignore_max(data) == [5, 1, 1]
    assert filter.ignore_min(data) == [5, 9, 1]
    assert data == [5, 1, 9, 1]
    assert filter.ignore([7], max) == [7]


def test_v8_subtest():
    assert filter.v8_subtest([1, 1], "Richards") == pytest.approx(35302.0)
    with pytest.raises(KeyError):
        filter.v8_subtest([1], "Unknown")


if __name__ == "__main__":
    mozunit.main()
