# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import sys

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_root = os.path.dirname(here)
sys.path.insert(0, os.path.join(raptor_root, "raptor"))

spec = importlib.util.spec_from_file_location(
    "nav_bench",
    os.path.join(raptor_root, "browsertime", "support-scripts", "nav_bench.py"),
)
nav_bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nav_bench)


@pytest.mark.parametrize(
    "test_name,expected",
    [
        ("nav-bench", "nav-bench-overall"),
        ("nav-bench-m", "nav-bench-overall"),
        ("nav-bench-a11y", "nav-bench-a11y-overall"),
        ("nav-bench-m-a11y", "nav-bench-a11y-overall"),
    ],
)
def test_summarize_suites_overall_name(test_name, expected):
    """Check that the a11y variants get their own overall suite name."""
    support = nav_bench.NavBenchSupport()
    support._speedindex = {("amazon", "load"): [1000.0, 2000.0]}
    suites = [
        {"name": f"{test_name}.amazon-nav-load", "alertThreshold": 5.0, "subtests": []}
    ]

    support.summarize_suites(suites)

    assert len(suites) == 2
    overall = suites[0]
    assert overall["name"] == expected
    assert [s["name"] for s in overall["subtests"]] == ["amazon-load-score"]
    assert overall["value"] > 0


if __name__ == "__main__":
    mozunit.main()
