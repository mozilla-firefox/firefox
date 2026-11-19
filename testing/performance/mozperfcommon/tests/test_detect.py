#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from collections import Counter
from datetime import datetime, timedelta, timezone
from unittest import mock

import mozunit
import pytest
import requests
from mozperfcommon import comparisons
from mozperfcommon.comparisons import (
    DEFAULT_THRESHOLD,
    Comparisons,
    UnknownComparisonError,
)
from mozperfcommon.detect import XtremeRegression, XtremeRegressionError
from mozperfcommon.perfherder import PerfherderClient
from mozperfcommon.utils import UnknownFrameworkError

SIGNATURES = {
    10: {
        "id": 10,
        "signature_hash": "suitehash",
        "suite": "about_preferences_basic",
        "application": "firefox",
        "extra_options": ["fission", "e10s"],
        "has_subtests": True,
    },
    11: {
        "id": 11,
        "signature_hash": "subtesthash",
        "suite": "about_preferences_basic",
        "test": "preferences",
        "application": "firefox",
        "extra_options": ["e10s", "fission"],
        "parent_signature": "suitehash",
    },
    12: {
        "id": 12,
        "signature_hash": "chromehash",
        "suite": "about_preferences_basic",
        "test": "preferences",
        "application": "chrome",
        "extra_options": ["e10s", "fission"],
    },
    13: {
        "id": 13,
        "signature_hash": "sessionrestorehash",
        "suite": "sessionrestore",
        "test": "sessionrestore",
        "application": "firefox",
        "extra_options": ["e10s", "fission"],
    },
}

PARENT_IDS = {11: 10}

# Pushes 2 to 7 are in the default history window, which ends at the landedrev
# push. Push 1 is before it.
PUSH_TIMES = {
    1: "2026-09-23T04:00:00",
    2: "2026-09-30T07:00:00",
    3: "2026-09-30T10:00:00",
    4: "2026-09-30T13:00:00",
    5: "2026-09-30T16:00:00",
    6: "2026-09-30T19:00:00",
    7: "2026-09-30T22:00:00",
}

# Historical (push id, value) points of each signature. The points of push 1
# are not used with the default history window.
DATA = {
    10: [
        (1, 90.0),
        (2, 110.0),
        (3, 110.0),
        (4, 100.0),
        (5, 110.0),
        (6, 120.0),
        (7, 110.0),
    ],
    11: [(1, 10.0), (2, 50.0), (3, 50.0), (4, 40.0), (5, 45.0), (6, 55.0), (7, 60.0)],
    12: [(6, 7.0), (7, 9.0)],
    13: [
        (1, 500.0),
        (2, 310.0),
        (3, 310.0),
        (4, 300.0),
        (5, 320.0),
        (6, 300.0),
        (7, 320.0),
    ],
}

PUSHES = {
    ("autoland", "landedrev"): 1790825216,
    ("try", "tryrev"): 1790900000,
}

END_DATE = "2026-10-01T03:26:56"
START_DATE = "2026-09-24T03:26:56"


SESSIONRESTORE_SUBTEST = {
    "name": "sessionrestore",
    "value": 330.0,
    "replicates": [325.0, 335.0],
}


def perfherder_data(**overrides):
    data = {
        "framework": {"name": "talos"},
        "application": {"name": "firefox"},
        "suites": [
            {
                "name": "about_preferences_basic",
                "value": 125.0,
                "replicates": [120.0, 130.0],
                "extraOptions": ["e10s", "fission"],
                "subtests": [
                    {
                        "name": "preferences",
                        "value": 45.0,
                        "replicates": [44.0, 46.0],
                        "shouldAlert": True,
                    },
                    {
                        "name": "search",
                        "value": 10.0,
                        "replicates": [9.0, 11.0],
                        "shouldAlert": True,
                    },
                ],
            }
        ],
    }
    data.update(overrides)
    return data


def make_response(payload):
    response = mock.Mock()
    response.json.return_value = payload
    return response


def summary(signature_id, params):
    signature = SIGNATURES[signature_id]
    # The nth point of a push comes from job 100 * push id + n, for every
    # signature, so the subtests share the jobs of their suite.
    points = []
    job_counts = Counter()
    for push_id, value in DATA[signature_id]:
        job_id = 100 * push_id + job_counts[push_id]
        job_counts[push_id] += 1
        if params["startday"] < PUSH_TIMES[push_id] < params["endday"]:
            points.append((push_id, job_id, value))
    all_data = params.get("all_data") == "true"
    return {
        "signature_id": signature_id,
        "signature_hash": signature["signature_hash"],
        "suite": signature["suite"],
        "test": signature.get("test", ""),
        "values": [] if all_data else [value for _, _, value in points],
        "job_ids": [] if all_data else [job_id for _, job_id, _ in points],
        "data": [
            {
                "push_id": push_id,
                "job_id": job_id,
                "push_timestamp": PUSH_TIMES[push_id],
                "value": value,
            }
            for push_id, job_id, value in points
        ]
        if all_data
        else [],
    }


@pytest.fixture
def session():
    session = mock.Mock()
    session.headers = {}

    def get(url, params=None, timeout=None):
        if url.endswith("/push/"):
            repository = url.split("/project/")[1].split("/")[0]
            timestamp = PUSHES.get((repository, params["revision"]))
            results = [{"push_timestamp": timestamp}] if timestamp else []
            return make_response({"results": results})
        if url.endswith("/performance/signatures/"):
            return make_response({
                str(signature_id): signature
                for signature_id, signature in SIGNATURES.items()
                if params["subtests"] or signature_id not in PARENT_IDS
            })
        if url.endswith("/api/performance/summary/"):
            if "signature" in params:
                return make_response([summary(params["signature"], params)])
            return make_response([
                summary(signature_id, params)
                for signature_id, parent_id in PARENT_IDS.items()
                if parent_id == params["parent_signature"]
            ])
        raise AssertionError(f"Unexpected URL {url}")

    session.get.side_effect = get
    return session


@pytest.fixture(autouse=True)
def historical_min_nums(monkeypatch):
    monkeypatch.setattr(comparisons, "HISTORICAL_MIN_NUMS", 2)


@pytest.fixture(autouse=True)
def ci_revision(monkeypatch):
    # Run as in CI by default, so that the history window contains PUSH_TIMES.
    monkeypatch.setenv("GECKO_HEAD_REV", "landedrev")


@pytest.fixture(autouse=True)
def client(session):
    with mock.patch("mozperfcommon.perfherder.requests.Session", return_value=session):
        client = PerfherderClient()
    with mock.patch("mozperfcommon.detect.PerfherderClient", return_value=client):
        yield client


@pytest.fixture(autouse=True)
def expected_changes():
    with mock.patch(
        "mozperfcommon.detect.load_expected_changes", return_value=[]
    ) as load_expected_changes:
        yield load_expected_changes


def calls_to(session, path):
    return [call for call in session.get.call_args_list if call.args[0].endswith(path)]


def summary_params(session):
    return [call.kwargs["params"] for call in calls_to(session, "/summary/")]


def test_detect(client):
    results = XtremeRegression("linux1804-64-shippable").detect(perfherder_data())

    assert results == [
        {
            "suite": "about_preferences_basic",
            "test": None,
            "value": 125.0,
            "new_mean": 125.0,
            "historical_mean": 110.0,
            "historical_stdev": pytest.approx((200 / 5) ** 0.5),
            "difference": 15.0,
            "percentage": pytest.approx(1500 / 110),
            "exceeds_variance": False,
            "new_outliers": 0,
            "new_count": 2,
            "count": 6,
            "signature_id": 10,
            "signature_key": (
                "about_preferences_basic",
                None,
                ("e10s", "fission"),
                "firefox",
            ),
            "expected_change": None,
        },
        {
            "suite": "about_preferences_basic",
            "test": "preferences",
            "value": 45.0,
            "new_mean": 45.0,
            "historical_mean": 50.0,
            "historical_stdev": pytest.approx((250 / 5) ** 0.5),
            "difference": -5.0,
            "percentage": -10.0,
            "exceeds_variance": False,
            "new_outliers": 0,
            "new_count": 2,
            "count": 6,
            "signature_id": 11,
            "signature_key": (
                "about_preferences_basic",
                "preferences",
                ("e10s", "fission"),
                "firefox",
            ),
            "expected_change": None,
        },
        {
            "suite": "about_preferences_basic",
            "test": "search",
            "value": 10.0,
            "new_mean": 10.0,
            "historical_mean": None,
            "historical_stdev": None,
            "difference": None,
            "percentage": None,
            "exceeds_variance": None,
            "new_outliers": 0,
            "new_count": 2,
            "count": 0,
            "signature_id": None,
            "signature_key": (
                "about_preferences_basic",
                "search",
                ("e10s", "fission"),
                "firefox",
            ),
            "expected_change": None,
        },
    ]


def test_detect_queries(client, session):
    XtremeRegression("linux1804-64-shippable", revision="landedrev").detect(
        perfherder_data()
    )

    [push_call] = calls_to(session, "/push/")
    assert (
        push_call.args[0] == "https://treeherder.mozilla.org/api/project/autoland/push/"
    )
    assert push_call.kwargs["params"] == {"revision": "landedrev"}

    [signatures_call] = calls_to(session, "/performance/signatures/")
    assert signatures_call.args[0] == (
        "https://treeherder.mozilla.org/api/project/autoland/performance/signatures/"
    )
    assert signatures_call.kwargs["params"] == {
        "framework": 1,
        "platform": "linux1804-64-shippable",
        "start_date": START_DATE,
        "subtests": 0,
    }

    common = {
        "repository": "autoland",
        "startday": START_DATE,
        "endday": END_DATE,
        "replicates": "true",
    }
    assert summary_params(session) == [
        {**common, "signature": 10, "all_data": "true"},
        {**common, "parent_signature": 10},
    ]


def test_detect_without_replicates(client, session):
    XtremeRegression("linux1804-64-shippable", replicates=False).detect(
        perfherder_data()
    )

    assert {params["replicates"] for params in summary_params(session)} == {"false"}


def test_detect_matches_application(client, session):
    data = perfherder_data(application={"name": "chrome"})
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [
        (result["signature_id"], result["historical_mean"]) for result in results
    ] == [(None, None), (12, 8.0), (None, None)]
    assert [params["signature"] for params in summary_params(session)] == [12]


def test_detect_suite_variants():
    data = perfherder_data()
    variant = dict(data["suites"][0], extraOptions=["e10s"])
    data["suites"].append(variant)
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [(result["signature_key"], result["count"]) for result in results] == [
        (("about_preferences_basic", None, ("e10s", "fission"), "firefox"), 6),
        (("about_preferences_basic", "preferences", ("e10s", "fission"), "firefox"), 6),
        (("about_preferences_basic", "search", ("e10s", "fission"), "firefox"), 0),
        (("about_preferences_basic", None, ("e10s",), "firefox"), 0),
        (("about_preferences_basic", "preferences", ("e10s",), "firefox"), 0),
        (("about_preferences_basic", "search", ("e10s",), "firefox"), 0),
    ]


def test_detect_expected_change_for_one_variant(expected_changes):
    expected_changes.return_value = [
        {
            "bug": 1234567,
            "suite": "about_preferences_basic",
            "extra_options": ["fission", "e10s"],
            "expires": datetime(2026, 12, 31).date(),
        }
    ]
    data = perfherder_data()
    data["suites"].append(dict(data["suites"][0], extraOptions=["e10s"]))
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [
        (result["signature_key"][2], result["expected_change"]) for result in results
    ] == [(("e10s", "fission"), 1234567)] * 3 + [(("e10s",), None)] * 3


def test_detect_suite_without_value(client, session):
    data = perfherder_data(
        suites=[
            {
                "name": "sessionrestore",
                "extraOptions": ["fission", "e10s"],
                "subtests": [SESSIONRESTORE_SUBTEST],
            }
        ]
    )
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert results == [
        {
            "suite": "sessionrestore",
            "test": "sessionrestore",
            "value": 330.0,
            "new_mean": 330.0,
            "historical_mean": 310.0,
            "historical_stdev": pytest.approx((400 / 5) ** 0.5),
            "difference": 20.0,
            "percentage": pytest.approx(2000 / 310),
            "exceeds_variance": False,
            "new_outliers": 0,
            "new_count": 2,
            "count": 6,
            "signature_id": 13,
            "signature_key": (
                "sessionrestore",
                "sessionrestore",
                ("e10s", "fission"),
                "firefox",
            ),
            "expected_change": None,
        }
    ]
    assert [params["signature"] for params in summary_params(session)] == [13]


def test_detect_uses_replicates(client):
    data = perfherder_data()
    data["suites"][0]["replicates"] = [120.0, 130.0, 140.0, 150.0]
    data["suites"][0]["subtests"][0]["replicates"] = [44.0, 46.0, 51.0]
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [
        (
            result["test"],
            result["value"],
            result["new_count"],
            result["new_mean"],
            result["difference"],
        )
        for result in results
    ] == [
        (None, 125.0, 4, 135.0, 25.0),
        ("preferences", 45.0, 3, 47.0, -3.0),
        ("search", 10.0, 2, 10.0, None),
    ]


def failing_response(error):
    response = mock.Mock()
    response.raise_for_status.side_effect = error
    return response


def invalid_json_response():
    response = mock.Mock()
    response.json.side_effect = ValueError("Expecting value")
    return response


@pytest.mark.parametrize(
    "failure",
    [
        lambda: requests.ConnectionError("connection reset"),
        lambda: failing_response(requests.HTTPError("503 Server Error")),
        invalid_json_response,
    ],
    ids=["connection-error", "http-error", "invalid-json"],
)
def test_detect_survives_summary_failure(client, session, caplog, failure):
    get = session.get.side_effect

    def get_or_fail(url, params=None, timeout=None):
        if url.endswith("/summary/") and "parent_signature" in params:
            result = failure()
            if isinstance(result, Exception):
                raise result
            return result
        return get(url, params=params, timeout=timeout)

    session.get.side_effect = get_or_fail
    results = XtremeRegression("linux1804-64-shippable").detect(perfherder_data())

    assert [(result["test"], result["historical_mean"]) for result in results] == [
        (None, 110.0),
        ("preferences", None),
        ("search", None),
    ]
    assert "Failed to get the Perfherder summary for {'parent_signature_id': 10}" in (
        caplog.text
    )


@pytest.mark.parametrize(
    ("push_limit", "expected"),
    [
        (2, [(None, 115.0, 2), ("preferences", 57.5, 2)]),
        (4, [(None, 110.0, 4), ("preferences", 50.0, 4)]),
        (
            None,
            [
                (None, pytest.approx(750 / 7), 7),
                ("preferences", pytest.approx(310 / 7), 7),
            ],
        ),
    ],
)
def test_detect_push_limit(client, push_limit, expected):
    results = XtremeRegression(
        "linux1804-64-shippable", days=9, push_limit=push_limit
    ).detect(perfherder_data())

    assert [
        (result["test"], result["historical_mean"], result["count"])
        for result in results[:2]
    ] == expected


def test_detect_push_limit_per_suite(client, session):
    data = perfherder_data()
    data["suites"].append({
        "name": "sessionrestore",
        "extraOptions": ["fission", "e10s"],
        "subtests": [SESSIONRESTORE_SUBTEST],
    })
    results = XtremeRegression("linux1804-64-shippable", push_limit=1).detect(data)

    assert [(result["test"], result["historical_mean"]) for result in results] == [
        (None, 110.0),
        ("preferences", 60.0),
        ("search", None),
        ("sessionrestore", 320.0),
    ]
    all_data_params = [p for p in summary_params(session) if "all_data" in p]
    assert [params["signature"] for params in all_data_params] == [10, 13]


def test_detect_push_limit_suite_value_missing(client, session):
    data = perfherder_data()
    del data["suites"][0]["value"]
    results = XtremeRegression("linux1804-64-shippable", push_limit=2).detect(data)

    assert [(result["test"], result["historical_mean"]) for result in results] == [
        ("preferences", 57.5),
        ("search", None),
    ]
    assert [
        (params.get("signature"), params.get("parent_signature"), "all_data" in params)
        for params in summary_params(session)
    ] == [(10, None, True), (None, 10, False)]


def test_detect_default_history(client, session, monkeypatch):
    # Every push of the default history window is used.
    monkeypatch.setitem(PUSH_TIMES, 8, "2026-09-25T04:00:00")
    monkeypatch.setitem(DATA, 10, DATA[10] + [(8, 110.0)])
    results = XtremeRegression("linux1804-64-shippable").detect(perfherder_data())

    assert (results[0]["historical_mean"], results[0]["count"]) == (110.0, 7)
    assert {p["startday"] for p in summary_params(session)} == {START_DATE}


def test_detect_ignores_pushes_after_revision(client, monkeypatch):
    monkeypatch.setitem(PUSH_TIMES, 8, "2026-10-01T05:00:00")
    monkeypatch.setitem(DATA, 10, DATA[10] + [(8, 1000.0)])
    results = XtremeRegression("linux1804-64-shippable").detect(perfherder_data())

    assert (results[0]["historical_mean"], results[0]["count"]) == (110.0, 6)


def test_detect_push_limit_filters_subtest_jobs(client, monkeypatch):
    # Push 7 is retriggered for the suite, and its second job has a subtest
    # value too, while the subtest also has a value from a job of push 8, after
    # the revision.
    monkeypatch.setitem(PUSH_TIMES, 8, "2026-10-01T05:00:00")
    monkeypatch.setitem(DATA, 10, DATA[10] + [(7, 130.0)])
    monkeypatch.setitem(DATA, 11, DATA[11] + [(7, 70.0), (8, 1000.0)])
    results = XtremeRegression("linux1804-64-shippable", push_limit=1).detect(
        perfherder_data()
    )

    assert [
        (result["test"], result["historical_mean"], result["count"])
        for result in results[:2]
    ] == [(None, 120.0, 2), ("preferences", 65.0, 2)]


def test_detect_push_limit_counts_pushes(client, monkeypatch):
    # Retriggers on push 7 give it two points, which are both used.
    monkeypatch.setitem(DATA, 10, [(5, 100.0), (6, 110.0), (7, 120.0), (7, 130.0)])
    results = XtremeRegression("linux1804-64-shippable", push_limit=2).detect(
        perfherder_data()
    )

    assert (results[0]["historical_mean"], results[0]["count"]) == (120.0, 3)


def test_detect_push_limit_without_parent(client):
    data = perfherder_data(
        suites=[
            {
                "name": "sessionrestore",
                "extraOptions": ["fission", "e10s"],
                "subtests": [SESSIONRESTORE_SUBTEST],
            }
        ]
    )
    results = XtremeRegression("linux1804-64-shippable", push_limit=1).detect(data)

    assert (results[0]["historical_mean"], results[0]["count"]) == (320.0, 1)


def test_detect_suite_without_replicates(client, session):
    data = perfherder_data()
    del data["suites"][0]["replicates"]
    results = XtremeRegression("linux1804-64-shippable", push_limit=2).detect(data)

    # The suite is not compared, but still limits its subtests to its pushes.
    assert [(result["test"], result["historical_mean"]) for result in results] == [
        ("preferences", 57.5),
        ("search", None),
    ]
    assert [
        (params.get("signature"), params.get("parent_signature"), "all_data" in params)
        for params in summary_params(session)
    ] == [(10, None, True), (None, 10, False)]


def test_detect_suite_without_replicates_or_subtests(client, session):
    data = perfherder_data()
    del data["suites"][0]["replicates"]
    data["suites"][0]["subtests"] = []

    assert XtremeRegression("linux1804-64-shippable").detect(data) == []
    assert summary_params(session) == []


@pytest.mark.parametrize("replicates", [None, [], [45.0]])
def test_detect_subtest_without_replicates(client, session, replicates):
    data = perfherder_data()
    for subtest in data["suites"][0]["subtests"]:
        subtest["replicates"] = replicates
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [result["test"] for result in results] == [None]
    assert [
        (params.get("signature"), params.get("parent_signature"))
        for params in summary_params(session)
    ] == [(10, None)]


@pytest.mark.parametrize("replicates", [None, [], [330.0]])
def test_detect_subtest_without_replicates_or_parent(client, session, replicates):
    data = perfherder_data(
        suites=[
            {
                "name": "sessionrestore",
                "extraOptions": ["fission", "e10s"],
                "subtests": [dict(SESSIONRESTORE_SUBTEST, replicates=replicates)],
            }
        ]
    )

    assert XtremeRegression("linux1804-64-shippable").detect(data) == []
    assert summary_params(session) == []


@pytest.mark.parametrize(
    ("suite_should_alert", "subtest_should_alert", "expected"),
    [
        (None, True, [None, "preferences"]),
        (None, None, [None]),
        (None, False, [None]),
        (True, None, [None]),
        (False, True, ["preferences"]),
        (False, None, []),
        (False, False, []),
    ],
)
def test_detect_should_alert(
    client, suite_should_alert, subtest_should_alert, expected
):
    data = perfherder_data()
    suite = data["suites"][0]
    suite["subtests"] = [
        {"name": "preferences", "value": 45.0, "replicates": [44.0, 46.0]}
    ]
    if suite_should_alert is not None:
        suite["shouldAlert"] = suite_should_alert
    if subtest_should_alert is not None:
        suite["subtests"][0]["shouldAlert"] = subtest_should_alert
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [result["test"] for result in results] == expected


@pytest.mark.parametrize(
    ("should_alert", "expected"),
    [(None, ["sessionrestore"]), (True, ["sessionrestore"]), (False, [])],
)
def test_detect_should_alert_suite_without_value(
    client, session, should_alert, expected
):
    subtest = dict(SESSIONRESTORE_SUBTEST)
    if should_alert is not None:
        subtest["shouldAlert"] = should_alert
    data = perfherder_data(
        suites=[
            {
                "name": "sessionrestore",
                "extraOptions": ["fission", "e10s"],
                "subtests": [subtest],
            }
        ]
    )
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [result["test"] for result in results] == expected
    assert [params["signature"] for params in summary_params(session)] == (
        [13] if expected else []
    )


def test_detect_should_alert_skips_subtest_queries(client, session):
    data = perfherder_data()
    for subtest in data["suites"][0]["subtests"]:
        subtest["shouldAlert"] = False
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [result["test"] for result in results] == [None]
    assert [
        (params.get("signature"), params.get("parent_signature"))
        for params in summary_params(session)
    ] == [(10, None)]


def test_detect_threshold(client):
    results = XtremeRegression("linux1804-64-shippable").detect(perfherder_data())
    assert results[1]["exceeds_variance"] is False

    results = XtremeRegression("linux1804-64-shippable").detect(
        perfherder_data(), threshold=0.5
    )
    # The preferences decrease is an improvement.
    assert [result["exceeds_variance"] for result in results] == [True, False, None]


def test_detect_higher_is_better(client):
    data = perfherder_data()
    data["suites"][0]["lowerIsBetter"] = False
    data["suites"][0]["subtests"][0]["lowerIsBetter"] = False
    results = XtremeRegression("linux1804-64-shippable").detect(data, threshold=0.5)
    # The suite increase is now an improvement, and the preferences decrease a
    # regression.
    assert [result["exceeds_variance"] for result in results] == [False, True, None]


def test_detect_expected_changes(expected_changes):
    expected_changes.return_value = [
        {
            "bug": 1234567,
            "suite": "about_preferences_*",
            "test": "pref*",
            "platform": "linux*",
            "expires": datetime(2026, 10, 1).date(),
        }
    ]
    data = perfherder_data()
    data["suites"][0]["subtests"][0]["lowerIsBetter"] = False
    results = XtremeRegression("linux1804-64-shippable").detect(data, threshold=0.5)

    assert [
        (result["test"], result["expected_change"], result["exceeds_variance"])
        for result in results
    ] == [(None, None, True), ("preferences", 1234567, None), ("search", None, None)]
    assert results[1]["difference"] == -5.0


def test_detect_expired_expected_change(expected_changes):
    expected_changes.return_value = [
        {"bug": 1234567, "suite": "*", "expires": datetime(2026, 9, 30).date()}
    ]
    results = XtremeRegression("linux1804-64-shippable").detect(
        perfherder_data(), threshold=0.5
    )

    assert [result["expected_change"] for result in results] == [None, None, None]
    assert results[0]["exceeds_variance"] is True


def test_detect_suite_without_subtests(client, session):
    data = perfherder_data()
    data["suites"][0]["subtests"] = []
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert [result["historical_mean"] for result in results] == [110.0]
    assert [params.get("signature") for params in summary_params(session)] == [10]


def test_detect_no_matches(client, session):
    data = perfherder_data(application={"name": "safari"})
    results = XtremeRegression("linux1804-64-shippable").detect(data)

    assert all(result["historical_mean"] is None for result in results)
    assert summary_params(session) == []


def test_detect_uses_ci_revision(client, session):
    XtremeRegression("linux1804-64-shippable").detect(perfherder_data())

    [push_call] = calls_to(session, "/push/")
    assert push_call.kwargs["params"] == {"revision": "landedrev"}
    assert {params["endday"] for params in summary_params(session)} == {END_DATE}


def test_detect_explicit_revision_overrides_ci(client, session, monkeypatch):
    monkeypatch.setenv("GECKO_HEAD_REV", "otherrev")
    XtremeRegression("linux1804-64-shippable", revision="landedrev").detect(
        perfherder_data()
    )

    [push_call] = calls_to(session, "/push/")
    assert push_call.kwargs["params"] == {"revision": "landedrev"}


def test_detect_revision_repository(client, session):
    XtremeRegression(
        "linux1804-64-shippable", revision="tryrev", revision_repository="try"
    ).detect(perfherder_data())

    [push_call] = calls_to(session, "/push/")
    assert push_call.args[0] == "https://treeherder.mozilla.org/api/project/try/push/"
    assert {params["repository"] for params in summary_params(session)} == {"autoland"}
    assert {params["endday"] for params in summary_params(session)} == {
        "2026-10-02T00:13:20"
    }


def test_detect_unknown_revision(client, session, caplog):
    results = XtremeRegression("linux1804-64-shippable", revision="tryrev").detect(
        perfherder_data()
    )

    assert all(result["historical_mean"] is None for result in results)
    assert calls_to(session, "/signatures/") == []
    assert summary_params(session) == []
    assert "No autoland push found for revision tryrev" in caplog.text


@pytest.mark.parametrize("path", ["/push/", "/signatures/"])
def test_detect_survives_treeherder_failure(client, session, caplog, path):
    get = session.get.side_effect

    def get_or_fail(url, params=None, timeout=None):
        if url.endswith(path):
            raise requests.ConnectionError("connection reset")
        return get(url, params=params, timeout=timeout)

    session.get.side_effect = get_or_fail
    detector = XtremeRegression("linux1804-64-shippable")
    results = detector.detect(perfherder_data())

    assert [(result["test"], result["historical_mean"]) for result in results] == [
        (None, None),
        ("preferences", None),
        ("search", None),
    ]
    assert summary_params(session) == []
    assert "Failed to get the Perfherder history" in caplog.text

    session.get.side_effect = get
    results = detector.detect(perfherder_data())
    assert results[0]["historical_mean"] == 110.0


def test_detect_without_revision_uses_now(client, session, monkeypatch):
    monkeypatch.delenv("GECKO_HEAD_REV")
    before = datetime.now(timezone.utc).replace(microsecond=0)
    XtremeRegression("linux1804-64-shippable", days=3).detect(perfherder_data())
    after = datetime.now(timezone.utc)

    assert calls_to(session, "/push/") == []
    params = summary_params(session)[0]
    start_date, end_date = (
        datetime.strptime(params[key], "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)
        for key in ("startday", "endday")
    )
    assert before <= end_date <= after
    assert end_date - start_date == timedelta(days=3)


def test_detect_median_comparison(client):
    results = XtremeRegression("linux1804-64-shippable").detect(
        perfherder_data(), comparison="median"
    )

    assert [
        (result["test"], result["historical_median"], result["difference"])
        for result in results
    ] == [(None, 110.0, 15.0), ("preferences", 50.0, -5.0), ("search", None, None)]
    assert all("historical_mean" not in result for result in results)


def test_detect_registered_comparison(client):
    with mock.patch.dict(Comparisons._methods):

        @Comparisons.add("count_lower")
        def count_lower(new_values, historical_values, threshold, lower_is_better):
            return {
                "lower": sum(1 for v in historical_values if v < min(new_values)),
                "threshold": threshold,
            }

        results = XtremeRegression("linux1804-64-shippable").detect(
            perfherder_data(), comparison="count_lower"
        )

    assert [result["lower"] for result in results] == [5, 1, 0]
    assert [result["threshold"] for result in results] == [DEFAULT_THRESHOLD] * 3


def test_detect_unknown_comparison(client, session):
    with pytest.raises(UnknownComparisonError):
        XtremeRegression("linux1804-64-shippable").detect(
            perfherder_data(), comparison="unknown"
        )
    assert session.get.call_count == 0


def test_unknown_framework(client):
    data = perfherder_data(framework={"name": "unknown"})
    with pytest.raises(UnknownFrameworkError):
        XtremeRegression("linux1804-64-shippable").detect(data)


def test_xtreme_regression_error(client):
    data = perfherder_data()
    data["suites"][0]["subtests"][0]["lowerIsBetter"] = False
    results = XtremeRegression("linux1804-64-shippable").detect(data, threshold=0.5)
    detections = [result for result in results if result["exceeds_variance"]]

    error = XtremeRegressionError(detections)

    assert error.detections == detections
    assert str(error) == (
        "2 extreme regression(s) detected, top 2: "
        "about_preferences_basic (e10s, fission) [firefox] (+13.64%), "
        "about_preferences_basic preferences (e10s, fission) [firefox] (-10.00%)\n"
        "  about_preferences_basic (e10s, fission) [firefox]: "
        "difference 15.00 (+13.64%)\n"
        "  about_preferences_basic preferences (e10s, fission) [firefox]: "
        "difference -5.00 (-10.00%)"
    )


def test_xtreme_regression_error_top_regressions():
    detections = [
        {
            "suite": "suite",
            "test": test,
            "signature_key": ("suite", test, (), ""),
            "difference": 1.0,
            "percentage": percentage,
        }
        for test, percentage in [("a", 5.0), ("b", -40.0), ("c", None), ("d", 20.0)]
    ]

    lines = str(XtremeRegressionError(detections)).splitlines()

    assert lines[0] == (
        "4 extreme regression(s) detected, top 3: "
        "suite b (-40.00%), suite d (+20.00%), suite a (+5.00%)"
    )
    assert len(lines) == 5


def test_detect_caches_signatures(session):
    detector = XtremeRegression("linux1804-64-shippable")
    first = detector.detect(perfherder_data())
    second = detector.detect(perfherder_data())

    assert len(calls_to(session, "/performance/signatures/")) == 1
    assert second == first


def test_detect_refetches_signatures_for_other_framework(session):
    detector = XtremeRegression("linux1804-64-shippable")
    detector.detect(perfherder_data())
    detector.detect(perfherder_data(framework={"name": "browsertime"}))

    assert [
        call.kwargs["params"]["framework"]
        for call in calls_to(session, "/performance/signatures/")
    ] == [1, 13]


def test_detect_raise_for_regressions():
    data = perfherder_data()
    data["suites"][0]["subtests"][0]["lowerIsBetter"] = False

    with pytest.raises(XtremeRegressionError) as error:
        XtremeRegression("linux1804-64-shippable").detect(
            data, threshold=0.5, raise_for_regressions=True
        )

    assert [
        (detection["suite"], detection["test"]) for detection in error.value.detections
    ] == [("about_preferences_basic", None), ("about_preferences_basic", "preferences")]


def test_detect_raise_for_regressions_without_regressions():
    results = XtremeRegression("linux1804-64-shippable").detect(
        perfherder_data(), raise_for_regressions=True
    )

    assert [result["exceeds_variance"] for result in results] == [False, False, None]


def test_raise_for_regressions_ignores_expected_changes(expected_changes):
    expected_changes.return_value = [
        {"bug": 1234567, "suite": "*", "expires": datetime(2026, 12, 31).date()}
    ]
    data = perfherder_data()
    data["suites"][0]["subtests"][0]["lowerIsBetter"] = False

    results = XtremeRegression("linux1804-64-shippable").detect(
        data, threshold=0.5, raise_for_regressions=True
    )

    assert [result["expected_change"] for result in results] == [1234567] * 3


def test_client_sets_user_agent(client, session):
    assert client.session is session
    assert session.headers["User-Agent"] == "mozperfcommon"


@pytest.mark.parametrize(
    "kwargs",
    [{}, {"parent_signature_id": 10, "all_data": True}],
    ids=["no-signature", "all-data-with-parent"],
)
def test_client_get_summary_invalid_arguments(client, session, kwargs):
    now = datetime.now(timezone.utc)
    with pytest.raises(ValueError):
        client.get_summary(now - timedelta(days=1), now, **kwargs)
    session.get.assert_not_called()


if __name__ == "__main__":
    mozunit.main()
