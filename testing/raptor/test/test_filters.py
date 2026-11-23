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


if __name__ == "__main__":
    mozunit.main()
